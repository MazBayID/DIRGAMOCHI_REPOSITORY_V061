/*
 * Dirgamochi-C3
 * A DasaiMochi-style companion firmware for the ESP32-C3 Super Mini,
 * with round kawaii eyes and a Chronos BLE data link (see fbiego/chronos-esp32).
 *
 * Boot-safety notes (please read include/dirgamochi_config.h too):
 *  - setup() never blocks forever: OLED init failure is logged and the
 *    firmware continues running Chronos over BLE even without a screen.
 *  - Audio (I2S) is OFF by default (ENABLE_AUDIO=0) until the base
 *    firmware is confirmed stable on your board. That flag only gates the
 *    mic + full voice pipeline: the speaker (MAX98357A) itself is always
 *    brought up in a TX-only mode so notification/nav/alarm beeps work
 *    out of the box (see ENABLE_SPEAKER_BEEP in dirgamochi_config.h).
 *  - No watchdog-starving loops: loop() never uses delay() for more than
 *    a few ms, so the Arduino/FreeRTOS task watchdog is never starved.
 *
 * v0.4.1: beeps now come out of the existing MAX98357A speaker (I2S,
 * non-blocking) instead of assuming a separate piezo on GPIO3 - most
 * boards built from the reference pinout only have the former. The GPIO3
 * piezo path from v0.4 is kept as an optional extra (ENABLE_BUZZER=0 by
 * default) for anyone who wires one in addition.
 *
 * v0.4 additions (see README for the full list):
 *  - Notification/music/nav/weather text is UTF-8 sanitized before being
 *    printed, fixing garbled long WhatsApp-style notifications.
 *  - Real hardware-RNG blink timing + occasional double-blinks + idle
 *    look-around, instead of the face sitting perfectly still.
 *  - A passive piezo buzzer (GPIO3) beeps for notifications, nav updates
 *    and active alarms - independent of the still-experimental I2S voice
 *    path.
 *  - A NEXT-long-press settings menu (buzzer on/off, OLED rotation,
 *    reset BLE pairing, firmware info), persisted to NVS.
 *  - A QR/link screen that renders whatever Chronos has relayed from the
 *    phone (the ChronosESP32 library already supports this - it just
 *    wasn't wired to anything before).
 *  - Chronos RemoteTouch ("petting" the watch face from the phone app)
 *    nudges the eyes and gives a brief happy reaction.
 *  - The face drifts to a sleepy expression during Chronos quiet/sleep
 *    hours, or after a few idle minutes, and wakes on the next touch.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>

#include "dirgamochi_config.h"
#include "face_engine.h"
#include "sprite_face_engine.h"
#include "generated/voice_clips.h"
#include "generated/sleep_logo.h"
#include "chronos_ui.h"
#include "button_engine.h"
#include "audio_engine.h"
#include "buzzer_engine.h"
#include "menu_engine.h"

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
ChronosESP32 watch; // use the fully initialized default constructor
FaceEngine face;             // procedural expressions (events, moods, quiet hours)
SpriteFaceEngine spriteFace; // bitmap idle-mood animations (plain idle only)
ChronosUI ui;
AudioEngine audio;
BuzzerEngine buzzer;
MenuEngine menu;

TouchButton btnTalk;
TouchButton btnNext;
TouchButton btnMode;

bool oledReady = false;
bool bleConnected = false;
unsigned long notificationUntil = 0;
unsigned long navigationUntil = 0;
unsigned long pettingUntil = 0;
unsigned long transientExprUntil = 0; // when a short-lived expression should revert to NORMAL
bool spriteActive = false;            // true while spriteFace currently owns the display
bool sleepLogoDrawn = false;          // true once the static sleep logo has been painted this session

int qrLinkCount = 0;

bool callRinging = false;              // an incoming call is currently ringing
unsigned long navCheckAt = 0;          // debounce: evaluate navigation sound at this time
String navLastKey;                     // last maneuver we already made a sound for
FaceExpression lastExprForSound = FACE_NORMAL; // to spot expression changes ("boink")

bool prevTouchState = false;
bool prevAlarmActive = false;
bool ambientIsSleepy = false;
unsigned long lastActivityMs = 0;

// Set an expression that should fade back to NORMAL on its own after a
// moment (connect "happy", find-phone "surprised", nav/music "cute", ...),
// so the plain idle state - and with it the bitmap sprite face - resumes
// instead of a one-off reaction staying on screen forever.
void setTransientExpression(FaceExpression expr, unsigned long durationMs)
{
    face.setExpression(expr);
    transientExprUntil = millis() + durationMs;
    ambientIsSleepy = false;
}

// ---------- Navigation direction -> sound ----------

static bool textHasAny(const String &text, const char *const *words, int count)
{
    for (int i = 0; i < count; i++)
        if (text.indexOf(words[i]) >= 0)
            return true;
    return false;
}

// Work out which way the next maneuver points. Navigation apps word this
// differently per language, so match English + Indonesian keywords in the
// instruction text first; if the text has no direction word (often it's just
// a street name) fall back to the 48x48 turn icon Chronos sends, using
// where its lit pixels sit horizontally. Best effort - the result is logged
// to Serial ("[NAV] turn=...") so it can be tuned against real navigation.
NavTurn classifyNavTurn(const Navigation &nav)
{
    String text = nav.directions + " " + nav.title;
    text.toLowerCase();

    static const char *const uturn[] = {"u-turn", "u turn", "putar balik", "balik arah"};
    static const char *const arriveStrong[] = {"arrive", "tiba"};
    static const char *const left[] = {"left", "kiri"};
    static const char *const right[] = {"right", "kanan"};
    static const char *const arriveWeak[] = {"destination", "tujuan"};
    static const char *const straight[] = {"straight", "lurus", "continue", "terus"};

    if (textHasAny(text, uturn, 4))
        return NAV_TURN_UTURN;
    if (textHasAny(text, arriveStrong, 2))
        return NAV_TURN_ARRIVE;
    if (textHasAny(text, left, 2))
        return NAV_TURN_LEFT;
    if (textHasAny(text, right, 2))
        return NAV_TURN_RIGHT;
    if (textHasAny(text, arriveWeak, 2))
        return NAV_TURN_ARRIVE;
    if (textHasAny(text, straight, 4))
        return NAV_TURN_STRAIGHT;

    if (nav.hasIcon)
    {
        long sumX = 0, lit = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 48; ++x)
                if ((nav.icon[(y * 48 + x) / 8] >> (7 - (x % 8))) & 0x01)
                {
                    sumX += x;
                    lit++;
                }
        if (lit > 20)
        {
            float meanX = (float)sumX / (float)lit; // icon center is ~23.5
            if (meanX < 21.0f)
                return NAV_TURN_LEFT;
            if (meanX > 26.0f)
                return NAV_TURN_RIGHT;
            return NAV_TURN_STRAIGHT;
        }
    }
    return NAV_TURN_UNKNOWN;
}

// ---------- Chronos callbacks ----------

void onConnectionChange(bool state)
{
    bleConnected = state;
    if (state)
        setTransientExpression(FACE_HAPPY, 4000UL);
    else
        face.setExpression(FACE_NORMAL);
    lastActivityMs = millis();

    if (state) {
        // Ask the app to synchronize time/settings immediately after reconnect.
        watch.syncRequest();
        watch.setNotifyBattery(true);
    }
}

void onNotification(Notification notification)
{
    // Chronos has already stored the notification. We immediately show it.
    face.setExpression(FACE_SURPRISED);
    notificationUntil = millis() + 6000UL;
    lastActivityMs = millis();
    if (oledReady)
        ui.goTo(SCR_NOTIFICATIONS);
    if (menu.buzzerEnabled())
        buzzer.playNotification();

    Serial.printf("[NOTIF] icon=0x%02X app=%s title=%s message=%s\n",
                  notification.icon, notification.app.c_str(),
                  notification.title.c_str(),
                  notification.message.c_str());
}

void onRinger(String caller, bool state)
{
    lastActivityMs = millis();
    if (state)
    {
        // incoming call: "beeep-beeep-beeep", repeating until it's
        // answered/ended (or any button is pressed)
        face.setExpression(FACE_SURPRISED);
        callRinging = true;
        if (menu.buzzerEnabled())
            buzzer.playCall();
    }
    else
    {
        face.setExpression(FACE_NORMAL);
        if (callRinging)
        {
            callRinging = false;
            buzzer.stop();
        }
    }
}

void onConfig(Config config, uint32_t a, uint32_t b)
{
    Serial.printf("[CONFIG] id=%u a=%lu b=%lu\n", (unsigned)config, (unsigned long)a, (unsigned long)b);
    switch (config)
    {
    case CF_NAV_DATA:
        Serial.printf("[NAV] state=%u\n", (unsigned)a);
        if (a) {
            navigationUntil = millis() + 9000UL;
            lastActivityMs = millis();
            if (oledReady)
                ui.goTo(SCR_NAVIGATION);
            setTransientExpression(FACE_CUTE, 3000UL);
            navCheckAt = millis() + 500UL; // let the icon finish arriving, then sound once
        } else {
            navigationUntil = 0;
            navCheckAt = 0;
            navLastKey = ""; // next navigation session starts fresh
            if (oledReady && ui.current() == SCR_NAVIGATION)
                ui.backToFace();
        }
        break;

    case CF_NAV_ICON:
        // The library assembles the 48x48 icon in its Navigation object.
        if (watch.getNavigation().active ) {
            navCheckAt = millis() + 500UL;
            navigationUntil = millis() + 9000UL;
            if (oledReady)
                ui.goTo(SCR_NAVIGATION);
        }
        break;

    case CF_FIND:
        setTransientExpression(FACE_SURPRISED, 4000UL);
        break;

    case CF_PBAT:
        Serial.printf("[PHONE] battery=%u%% charging=%u\n",
                      (unsigned)b, (unsigned)a);
        break;

    case CF_MUSIC:
        Serial.println("[MUSIC] metadata/state updated");
        break;

    case CF_QR:
        // a==1: all links for this batch finished transferring, b = count.
        // a==0: one individual link updated, b = its index - nothing to do
        // until the batch completes, the QR screen just re-reads on demand.
        if (a == 1) {
            qrLinkCount = (int)b;
            ui.setQrCount(qrLinkCount);
            Serial.printf("[QR] %d link(s) received from phone\n", qrLinkCount);
        }
        break;

    default:
        break;
    }
}

void onData(uint8_t *data, int length)
{
    Serial.printf("[DATA] len=%d type=", length);
    if (length > 4) Serial.printf("0x%02X", data[4]);
    else Serial.print("??");
    Serial.print(" bytes=");
    int n = min(length, 24);
    for (int i = 0; i < n; ++i) Serial.printf("%02X ", data[i]);
    if (length > n) Serial.print("...");
    Serial.println();
}

void onRawData(uint8_t *data, int length)
{
    Serial.printf("[RAW] len=%d ", length);
    int n = min(length, 40);
    for (int i = 0; i < n; ++i) Serial.printf("%02X ", data[i]);
    if (length > n) Serial.print("...");
    Serial.println();
}

// ---------- ambient face / idle behaviour ----------

// Decides whether the face should be resting in a sleepy expression right
// now (Chronos quiet/sleep hours, or a few idle minutes with no button
// press / notification / navigation), without fighting any other explicit
// expression change that happened this frame (find-phone, BLE connect,
// music toggle, ...). Only ever moves the face between NORMAL and SLEEPY,
// and only takes it back out of SLEEPY if it was the one that put it there.
void updateAmbientFace()
{
    bool wantCalm = watch.isSleepActive() || watch.isQuietActive();
    bool idle = (millis() - lastActivityMs) > IDLE_SLEEPY_TIMEOUT_MS;
    bool wantSleepy = wantCalm || idle;

    if (wantSleepy && !ambientIsSleepy && face.getExpression() == FACE_NORMAL)
    {
        face.setExpression(FACE_SLEEPY);
        ambientIsSleepy = true;
    }
    else if (!wantSleepy && ambientIsSleepy)
    {
        face.setExpression(FACE_NORMAL);
        ambientIsSleepy = false;
    }
}

// Chronos apps can relay a "touch" from a phone-side face preview
// (RemoteTouch, coordinate space set by watch.setScreen()). Use it to
// nudge the pupils, and give a brief happy reaction on release, like the
// companion is being petted.
void updateRemoteTouch()
{
    RemoteTouch t = watch.getTouch();

    if (t.state)
    {
        float lx = ((float)t.x / (float)TOUCH_SPACE_W) * 2.0f - 1.0f;
        float ly = ((float)t.y / (float)TOUCH_SPACE_H) * 2.0f - 1.0f;
        face.lookAt(lx, ly);
        lastActivityMs = millis();
    }
    else if (prevTouchState && !t.state)
    {
        // just released - "petting" reaction
        face.setExpression(FACE_HAPPY);
        pettingUntil = millis() + 1200UL;
        ambientIsSleepy = false;
        lastActivityMs = millis();
    }
    prevTouchState = t.state;
}

// ---------- setup / loop ----------

void setup()
{
    Serial.begin(115200);

    pinMode(BTN_TALK_PIN, INPUT);
    pinMode(BTN_NEXT_PIN, INPUT);
    pinMode(BTN_MODE_PIN, INPUT);
    btnTalk.begin(BTN_TALK_PIN);
    btnNext.begin(BTN_NEXT_PIN);
    btnMode.begin(BTN_MODE_PIN);

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    Wire.setClock(OLED_I2C_CLOCK);

    oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);

    // Bring up the speaker (I2S, TX-only unless ENABLE_AUDIO=1) before the
    // beep sequencer that drives it, then load persisted settings (buzzer
    // on/off, OLED rotation) and apply them right away - before anything
    // is drawn - so a rotation flipped from the menu on a previous boot is
    // correct from the very first frame, and notifications beep (or
    // don't) per the saved preference even before Chronos finishes
    // connecting. Safe to call whether or not the OLED was actually found.
    audio.begin();
    buzzer.begin(&audio);
    menu.begin(&display, &watch, &buzzer);

    if (oledReady)
    {
        display.clearDisplay();
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println(DIRGA_FW_NAME);
        display.println(DIRGA_FW_VERSION);
        display.println("Booting...");
        display.display();
        face.begin(&display);
        spriteFace.begin(&display);
    }
    else
    {
        Serial.println("[Dirgamochi] OLED not found at 0x3C - continuing without display");
    }

    watch.setConnectionCallback(onConnectionChange);
    watch.setNotificationCallback(onNotification);
    watch.setRingerCallback(onRinger);
    watch.setConfigurationCallback(onConfig);
    watch.setDataCallback(onData);
    watch.setRawDataCallback(onRawData);
    watch.setName(DIRGA_BLE_NAME);
    watch.setScreen(CF_ESP32_240x240);
    watch.setChunkedTransfer(true);

    watch.begin();
    watch.set24Hour(true);
    watch.setNotifyBattery(true);
    watch.setBattery(100, false); // update from a real fuel gauge later if you add one

    Serial.print("[Dirgamochi] BLE address: ");
    Serial.println(watch.getAddress());

    if (oledReady)
    {
        ui.begin(&display, &watch, &face, &menu);
        face.setExpression(FACE_NORMAL);
    }

    lastActivityMs = millis();

    Serial.println("[Dirgamochi] setup complete");
}

void loop()
{
    watch.loop(); // internal Chronos housekeeping - must be called every loop

    ButtonEvent talkEv = btnTalk.update();
    ButtonEvent nextEv = btnNext.update();
    ButtonEvent modeEv = btnMode.update();

    if (talkEv != BTN_NONE || nextEv != BTN_NONE || modeEv != BTN_NONE)
        lastActivityMs = millis();

    // Any button press silences a ringing alarm rather than requiring a
    // specific one - three tiny touch buttons isn't enough for a dedicated
    // "dismiss" control, and reacting to whichever one is pressed first is
    // the least surprising behaviour at 3am.
    if ((prevAlarmActive || callRinging) &&
        (talkEv != BTN_NONE || nextEv != BTN_NONE || modeEv != BTN_NONE))
        buzzer.stop();

    bool inMenu = oledReady && ui.current() == SCR_MENU;
    bool inQr = oledReady && ui.current() == SCR_QR;

    if (inMenu)
    {
        if (menu.isShowingDetail())
        {
            // A sub-panel (Device info) is open - any of TALK/MODE closes
            // just that panel and returns to the menu list, not all the
            // way back to the face.
            if (talkEv == BTN_SHORT_PRESS || modeEv == BTN_SHORT_PRESS)
                menu.closeDetail();
        }
        else
        {
            // Inside the settings menu, NEXT/TALK/MODE mean move/select/exit
            // instead of their normal screen-cycling roles.
            if (nextEv == BTN_SHORT_PRESS)
                menu.moveNext();
            if (talkEv == BTN_SHORT_PRESS)
                menu.activate();
            if (modeEv == BTN_SHORT_PRESS)
                ui.backToFace();
        }
    }
    else
    {
        if (talkEv == BTN_SHORT_PRESS)
        {
            if (inQr)
                ui.nextQr(); // cycle between multiple relayed links/QRs
            else if (oledReady)
                ui.goTo(SCR_PHONE); // TALK jumps straight to the phone/status page
        }
        else if (talkEv == BTN_LONG_PRESS)
        {
            watch.findPhone(true);
            setTransientExpression(FACE_SURPRISED, 4000UL);
        }

        if (nextEv == BTN_SHORT_PRESS)
        {
            if (oledReady)
                ui.nextScreen();
        }
        else if (nextEv == BTN_LONG_PRESS)
        {
            if (oledReady)
                ui.openMenu();
        }

        if (modeEv == BTN_SHORT_PRESS)
        {
            if (oledReady)
            {
                ui.backToFace();
                face.setExpression(FACE_NORMAL);
                ambientIsSleepy = false;
            }
        }
        else if (modeEv == BTN_LONG_PRESS)
        {
            watch.musicControl(MUSIC_TOGGLE);
            setTransientExpression(FACE_CUTE, 3000UL);
        }
    }

    // Alarm edge detection - fire the buzzer the instant any configured
    // alarm starts ringing, and stop it the instant none are active
    // anymore (e.g. dismissed from the phone side).
    bool alarmActive = watch.isAnyAlarmActive();
    if (alarmActive && !prevAlarmActive)
    {
        face.setExpression(FACE_SURPRISED);
        if (menu.buzzerEnabled())
            buzzer.playAlarm();
    }
    else if (!alarmActive && prevAlarmActive)
    {
        buzzer.stop();
        face.setExpression(FACE_NORMAL); // alarm over - let the idle face resume
    }
    prevAlarmActive = alarmActive;

    // Navigation direction sound: play once per new maneuver (its turn icon
    // or instruction changed), not on every distance update.
    if (navCheckAt && millis() >= navCheckAt)
    {
        navCheckAt = 0;
        Navigation nav = watch.getNavigation();
        if (nav.active)
        {
            String key = nav.directions + "|" + String((unsigned long)nav.iconCRC);
            if (key != navLastKey)
            {
                navLastKey = key;
                NavTurn turn = classifyNavTurn(nav);
                Serial.printf("[NAV] turn=%d (0=?,1=L,2=R,3=straight,4=U,5=arrive) dir=\"%s\"\n",
                              (int)turn, nav.directions.c_str());
                if (menu.buzzerEnabled())
                    buzzer.playNavTurn(turn);
            }
        }
    }

    if (oledReady)
    {
        // Navigation has priority over the normal UI cycle while active.
        if (watch.getNavigation().active) {
            if (ui.current() != SCR_NAVIGATION)
                ui.goTo(SCR_NAVIGATION);
        } else if (notificationUntil && millis() < notificationUntil) {
            if (ui.current() != SCR_NOTIFICATIONS)
                ui.goTo(SCR_NOTIFICATIONS);
        } else if (ui.current() == SCR_NOTIFICATIONS && notificationUntil &&
                   millis() >= notificationUntil) {
            notificationUntil = 0;
            ui.backToFace();
            face.setExpression(FACE_NORMAL);
        }

        // "boink" whenever the face changes to a named expression (Happy,
        // Surprised, Cute, ...) while it's actually on screen. Returning to
        // NORMAL, the quiet-hours/idle Sleepy drift, and changes that
        // happen while another screen is showing stay silent.
        FaceExpression exprNow = face.getExpression();
        if (exprNow != lastExprForSound)
        {
            if (ui.current() == SCR_FACE && exprNow != FACE_NORMAL && !ambientIsSleepy)
                buzzer.playBoink();
            lastExprForSound = exprNow;
        }

        if (ui.current() == SCR_FACE)
        {
            updateRemoteTouch();

            if (pettingUntil && millis() >= pettingUntil)
            {
                pettingUntil = 0;
                face.setExpression(FACE_NORMAL);
            }
            else if (!pettingUntil)
            {
                updateAmbientFace();
            }

            // A short-lived reaction (connect "happy", find-phone, ...) has
            // run its course - fall back to NORMAL so idle can resume.
            if (transientExprUntil && millis() >= transientExprUntil)
            {
                transientExprUntil = 0;
                if (!ambientIsSleepy && !pettingUntil)
                    face.setExpression(FACE_NORMAL);
            }

            // Who owns the screen this tick? Only the *plain* idle state
            // (NORMAL expression, nobody being petted, not the sleepy
            // quiet-hours/idle face) uses the bitmap sprite mood engine;
            // every named expression stays on the procedural FaceEngine.
            bool plainIdle = face.getExpression() == FACE_NORMAL &&
                             !pettingUntil && !ambientIsSleepy && !prevTouchState;

            if (ambientIsSleepy)
            {
                // Quiet hours / idle timeout: show the static sleep logo
                // instead of any face. Painted once on entry, not redrawn
                // every tick - it never changes, so there's nothing to gain
                // from re-sending the same bitmap over I2C repeatedly.
                spriteActive = false;
                if (!sleepLogoDrawn)
                {
                    display.clearDisplay();
                    display.drawBitmap(0, 0, sleepLogoBitmap, SLEEP_LOGO_WIDTH, SLEEP_LOGO_HEIGHT, SSD1306_WHITE);
                    display.display();
                    sleepLogoDrawn = true;
                }
            }
            else if (plainIdle)
            {
                sleepLogoDrawn = false;
                if (!spriteActive)
                {
                    spriteFace.resetIdleTimers(); // also forces a fresh redraw
                    spriteActive = true;
                }
                spriteFace.update(bleConnected);

                // Face sound effects (muted by Sound = ALRT/OFF in the menu):
                // a full mood animation starting plays that mood's own
                // recorded voice clip, a blink plays the recorded blink
                // clip, a glance still uses the synthetic "blup" tone (no
                // recorded clip for that).
                uint8_t ev = spriteFace.takeEvents();
                if (ev & SPRITE_EV_MOOD)
                {
                    int set = spriteFace.currentAnimSet();
                    if (set >= 0 && set < VOICE_MOOD_COUNT && moodVoiceData[set])
                        buzzer.playVoice(moodVoiceData[set], moodVoiceLength[set], VOICE_BLOCK_ALIGN, 1);
                    else
                        buzzer.playBoink(); // fallback synthetic sweep if no clip for this mood
                }
                else if (ev & SPRITE_EV_GLANCE)
                    buzzer.playBlup();
                else if (ev & SPRITE_EV_BLINK)
                {
                    if (blinkVoiceData_)
                        buzzer.playVoice(blinkVoiceData_, blinkVoiceLength, VOICE_BLOCK_ALIGN, 0);
                    else
                        buzzer.playBlink(); // fallback synthetic tick if no blink clip was generated
                }
            }
            else
            {
                sleepLogoDrawn = false;
                spriteActive = false;
                face.update(bleConnected);
            }
        }
        else
        {
            spriteActive = false; // another screen owns the display now
            sleepLogoDrawn = false;
            ui.update();
        }
    }

    // Advance the actual I2S stream first. BuzzerEngine only observes
    // AudioEngine's ADPCM state; running it first could make completion
    // detection lag by one loop tick. Both calls remain non-blocking.
    audio.loop();
    buzzer.loop();
}
