# Reference hardware & face design notes

## Face reference

The round "kawaii" eye style (two large circular eyes with a small white
highlight dot, tiny gentle mouth, occasional blush) was modeled directly on
the builder's own reference photos of an existing SSD1306 desk companion —
the same six-expression set (**Normal / Happy / Sleepy / Surprised / Angry /
Cute**) shown in that reference grid is what `src/face_engine.cpp`
reproduces, procedurally (no bitmaps) so it's cheap to re-tune.

## Prior Chronos ESP32-C3 companion attempts (community reference)

These public repos were used as reference points for pinout conventions and
what breaks on the ESP32-C3 Super Mini + Chronos combination:

- `fbiego/chronos-esp32` — the BLE library itself; see `api.md` and
  `examples/watch/watch.ino` upstream for the full callback surface
  (contacts/SOS, camera command, sedentary/water reminders, user profile)
  that still isn't wired into the OLED UI but is available on the `watch`
  object for future screens. Alarms, QR/link relay, remote touch, and
  quiet/sleep-hours awareness *were* in this category as of v0.3 - they're
  wired up as of v0.4 (buzzer + face reaction on alarm, a QR screen, an
  ambient sleepy face, and an eye-nudge/"petting" reaction respectively).
- `MazBayID/Dasai-Mochi-Bot-firmware-for-ESP32-C3-Super-Mini` — same
  INMP441/MAX98357A/TTP223 wiring convention (SCK/BCLK=GPIO1,
  WS/LRC=GPIO2, mic SD=GPIO10, amp DIN=GPIO5, OLED SDA=21/SCL=20) that this
  project's default pin map originally followed; the mic `SD` line was
  since moved to GPIO10 to avoid sharing a strapping pin.
- A prior "Dirgamochi-C3" attempt on the same hardware combination hit a
  boot loop; this rewrite's boot-safety choices (audio disabled by
  default, no blocking waits in `setup()`/`loop()`, `huge_app.csv`
  partition table, native USB-CDC build flags, documented strapping-pin
  caveats) are aimed squarely at avoiding that failure mode. See the
  "If it boot-loops" section in the main README for the recovery steps.

## Why the face is drawn procedurally instead of stored as bitmaps

- Keeps flash/RAM usage predictable and small (no `PROGMEM` bitmap arrays
  to get the geometry wrong on, which is a common source of glitchy faces
  or, in the worst case, an out-of-bounds write into the display buffer).
- Makes it trivial to retune eye size/spacing/mouth shape in one place
  (`src/face_engine.cpp`) without regenerating image assets.
- Leaves headroom to add more expressions later purely in code.
