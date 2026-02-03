#define FASTLED_ALLOW_INTERRUPTS 0

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

NesController controller(
  PIN_NES_DATA,
  PIN_NES_LATCH,
  PIN_NES_PULSE
);

struct Piece {
  uint8_t id, rot;
  int8_t x, y;
};

Piece curPiece = {0, 0, 0, 31};
uint8_t curColorId = 1;

unsigned long lastClockUpdate = 0;

long brightness = 10;

unsigned long fallDelay = 100;
unsigned long lastFall;

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

uint16_t XY(uint8_t x, uint8_t y) {
  return (y & 1) ? (y * WIDTH + (WIDTH - 1 - x)) : (y * WIDTH + x);
}

void setup() {
  Serial.begin(9600);     // pc

  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_STRIP_VOLTAGE, MAX_POWER_MILLIAMPS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();

  lastFall = millis();
  clearBoard();

  controller.begin();
}

void clearBoard() {
  for (int x=0; x<WIDTH; x++) {
    for (int y=0; y<HEIGHT; y++) {
      board[x][y] = EMPTY;
    }
  }
}

void handleInput(unsigned long now) {
  controller.update();

  Piece p = curPiece;
  if (
      controller.isHeld(NesController::Select) &&
      controller.justPressed(NesController::Down)
    ) {
    brightness = max(brightness - 2, 1);
    FastLED.setBrightness(brightness);
  }

  else if (
      controller.isHeld(NesController::Select) &&
      controller.justPressed(NesController::Up)
    ) {
    brightness = min(brightness + 2, 255);
    FastLED.setBrightness(brightness);
  }

  else if (controller.justPressed(NesController::Down)) {
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
    p.rot = (p.rot + 1) % 4; // rotate left
  else if (controller.justPressed(NesController::B))
    p.rot = (p.rot + 3) % 4; // rotate right
  else if (controller.justPressed(NesController::Left))
    p.x--;
  else if (controller.justPressed(NesController::Right))
    p.x++;
  else if (
      controller.isHeld(NesController::Start) &&
      controller.isHeld(NesController::Select) &&
      (controller.justPressed(NesController::Start) ||
      controller.justPressed(NesController::Select))
    ) {
    resetGame();
    return;
  }

  // prevent piece from going out of bounds
  if (pieceInBounds(p) && !collides(p))
    curPiece = p;
}

void updateGameState(unsigned long now) {
  while (now - lastFall >= fallDelay) {
    stepGravity();
    lastFall += fallDelay;
  }
}

bool pieceInBounds(Piece p) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (tetronimo[p.id][p.rot][dy][dx] == 1 && !inPlayfield(p.x + dx, p.y - dy)) 
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
  return y < HEIGHT && board[x][y] != EMPTY;
}

bool collidesAt(Piece p, int dyOffset) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++) {
      if (tetronimo[p.id][p.rot][dy][dx] == 1) {
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
      if (tetronimo[p.id][p.rot][dy][dx] == 1) {
        int x = p.x + dx;
        int y = p.y - dy;
        if (inBoard(x, y))
          board[x][y] = value;
      }
}

void clearFullLines() {
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
    }
  }
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
      if (inBoard(x, y) && tetronimo[ghost.id][ghost.rot][dy][dx]) {
        CRGB c = piece_colors[curColorId];
        c.fadeLightBy(205);
        leds[XY(x,y)] = c;
      }
    }

  // current piece
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (tetronimo[curPiece.id][curPiece.rot][dy][dx]) {
        int x = curPiece.x + dx;
        int y = curPiece.y - dy;
        if (inBoard(x, y))
          leds[XY(x, y)] = piece_colors[curColorId];
      }

  FastLED.show();
}

void spawnNewPiece() {
  curPiece.x = 0;
  curPiece.y = HEIGHT+4;
  curPiece.id = (curPiece.id + 1) % (sizeof(tetronimo) / sizeof(tetronimo[0]));
  curPiece.rot = 0;

  curColorId++;
  if (curColorId >= sizeof(piece_colors) / sizeof(piece_colors[0]))
    curColorId = 1;
}

void resetGame() {
  spawnNewPiece();
  clearBoard();
}

void loop() {
  unsigned long now = millis();
  handleInput(now);
  updateGameState(now);
  renderFrame(now);
}
