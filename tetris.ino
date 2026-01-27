// 8x32 LED STRIP (DIN = Pin 9)
// DIY Controller (UART Serial1 on RX1)

#define FASTLED_ALLOW_INTERRUPTS 1

#include <Wire.h>
#include <FastLED.h>

#define WIDTH 8
#define HEIGHT 32
#define NUM_LEDS (WIDTH * HEIGHT)

#define DATA_PIN 9
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

#define MAX_POWER_MILLIAMPS 500
#define LED_STRIP_VOLTAGE 5

#define EVT_BUTTON 0x01
#define EVT_ENCODER 0x02

#define EMPTY 0

unsigned long brightness = 16;

struct Piece {
  uint8_t id, rot;
  int8_t x, y;
};

Piece curPiece = {0, 0, 0, 31};
uint8_t curColorId = 1;

unsigned long lastClockUpdate = 0;

unsigned long fallDelay = 100;
unsigned long lastFall = millis();

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

#define PIECE_HEIGHT 4
#define PIECE_WIDTH 4

uint8_t tetronimo[3][4][PIECE_HEIGHT][PIECE_WIDTH] = {

  // L block
  {
    {
      {0, 0, 0, 0},
      {0, 1, 0, 0},
      {0, 1, 0, 0},
      {1, 1, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 0, 0, 0},
      {1, 0, 0, 0},
      {1, 1, 1, 0},
    }, {
      {0, 0, 0, 0},
      {1, 1, 0, 0},
      {1, 0, 0, 0},
      {1, 0, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 0, 0, 0},
      {1, 1, 1, 0},
      {0, 0, 1, 0},
    }
  },

  // I block
  {
    {
      {0, 1, 0, 0},
      {0, 1, 0, 0},
      {0, 1, 0, 0},
      {0, 1, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 0, 0, 0},
      {1, 1, 1, 1},
      {0, 0, 0, 0},
    }, {
      {0, 1, 0, 0},
      {0, 1, 0, 0},
      {0, 1, 0, 0},
      {0, 1, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 0, 0, 0},
      {1, 1, 1, 1},
      {0, 0, 0, 0},
    }
  },

  // 2x2 block
  {
    {
      {0, 0, 0, 0},
      {0, 1, 1, 0},
      {0, 1, 1, 0},
      {0, 0, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 1, 1, 0},
      {0, 1, 1, 0},
      {0, 0, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 1, 1, 0},
      {0, 1, 1, 0},
      {0, 0, 0, 0},
    }, {
      {0, 0, 0, 0},
      {0, 1, 1, 0},
      {0, 1, 1, 0},
      {0, 0, 0, 0},
    },
  },

};

uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= WIDTH || y >= HEIGHT) return 0;
  return (y % 2 == 0) ? y * WIDTH + x : y * WIDTH + (WIDTH - 1 - x);
}

void setup() {
  Serial.begin(9600);     // pc
  Serial1.begin(115200);  // controller

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_STRIP_VOLTAGE, MAX_POWER_MILLIAMPS);
  FastLED.setBrightness(brightness);
  FastLED.clear();
  FastLED.show();

  clearBoard();
}

void clearBoard() {
  for (int x=0; x<WIDTH; x++) {
    for (int y=0; y<HEIGHT; y++) {
      board[x][y] = EMPTY;
    }
  }
}

void handleInput(unsigned long now) {
  while (Serial1.available() >= 3) {
    uint8_t type = Serial1.read();
    uint8_t id = Serial1.read();
    int8_t value = (int8_t)Serial1.read();

    if (type == EVT_BUTTON) {
      uint8_t row = id / 5;
      uint8_t col = id % 5;
      if (value) {
        Piece p = curPiece;
        if (row == 0 && col == 3) {
          p.rot = (p.rot + 1) % 4;
        }
        else if (row == 0 && col == 1) {
          p.rot = (p.rot - 1) % 4;
        }
        else if (row == 1 && col == 1)
          p.x--;
        else if (row == 1 && col == 3)
          p.x++;
        else if (row == 1 && col == 4) {
          Serial.println("Resetting");
          resetGame();
          return;
        }
        else if (row == 0 && col == 4) {
          brightness = brightness == 16 ? 8 : 16;
          FastLED.setBrightness(brightness);
        }
        else {
          Serial.print(row);
          Serial.print(' ');
          Serial.println(col);
        }

        if (pieceInBounds(p) && !collides(p))
          curPiece = p;
      }
    }

    else if (type == EVT_ENCODER) {
      Serial.print("ENCODER delta=");
      Serial.println(value);
    }

    else {
      Serial.print("UNKNOWN EVT ");
      Serial.println(type, HEX);
    }
  }
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

bool settled(Piece p) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++) {
      if (tetronimo[p.id][p.rot][dy][dx] == 1) {
        int x = p.x + dx;
        int y = p.y - dy - 1;
        if (!inPlayfield(x, y)) return true;
        if (y < HEIGHT && board[x][y] != EMPTY) return true;
      }
    }
  return false;
}

bool collides(Piece p) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++) {
      if (tetronimo[p.id][p.rot][dy][dx] == 1) {
        int x = p.x + dx;
        int y = p.y - dy;
        if (!inPlayfield(x, y)) return true;
        if (y < HEIGHT && board[x][y] != EMPTY) return true;
      }
    }
  return false;
}

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

void stepGravity() {
  if (settled(curPiece)) {
    writePiece(curPiece, curColorId);
    spawnNewPiece();
    return;
  }
  curPiece.y--;
}

void renderFrame(unsigned long now) {
  for (int y=0; y<HEIGHT; y++)
    for (int x=0; x<WIDTH; x++)
      leds[XY(x, y)] = piece_colors[board[x][y]];

  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++)
      if (tetronimo[curPiece.id][curPiece.rot][dy][dx] == 1) {
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
