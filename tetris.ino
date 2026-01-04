// 8x32 LED STRIP (DIN = Pin 9)
// DIY Controller (UART Serial1 on RX1)

#include <Wire.h>
#include <FastLED.h>

#define WIDTH     8
#define HEIGHT    32
#define NUM_LEDS  (WIDTH * HEIGHT)

#define DATA_PIN  9
#define LED_TYPE  WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 16

#define MAX_POWER_MILLIAMPS 500
#define LED_STRIP_VOLTAGE 5

#define EVT_BUTTON  0x01
#define EVT_ENCODER 0x02

CRGB leds[NUM_LEDS];

int brightness = 32;
unsigned long lastClockUpdate = 0;

int player_x, player_y;

uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= WIDTH || y >= HEIGHT) return 0;
  return (y % 2 == 0) ? y * WIDTH + x : y * WIDTH + (WIDTH - 1 - x);
}

void setup() {
  Serial.begin(9600); // pc
  Serial1.begin(115200); // controller

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(LED_STRIP_VOLTAGE, MAX_POWER_MILLIAMPS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  player_x = 0;
  player_y = 0;
}

void loop() {

  while (Serial1.available() >= 3) {
    uint8_t type = Serial1.read();
    uint8_t id   = Serial1.read();
    int8_t value = (int8_t)Serial1.read();

    if (type == EVT_BUTTON) {
      uint8_t row = id / 5;
      uint8_t col = id % 5;

      Serial.print("BUTTON ");
      Serial.print(value ? "PRESS  " : "RELEASE ");
      Serial.print("R");
      Serial.print(row);
      Serial.print(" C");
      Serial.println(col);

      if (value) {
        leds[XY(player_x,player_y)] = CRGB::Black;
        if (row == 0 && col == 2) player_y++;
        if (row == 1 && col == 2) player_y--;
        if (row == 1 && col == 1) player_x--;
        if (row == 1 && col == 3) player_x++;
        leds[XY(player_x,player_y)] = CRGB::Green;
        FastLED.show();
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
