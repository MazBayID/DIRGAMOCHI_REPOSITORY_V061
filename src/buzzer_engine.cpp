#include "buzzer_engine.h"
#include "dirgamochi_config.h"

void BuzzerEngine::begin(AudioEngine *audio)
{
    _audio = audio;

#if ENABLE_BUZZER
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, BUZZER_LEDC_RES_BITS);
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    _hwReady = true;
#else
    _hwReady = false;
#endif
}

void BuzzerEngine::setEnabled(bool enabled)
{
    _enabled = enabled;
    if (!enabled)
        stop();
}

void BuzzerEngine::setVolume(uint8_t level)
{
    if (_audio)
        _audio->setVolume(level);
}

void BuzzerEngine::applyStep(const Step &s)
{
#if ENABLE_BUZZER
    if (_hwReady)
    {
        if (s.freqHz == 0)
            ledcWrite(BUZZER_LEDC_CHANNEL, 0);
        else
            ledcWriteTone(BUZZER_LEDC_CHANNEL, s.freqHz);
    }
#endif

#if ENABLE_SPEAKER_BEEP
    if (_audio)
    {
        if (s.freqHz == 0)
            _audio->stopTone();
        else
            _audio->startTone(s.freqHz, s.gain);
    }
#endif

#if !ENABLE_BUZZER && !ENABLE_SPEAKER_BEEP
    (void)s;
#endif
}

void BuzzerEngine::loadPattern(const Step *steps, int count, bool loop, int priority)
{
    if (!_enabled || count <= 0)
        return;
    // A quieter/less important sound never cuts off a more important one.
    if (_playing && priority < _currentPriority)
        return;

    if (count > MAX_STEPS)
        count = MAX_STEPS;
    for (int i = 0; i < count; i++)
        _steps[i] = steps[i];
    _stepCount = count;
    _stepIndex = 0;
    _looping = loop;
    _voiceMode = false; // a tone pattern is taking over from any prior voice clip
    _currentPriority = priority;
    _playing = true;
    _stepStartedAt = millis();
    applyStep(_steps[0]);
}

// ---- ambient face sounds (priority 0-1, muted by the "ALRT" sound mode) ----

void BuzzerEngine::playBlink()
{
    if (!_ambientEnabled)
        return;
    // tiny soft rising tick: "bl-ink"
    static const Step p[] = {{2000, 12, 40}, {3600, 18, 40}};
    loadPattern(p, 2, false, 0);
}

void BuzzerEngine::playBlup()
{
    if (!_ambientEnabled)
        return;
    // bubble: quick rise, tiny gap, lower "pop": "bl-up"
    static const Step p[] = {{650, 18, 60}, {950, 18, 60}, {1350, 22, 60}, {0, 8, 0}, {850, 34, 60}};
    loadPattern(p, 5, false, 0);
}

void BuzzerEngine::playBoink()
{
    if (!_ambientEnabled)
        return;
    // springy sweep up, short gap, bright "k": "boi-nk"
    static const Step p[] = {{450, 22, 80}, {700, 22, 80}, {1050, 26, 80}, {1500, 22, 80}, {0, 14, 0}, {1200, 40, 80}};
    loadPattern(p, 6, false, 1);
}

// ---- alerts ----

void BuzzerEngine::playNotification()
{
    // chat message: "beep-beep"
    static const Step p[] = {{2200, 90, 100}, {0, 70, 0}, {2200, 90, 100}};
    loadPattern(p, 3, false, 3);
}

void BuzzerEngine::playCall()
{
    // incoming call: three long "beeep"s, then a pause, repeating until
    // stop() (call answered/ended, or a button press).
    static const Step p[] = {{2000, 260, 100}, {0, 130, 0}, {2000, 260, 100}, {0, 130, 0}, {2000, 260, 100}, {0, 1200, 0}};
    loadPattern(p, 6, true, 4);
}

void BuzzerEngine::playNavigation()
{
    static const Step p[] = {{1800, 60, 100}};
    loadPattern(p, 1, false, 3);
}

void BuzzerEngine::playNavTurn(NavTurn turn)
{
    switch (turn)
    {
    case NAV_TURN_LEFT:
    { // falling pair: high -> low
        static const Step p[] = {{1600, 75, 100}, {0, 25, 0}, {1100, 110, 100}};
        loadPattern(p, 3, false, 3);
        break;
    }
    case NAV_TURN_RIGHT:
    { // rising pair: low -> high
        static const Step p[] = {{1100, 75, 100}, {0, 25, 0}, {1600, 110, 100}};
        loadPattern(p, 3, false, 3);
        break;
    }
    case NAV_TURN_STRAIGHT:
    { // one steady long beep
        static const Step p[] = {{1500, 150, 100}};
        loadPattern(p, 1, false, 3);
        break;
    }
    case NAV_TURN_UTURN:
    { // zig-zag wobble
        static const Step p[] = {{1600, 60, 100}, {1300, 60, 100}, {1000, 60, 100}, {1300, 60, 100}, {1600, 80, 100}};
        loadPattern(p, 5, false, 3);
        break;
    }
    case NAV_TURN_ARRIVE:
    { // three-note rising "you're here" chime
        static const Step p[] = {{1200, 80, 100}, {0, 20, 0}, {1600, 80, 100}, {0, 20, 0}, {2100, 160, 100}};
        loadPattern(p, 5, false, 3);
        break;
    }
    default:
        playNavigation();
        break;
    }
}

void BuzzerEngine::playClick()
{
    static const Step p[] = {{2600, 25, 100}, {0, 20, 0}, {3400, 25, 100}};
    loadPattern(p, 3, false, 2);
}

void BuzzerEngine::playAlarm()
{
    // urgent repeating two-tone; loops until stop() is called
    static const Step p[] = {{2000, 200, 100}, {0, 80, 0}, {2600, 200, 100}, {0, 200, 0}};
    loadPattern(p, 4, true, 4);
}

// ---- recorded voice clips ----

void BuzzerEngine::playVoice(const unsigned char *adpcmData, uint32_t len, uint16_t blockAlign, int priority)
{
    if (!_enabled || !adpcmData || len == 0)
        return;
    // Same ambient-mute rule as playBlink()/playBlup()/playBoink(): the
    // blink/mood voice clips sit at that same tier (priority 0-1).
    if (priority < 2 && !_ambientEnabled)
        return;
    if (_playing && priority < _currentPriority)
        return;

    // Explicitly stop the previous output before handing the speaker to a
    // recorded clip. AudioEngine::playADPCM() also clears the I2S DMA, but
    // keeping the ownership transition here makes the BuzzerEngine state
    // unambiguous and prevents a stale tone pattern from being considered
    // active while the ADPCM stream is starting.
    if (_playing && !_voiceMode && _audio)
        _audio->stopTone();

    _stepCount = 0; // any in-progress tone pattern is superseded
    _looping = false;
    _voiceMode = true;
    _currentPriority = priority;
    _playing = true;
    if (_audio)
        _audio->playADPCM(adpcmData, len, blockAlign);
}

void BuzzerEngine::stop()
{
    _playing = false;
    _looping = false;
    _stepCount = 0;
    _currentPriority = 0;
    if (_voiceMode && _audio)
        _audio->stopADPCM();
    _voiceMode = false;
    Step off = {0, 0, 0};
    applyStep(off);
}

void BuzzerEngine::loop()
{
    if (!_playing)
        return;

    if (_voiceMode)
    {
        // A recorded clip streams itself via AudioEngine; just watch for
        // it finishing (it never advances through _steps[]).
        if (!_audio || !_audio->isADPCMPlaying())
        {
            _playing = false;
            _voiceMode = false;
            _currentPriority = 0;
        }
        return;
    }

    unsigned long now = millis();
    if (now - _stepStartedAt < _steps[_stepIndex].durationMs)
        return;

    _stepIndex++;
    if (_stepIndex >= _stepCount)
    {
        if (_looping)
        {
            _stepIndex = 0;
        }
        else
        {
            stop();
            return;
        }
    }

    _stepStartedAt = now;
    applyStep(_steps[_stepIndex]);
}
