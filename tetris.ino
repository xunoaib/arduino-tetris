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

struct Pos {
  uint8_t x, y;
};

int brightness = 32;
unsigned long lastClockUpdate = 0;

Pos piece = {3, 31};

CRGB leds[NUM_LEDS];

CRGB piece_colors[] = {
  CRGB::Black,
  CRGB::Green,
  CRGB::Red,
  CRGB::Blue,
};

uint8_t board[WIDTH][HEIGHT];

// [piece][rotation][y][x]
uint8_t tetronimo[1][4][3][2] = {
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
      board[x][y] = 0;
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

      // Serial.print("BUTTON ");
      // Serial.print(value ? "PRESS  " : "RELEASE ");
      // Serial.print("R");
      // Serial.print(row);
      // Serial.print(" C");
      // Serial.println(col);

      if (value) {
        // leds[XY(player_x, player_y)] = CRGB::Black;
        // if (row == 0 && col == 2) player_y++;
        // if (row == 1 && col == 2) player_y--;
        if (row == 1 && col == 1) piece.x--;
        if (row == 1 && col == 3) piece.x++;
        //
        // player_x = max(0, min(player_x, WIDTH - 1));
        // player_y = max(0, min(player_y, HEIGHT - 1));
        //
        // leds[XY(player_x, player_y)] = CRGB::Green;
        // FastLED.show();
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

unsigned long fallDelay = 1000;
unsigned long lastFall = millis();

void updateGameState(unsigned long now) {
  // apply gravity
  while (now - lastFall >= fallDelay) {
    stepGravity();
    lastFall += fallDelay;
  }
}

void stepGravity() {
  if (piece.y < 4)
    return; // hit bottom

  // wipe previous piece
  board[piece.x][piece.y] = 0;

  piece.y--;

  // write new piece
  board[piece.x][piece.y] = 1;
}

void renderFrame(unsigned long now) {
  for (int y=0; y<HEIGHT; y++) {
    for (int x=0; x<WIDTH; x++) {
      leds[XY(x, y)] = piece_colors[board[x][y]];
    }
  }
  FastLED.show();
}

void loop() {
  unsigned long now = millis();
  handleInput(now);
  updateGameState(now);
  renderFrame(now);
}
