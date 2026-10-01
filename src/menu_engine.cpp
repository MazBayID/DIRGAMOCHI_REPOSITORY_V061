#include "menu_engine.h"
#include "dirgamochi_config.h"

void MenuEngine::begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, BuzzerEngine *buzzer)
{
    _display = display;
    _watch = watch;
    _buzzer = buzzer;
    loadPrefs();

    // Apply the persisted settings immediately at boot so a rotation/buzzer/
    // volume choice made in the menu survives a power cycle without
    // recompiling.
    if (_buzzer)
    {
        _buzzer->setEnabled(_soundMode >= 1);
        _buzzer->setAmbientEnabled(_soundMode >= 2);
        _buzzer->setVolume(_volumeLevel);
    }
    if (_display)
        _display->setRotation(_oledRotation);
}

void MenuEngine::loadPrefs()
{
    _prefs.begin(SETTINGS_NAMESPACE, false);
    // "snd" replaced the older on/off "buz" flag; carry an old setting over
    // once so nobody's mute choice is lost on upgrade.
    _soundMode = _prefs.getUChar("snd", 255);
    if (_soundMode > 2)
        _soundMode = _prefs.getBool("buz", true) ? 2 : 0;
    _oledRotation = _prefs.getUChar("rot", OLED_ROTATION);
    if (_oledRotation != 0 && _oledRotation != 2)
        _oledRotation = OLED_ROTATION; // guard against garbage/first-run NVS
    _volumeLevel = _prefs.getUChar("vol", 1);
    if (_volumeLevel > 2)
        _volumeLevel = 1;
    _prefs.end();
}

void MenuEngine::enter()
{
    _selected = 0;
    _statusMsg = "";
    _showingDetail = false;
}

void MenuEngine::moveNext()
{
    _selected = (_selected + 1) % MENU_COUNT;
    if (_buzzer)
        _buzzer->playClick();
}

void MenuEngine::setStatus(const char *msg, unsigned long durationMs)
{
    _statusMsg = msg;
    _statusUntil = millis() + durationMs;
}

void MenuEngine::activate()
{
    _prefs.begin(SETTINGS_NAMESPACE, false);

    switch (_selected)
    {
    case MENU_BUZZER:
        _soundMode = (_soundMode + 1) % 3; // OFF -> ALRT -> ALL -> OFF
        if (_buzzer)
        {
            _buzzer->setEnabled(_soundMode >= 1);
            _buzzer->setAmbientEnabled(_soundMode >= 2);
        }
        _prefs.putUChar("snd", _soundMode);
        break;

    case MENU_VOLUME:
        _volumeLevel = (_volumeLevel + 1) % 3;
        if (_buzzer)
            _buzzer->setVolume(_volumeLevel);
        _prefs.putUChar("vol", _volumeLevel);
        break;

    case MENU_ROTATION:
        _oledRotation = (_oledRotation == 0) ? 2 : 0;
        if (_display)
            _display->setRotation(_oledRotation);
        _prefs.putUChar("rot", _oledRotation);
        setStatus("Saved");
        break;

    case MENU_RESET_PAIRING:
        if (_watch)
        {
            // Drop the current BLE connection/advertising and start fresh -
            // useful if the phone's Chronos app has a stale pairing entry.
            _watch->stop(false); // keep saved app-side data (name/settings)
            _watch->begin();
        }
        setStatus("Reset");
        break;

    case MENU_DEVICE_INFO:
        _showingDetail = true;
        break;

    default:
        break;
    }

    _prefs.end();

    // Every action gets a confirmation click (Volume plays it at the NEW
    // level so you hear what you picked). Silent: opening the info panel
    // (nothing was "done"), and switching Sound to OFF (it would be muted
    // anyway).
    if (_buzzer)
    {
        bool silent = (_selected == MENU_DEVICE_INFO) ||
                      (_selected == MENU_BUZZER && _soundMode == 0);
        if (!silent)
            _buzzer->playClick();
    }
}

void MenuEngine::drawDeviceInfo()
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(0, 0);
    _display->print("DEVICE INFO");
    _display->drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);

    _display->setCursor(0, 13);
    _display->print("Name: ");
    _display->print(DIRGA_BLE_NAME);

    _display->setCursor(0, 23);
    _display->print("BLE: ");
    _display->print(_watch && _watch->isConnected() ? "Connected" : "Advertising");

    _display->setCursor(0, 33);
    _display->print("MAC:");
    _display->setCursor(0, 43);
    _display->print(_watch ? _watch->getAddress() : "n/a");

    _display->setCursor(0, 53);
    _display->print("FW: ");
    _display->print(DIRGA_FW_VERSION);
}

void MenuEngine::draw()
{
    if (!_display)
        return;

    if (_showingDetail)
    {
        drawDeviceInfo();
        return;
    }

    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(0, 0);
    _display->print("MENU");
    _display->drawFastHLine(0, 9, OLED_WIDTH, SSD1306_WHITE);

    const char *labels[MENU_COUNT] = {
        "Sound",
        "Volume",
        "Rotate",
        "Reset BLE",
        "Device info"};
    const int valueX = 92;

    int y = 12;
    for (int i = 0; i < MENU_COUNT; i++)
    {
        _display->setCursor(0, y);
        _display->print(i == _selected ? "> " : "  ");
        _display->print(labels[i]);

        _display->setCursor(valueX, y);
        if (i == _selected && _statusMsg.length() > 0 && millis() < _statusUntil)
        {
            _display->print(_statusMsg);
        }
        else
        {
            switch (i)
            {
            case MENU_BUZZER:
                _display->print(_soundMode == 0 ? "OFF" : (_soundMode == 1 ? "ALRT" : "ALL"));
                break;
            case MENU_VOLUME:
                _display->print(_volumeLevel == 0 ? "Low" : (_volumeLevel == 1 ? "Med" : "High"));
                break;
            case MENU_ROTATION:
                _display->print(_oledRotation == 2 ? "ON" : "OFF");
                break;
            case MENU_RESET_PAIRING:
            case MENU_DEVICE_INFO:
                _display->print("TALK");
                break;
            }
        }
        y += 10;
    }
}
