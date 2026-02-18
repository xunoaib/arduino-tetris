#include <Wire.h>
#include <FastLED.h>
#include "NesController.h"
#include "tetronimoes.h"

// nes controller pins
constexpr uint8_t PIN_NES_DATA  = 6;
constexpr uint8_t PIN_NES_LATCH = 7;
constexpr uint8_t PIN_NES_PULSE = 8;

// led data pin
#define PIN_LED_DATA 9

// board dimensions
#define WIDTH 8
#define HEIGHT 32

// led configuration
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS (WIDTH * HEIGHT)

// led power
#define MAX_POWER_MILLIAMPS 500
#define LED_STRIP_VOLTAGE 5

// alias board value
#define EMPTY 0

struct Kick { int8_t dx, dy; };

const Kick basicKicks[] = {
  { 0,  0},
  { 1,  0},
  {-1,  0},
  { 2,  0},
  {-2,  0},
  { 0,  1}, // from floor
};

NesController controller(
  PIN_NES_DATA,
  PIN_NES_LATCH,
  PIN_NES_PULSE
);

struct Piece {
  uint8_t id, rot;
  int8_t x, y;
};

uint8_t brightness = 10;
uint16_t fallDelay;
unsigned long lastClockUpdate = 0;
unsigned long lastFall;
bool paused = false;

uint8_t level = 0;
uint32_t score = 0;
uint16_t lines_cleared = 0;

const uint16_t levelSpeeds[] PROGMEM = {
  470, 380, 300, 220, 130,
  100, 80, 80, 70, 70, 60, 60,
  50, 50, 40, 40
};

uint8_t board[WIDTH][HEIGHT];

CRGB leds[NUM_LEDS];

CRGB piece_colors[] = {
  CRGB::Black,
  CRGB::Green,
  CRGB::Red,
  CRGB::Blue,
  CRGB::Yellow,
  CRGB::Magenta,
  // CRGB::Orange, // sucks
};

Piece curPiece;
uint8_t curColorId;

constexpr uint16_t XY(uint8_t x, uint8_t y) {
  return (y & 1) ? (y * WIDTH + (WIDTH - 1 - x)) : (y * WIDTH + x);
}

void setup() {
  Serial.begin(9600);

  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_STRIP_VOLTAGE, MAX_POWER_MILLIAMPS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();

  lastFall = millis();
  controller.begin();

  resetGame();
}

void clearBoard() {
  for (int x=0; x<WIDTH; x++)
    for (int y=0; y<HEIGHT; y++)
      board[x][y] = EMPTY;
}

void handleInput(unsigned long now) {
  controller.update();

  if (controller.justPressed(NesController::Select)) {
    paused = !paused;
  } else if (paused) {

    if (controller.justPressed(NesController::Down)) {
      brightness = max(brightness - 2, 1);
      FastLED.setBrightness(brightness);
    } else if (controller.justPressed(NesController::Up)) {
      brightness = min(brightness + 2, 255);
      FastLED.setBrightness(brightness);
    } else if (controller.justPressed(NesController::Start)) {
      resetGame();
    }
    return;
  }

  Piece p = curPiece;

  if (controller.justPressed(NesController::Down)) {
    // hard drop
    while (!collidesAt(curPiece, -1)) curPiece.y--;

    // lock immediately
    writePiece(curPiece, curColorId);
    clearFullLines();
    spawnNewPiece();

    lastFall = now; // sync gravity timer
    return;
  }

  else if (controller.justPressed(NesController::A))
    p.rot = (p.rot + 1) % 4;
  else if (controller.justPressed(NesController::B))
    p.rot = (p.rot + 3) % 4;
  else if (controller.justPressed(NesController::Left))
    p.x--;
  else if (controller.justPressed(NesController::Right))
    p.x++;
  else if (controller.justPressed(NesController::Start)) {
    resetGame();
    return;
  }

  // prevent piece from going out of bounds
  if (pieceInBounds(p) && !collides(p))
    curPiece = p;
}

void updateGameState(unsigned long now) {
  if (paused) {
    lastFall = now;
    return;
  }

  if (now - lastFall >= fallDelay) {
    lastFall = now;
    stepGravity();
  }
}

bool tryRotate(int8_t dir) {
  Piece rotated = curPiece;
  rotated.rot = (rotated.rot + dir + 4) % 4;

  for (uint8_t i = 0; i < sizeof(basicKicks)/sizeof(basicKicks[0]); i++) {
    Piece test = rotated;
    test.x += basicKicks[i].dx;
    test.y += basicKicks[i].dy;

    if (pieceInBounds(test) && !collides(test)) {
      curPiece = test;
      return true;
    }
  }

  return false;
}

bool pieceInBounds(Piece p) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (pgm_read_byte(&tetronimo[p.id][p.rot][dy][dx]) && !inPlayfield(p.x + dx, p.y - dy))
        return false;
  return true;
}

bool inPlayfield(int x, int y) {
  return x >= 0 && x < WIDTH && y >= 0; // no y < HEIGHT check
}

inline bool inBoard(int x, int y) {
  return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

inline bool cellOccupied(int x, int y) {
  return inBoard(x, y) && board[x][y] != EMPTY;
}

bool collidesAt(Piece p, int dyOffset) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++) {
      if (pgm_read_byte(&tetronimo[p.id][p.rot][dy][dx])) {
        int x = p.x + dx;
        int y = p.y - dy + dyOffset;
        if (!inPlayfield(x, y)) return true;
        if (cellOccupied(x, y)) return true;
      }
    }
  return false;
}
bool collides(Piece p) { return collidesAt(p, 0); }
bool settled(Piece p) { return collidesAt(p, -1); }

// writes the given value to spots on the board masked by the given piece at a location
void writePiece(Piece p, uint8_t value) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (pgm_read_byte(&tetronimo[p.id][p.rot][dy][dx])) {
        int x = p.x + dx;
        int y = p.y - dy;
        if (inBoard(x, y))
          board[x][y] = value;
      }
}

void clearFullLines() {
  int num_lines = 0;

  for (int y=0; y<HEIGHT; y++) {
    bool full = true;
    for (int x=0; x<WIDTH; x++) {
      if (board[x][y] == EMPTY) {
        full = false;
        break;
      }
    }

    if (full) {
      // shift everything above down
      for (int yy=y; yy<HEIGHT-1; yy++)
        for (int x=0; x<WIDTH; x++)
          board[x][yy] = board[x][yy+1];

      // wipe top row
      for (int x=0; x<WIDTH; x++)
        board[x][HEIGHT-1] = EMPTY;

      y--; // recheck same row after collapse
      num_lines++;
    }
  }

  // update score & level
  if (num_lines) {
    int base = 0;
    switch (num_lines) {
      case 1: base = 40; break;
      case 2: base = 100; break;
      case 3: base = 300; break;
      case 4: base = 1200; break;
    }
    score += base * (level + 1);
    lines_cleared += num_lines;
    level = lines_cleared / 10;

    Serial.print("Level ");
    Serial.print(level);
    Serial.print(", Score: ");
    Serial.println(score);

    updateFallDelay();
  }
}


void updateFallDelay() {
  uint8_t l = min(level, (int)(sizeof(levelSpeeds)/sizeof(levelSpeeds[0]) - 1));
  fallDelay = pgm_read_word(&levelSpeeds[l]);
}

void stepGravity() {
  if (settled(curPiece)) {
    writePiece(curPiece, curColorId);
    clearFullLines();
    spawnNewPiece();
    return;
  }
  curPiece.y--;
}

void renderFrame(unsigned long now) {
  FastLED.clear();

  // current board
  for (int y=0; y<HEIGHT; y++)
    for (int x=0; x<WIDTH; x++)
      leds[XY(x, y)] = piece_colors[board[x][y]];

  // ghost piece
  Piece ghost = curPiece;
  while (!settled(ghost)) ghost.y--;

  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++) {
      int x = ghost.x + dx;
      int y = ghost.y - dy;
      if (inBoard(x, y) && pgm_read_byte(&tetronimo[ghost.id][ghost.rot][dy][dx])) {
        CRGB c = piece_colors[curColorId];
        c.fadeLightBy(205);
        leds[XY(x,y)] = c;
      }
    }

  // current piece
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (pgm_read_byte(&tetronimo[curPiece.id][curPiece.rot][dy][dx])) {
        int x = curPiece.x + dx;
        int y = curPiece.y - dy;
        if (inBoard(x, y))
          leds[XY(x, y)] = piece_colors[curColorId];
      }

  FastLED.show();
}

void spawnNewPiece() {
  curPiece.x = (WIDTH - PIECE_WIDTH) / 2;
  curPiece.y = HEIGHT + PIECE_HEIGHT;
  curPiece.id = random(0, sizeof(tetronimo) / sizeof(tetronimo[0]));
  curPiece.rot = 0;
  curColorId = random(1, sizeof(piece_colors) / sizeof(piece_colors[0]));
}

void resetGame() {
  level = 0;
  score = 0;
  lines_cleared = 0;
  updateFallDelay();
  spawnNewPiece();
  clearBoard();
}

void loop() {
  unsigned long now = millis();
  handleInput(now);
  updateGameState(now);
  renderFrame(now);
}
