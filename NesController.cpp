#include "NesController.h"
#include <Arduino.h>

NesController::NesController(uint8_t data, uint8_t latch, uint8_t pulse)
    : _dataPin(data), _latchPin(latch), _pulsePin(pulse), _current(0),
      _previous(0) {}

void NesController::begin() {
    pinMode(_dataPin, INPUT_PULLUP);
    pinMode(_latchPin, OUTPUT);
    pinMode(_pulsePin, OUTPUT);

    digitalWrite(_latchPin, LOW);
    digitalWrite(_pulsePin, LOW);
}

void NesController::update() {
    _previous = _current;
    _current = readRaw();
}

bool NesController::isHeld(Button b) const { return _current & (1 << b); }

bool NesController::justPressed(Button b) const {
    return (_current & (1 << b)) && !(_previous & (1 << b));
}

bool NesController::justReleased(Button b) const {
    return !(_current & (1 << b)) && (_previous & (1 << b));
}

uint8_t NesController::readRaw() {
    uint8_t state = 0;

    digitalWrite(_latchPin, HIGH);
    delayMicroseconds(12);
    digitalWrite(_latchPin, LOW);
    delayMicroseconds(6); // wait to settle

    for (int i = 0; i < 8; i++) {
        if (!digitalRead(_dataPin))
            state |= (1 << i);

        digitalWrite(_pulsePin, HIGH);
        delayMicroseconds(6);
        digitalWrite(_pulsePin, LOW);
        delayMicroseconds(6);
    }

    return state;
}

// vi: ft=arduino
