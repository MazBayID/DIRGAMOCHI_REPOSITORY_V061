#include "face_engine.h"
#include <math.h>
#include <esp_system.h> // esp_random() - true hardware RNG, used to seed Arduino's random()

static const int EYE_R = 15;         // outer eye radius
static const int EYE_CY = 26;        // eye vertical center
static const int EYE_LX = 38;        // left eye center x
static const int EYE_RX = 90;        // right eye center x
static const int HIGHLIGHT_R = 4;    // pupil highlight radius

// smoothstep easing: decelerate into / accelerate out of a motion instead
// of moving at constant speed, used for both the blink envelope and the
// eased look-around glide.
float FaceEngine::easeInOut(float t)
{
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

void FaceEngine::begin(Adafruit_SSD1306 *display)
{
    _display = display;
    if (!_seeded)
    {
        // millis()-based seeding repeats the same sequence every boot;
        // esp_random() is the chip's hardware RNG, so blink/idle timing is
        // genuinely different each power-up instead of feeling "scripted".
        randomSeed(esp_random());
        _seeded = true;
    }
    scheduleNextBlink();
    _nextIdleLookAt = millis() + random(1500, 4000);
    draw();
}

void FaceEngine::scheduleNextBlink()
{
    _nextBlinkAt = millis() + (unsigned long)random(2000, 4500); // true 2-4.5s random
}

void FaceEngine::setExpression(FaceExpression expr)
{
    if (_expr != expr)
    {
        _expr = expr;
        draw();
    }
}

void FaceEngine::lookAt(float xOffset, float yOffset)
{
    _targetLookX = constrain(xOffset, -1.0f, 1.0f);
    _targetLookY = constrain(yOffset, -1.0f, 1.0f);
    // give an explicit look (e.g. a remote touch) a few seconds before idle
    // wandering is allowed to pick a new target and override it.
    _nextIdleLookAt = millis() + 3000;
}

void FaceEngine::updateIdleLook(unsigned long now)
{
    if (!_idleAnimEnabled)
        return;
    if (now < _nextIdleLookAt)
        return;

    // ~30% of the time drift back to dead-center (a "resting" glance),
    // otherwise pick a new random point to look toward.
    if (random(0, 100) < 30)
    {
        _targetLookX = 0.0f;
        _targetLookY = 0.0f;
    }
    else
    {
        _targetLookX = random(-100, 101) / 100.0f;
        _targetLookY = random(-60, 61) / 100.0f;
    }
    _nextIdleLookAt = now + (unsigned long)random(1800, 4500);
}

void FaceEngine::update(bool bleConnected)
{
    unsigned long now = millis();

    // Sleepy/angry/surprised/happy/cute expressions have their own fixed eye
    // shapes and don't blink the same way normal eyes do.
    if (_expr == FACE_NORMAL)
    {
        if (!_blinking && now >= _nextBlinkAt)
        {
            _blinking = true;
            _blinkStartedAt = now;
            _blinkDurationMs = 160 + random(0, 40); // 160-200ms, slight natural variance
        }
        if (_blinking && (now - _blinkStartedAt) > _blinkDurationMs)
        {
            // Real eyes double-blink now and then - roll for it once per
            // blink cycle instead of always doing a single clean blink.
            if (!_doubleBlinkPending && random(0, 100) < 18)
            {
                _doubleBlinkPending = true;
                _blinkStartedAt = now;
                _blinkDurationMs = 110 + random(0, 30); // the follow-up blink is quicker
            }
            else
            {
                _blinking = false;
                _doubleBlinkPending = false;
                scheduleNextBlink();
            }
        }
    }
    else
    {
        _blinking = false;
        _doubleBlinkPending = false;
    }

    updateIdleLook(now);

    // Ease the drawn pupil position toward its target every tick instead of
    // snapping, so look changes read as a glance rather than a glitch.
    const float EASE = 0.18f;
    _lookX += (_targetLookX - _lookX) * EASE;
    _lookY += (_targetLookY - _lookY) * EASE;

    // redraw at ~15fps max to keep I2C traffic light
    if (now - _lastDrawMs >= 66)
    {
        _lastDrawMs = now;
        draw();
        drawConnIcon(bleConnected);
        _display->display();
    }
}

void FaceEngine::fillEllipse(Adafruit_SSD1306 *d, int cx, int cy, int rx, int ry, uint16_t color)
{
    if (rx <= 0 || ry <= 0)
        return;
    for (int y = -ry; y <= ry; y++)
    {
        int dx = (int)((float)rx * sqrtf(1.0f - ((float)(y * y) / (float)(ry * ry))));
        d->drawFastHLine(cx - dx, cy + y, dx * 2 + 1, color);
    }
}

void FaceEngine::drawArc(Adafruit_SSD1306 *d, int cx, int cy, int r, float startDeg, float endDeg, uint16_t color, int thickness)
{
    for (float ang = startDeg; ang <= endDeg; ang += 4.0f)
    {
        float rad = ang * (float)M_PI / 180.0f;
        for (int t = 0; t < thickness; t++)
        {
            int rr = r - t;
            int x = cx + (int)(rr * cosf(rad));
            int y = cy + (int)(rr * sinf(rad));
            d->drawPixel(x, y, color);
        }
    }
}

void FaceEngine::drawEye(int cx, int cy, int r, float openness, FaceExpression expr, bool leftEye)
{
    Adafruit_SSD1306 *d = _display;
    int lookDX = (int)(_lookX * (r * 0.25f));
    int lookDY = (int)(_lookY * (r * 0.2f));

    switch (expr)
    {
    case FACE_HAPPY:
    {
        // closed happy eye: upward smiling arc "^"
        drawArc(d, cx, cy + 4, r - 2, 200.0f, 340.0f, SSD1306_WHITE, 3);
        break;
    }
    case FACE_SLEEPY:
    {
        // half-lidded: flat-topped ellipse (lower half of a circle)
        int ry = r / 2;
        fillEllipse(d, cx, cy + r / 3, r, ry, SSD1306_WHITE);
        // eyelid covers top half (erase) then redraw a clean lid line
        d->fillRect(cx - r - 1, cy - r, (r * 2) + 2, r, SSD1306_BLACK);
        d->drawFastHLine(cx - r, cy - 1, r * 2, SSD1306_WHITE);
        break;
    }
    case FACE_ANGRY:
    {
        // round eye, slightly smaller, plus a slanted eyebrow above it
        int rr = (int)(r * 0.85f * openness);
        if (rr < 2)
            rr = 2;
        d->drawCircle(cx, cy, rr, SSD1306_WHITE);
        d->fillCircle(cx + lookDX, cy + lookDY, max(2, rr - 6), SSD1306_WHITE);
        d->fillCircle(cx + lookDX - 1, cy + lookDY - 2, min(2, HIGHLIGHT_R - 2), SSD1306_BLACK);
        // eyebrow: angled line, slanting down toward the nose
        if (leftEye)
            d->drawLine(cx - r, cy - r - 2, cx + r / 2, cy - r + 6, SSD1306_WHITE);
        else
            d->drawLine(cx + r, cy - r - 2, cx - r / 2, cy - r + 6, SSD1306_WHITE);
        return; // custom drawn, skip generic highlight below
    }
    case FACE_SURPRISED:
    {
        int rr = (int)(r * 1.05f);
        d->drawCircle(cx, cy, rr, SSD1306_WHITE);
        d->drawCircle(cx, cy, rr - 1, SSD1306_WHITE);
        d->fillCircle(cx + lookDX, cy + lookDY, rr / 2, SSD1306_WHITE);
        d->fillCircle(cx + lookDX - 2, cy + lookDY - 2, 2, SSD1306_BLACK);
        return;
    }
    case FACE_CUTE:
    {
        // one eye winks (happy arc), the other stays a normal round sparkly eye
        bool winkThis = leftEye; // left eye winks, right eye normal
        if (winkThis)
        {
            drawArc(d, cx, cy + 4, r - 2, 200.0f, 340.0f, SSD1306_WHITE, 3);
        }
        else
        {
            int rr = (int)(r * openness);
            if (rr < 2)
                rr = 2;
            d->drawCircle(cx, cy, rr, SSD1306_WHITE);
            d->fillCircle(cx + lookDX, cy + lookDY, max(2, rr - 6), SSD1306_WHITE);
            d->fillCircle(cx + lookDX - 1, cy + lookDY - 2, min(2, HIGHLIGHT_R - 2), SSD1306_BLACK);
        }
        return;
    }
    case FACE_NORMAL:
    default:
    {
        int rr = (int)(r * openness);
        if (rr < 2)
            rr = 2;
        d->drawCircle(cx, cy, rr, SSD1306_WHITE);
        d->fillCircle(cx + lookDX, cy + lookDY, max(2, rr - 6), SSD1306_WHITE);
        d->fillCircle(cx + lookDX - 1, cy + lookDY - 2, min(2, HIGHLIGHT_R - 2), SSD1306_BLACK);
        break;
    }
    }
}

void FaceEngine::drawMouth(FaceExpression expr)
{
    Adafruit_SSD1306 *d = _display;
    int cx = (EYE_LX + EYE_RX) / 2;
    int cy = 52;

    switch (expr)
    {
    case FACE_HAPPY:
        drawArc(d, cx, cy - 6, 10, 20.0f, 160.0f, SSD1306_WHITE, 2);
        break;
    case FACE_SLEEPY:
        d->drawFastHLine(cx - 5, cy, 10, SSD1306_WHITE);
        break;
    case FACE_SURPRISED:
        d->drawCircle(cx, cy, 5, SSD1306_WHITE);
        break;
    case FACE_ANGRY:
        drawArc(d, cx, cy + 10, 10, 200.0f, 340.0f, SSD1306_WHITE, 2);
        break;
    case FACE_CUTE:
        drawArc(d, cx, cy - 6, 9, 20.0f, 160.0f, SSD1306_WHITE, 2);
        d->drawFastHLine(cx - 2, cy + 2, 4, SSD1306_WHITE);
        break;
    case FACE_NORMAL:
    default:
        // small gentle "n" shaped mouth, like the reference photos
        drawArc(d, cx, cy - 4, 5, 200.0f, 340.0f, SSD1306_WHITE, 1);
        break;
    }
}

void FaceEngine::drawConnIcon(bool connected)
{
    // small dot top-right corner: filled = BLE connected, hollow = advertising
    int cx = 122;
    int cy = 4;
    if (connected)
        _display->fillCircle(cx, cy, 3, SSD1306_WHITE);
    else
        _display->drawCircle(cx, cy, 3, SSD1306_WHITE);
}

void FaceEngine::draw()
{
    if (!_display)
        return;
    _display->clearDisplay();

    float openness = 1.0f;
    if (_blinking)
    {
        unsigned long dt = millis() - _blinkStartedAt;
        float t = (float)dt / (float)_blinkDurationMs;
        if (t > 1.0f)
            t = 1.0f;
        // eased close-then-open envelope: decelerates into fully-closed and
        // accelerates back out, instead of moving at constant linear speed
        if (t < 0.5f)
            openness = 1.0f - easeInOut(t / 0.5f);
        else
            openness = easeInOut((t - 0.5f) / 0.5f);
        if (openness < 0.05f)
            openness = 0.05f;
    }

    drawEye(EYE_LX, EYE_CY, EYE_R, openness, _expr, true);
    drawEye(EYE_RX, EYE_CY, EYE_R, openness, _expr, false);
    drawMouth(_expr);
}
