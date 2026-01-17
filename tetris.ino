// 8x32 LED STRIP (DIN = Pin 9)
// DIY Controller (UART Serial1 on RX1)

#include <Wire.h>
#include <FastLED.h>

#define WIDTH 8
#define HEIGHT 32
#define NUM_LEDS (WIDTH * HEIGHT)

#define DATA_PIN 9
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 16

#define MAX_POWER_MILLIAMPS 500
#define LED_STRIP_VOLTAGE 5

#define EVT_BUTTON 0x01
#define EVT_ENCODER 0x02

#define EMPTY 0

struct Piece {
  uint8_t id, x, y, rot;
};

Piece curPiece = {0, 0, 31, 0};
uint8_t curColorId = 0;

int brightness = 32;
unsigned long lastClockUpdate = 0;

unsigned long fallDelay = 1000;
unsigned long lastFall = millis();

uint8_t board[WIDTH][HEIGHT];

CRGB leds[NUM_LEDS];

CRGB piece_colors[] = {
  CRGB::Black,
  CRGB::Green,
  CRGB::Red,
  CRGB::Blue,
};

#define PIECE_HEIGHT 3
#define PIECE_WIDTH 2

// [piece][rotation][y][x]
uint8_t tetronimo[1][4][PIECE_HEIGHT][PIECE_WIDTH] = {
  {{
     {0, 1},
     {0, 1},
     {1, 1},
   }}
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
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  for (int x=0; x<WIDTH; x++) {
    for (int y=0; y<WIDTH; y++) {
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
        writePiece(curPiece, EMPTY);
        // if (row == 0 && col == 2) player_y++;
        // if (row == 1 && col == 2) player_y--;
        if (row == 1 && col == 1) curPiece.x--;
        if (row == 1 && col == 3) curPiece.x++;
        writePiece(curPiece, curColorId);
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
      if (tetronimo[p.id][p.rot][dy][dx] == 1 && !inBounds(p.x + dx, p.y + dy)) 
        return false;
  return true;
}

bool inBounds(int x, int y) {
  return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT;
}

bool collides(Piece p) {
  for (int dx=0; dx<PIECE_WIDTH; dx++)
    for (int dy=0; dy<PIECE_HEIGHT; dy++) {
      if (tetronimo[p.id][p.rot][dy][dx] == 1) {
        int x = p.x + dx;
        int y = p.y + dy;
        if (!inBounds(x, y)) return true;
        if (board[x][y] != EMPTY) return true;
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
        int y = p.y + dy;
        if (inBounds(x, y))
          board[x][y] = value;
      }
}

void stepGravity() {
  Piece newPiece = curPiece;
  Serial.print(newPiece.x);
  Serial.print(' ');
  Serial.println(newPiece.y);
  newPiece.y--;

  // hit bottom
  if (collides(newPiece) || !pieceInBounds(newPiece)) {
    spawnNewPiece();
    return;
  }

  writePiece(curPiece, EMPTY); // wipe old piece
  writePiece(newPiece, curColorId); // write new piece
  curPiece = newPiece;
}

void renderFrame(unsigned long now) {
  for (int y=0; y<HEIGHT; y++)
    for (int x=0; x<WIDTH; x++)
      leds[XY(x, y)] = piece_colors[board[x][y]];
  FastLED.show();
}

void spawnNewPiece() {
  Piece newPiece = {0, 0, 31, 0};
  curPiece = newPiece;
  curColorId = (curColorId == sizeof(piece_colors) / sizeof(piece_colors[0]) ? 1 : curColorId+1);
}

void error() {
  board[WIDTH-1][0] = CRGB::Red;
  renderFrame();
  // for (int x=0; x<WIDTH, x++) {
  //   board[x][0] = CRGB::Red;
  // }
}

void loop() {
  unsigned long now = millis();
  handleInput(now);
  updateGameState(now);
  renderFrame(now);
}
