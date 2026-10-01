#ifndef MENU_ENGINE_H
#define MENU_ENGINE_H

#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>
#include <Preferences.h>
#include "buzzer_engine.h"

enum MenuItem
{
    MENU_BUZZER = 0,
    MENU_VOLUME,
    MENU_ROTATION,
    MENU_RESET_PAIRING,
    MENU_DEVICE_INFO,
    MENU_COUNT // keep last
};

// A tiny single-column settings list, driven entirely by the three
// existing TTP223 buttons (no new hardware): NEXT moves the selection,
// TALK activates/toggles it, MODE exits back to the face. Settings are
// persisted in NVS via Preferences so they survive reboots/power loss.
class MenuEngine
{
public:
    void begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, BuzzerEngine *buzzer);

    void enter();  // called when the menu screen is opened
    void moveNext(); // NEXT short press while menu is open
    void activate(); // TALK short press while menu is open
    void draw();      // render the menu; call every ChronosUI::update() tick

    // MENU_DEVICE_INFO opens a full-screen detail panel instead of toggling
    // in place - main.cpp checks this to route MODE/TALK to "close panel"
    // instead of "exit menu" while it's showing.
    bool isShowingDetail() const { return _showingDetail; }
    void closeDetail() { _showingDetail = false; }

    // Sound mode (menu item "Sound"): 0 = OFF, 1 = ALRT (message / call /
    // navigation / alarm only), 2 = ALL (alerts + face effects: blink,
    // blup, boink).
    bool buzzerEnabled() const { return _soundMode >= 1; } // alerts on
    bool ambientEnabled() const { return _soundMode >= 2; } // face effects on
    uint8_t oledRotation() const { return _oledRotation; }

private:
    Adafruit_SSD1306 *_display = nullptr;
    ChronosESP32 *_watch = nullptr;
    BuzzerEngine *_buzzer = nullptr;
    Preferences _prefs;

    int _selected = 0;
    uint8_t _soundMode = 2; // 0=OFF, 1=ALRT, 2=ALL
    uint8_t _oledRotation = 2;
    uint8_t _volumeLevel = 1; // 0=low,1=med,2=high
    bool _showingDetail = false;

    // transient one-line status shown after an action (e.g. "BLE restarted")
    String _statusMsg;
    unsigned long _statusUntil = 0;

    void loadPrefs();
    void setStatus(const char *msg, unsigned long durationMs = 1500);
    void drawDeviceInfo();
};

#endif // MENU_ENGINE_H
