#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <Arduino.h>

// Wrapper around the shared I2S bus (INMP441 mic + MAX98357A amp).
//
// Two independent things live here:
//  1. The full mic+speaker voice pipeline (push-to-talk / TTS, a future
//     milestone) - only compiled/active when ENABLE_AUDIO=1, deliberately
//     isolated so an untested wiring/driver issue on that side can never
//     take down the OLED/Chronos base.
//  2. A simple, always-available "beep out of the existing speaker" path
//     (startTone/stopTone), used by BuzzerEngine for notification/nav/
//     alarm sounds. This only ever touches the I2S TX (speaker) side - the
//     mic pin is never configured unless ENABLE_AUDIO=1 - and it renders
//     audio in small non-blocking chunks from loop(), so a long alarm
//     pattern never stalls BLE/button handling the way a single big
//     blocking write would.
class AudioEngine
{
public:
    void begin();
    void loop(); // call every loop(); also advances any in-progress tone
    bool isEnabled() const; // true once ENABLE_AUDIO's full mic+speaker path is up

    // placeholders for the next milestone (push-to-talk streaming, TTS playback)
    void startListening();
    void stopListening();
    void playTone(uint16_t freqHz, uint16_t durationMs); // blocking, ENABLE_AUDIO-only

    // non-blocking continuous tone out of the speaker, used by BuzzerEngine.
    // Safe to call even with ENABLE_AUDIO=0 - a speaker-only I2S TX path is
    // always installed in begin() regardless of that flag.
    // gainPercent scales the current volume level (100 = full level), so
    // quiet ambient sounds can sit under alerts without a separate volume.
    void startTone(uint16_t freqHz, uint8_t gainPercent = 100);
    void stopTone();

    // 0=low, 1=med, 2=high - see BEEP_VOLUME_* in dirgamochi_config.h
    void setVolume(uint8_t level);

    // Non-blocking IMA ADPCM (WAV tag 0x11) playback for the mood/blink
    // voice clips (see include/generated/voice_clips.h). data must stay
    // valid for the whole playback (it's a PROGMEM pointer into flash, so
    // that's always true here). Volume-scaled the same way as startTone().
    void playADPCM(const unsigned char *data, uint32_t len, uint16_t blockAlign);
    bool isADPCMPlaying() const { return _adpcmActive; }
    void stopADPCM();

private:
    bool _inited = false;      // full mic+speaker pipeline (ENABLE_AUDIO) is up
    bool _speakerReady = false; // *some* I2S TX path (speaker-only or full) is up

    bool _toneActive = false;
    float _tonePhase = 0.0f;
    float _toneFreqHz = 0.0f;
    int _toneGain = 100;
    int _toneAmplitude = 5000; // overwritten by setVolume(); see .cpp for the level table

    // ADPCM streaming decode state
    bool _adpcmActive = false;
    const unsigned char *_adpcmData = nullptr;
    uint32_t _adpcmLen = 0;
    uint32_t _adpcmPos = 0;
    uint16_t _adpcmBlockAlign = 256;
    int16_t _adpcmPredictor = 0;
    int8_t _adpcmStepIndex = 0;
    bool _adpcmHighNibbleDone = false;

    int16_t adpcmDecodeNextSample();
};

#endif // AUDIO_ENGINE_H
