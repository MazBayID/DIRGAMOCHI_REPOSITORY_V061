#include "chronos_ui.h"
#include "dirgamochi_config.h"
#include "text_utils.h"
#include <qrcode.h>

void ChronosUI::begin(Adafruit_SSD1306 *display, ChronosESP32 *watch, FaceEngine *face, MenuEngine *menu)
{
    _display = display;
    _watch = watch;
    _face = face;
    _menu = menu;
    _screen = SCR_FACE;
}

void ChronosUI::nextScreen()
{
    int s = (int)_screen + 1;
    // SCR_MENU is deliberately excluded from the normal NEXT cycle - it's
    // only reachable via a long NEXT press (openMenu()), so a settings
    // screen never appears unannounced in the middle of a casual glance.
    if (s == (int)SCR_MENU || s >= (int)SCR_COUNT)
        s = SCR_FACE;
    _screen = (UiScreen)s;
}

void ChronosUI::backToFace()
{
    _screen = SCR_FACE;
}

void ChronosUI::goTo(UiScreen s)
{
    if (s >= SCR_FACE && s < SCR_COUNT)
        _screen = s;
}

void ChronosUI::openMenu()
{
    _screen = SCR_MENU;
    if (_menu)
        _menu->enter();
}

void ChronosUI::nextQr()
{
    if (_qrCount <= 0)
        return;
    _qrIndex = (_qrIndex + 1) % _qrCount;
}

void ChronosUI::header(const char *title)
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    _display->setCursor(0, 0);
    _display->print(title);
    _display->drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
}

void ChronosUI::drawTime()
{
    header("TIME");
    String h = _watch->getHourZ();
    String rest = _watch->getTime(":%M ");
    String ap = _watch->getAmPmC();

    _display->setTextSize(2);
    _display->setCursor(10, 22);
    _display->print(h + rest);
    _display->setTextSize(1);
    _display->setCursor(100, 30);
    _display->print(ap);

    _display->setCursor(0, 50);
    _display->print(_watch->getTime("%d/%m/%Y")); // getTime() takes a strftime format string
}

void ChronosUI::drawWeather()
{
    header("WEATHER");
    int n = _watch->getWeatherCount();
    if (n <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No weather data yet");
        return;
    }
    Weather w = _watch->getWeatherAt(0);
    _display->setCursor(0, 15);
    _display->print(sanitizeAsciiOled(_watch->getWeatherCity()));
    _display->setTextSize(2);
    _display->setCursor(0, 28);
    _display->print(String(w.temp) + "C");
    _display->setTextSize(1);
    _display->setCursor(0, 50);
    _display->print("H:" + String(w.high) + " L:" + String(w.low));
}

void ChronosUI::drawNotifications()
{
    header("NOTIFICATIONS");
    int n = _watch->getNotificationCount();
    if (n <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No notifications");
        return;
    }

    Notification note = _watch->getNotificationAt(0);

    // App + position
    _display->setCursor(0, 13);
    String app = sanitizeAsciiOled(note.app);
    if (app.length() > 15) app = app.substring(0, 15);
    _display->print(app);
    _display->setCursor(106, 13);
    _display->print("1/");
    _display->print(n);

    // Sanitize BEFORE wrapping: this collapses every UTF-8 multi-byte
    // sequence (emoji, smart quotes, ellipsis, ...) down to a single ASCII
    // placeholder character. That fixes two things at once - the font
    // can't draw those glyphs anyway, and it guarantees the byte-count-based
    // wrapper below can never slice a multi-byte sequence in half (which is
    // what made long notifications render as garbled/misaligned text).
    String content = sanitizeAsciiOled(note.message);
    if (content.length() == 0)
        content = sanitizeAsciiOled(note.title);
    content.trim();

    // 4 compact lines + final truncation marker when necessary.
    const int maxChars = 21;
    int pos = 0;
    int y = 25;
    for (int lineNo = 0; lineNo < 4 && pos < (int)content.length(); ++lineNo)
    {
        int remaining = content.length() - pos;
        int take = min(maxChars, remaining);

        if (remaining > maxChars && lineNo < 3)
        {
            int space = content.lastIndexOf(' ', pos + take - 1);
            if (space > pos + 7)
                take = space - pos;
        }

        String line = content.substring(pos, pos + take);
        line.trim();
        if (lineNo == 3 && pos + take < (int)content.length())
        {
            if (line.length() > 18) line = line.substring(0, 18);
            line += "...";
        }

        _display->setCursor(0, y);
        _display->print(line);
        y += 9;
        pos += take;
        while (pos < (int)content.length() && content[pos] == ' ')
            pos++;
    }
}

void ChronosUI::drawNavigation()
{
    header("NAVIGATION");
    Navigation nav = _watch->getNavigation();

    if (!nav.active)
    {
        _display->setCursor(0, 27);
        _display->print("No navigation");
        return;
    }

    // Chronos sends a 48x48, 1-bit icon in two 96-byte chunks.
    // Draw it exactly as specified by the upstream navigation example.
    if (nav.hasIcon)
    {
        for (int y = 0; y < 48; ++y)
        {
            for (int x = 0; x < 48; ++x)
            {
                int byteIndex = (y * 48 + x) / 8;
                int bitPos = 7 - (x % 8);
                if ((nav.icon[byteIndex] >> bitPos) & 0x01)
                    _display->drawPixel(x, 14 + y, SSD1306_WHITE);
            }
        }
    }
    else
    {
        _display->setCursor(4, 30);
        _display->print(">");
    }

    // Right-side text area is intentionally narrow on a 128x64 OLED.
    const int x = 54;
    String dir = sanitizeAsciiOled(nav.directions);
    String title = sanitizeAsciiOled(nav.title);
    if (dir.length() == 0) dir = title;
    dir.trim();

    _display->setTextSize(1);
    _display->setCursor(x, 14);
    _display->print(title.substring(0, 11));

    _display->setCursor(x, 27);
    _display->print(dir.substring(0, 11));

    _display->setCursor(x, 40);
    _display->print(sanitizeAsciiOled(nav.distance).substring(0, 11));

    _display->setCursor(x, 52);
    _display->print(sanitizeAsciiOled(nav.duration).substring(0, 11));
}

void ChronosUI::drawMusic()
{
    header("MUSIC");
    MusicInfo m = _watch->getMusicInfo();
    _display->setCursor(0, 14);
    _display->print(m.state ? "Playing" : "Paused");
    _display->setCursor(0, 28);
    _display->print(sanitizeAsciiOled(m.title).substring(0, 21));
    _display->setCursor(0, 40);
    _display->print(sanitizeAsciiOled(m.artist).substring(0, 21));
}

void ChronosUI::drawPhone()
{
    if (!_watch->isConnected())
    {
        header("PHONE");
        _display->setCursor(0, 14);
        _display->print("BLE: Waiting...");
        _display->setCursor(0, 26);
        _display->print("Batt: " + String(_watch->getPhoneBattery()) + "%");
        _display->setCursor(0, 38);
        _display->print(_watch->isPhoneCharging() ? "Charging" : "On battery");
        _display->setCursor(0, 52);
        _display->print("Hold TALK: find phone");
        return;
    }

    // Connected: a big checkmark-in-circle centered on screen, with
    // "Connected" below it, instead of a plain text line - the phone/
    // battery details give way to this while paired.
    const int cx = OLED_WIDTH / 2;
    const int cy = 25;
    const int r = 20;
    _display->drawCircle(cx, cy, r, SSD1306_WHITE);
    _display->drawCircle(cx, cy, r - 1, SSD1306_WHITE); // 2px-thick ring

    // Checkmark, drawn as two doubled-up strokes for a bit of weight.
    for (int off = 0; off <= 1; off++)
    {
        _display->drawLine(cx - 10, cy + 2 + off, cx - 3, cy + 9 + off, SSD1306_WHITE);
        _display->drawLine(cx - 3, cy + 9 + off, cx + 12, cy - 10 + off, SSD1306_WHITE);
    }

    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);
    // "Connected" = 9 chars * 6px/char at text size 1 = 54px wide
    _display->setCursor(cx - 27, 52);
    _display->print("Connected");
}

void ChronosUI::drawQr()
{
    _display->setTextSize(1);
    _display->setTextColor(SSD1306_WHITE);

    if (_qrCount <= 0)
    {
        _display->setCursor(0, 25);
        _display->print("No QR from phone yet");
        _display->setCursor(0, 40);
        _display->print("(Chronos app: share");
        _display->setCursor(0, 50);
        _display->print(" a QR/link to watch)");
        return;
    }

    String link = _watch->getQrAt(_qrIndex);

    // Auto-pick the smallest QR version (ECC_LOW - most capacity per
    // module) that fits the link, then the largest integer pixel-per-module
    // scale that still fits the 64px-tall OLED. This maximizes the QR's
    // size on a small 0.96" screen instead of a fixed small code with a lot
    // of wasted margin around it.
    QRCode qrcode;
    uint8_t buf[qrcode_getBufferSize(6)]; // big enough for every version tried below
    bool ok = false;
    for (uint8_t version = 2; version <= 6 && !ok; version++)
        ok = qrcode_initText(&qrcode, buf, version, ECC_LOW, link.c_str()) == 0;

    if (!ok)
    {
        _display->setCursor(0, 25);
        _display->print("Link too long");
        _display->setCursor(0, 37);
        _display->print("to show as a QR");
        return;
    }

    int scale = OLED_HEIGHT / qrcode.size;
    if (scale < 1)
        scale = 1;
    int qrPx = qrcode.size * scale;
    int originX = (OLED_WIDTH - qrPx) / 2;
    int originY = (OLED_HEIGHT - qrPx) / 2;

    // Inverted for a 0.96" OLED: a lit (white) background with the QR's
    // data modules drawn as unlit (black) pixels reads like a normal
    // printed QR code (dark squares on a light background) - what phone
    // camera scanners are tuned to expect - instead of the other way
    // around. The plain white margin around it doubles as the quiet zone.
    _display->fillRect(0, 0, OLED_WIDTH, OLED_HEIGHT, SSD1306_WHITE);
    for (uint8_t y = 0; y < qrcode.size; y++)
    {
        for (uint8_t x = 0; x < qrcode.size; x++)
        {
            if (qrcode_getModule(&qrcode, x, y))
                _display->fillRect(originX + x * scale, originY + y * scale, scale, scale, SSD1306_BLACK);
        }
    }

    if (_qrCount > 1)
    {
        _display->setTextColor(SSD1306_BLACK); // background here is now white
        _display->setCursor(2, 2);
        _display->print(String(_qrIndex + 1) + "/" + String(_qrCount));
    }
}

void ChronosUI::update()
{
    if (_screen == SCR_FACE)
        return; // FaceEngine::update() handles drawing + display() itself

    unsigned long now = millis();
    if (now - _lastDrawMs < 200)
        return;
    _lastDrawMs = now;

    _display->clearDisplay();
    switch (_screen)
    {
    case SCR_TIME:
        drawTime();
        break;
    case SCR_WEATHER:
        drawWeather();
        break;
    case SCR_NOTIFICATIONS:
        drawNotifications();
        break;
    case SCR_NAVIGATION:
        drawNavigation();
        break;
    case SCR_MUSIC:
        drawMusic();
        break;
    case SCR_PHONE:
        drawPhone();
        break;
    case SCR_QR:
        drawQr();
        break;
    case SCR_MENU:
        if (_menu)
            _menu->draw();
        break;
    default:
        break;
    }
    _display->display();
}
