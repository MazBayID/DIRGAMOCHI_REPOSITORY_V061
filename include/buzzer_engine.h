#ifndef BUZZER_ENGINE_H
#define BUZZER_ENGINE_H

#include <Arduino.h>
#include "audio_engine.h"

// Direction cue for navigation sounds (see playNavTurn()).
enum NavTurn
{
    NAV_TURN_UNKNOWN = 0,
    NAV_TURN_LEFT,
    NAV_TURN_RIGHT,
    NAV_TURN_STRAIGHT,
    NAV_TURN_UTURN,
    NAV_TURN_ARRIVE
};

// Non-blocking sequencer for short sound patterns. Renders each pattern to
// whichever output is wired on the board:
//   - ENABLE_SPEAKER_BEEP (default ON): the MAX98357A speaker, via
//     AudioEngine's non-blocking I2S tone.
//   - ENABLE_BUZZER (default OFF): an optional separate passive piezo.
// Nothing here ever calls delay().
//
// Sounds have a priority. A new sound only interrupts the one currently
// playing if its priority is >= the current one, so a tiny ambient "blink"
// tick can never cut off a notification, a call ring or an alarm:
//   0 ambient (blink, blup)   1 expression change (boink)   2 menu click
//   3 alerts (message, navigation)                          4 call / alarm
class BuzzerEngine
{
public:
    void begin(AudioEngine *audio = nullptr);
    void loop(); // call every loop(); advances any in-progress pattern

    void setEnabled(bool enabled); // master on/off: alerts + everything
    bool isEnabled() const { return _enabled; }

    // Whether any sound (tone pattern or voice clip) is currently playing,
    // and at what priority - lets main.cpp avoid stepping on its own sounds.
    bool isPlaying() const { return _playing; }
    int currentPriority() const { return _currentPriority; }

    // Ambient effects (blink / blup / boink) can be muted separately while
    // alerts (message, call, navigation, alarm) stay on.
    void setAmbientEnabled(bool enabled) { _ambientEnabled = enabled; }

    // 0=low, 1=med, 2=high - forwarded to AudioEngine (the speaker path);
    // the optional GPIO3 piezo has no meaningful volume control.
    void setVolume(uint8_t level);

    // --- ambient (face) sounds ---
    void playBlink(); // eyes blink: tiny soft tick
    void playBlup();  // eyes glance left/right/up/down: bubble "blup"
    void playBoink(); // expression change: springy "boink"

    // --- alerts ---
    void playNotification(); // chat message: "beep-beep"
    void playCall();         // incoming call: "beeep-beeep-beeep", repeats until stop()
    void playNavTurn(NavTurn turn); // navigation direction cue
    void playNavigation();   // generic single beep (unknown direction)
    void playClick();        // menu navigation
    void playAlarm();        // repeating urgent pattern until stop()
    void stop();

    // --- recorded voice clips (see include/generated/voice_clips.h) ---
    // Same priority scheme as the tone patterns above (blink/mood voice
    // sit at ambient/expression tier 0-1, so a real alert still cuts them
    // off). No-op if data is null (a mood with no matching clip).
    void playVoice(const unsigned char *adpcmData, uint32_t len, uint16_t blockAlign, int priority);

private:
    struct Step
    {
        uint16_t freqHz;     // 0 = silence
        uint16_t durationMs;
        uint8_t gain;        // 0-100, % of the current volume level
    };

    static const int MAX_STEPS = 12;
    Step _steps[MAX_STEPS];
    int _stepCount = 0;
    int _stepIndex = 0;
    unsigned long _stepStartedAt = 0;
    bool _playing = false;
    bool _looping = false;
    bool _voiceMode = false; // true while a recorded ADPCM clip owns the sound output
    int _currentPriority = 0;

    bool _enabled = true;
    bool _ambientEnabled = true;
    bool _hwReady = false;
    AudioEngine *_audio = nullptr;

    void loadPattern(const Step *steps, int count, bool loop, int priority);
    void applyStep(const Step &s);
};

#endif // BUZZER_ENGINE_H
