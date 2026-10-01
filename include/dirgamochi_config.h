#ifndef DIRGAMOCHI_CONFIG_H
#define DIRGAMOCHI_CONFIG_H

/*
 * Dirgamochi-C3 — centralized hardware map
 * ESP32-C3 Super Mini
 *
 * IMPORTANT (read before rewiring):
 *  - GPIO2, GPIO8, GPIO9 are strapping pins on the ESP32-C3. GPIO9 is the
 *    BOOT button (must float/high to boot from flash). GPIO8 is unused in
 *    this pin map, which sidesteps any strapping-pin ambiguity for the mic.
 *  - GPIO1/GPIO2 are shared between the mic (SCK/WS) and the speaker
 *    (BCLK/LRC). The ESP32-C3 has a single I2S peripheral, so mic and
 *    speaker share the same bit clock / word-select lines by design —
 *    do not run RX and TX on two different I2S configs at once.
 *  - Audio (I2S mic + amp) is compiled in but DISABLED by default
 *    (see ENABLE_AUDIO below) for the very first bring-up. This guarantees
 *    the board boots and shows the face + connects to Chronos even if the
 *    I2S wiring/hardware is not attached yet or not yet tuned. Flip
 *    ENABLE_AUDIO to 1 once the base firmware is confirmed stable.
 */

// ---------- Feature flags ----------
#ifndef ENABLE_AUDIO
#define ENABLE_AUDIO 0 // 0 = safe bring-up (no I2S init at all), 1 = enable mic/speaker
#endif

#define DIRGA_FW_NAME "Dirgamochi-C3"
#define DIRGA_FW_VERSION "0.6.1-DIAG"
#define DIRGA_BLE_NAME "Dirgamochi"

// ---------- OLED (SSD1306 128x64, I2C) ----------
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 20
#define OLED_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_CLOCK 400000UL
// 0 = normal, 2 = rotated 180°. If the face/text appears upside down on
// your build, this is the only line you need to touch (1 and 3 are 90°
// rotations meant for square panels and will scramble a 128x64 layout).
#define OLED_ROTATION 2

// ---------- INMP441 (I2S microphone, input) ----------
#define MIC_SCK_PIN 1  // I2S bit clock (shared with speaker BCLK)
#define MIC_WS_PIN 2   // I2S word select (shared with speaker LRC)
#define MIC_SD_PIN 10  // I2S data in

// ---------- MAX98357A (I2S amplifier, output) ----------
#define SPK_BCLK_PIN 1 // shared with MIC_SCK_PIN
#define SPK_LRC_PIN 2  // shared with MIC_WS_PIN
#define SPK_DIN_PIN 5  // I2S data out

// ---------- TTP223 touch buttons ----------
#define BTN_TALK_PIN 4
#define BTN_NEXT_PIN 6
#define BTN_MODE_PIN 7

#define BTN_DEBOUNCE_MS 40
#define BTN_LONGPRESS_MS 600

// ---------- Speaker beep (through the existing MAX98357A amp, via I2S) ----------
// Default sound output: reuses the speaker/amp you already have wired
// (SPK_BCLK/SPK_LRC/SPK_DIN below), through a small non-blocking I2S tone
// generator in audio_engine.cpp - it never touches the mic pin and works
// even with ENABLE_AUDIO left at 0.
#ifndef ENABLE_SPEAKER_BEEP
#define ENABLE_SPEAKER_BEEP 1
#endif

// ---------- Optional separate piezo buzzer (passive, PWM tone via ledc) ----------
// OFF by default: most boards built from this pin map only have the
// MAX98357A speaker above, not a second dedicated buzzer. Flip this to 1
// only if you've actually wired a passive piezo to BUZZER_PIN.
// GPIO3 is not used anywhere else in this pin map and is NOT a strapping
// pin on the ESP32-C3 (strapping pins are GPIO2/8/9 - see note above), so
// it's a safe free pin to use for one if you add it later.
#ifndef ENABLE_BUZZER
#define ENABLE_BUZZER 0 // 0 = no separate piezo wired, 1 = piezo buzzer on BUZZER_PIN
#endif
#define BUZZER_PIN 3
#define BUZZER_LEDC_CHANNEL 4 // pick a channel not used elsewhere (0-7 on C3)
#define BUZZER_LEDC_RES_BITS 10

// ---------- Speaker beep volume (sine amplitude, out of int16 max 32767) ----------
// Index 0/1/2 = Low/Med/High, selected from the settings menu and persisted.
#define BEEP_VOLUME_LOW 2000
#define BEEP_VOLUME_MED 5000
#define BEEP_VOLUME_HIGH 10000

// ---------- Idle behaviour ----------
// After this long with no button press / notification / navigation while
// showing the face, drift to a sleepy expression until the next touch.
#define IDLE_SLEEPY_TIMEOUT_MS (5UL * 60UL * 1000UL)

// ---------- Chronos remote touch (from the phone app) ----------
// RemoteTouch.x/.y arrive in the coordinate space of the screen profile
// passed to watch.setScreen() in main.cpp (CF_ESP32_240x240 = 240x240).
#define TOUCH_SPACE_W 240
#define TOUCH_SPACE_H 240

// ---------- Settings persistence (NVS via Preferences) ----------
#define SETTINGS_NAMESPACE "dirga"

// ---------- Bitmap idle-mood sprites (assets/faces/*.zip) ----------
// Playback rate of the bitmap animations and the idle blink clip.
#define SPRITE_FPS 10
#define SPRITE_FRAME_INTERVAL_MS (1000 / SPRITE_FPS)

// How often the resting face blinks (random within this range). The blink
// is one real "eyes closed" frame from the mood loops (FACE_BLINK_* in the
// generated header), held for SPRITE_BLINK_HOLD_MS, occasionally followed
// by a quick second blink (a natural-looking double-blink).
#define SPRITE_BLINK_INTERVAL_MIN_MS 2500UL
#define SPRITE_BLINK_INTERVAL_MAX_MS 5500UL
#define SPRITE_BLINK_HOLD_MS 400UL // matches mochi_blink.wav's ~0.41s length
#define SPRITE_DOUBLE_BLINK_PERCENT 20
#define SPRITE_DOUBLE_BLINK_GAP_MS 150UL

// How often a full ~17s random mood animation plays while resting
// (random within this range). Lower = more show, higher = calmer/cheaper.
// Set both to a tiny value (e.g. 100) to play mood loops back to back.
#define SPRITE_SHOW_INTERVAL_MIN_MS 20000UL
#define SPRITE_SHOW_INTERVAL_MAX_MS 45000UL

// Occasional glance left/right/up/down while resting. The mood frames are
// baked bitmaps, so a "glance" slides the whole face image by a few pixels
// (with a short ease in/out) and plays the "blup" sound.
#define SPRITE_GLANCE_INTERVAL_MIN_MS 6000UL
#define SPRITE_GLANCE_INTERVAL_MAX_MS 14000UL
#define SPRITE_GLANCE_X 5 // px shifted for left/right
#define SPRITE_GLANCE_Y 3 // px shifted for up/down (edges are black, so it just clips)

// 98px-wide art centered on the 128px-wide OLED; the leftover right margin
// (x >= 113) is where the BLE status dot lives, so nothing overlaps.
#define SPRITE_X_OFFSET ((OLED_WIDTH - 98) / 2)
#define SPRITE_Y_OFFSET 0

#endif // DIRGAMOCHI_CONFIG_H
