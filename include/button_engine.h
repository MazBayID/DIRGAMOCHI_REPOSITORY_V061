#ifndef BUTTON_ENGINE_H
#define BUTTON_ENGINE_H

#include <Arduino.h>

enum ButtonEvent
{
    BTN_NONE = 0,
    BTN_SHORT_PRESS,
    BTN_LONG_PRESS
};

class TouchButton
{
public:
    void begin(uint8_t pin);
    // call every loop(); returns the event that happened THIS call (edge-triggered)
    ButtonEvent update();

private:
    uint8_t _pin = 0;
    bool _lastRaw = false;
    bool _stableState = false;
    bool _longFired = false;
    unsigned long _lastChangeMs = 0;
    unsigned long _pressStartMs = 0;
};

#endif // BUTTON_ENGINE_H
