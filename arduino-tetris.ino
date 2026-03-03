#include <Wire.h>
#include <EEPROM.h>
#include <FastLED.h>
#include "NesController.h"
#include "tetronimoes.h"

constexpr uint8_t PIN_LED_DATA = 5;
constexpr uint8_t PIN_NES_DATA  = 6;
constexpr uint8_t PIN_NES_LATCH = 7;
constexpr uint8_t PIN_NES_PULSE = 8;

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
  STATE_ENTRY_DELAY,
  STATE_GAME_OVER,
  STATE_LEVEL_UP
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
  0xFF4500,
  CRGB::Cyan,
};

constexpr uint16_t ENTRY_DELAY_TIME = 200; // ms between lock and spawn

Piece curPiece;
uint8_t curColorId;
uint8_t lastPieceId = 255;

unsigned long animStart = 0;
uint16_t animDuration = 0;
uint8_t animData[HEIGHT];     // mask of rows being cleared
bool pendingSpawn = false;    // spawn piece after clear animation completes
bool levelUpPending = false;

// soft drop latch for current frame
bool softDropActive = false;

// --- game over dissolve ---
bool dissolveActive = false;
uint8_t dissolveOrder[NUM_LEDS];
uint16_t dissolveIndex = 0;
unsigned long dissolveLastStep = 0;
constexpr uint8_t dissolveStepDelay = 8; // ms between pixel wipes

// --- final score screen ---
bool finalScoreActive = false;
unsigned long finalScoreStart = 0;
constexpr uint16_t finalScoreFadeTime = 800; // ms fade-in time

// 7 bag randomization
constexpr uint8_t NUM_PIECES = sizeof(tetronimo) / sizeof(tetronimo[0]);
uint8_t pieceBag[NUM_PIECES];
uint8_t bagIndex = NUM_PIECES;

// delayed auto shift
unsigned long lastDasMove = 0;
unsigned long dasHoldTime = 0;
uint8_t lastDir = 0; // 0: none, 1: left, 2: right

constexpr uint16_t DAS_DELAY = 180; // delay before repeat starts
constexpr uint16_t DAS_SPEED = 60;  // speed of repeat

// lock piece grace period
constexpr uint16_t LOCK_DELAY = 500; // ms grace period on ground
unsigned long lockTimer = 0;
bool isSettled = false;

// limit the number of lock resets
constexpr uint8_t MAX_LOCK_RESETS = 15;
uint8_t lockResetCount = 0;

uint32_t highScore = 0;
constexpr int EEPROM_ADDR = 0;

int8_t ghostY = 0; // bottommost Y for ghost piece

// ------------------------------

void loadHighScore() {
  EEPROM.get(EEPROM_ADDR, highScore);
  if (highScore == 0xFFFFFFFF) highScore = 0;
}

void updateHighScore() {
  if (score > highScore) {
    highScore = score;
    EEPROM.put(EEPROM_ADDR, highScore);
    Serial.print("New High Score: ");
    Serial.println(highScore);
  }
}

void refillBag() {
  for (uint8_t i = 0; i < NUM_PIECES; i++)
    pieceBag[i] = i;

  for (int i = 0; i < NUM_PIECES; i++) {
    int j = random(i + 1);
    uint8_t tmp = pieceBag[i];
    pieceBag[i] = pieceBag[j];
    pieceBag[j] = tmp;
  }

  bagIndex = 0;
}

uint8_t getNextPieceId() {
  if (bagIndex >= NUM_PIECES)
    refillBag();

  uint8_t id = pieceBag[bagIndex++];
  lastPieceId = id;
  return id;
}

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

  uint8_t newLevel = lines_cleared / 10;
  if (newLevel > level) {
    levelUpPending = true;
    level = newLevel;
  }

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
  curPiece.y = (HEIGHT - 1) + (PIECE_HEIGHT - 1);

  curPiece.id = getNextPieceId();
  curPiece.rot = 0;

  updateGhostPosition();

  lockResetCount = 0;
  isSettled = false;

  uint8_t oldColorId = curColorId;
  while (curColorId == oldColorId) {
    curColorId = random(1, sizeof(piece_colors) / sizeof(piece_colors[0]));
  }

  if (collides(curPiece)) {
    gameState = STATE_GAME_OVER;
    updateHighScore();

    pendingSpawn = false;
    softDropActive = false;

    // initialize dissolve
    dissolveActive = true;
    dissolveIndex = 0;
    dissolveLastStep = millis();

    // build shuffled list of indices
    for (uint16_t i = 0; i < NUM_LEDS; i++)
      dissolveOrder[i] = i;

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
      uint16_t j = random(NUM_LEDS);
      uint16_t tmp = dissolveOrder[i];
      dissolveOrder[i] = dissolveOrder[j];
      dissolveOrder[j] = tmp;
    }

    Serial.println("Game over!");
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

  gameState = STATE_PLAYING;
  spawnNewPiece();

  lastFall = millis();
}

void lockPieceAndMaybeClear(unsigned long now) {
  writePiece(curPiece, curColorId);

  if (startLineClearAnimIfNeeded(now)) {
    pendingSpawn = true;
    lastFall = now;
    return;
  }

  // start new piece entry delay
  if (gameState != STATE_GAME_OVER) {
    gameState = STATE_ENTRY_DELAY;
    animStart = now;
    animDuration = ENTRY_DELAY_TIME;
  }

  lastFall = now;
}

bool stepGravity(unsigned long now) {
  if (!settled(curPiece)) {
    curPiece.y--;
    lastFall = now;
    return true; // piece moved down
  }
  return false; // piece is blocked/settled
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

  bool actionTaken = false; // track if we moved/rotated (to reset lock)

  // hard drop
  if (controller.justPressed(NesController::Down)) {
    int8_t startY = curPiece.y;

    while (!collidesAt(curPiece, -1)) {
      curPiece.y--;
    }

    int8_t distance = startY - curPiece.y;

    if (distance > 0) {
      score += (distance * 2);
    }

    lockPieceAndMaybeClear(now);
    return;
  }

  // rotate piece left/right
  if (controller.justPressed(NesController::A)) {
    if (tryRotate(+1)) {
      actionTaken = true;
      updateGhostPosition();
    }
  } else if (controller.justPressed(NesController::B)) {
    if (tryRotate(-1)) {
      actionTaken = true;
      updateGhostPosition();
    }
  }

  // lateral movement (DAS)
  bool leftHeld = controller.isHeld(NesController::Left);
  bool rightHeld = controller.isHeld(NesController::Right);
  uint8_t currentDir = (leftHeld) ? 1 : (rightHeld ? 2 : 0);

  if (currentDir != 0) {
    if (lastDir != currentDir) {
      Piece p = curPiece;
      if (currentDir == 1) p.x--; else p.x++;
      if (pieceInBounds(p) && !collides(p)) {
        curPiece = p;
        actionTaken = true;
        updateGhostPosition();
      }
      dasHoldTime = now;
      lastDasMove = now;
      lastDir = currentDir;
    } else if (now - dasHoldTime >= DAS_DELAY) {
      if (now - lastDasMove >= DAS_SPEED) {
        Piece p = curPiece;
        if (currentDir == 1) p.x--; else p.x++;
        if (pieceInBounds(p) && !collides(p)) {
          curPiece = p;
          actionTaken = true;
          updateGhostPosition();
        }
        lastDasMove = now;
      }
    }
  } else {
    lastDir = 0;
  }

  // reset the timers if we successfully moved or rotated piece
  if (actionTaken && settled(curPiece)) {
    if (lockResetCount < MAX_LOCK_RESETS) {
      lockTimer = now;
      lockResetCount++;
    }
  }
}

void updateGameState(unsigned long now) {
  if (gameState == STATE_PLAYING) {
    bool currentlySettled = settled(curPiece);

    // handle gravity
    uint16_t activeDelay = softDropActive ? SOFT_DROP_DELAY : fallDelay;
    if (now - lastFall >= activeDelay) {
      if (stepGravity(now)) {
        updateGhostPosition();
        if (softDropActive) {
          score += 1;
        }
      }
    }

    // handle lock delay ("infinity" mechanic)
    if (currentlySettled) {
      // if we just landed, start timer
      if (!isSettled) {
        lockTimer = now;
        isSettled = true;
      }

      // lock if we've been on the ground longer than LOCK_DELAY
      if (now - lockTimer >= LOCK_DELAY) {
        lockPieceAndMaybeClear(now);
        isSettled = false; // reset for next piece
      }
    } else {
      // piece is in the air, reset settled flag
      isSettled = false;
    }

  } else if (gameState == STATE_LINE_CLEAR_ANIM) {
    if (now - animStart >= animDuration) {
      collapseClearedLines();

      if (levelUpPending) {
          gameState = STATE_LEVEL_UP;
          animStart = now;
          animDuration = 150;
          levelUpPending = false;
      } else {
          gameState = STATE_ENTRY_DELAY;
          animStart = now;
          animDuration = ENTRY_DELAY_TIME;
      }
    }
  } else if (gameState == STATE_LEVEL_UP) {
    if (now - animStart >= animDuration) {
      gameState = STATE_ENTRY_DELAY;
      animStart = now;
      animDuration = ENTRY_DELAY_TIME;
    }
  } else if (gameState == STATE_ENTRY_DELAY) {
    // wait for delay to finish
    if (now - animStart >= animDuration) {
      gameState = STATE_PLAYING;
      spawnNewPiece();
      lastFall = now;
    }
  } else {
    lastFall = now;
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
      // render high score on right
      if (highScore & (1UL << bit)) {
        for (uint8_t x = 5; x < 7; x++) {
          leds[XY(x, bit)] = CRGB::Gold;
          leds[XY(x, bit)].fadeLightBy(255 - fade);
        }
      }

      // render current score on left
      if (score & (1UL << bit)) {
        for (uint8_t x = 1; x < 3; x++) {
          if (score >= highScore) {
            leds[XY(x, bit)] = CRGB::Green;
          } else {
            leds[XY(x, bit)] = CRGB::White;
          }
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
    for (int x=0; x<WIDTH; x++) {
      CRGB c;
      memcpy_P(&c, &piece_colors[board[x][y]], sizeof(CRGB));
      leds[XY(x, y)] = c;
    }

  if (gameState == STATE_LEVEL_UP) {
    float progress = (float)(now - animStart) / animDuration;
    int waveY = progress * (HEIGHT + 5);

    for (int y = 0; y < HEIGHT; y++) {
      int dist = abs(y - waveY);
      if (dist < 4) {
        uint8_t glow = map(dist, 0, 4, 255, 0);
        for (int x = 0; x < WIDTH; x++) {
          leds[XY(x, y)] += CRGB(glow, glow, glow);
        }
      }
    }
    FastLED.show();
    return;
  }

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
  if (gameState == STATE_PLAYING) {
    for (int dx=0; dx<PIECE_WIDTH; dx++)
      for (int dy=0; dy<PIECE_HEIGHT; dy++) {
        int x = curPiece.x + dx;
        int y = ghostY - dy;

        if (inBoard(x, y) && pgm_read_byte(&tetronimo[curPiece.id][curPiece.rot][dy][dx])) {
          CRGB c;
          memcpy_P(&c, &piece_colors[curColorId], sizeof(CRGB));
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
          if (inBoard(x, y)) {
            CRGB c;
            memcpy_P(&c, &piece_colors[curColorId], sizeof(CRGB));
            leds[XY(x, y)] = c;
          }
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
  loadHighScore();

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

void updateGhostPosition() {
  Piece ghost = curPiece;
  while (!settled(ghost)) {
    ghost.y--;
  }
  ghostY = ghost.y;
}
