#ifndef CHRONOS_UI_H
#define CHRONOS_UI_H

#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>
#include "face_engine.h"
#include "menu_engine.h"

enum UiScreen
{
    SCR_FACE = 0,
    SCR_TIME,
    SCR_WEATHER,
    SCR_NOTIFICATIONS,
    SCR_NAVIGATION,
    SCR_MUSIC,
    SCR_PHONE,
    SCR_QR,
    SCR_MENU, // not part of the NEXT cycle - only entered via a long-press
    SCR_COUNT // keep last
};

class ChronosUI
{
public:
    void begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, FaceEngine *face, MenuEngine *menu);

    void nextScreen();       // NEXT short press - cycles the glanceable screens
    void backToFace();       // MODE short press
    void goTo(UiScreen s);   // jump directly to a screen (e.g. TALK -> SCR_PHONE)
    void openMenu();         // NEXT long press - opens the settings menu
    UiScreen current() const { return _screen; }

    // draws the current screen (except SCR_FACE, which FaceEngine owns)
    // call every loop(); internally throttled
    void update();

    // let main.cpp tell the QR screen how many links Chronos has sent, and
    // which one to show (cycled with TALK while the QR screen is open)
    void setQrCount(int count) { _qrCount = count; }
    void nextQr();

private:
    Adafruit_SSD1306 *_display = nullptr;
    ChronosESP32 *_watch = nullptr;
    FaceEngine *_face = nullptr;
    MenuEngine *_menu = nullptr;
    UiScreen _screen = SCR_FACE;
    unsigned long _lastDrawMs = 0;

    int _qrCount = 0;
    int _qrIndex = 0;

    void drawTime();
    void drawWeather();
    void drawNotifications();
    void drawNavigation();
    void drawMusic();
    void drawPhone();
    void drawQr();

    void header(const char *title);
};

#endif // CHRONOS_UI_H
