#include <Wire.h>
#include <FastLED.h>
#include "NesController.h"
#include "tetronimoes.h"

constexpr uint8_t PIN_NES_DATA  = 6;
constexpr uint8_t PIN_NES_LATCH = 7;
constexpr uint8_t PIN_NES_PULSE = 8;

#define PIN_LED_DATA 9

#define WIDTH 8
#define HEIGHT 32

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define NUM_LEDS (WIDTH * HEIGHT)

#define MAX_POWER_MILLIAMPS 500
#define LED_STRIP_VOLTAGE 5

#define EMPTY 0

#define SOFT_DROP_DELAY 33

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

enum GameState {
  STATE_PLAYING,
  STATE_PAUSED,
  STATE_LINE_CLEAR_ANIM,
  STATE_GAME_OVER
};

GameState gameState = STATE_PLAYING;

uint8_t brightness = 10;
uint16_t fallDelay;
unsigned long lastFall = 0;

uint8_t level = 0;
uint32_t score = 0;
uint16_t lines_cleared = 0;

const uint16_t levelSpeeds[] PROGMEM = {
  800, 717, 633, 550, 467, 383, 300, 217, 133, 100,
   90,  85,  80,  70,  65,  60,  55,  50,  45,  40,
   40,  40,  40,  40,  40,  40,  40,  40,  40,  35
};

uint8_t board[WIDTH][HEIGHT];
CRGB leds[NUM_LEDS];

const CRGB piece_colors[] PROGMEM = {
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

unsigned long animStart = 0;
uint16_t animDuration = 0;
uint8_t animData[HEIGHT];     // mask of rows being cleared
bool pendingSpawn = false;    // spawn piece after clear animation completes

// soft drop latch for current frame
bool softDropActive = false;

// --- Game Over Dissolve ---
bool dissolveActive = false;
uint8_t dissolveOrder[NUM_LEDS];
uint16_t dissolveIndex = 0;
unsigned long dissolveLastStep = 0;
const uint8_t dissolveStepDelay = 8; // ms between pixel kills

// --- Final Score Screen ---
bool finalScoreActive = false;
unsigned long finalScoreStart = 0;
const uint16_t finalScoreFadeTime = 800; // ms fade-in time

constexpr uint16_t XY(uint8_t x, uint8_t y) {
  return (y & 1) ? (y * WIDTH + (WIDTH - 1 - x)) : (y * WIDTH + x);
}

inline bool inBoard(int x, int y) {
  return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

bool inPlayfield(int x, int y) {
  return x >= 0 && x < WIDTH && y >= 0; // no y < HEIGHT check
}

inline bool cellOccupied(int x, int y) {
  return inBoard(x, y) && board[x][y] != EMPTY;
}

void clearBoard() {
  for (int x=0; x<WIDTH; x++)
    for (int y=0; y<HEIGHT; y++)
      board[x][y] = EMPTY;
}

void updateFallDelay() {
  uint8_t l = min(level, (uint8_t)(sizeof(levelSpeeds)/sizeof(levelSpeeds[0]) - 1));
  fallDelay = pgm_read_word(&levelSpeeds[l]);
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
bool settled(Piece p)  { return collidesAt(p, -1); }

bool pieceInBounds(Piece p) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (pgm_read_byte(&tetronimo[p.id][p.rot][dy][dx]) &&
          !inPlayfield(p.x + dx, p.y - dy))
        return false;
  return true;
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

int detectFullLines(uint8_t outMask[HEIGHT]) {
  int num = 0;
  for (int y=0; y<HEIGHT; y++) {
    bool full = true;
    for (int x=0; x<WIDTH; x++) {
      if (board[x][y] == EMPTY) { full = false; break; }
    }
    outMask[y] = full ? 1 : 0;
    if (full) num++;
  }
  return num;
}

void collapseClearedLines() {
  for (int y = 0; y < HEIGHT; ) {
    if (!animData[y]) { 
      y++;
      continue;
    }

    for (int yy = y; yy < HEIGHT - 1; yy++)
      for (int x = 0; x < WIDTH; x++)
        board[x][yy] = board[x][yy + 1];

    for (int x = 0; x < WIDTH; x++)
      board[x][HEIGHT - 1] = EMPTY;

    for (int yy = y; yy < HEIGHT - 1; yy++)
      animData[yy] = animData[yy + 1];
    animData[HEIGHT - 1] = 0;
  }
}

bool startLineClearAnimIfNeeded(unsigned long now) {
  uint8_t mask[HEIGHT];
  int num_lines = detectFullLines(mask);
  if (!num_lines) return false;

  int base = 0;
  switch (num_lines) {
    case 1: base = 40; break;
    case 2: base = 100; break;
    case 3: base = 300; break;
    case 4: base = 1200; break;
  }
  score += (uint32_t)base * (level + 1);
  lines_cleared += num_lines;
  level = lines_cleared / 10;

  Serial.print("Level ");
  Serial.print(level);
  Serial.print(", Score: ");
  Serial.println(score);

  updateFallDelay();

  // arm animation
  for (int y=0; y<HEIGHT; y++) animData[y] = mask[y];
  gameState = STATE_LINE_CLEAR_ANIM;
  animStart = now;
  animDuration = 250; // flash time

  return true;
}

void spawnNewPiece() {
  curPiece.x = (WIDTH - PIECE_WIDTH) / 2;

  // Spawn so the piece occupies the top rows immediately.
  // With your coordinate system (cells at y = curPiece.y - dy),
  // setting y like this makes the top-most dy land on y = HEIGHT-1.
  curPiece.y = (HEIGHT - 1) + (PIECE_HEIGHT - 1);

  curPiece.id  = random(0, sizeof(tetronimo) / sizeof(tetronimo[0]));
  curPiece.rot = 0;
  curColorId   = random(1, sizeof(piece_colors) / sizeof(piece_colors[0]));

  if (collides(curPiece)) {
    gameState = STATE_GAME_OVER;
    pendingSpawn = false;
    softDropActive = false;

    // Initialize dissolve
    dissolveActive = true;
    dissolveIndex = 0;
    dissolveLastStep = millis();

    // Build shuffled index list
    for (uint16_t i = 0; i < NUM_LEDS; i++)
      dissolveOrder[i] = i;

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
      uint16_t j = random(NUM_LEDS);
      uint16_t tmp = dissolveOrder[i];
      dissolveOrder[i] = dissolveOrder[j];
      dissolveOrder[j] = tmp;
    }

    Serial.println("Game over!");
    return;
  }

}

void resetGame() {
  level = 0;
  score = 0;
  lines_cleared = 0;
  pendingSpawn = false;
  softDropActive = false;

  clearBoard();
  updateFallDelay();

  gameState = STATE_PLAYING;   // set BEFORE spawn
  spawnNewPiece();

  lastFall = millis();
}

void lockPieceAndMaybeClear(unsigned long now) {
  writePiece(curPiece, curColorId);

  // if we start a line clear anim, delay spawn until animation completes
  if (startLineClearAnimIfNeeded(now)) {
    pendingSpawn = true;
    lastFall = now;
    return;
  }

  if (gameState != STATE_GAME_OVER)
    spawnNewPiece();

  lastFall = now;
}

void stepGravity(unsigned long now) {
  if (settled(curPiece)) {
    lockPieceAndMaybeClear(now);
    return;
  }
  curPiece.y--;
}

void handleInput(unsigned long now) {
  // restart on game over
  if (gameState == STATE_GAME_OVER) {
    softDropActive = false;
    if (controller.justPressed(NesController::Start)) {
      finalScoreActive = false;
      resetGame();
    }
    return;
  }

  // ignore movement during line clear animation
  if (gameState == STATE_LINE_CLEAR_ANIM) {
    softDropActive = false;
    return;
  }

  // pause toggle (only playing <-> paused)
  if (controller.justPressed(NesController::Select)) {
    if (gameState == STATE_PLAYING) {
      gameState = STATE_PAUSED;
      animStart = now;
      animDuration = 300; // fade time
      softDropActive = false;
    } else if (gameState == STATE_PAUSED) {
      gameState = STATE_PLAYING;
      lastFall = now; // prevent gravity jump
    }
  }

  // paused menu controls
  if (gameState == STATE_PAUSED) {
    softDropActive = false;
    if (controller.justPressed(NesController::Down)) {
      brightness = max((int)brightness - 2, 1);
      FastLED.setBrightness(brightness);
    } else if (controller.justPressed(NesController::Up)) {
      brightness = min((int)brightness + 2, 255);
      FastLED.setBrightness(brightness);
    } else if (controller.justPressed(NesController::Start)) {
      resetGame();
    }
    return;
  }

  if (gameState != STATE_PLAYING) {
    softDropActive = false;
    return;
  }

  softDropActive = controller.isHeld(NesController::Up);

  // hard drop
  if (controller.justPressed(NesController::Down)) {
    while (!collidesAt(curPiece, -1)) curPiece.y--;
    lockPieceAndMaybeClear(now);
    return;
  }

  Piece p = curPiece;

  // rotation (use kicks)
  if (controller.justPressed(NesController::A)) {
    tryRotate(+1);
    return;
  } else if (controller.justPressed(NesController::B)) {
    tryRotate(-1);
    return;
  }

  // lateral movement
  if (controller.justPressed(NesController::Left))  p.x--;
  if (controller.justPressed(NesController::Right)) p.x++;

  // reset
  if (controller.justPressed(NesController::Start)) {
    resetGame();
    return;
  }

  // apply movement if legal
  if ((p.x != curPiece.x) && pieceInBounds(p) && !collides(p))
    curPiece = p;
}

void updateGameState(unsigned long now) {
  if (gameState == STATE_PLAYING) {

    uint16_t activeDelay = softDropActive ? SOFT_DROP_DELAY : fallDelay;

    if (now - lastFall >= activeDelay) {
      lastFall = now;
      stepGravity(now);
    }

  } else if (gameState == STATE_LINE_CLEAR_ANIM) {
    if (now - animStart >= animDuration) {
      collapseClearedLines();
      gameState = STATE_PLAYING;

      if (pendingSpawn && gameState != STATE_GAME_OVER) {
        pendingSpawn = false;
        spawnNewPiece();
      }

      lastFall = now; // prevent gravity after animation
    }
  } else {
    lastFall = now; // prevent gravity after paused/game over
  }
}

void renderFrame(unsigned long now) {
  // --- Dissolve Effect ---
  if (gameState == STATE_GAME_OVER && dissolveActive) {

    if (millis() - dissolveLastStep >= dissolveStepDelay) {
      dissolveLastStep = millis();

      if (dissolveIndex < NUM_LEDS) {
        leds[dissolveOrder[dissolveIndex]] = CRGB::Black;
        dissolveIndex++;
      } else {
          dissolveActive = false;

          // start final score screen
          finalScoreActive = true;
          finalScoreStart = millis();
      }
    }

    FastLED.show();
    return;
  }

  // --- Final Score Screen ---
  if (gameState == STATE_GAME_OVER && finalScoreActive) {

    FastLED.clear();

    uint8_t fade = map(
      (uint16_t)min(millis() - finalScoreStart, (unsigned long)finalScoreFadeTime),
      0, finalScoreFadeTime,
      0, 255
    );

    for (uint8_t bit = 0; bit < 32; bit++) {
      if (score & (1UL << bit)) {
        for (uint8_t x = 2; x < 6; x++) {
          leds[XY(x, bit)] = CRGB::White;
          leds[XY(x, bit)].fadeLightBy(255 - fade);
        }
      }
    }

    FastLED.show();
    return;
  }

  FastLED.clear();

  // render base board
  for (int y=0; y<HEIGHT; y++)
    for (int x=0; x<WIDTH; x++)
      leds[XY(x, y)] = pgm_read_dword(&piece_colors[board[x][y]]);

  // line clear flash overlay
  if (gameState == STATE_LINE_CLEAR_ANIM) {
    bool flash = ((now - animStart) / 50) % 2;
    for (int y=0; y<HEIGHT; y++) {
      if (animData[y]) {
        for (int x=0; x<WIDTH; x++) {
          leds[XY(x,y)] = flash ? CRGB::White : CRGB::Black;
        }
      }
    }
    // don't draw ghost/current piece during animation
    FastLED.show();
    return;
  }

  // ghost piece
  if (gameState == STATE_PLAYING || gameState == STATE_PAUSED) {
    Piece ghost = curPiece;
    while (!settled(ghost)) ghost.y--;

    for (int dx=0; dx<PIECE_WIDTH; dx++)
      for (int dy=0; dy<PIECE_HEIGHT; dy++) {
        int x = ghost.x + dx;
        int y = ghost.y - dy;
        if (inBoard(x, y) && pgm_read_byte(&tetronimo[ghost.id][ghost.rot][dy][dx])) {
          CRGB c = pgm_read_dword(&piece_colors[curColorId]);
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
            leds[XY(x, y)] = pgm_read_dword(&piece_colors[curColorId]);
        }
  }

  // pause fade-to-black
  if (gameState == STATE_PAUSED) {
    uint8_t fade = map((uint16_t)min(now - animStart, (unsigned long)animDuration),
                       0, animDuration,
                       0, 255);
    for (int i=0; i<NUM_LEDS; i++)
      leds[i].fadeLightBy(fade);
  }

  FastLED.show();
}

void setup() {
  Serial.begin(9600);

  FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_STRIP_VOLTAGE, MAX_POWER_MILLIAMPS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();

  controller.begin();
  randomSeed(analogRead(A0));

  resetGame();
}

void loop() {
  unsigned long now = millis();

  controller.update();

  handleInput(now);
  updateGameState(now);
  renderFrame(now);
}
