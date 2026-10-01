#ifndef FACE_ENGINE_H
#define FACE_ENGINE_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

enum FaceExpression
{
    FACE_NORMAL = 0,
    FACE_HAPPY,
    FACE_SLEEPY,
    FACE_SURPRISED,
    FACE_ANGRY,
    FACE_CUTE
};

class FaceEngine
{
public:
    void begin(Adafruit_SSD1306 *display);

    // call every loop() so blinking / idle look / connection icon stay animated
    void update(bool bleConnected);

    void setExpression(FaceExpression expr);
    FaceExpression getExpression() const { return _expr; }

    // nudge the pupils to look around (e.g. on a remote touch) -1.0 .. 1.0.
    // Smoothly eased toward over the next few frames, not snapped to.
    void lookAt(float xOffset, float yOffset);

    // temporarily suspend idle blinking/wandering, e.g. while a menu or
    // another screen owns the display and FaceEngine isn't the one drawing
    void setIdleAnimationEnabled(bool enabled) { _idleAnimEnabled = enabled; }

private:
    Adafruit_SSD1306 *_display = nullptr;
    FaceExpression _expr = FACE_NORMAL;

    // blink animation state
    unsigned long _nextBlinkAt = 0;
    unsigned long _blinkStartedAt = 0;
    unsigned long _blinkDurationMs = 180;
    bool _blinking = false;
    bool _doubleBlinkPending = false; // fires a 2nd, shorter blink right after

    // pupil position: _lookX/_lookY are what's actually drawn, eased toward
    // _targetLookX/_targetLookY every frame so movement never snaps.
    float _lookX = 0.0f;
    float _lookY = 0.0f;
    float _targetLookX = 0.0f;
    float _targetLookY = 0.0f;
    bool _idleAnimEnabled = true;
    unsigned long _nextIdleLookAt = 0;

    unsigned long _lastDrawMs = 0;
    bool _seeded = false;

    void draw();
    void drawEye(int cx, int cy, int r, float openness, FaceExpression expr, bool leftEye);
    void drawMouth(FaceExpression expr);
    void drawConnIcon(bool connected);
    void scheduleNextBlink();
    void updateIdleLook(unsigned long now);
    static float easeInOut(float t);

    static void fillEllipse(Adafruit_SSD1306 *d, int cx, int cy, int rx, int ry, uint16_t color);
    static void drawArc(Adafruit_SSD1306 *d, int cx, int cy, int r, float startDeg, float endDeg, uint16_t color, int thickness = 2);
};

#endif // FACE_ENGINE_H
