#include <Arduino.h>

class NesController {
  public:
    enum Button { A, B, Select, Start, Up, Down, Left, Right };

    NesController(uint8_t data, uint8_t latch, uint8_t pulse);

    void begin();
    void update();

    bool isHeld(Button b) const;
    bool justPressed(Button b) const;
    bool justReleased(Button b) const;

  private:
    uint8_t readRaw();

    uint8_t _dataPin, _latchPin, _pulsePin;
    uint8_t _current;
    uint8_t _previous;
};
