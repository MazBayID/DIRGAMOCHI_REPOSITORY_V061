#include "button_engine.h"
#include "dirgamochi_config.h"

void TouchButton::begin(uint8_t pin)
{
    _pin = pin;
    pinMode(_pin, INPUT); // TTP223 has its own push-pull output, no internal pull needed
    _lastRaw = digitalRead(_pin) == HIGH;
    _stableState = _lastRaw;
    _lastChangeMs = millis();
}

ButtonEvent TouchButton::update()
{
    bool raw = digitalRead(_pin) == HIGH;
    unsigned long now = millis();

    if (raw != _lastRaw)
    {
        _lastChangeMs = now;
        _lastRaw = raw;
    }

    ButtonEvent ev = BTN_NONE;

    if ((now - _lastChangeMs) > BTN_DEBOUNCE_MS && raw != _stableState)
    {
        _stableState = raw;
        if (_stableState)
        {
            // just pressed
            _pressStartMs = now;
            _longFired = false;
        }
        else
        {
            // just released -> short press only if long wasn't already fired
            if (!_longFired)
            {
                ev = BTN_SHORT_PRESS;
            }
        }
    }

    // fire long-press once while still held
    if (_stableState && !_longFired && (now - _pressStartMs) >= BTN_LONGPRESS_MS)
    {
        _longFired = true;
        ev = BTN_LONG_PRESS;
    }

    return ev;
}
