#!/usr/bin/env python3
"""
Converts the Dirgamochi bitmap face animations and voice clips into
generated C++ sources, before every build:

  assets/faces/mochi_0.zip .. mochi_9.zip
      Each a zip of numbered PNG frames (any depth of subfolder - only the
      leading number in each filename matters), one "mood" animation.
      -> src/generated/face_frames.cpp + include/generated/face_frames.h

  assets/audio/mochi_voices.zip
      mochi_001.wav .. mochi_010.wav (one voice clip per mood, in the same
      order as the face zips) + mochi_blink.wav, all IMA ADPCM (WAV format
      tag 0x11), mono, 16000 Hz, rendered to match their mood's frame count
      at 10 fps.
      -> src/generated/voice_clips.cpp + include/generated/voice_clips.h

Why generated instead of committed: PNG/WAV data is already compressed,
but a text C array of the same bytes is much bigger (each byte becomes
"0xNN, " - roughly 5-6x larger) and diffs terribly in git. So the repo
keeps the small zips under assets/, and this script regenerates the actual
firmware sources every build - either automatically (platformio.ini's
`extra_scripts = pre:` hook, for local `pio run`) or as an explicit CI
step (see .github/workflows/build.yml). Output is .gitignore'd.

Usage:
    python3 tools/generate_assets.py [--force]
    (or invoked automatically by PlatformIO as a pre: extra_script)

Requires: Pillow (`pip install pillow`). The audio conversion uses only
the standard library (struct) - no extra dependency.
"""

import os
import re
import sys
import zipfile
import io
import struct

# ---------------------------------------------------------------------------
# Detect whether we're running as a PlatformIO extra_script (SCons injects an
# `env` global and an `Import` builtin) or as a plain standalone script.
# ---------------------------------------------------------------------------
try:
    Import("env")  # noqa: F821 - only defined under PlatformIO/SCons
    PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
    _PIO_MODE = True
except NameError:
    PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    _PIO_MODE = False

try:
    from PIL import Image
except ImportError:
    if _PIO_MODE:
        # PlatformIO runs extra scripts inside its own Python environment,
        # which usually doesn't have Pillow - install it there on demand.
        env.Execute("$PYTHONEXE -m pip install pillow")  # noqa: F821
        from PIL import Image
    else:
        print("[generate_assets] ERROR: Pillow is required (`pip install pillow`).", file=sys.stderr)
        sys.exit(1)

FACES_DIR = os.path.join(PROJECT_DIR, "assets", "faces")
AUDIO_ZIP = os.path.join(PROJECT_DIR, "assets", "audio", "mochi_voices.zip")

OUT_HEADER_DIR = os.path.join(PROJECT_DIR, "include", "generated")
OUT_SOURCE_DIR = os.path.join(PROJECT_DIR, "src", "generated")

FACE_OUT_HEADER = os.path.join(OUT_HEADER_DIR, "face_frames.h")
FACE_OUT_SOURCE = os.path.join(OUT_SOURCE_DIR, "face_frames.cpp")
VOICE_OUT_HEADER = os.path.join(OUT_HEADER_DIR, "voice_clips.h")
VOICE_OUT_SOURCE = os.path.join(OUT_SOURCE_DIR, "voice_clips.cpp")
LOGO_PNG = os.path.join(PROJECT_DIR, "assets", "misc", "sleep_logo.png")
LOGO_OUT_HEADER = os.path.join(OUT_HEADER_DIR, "sleep_logo.h")
LOGO_OUT_SOURCE = os.path.join(OUT_SOURCE_DIR, "sleep_logo.cpp")

FRAME_W = 98
FRAME_H = 64
OLED_W = 128
OLED_H = 64

# One real "eyes closed" frame (found by scanning every set for the darkest
# frame relative to its own resting pose) used for the idle blink, held for
# about as long as mochi_blink.wav runs, instead of the whole mood loop.
BLINK_SET = 2
BLINK_SOURCE_FRAME = 9  # 1-indexed source frame number (009_mochi_3.png)


def log(msg):
    print(f"[generate_assets] {msg}")


def is_up_to_date(out_paths, input_paths):
    if "--force" in sys.argv:
        return False
    if not all(os.path.exists(p) for p in out_paths):
        return False
    newest_input = max(os.path.getmtime(p) for p in input_paths)
    oldest_output = min(os.path.getmtime(p) for p in out_paths)
    return oldest_output >= newest_input


# ===========================================================================
# Faces: PNG zips -> 1-bit PROGMEM bitmaps
# ===========================================================================

def pack_1bit(img):
    """Pack a PIL '1' mode image into MSB-first, byte-per-row-boundary bytes
    - the format Adafruit_GFX::drawBitmap() expects. A set bit = lit pixel.
    """
    w, h = img.size
    row_bytes = (w + 7) // 8
    out = bytearray(row_bytes * h)
    px = img.load()
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            if px[x, y]:
                out[base + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def load_and_pack_png(png_bytes):
    im = Image.open(io.BytesIO(png_bytes)).convert("L")
    if im.size != (FRAME_W, FRAME_H):
        im = im.resize((FRAME_W, FRAME_H))
    # Default Pillow dithering (Floyd-Steinberg) when going L -> 1 keeps the
    # source art's soft gradients/shading legible on a 1-bit OLED, instead of
    # a harsh flat 50% threshold.
    return pack_1bit(im.convert("1"))


def frame_sort_key(name):
    m = re.match(r"(\d+)_", os.path.basename(name))
    return int(m.group(1)) if m else 0


def format_byte_array(sym, data, storage="static const unsigned char"):
    lines = [f"{storage} {sym}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), 16):
        row = ", ".join(f"0x{b:02X}" for b in data[i : i + 16])
        lines.append(f"    {row},")
    lines.append("};")
    return "\n".join(lines)


def format_pointer_table(sym, item_syms, storage="static const unsigned char *const"):
    lines = [f"{storage} {sym}[{len(item_syms)}] PROGMEM = {{"]
    for i in range(0, len(item_syms), 8):
        row = ", ".join(item_syms[i : i + 8])
        lines.append(f"    {row},")
    lines.append("};")
    return "\n".join(lines)


def generate_faces():
    if not os.path.isdir(FACES_DIR):
        log(f"no {FACES_DIR} - skipping face generation")
        return

    zips = sorted(f for f in os.listdir(FACES_DIR) if re.match(r"mochi_\d+\.zip$", f))
    if not zips:
        log(f"no mochi_*.zip in {FACES_DIR} - skipping face generation")
        return

    zpaths = [os.path.join(FACES_DIR, z) for z in zips]
    if is_up_to_date([FACE_OUT_SOURCE, FACE_OUT_HEADER], zpaths + [os.path.abspath(__file__)]):
        log("face sources are up to date - skipping (use --force to rebuild)")
        return

    os.makedirs(OUT_HEADER_DIR, exist_ok=True)
    os.makedirs(OUT_SOURCE_DIR, exist_ok=True)

    anim_frame_counts = []
    cpp_chunks = []
    table_chunks = []

    for zip_name in zips:
        set_index = int(re.match(r"mochi_(\d+)\.zip$", zip_name).group(1))
        with zipfile.ZipFile(os.path.join(FACES_DIR, zip_name)) as zf:
            names = sorted(
                (n for n in zf.namelist() if n.lower().endswith(".png")),
                key=frame_sort_key,
            )
            log(f"{zip_name}: {len(names)} frames")
            anim_frame_counts.append(len(names))

            frame_syms = []
            for pos, name in enumerate(names):
                data = load_and_pack_png(zf.read(name))
                sym = f"faceSet{set_index}Frame{pos}"
                frame_syms.append(sym)
                cpp_chunks.append(format_byte_array(sym, data))

            table_chunks.append(format_pointer_table(f"faceSet{set_index}Frames", frame_syms))

    with open(FACE_OUT_SOURCE, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_assets.py - do not edit by hand.\n")
        f.write('#include "generated/face_frames.h"\n\n')
        f.write("\n".join(cpp_chunks) + "\n\n")
        f.write("\n".join(table_chunks) + "\n\n")
        f.write("const uint16_t faceAnimFrameCount[FACE_ANIM_COUNT] PROGMEM = {\n    ")
        f.write(", ".join(str(c) for c in anim_frame_counts))
        f.write("\n};\n\n")
        f.write("const unsigned char *const *const faceAnimFrames[FACE_ANIM_COUNT] = {\n    ")
        f.write(", ".join(f"faceSet{i}Frames" for i in range(len(zips))))
        f.write("\n};\n")

    with open(FACE_OUT_HEADER, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_assets.py - do not edit by hand.\n")
        f.write("#ifndef FACE_FRAMES_H\n#define FACE_FRAMES_H\n\n#include <Arduino.h>\n\n")
        f.write(f"#define FACE_FRAME_W {FRAME_W}\n#define FACE_FRAME_H {FRAME_H}\n")
        f.write(f"#define FACE_ANIM_COUNT {len(zips)}\n\n")
        f.write(f"#define FACE_BLINK_SET {BLINK_SET}\n")
        f.write(f"#define FACE_BLINK_FRAME {BLINK_SOURCE_FRAME - 1}\n\n")  # 0-indexed
        f.write("// faceAnimFrameCount[set] = frame count; faceAnimFrames[set][frame] =\n")
        f.write("// pointer to that frame's packed 1-bit bitmap (FACE_FRAME_W x FACE_FRAME_H,\n")
        f.write("// MSB-first, Adafruit_GFX::drawBitmap() layout).\n")
        f.write("extern const uint16_t faceAnimFrameCount[FACE_ANIM_COUNT];\n")
        f.write("extern const unsigned char *const *const faceAnimFrames[FACE_ANIM_COUNT];\n\n")
        f.write("#endif // FACE_FRAMES_H\n")

    total_bytes = sum(anim_frame_counts) * ((FRAME_W + 7) // 8) * FRAME_H
    log(f"faces: {len(zips)} animations, {sum(anim_frame_counts)} frames, "
        f"~{total_bytes/1024:.0f} KiB -> {FACE_OUT_SOURCE}")


# ===========================================================================
# Audio: IMA ADPCM WAVs (already 4-bit compressed) -> raw PROGMEM byte blobs
# ===========================================================================
# Firmware decodes these itself (see AudioEngine's ADPCM streamer) - we only
# strip the WAV container here and keep the compressed nibble data as-is,
# since re-encoding or pre-decoding to PCM16 would be either lossy or ~4x
# bigger in flash for no benefit.

def parse_wav_adpcm(wav_bytes):
    """Returns (adpcm_data_bytes, block_align, sample_rate). Raises if the
    file isn't IMA ADPCM (WAV format tag 0x11) mono."""
    if wav_bytes[:4] != b"RIFF" or wav_bytes[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos = 12
    fmt = None
    data = None
    while pos < len(wav_bytes) - 8:
        cid = wav_bytes[pos : pos + 4]
        csize = struct.unpack("<I", wav_bytes[pos + 4 : pos + 8])[0]
        body = wav_bytes[pos + 8 : pos + 8 + csize]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            data = body
        pos += 8 + csize + (csize % 2)
    if fmt is None or data is None:
        raise ValueError("missing fmt/data chunk")
    tag, channels, rate, _byterate, block_align, bits = fmt
    if tag != 0x11:
        raise ValueError(f"expected IMA ADPCM (tag 0x11), got 0x{tag:02X}")
    if channels != 1:
        raise ValueError(f"expected mono, got {channels} channels")
    if bits != 4:
        raise ValueError(f"expected 4-bit ADPCM, got {bits}-bit")
    return data, block_align, rate


def voice_sym(name):
    # "mochi_001" -> ("faceSet0Voice", set_index 0), "mochi_blink" -> None
    m = re.match(r"mochi_(\d+)$", name)
    if m:
        idx = int(m.group(1)) - 1
        return f"faceSet{idx}Voice", idx
    return None, None


def generate_audio():
    if not os.path.exists(AUDIO_ZIP):
        log(f"no {AUDIO_ZIP} - skipping audio generation")
        return

    if is_up_to_date([VOICE_OUT_SOURCE, VOICE_OUT_HEADER], [AUDIO_ZIP, os.path.abspath(__file__)]):
        log("voice sources are up to date - skipping (use --force to rebuild)")
        return

    os.makedirs(OUT_HEADER_DIR, exist_ok=True)
    os.makedirs(OUT_SOURCE_DIR, exist_ok=True)

    cpp_chunks = []
    mood_entries = {}  # set index -> (sym, len)
    blink_entry = None  # (sym, len)
    sample_rate = 16000
    block_align = 256

    with zipfile.ZipFile(AUDIO_ZIP) as zf:
        names = sorted(n for n in zf.namelist() if n.lower().endswith(".wav"))
        for name in names:
            stem = os.path.splitext(os.path.basename(name))[0]
            adpcm, block_align, sample_rate = parse_wav_adpcm(zf.read(name))

            if stem == "mochi_blink":
                sym = "blinkVoiceData"
                cpp_chunks.append(format_byte_array(sym, adpcm))
                blink_entry = (sym, len(adpcm))
                log(f"{name}: blink clip, {len(adpcm)} bytes ADPCM")
                continue

            sym_base, set_index = voice_sym(stem)
            if sym_base is None:
                log(f"{name}: unrecognized filename, skipping")
                continue
            sym = f"{sym_base}Data"
            cpp_chunks.append(format_byte_array(sym, adpcm))
            mood_entries[set_index] = (sym, len(adpcm))
            log(f"{name}: mood {set_index}, {len(adpcm)} bytes ADPCM")

    if not mood_entries and blink_entry is None:
        log("no recognizable voice clips found - skipping")
        return

    mood_count = (max(mood_entries.keys()) + 1) if mood_entries else 0

    with open(VOICE_OUT_SOURCE, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_assets.py - do not edit by hand.\n")
        f.write('#include "generated/voice_clips.h"\n\n')
        f.write("\n".join(cpp_chunks) + "\n\n")

        f.write("const unsigned char *const moodVoiceData[VOICE_MOOD_COUNT] = {\n    ")
        f.write(", ".join(mood_entries[i][0] if i in mood_entries else "nullptr" for i in range(mood_count)))
        f.write("\n};\n")
        f.write("const uint32_t moodVoiceLength[VOICE_MOOD_COUNT] PROGMEM = {\n    ")
        f.write(", ".join(str(mood_entries[i][1]) if i in mood_entries else "0" for i in range(mood_count)))
        f.write("\n};\n\n")

        if blink_entry:
            f.write(f"const unsigned char *const blinkVoiceData_ = {blink_entry[0]};\n")
            f.write(f"const uint32_t blinkVoiceLength = {blink_entry[1]};\n")
        else:
            f.write("const unsigned char *const blinkVoiceData_ = nullptr;\n")
            f.write("const uint32_t blinkVoiceLength = 0;\n")

    with open(VOICE_OUT_HEADER, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_assets.py - do not edit by hand.\n")
        f.write("#ifndef VOICE_CLIPS_H\n#define VOICE_CLIPS_H\n\n#include <Arduino.h>\n\n")
        f.write(f"#define VOICE_MOOD_COUNT {mood_count}\n")
        f.write(f"#define VOICE_SAMPLE_RATE {sample_rate}\n")
        f.write(f"#define VOICE_BLOCK_ALIGN {block_align}\n\n")
        f.write("// IMA ADPCM (WAV tag 0x11) raw data, one clip per mood animation, decoded\n")
        f.write("// and streamed by AudioEngine's ADPCM player. moodVoiceData[i]/[Length] may\n")
        f.write("// be nullptr/0 for a mood with no matching clip.\n")
        f.write("extern const unsigned char *const moodVoiceData[VOICE_MOOD_COUNT];\n")
        f.write("extern const uint32_t moodVoiceLength[VOICE_MOOD_COUNT];\n\n")
        f.write("extern const unsigned char *const blinkVoiceData_;\n")
        f.write("extern const uint32_t blinkVoiceLength;\n\n")
        f.write("#endif // VOICE_CLIPS_H\n")

    total_bytes = sum(mood_entries[i][1] for i in mood_entries) + (blink_entry[1] if blink_entry else 0)
    log(f"audio: {len(mood_entries)} mood clip(s) + {'1' if blink_entry else '0'} blink clip, "
        f"~{total_bytes/1024:.0f} KiB ADPCM -> {VOICE_OUT_SOURCE}")


# ===========================================================================
# Sleep logo: one static PNG -> one 128x64 1-bit PROGMEM bitmap
# ===========================================================================

def generate_logo():
    if not os.path.exists(LOGO_PNG):
        log(f"no {LOGO_PNG} - skipping sleep logo generation")
        return

    if is_up_to_date([LOGO_OUT_SOURCE, LOGO_OUT_HEADER], [LOGO_PNG, os.path.abspath(__file__)]):
        log("sleep logo source is up to date - skipping (use --force to rebuild)")
        return

    os.makedirs(OUT_HEADER_DIR, exist_ok=True)
    os.makedirs(OUT_SOURCE_DIR, exist_ok=True)

    src = Image.open(LOGO_PNG).convert("RGBA")
    # Composite onto black first: transparent areas become "off" pixels and
    # the logo's light linework becomes "lit" pixels, matching how every
    # other bitmap on this OLED is authored (lit-on-black), regardless of
    # whether the source PNG uses real transparency or a white background.
    black_bg = Image.new("RGBA", src.size, (0, 0, 0, 255))
    comp = Image.alpha_composite(black_bg, src).convert("L")

    # Fit within the full 128x64 panel preserving aspect ratio (the source
    # is portrait; this OLED is wide), then center on a black canvas.
    scale = min(OLED_W / comp.width, OLED_H / comp.height)
    new_w, new_h = max(1, int(comp.width * scale)), max(1, int(comp.height * scale))
    resized = comp.resize((new_w, new_h), Image.LANCZOS)
    canvas = Image.new("L", (OLED_W, OLED_H), 0)
    canvas.paste(resized, ((OLED_W - new_w) // 2, (OLED_H - new_h) // 2))

    data = pack_1bit(canvas.convert("1"))

    with open(LOGO_OUT_SOURCE, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_assets.py - do not edit by hand.\n")
        f.write('#include "generated/sleep_logo.h"\n\n')
        f.write(format_byte_array("sleepLogoBitmap", data, storage="const unsigned char"))
        f.write("\n")

    with open(LOGO_OUT_HEADER, "w", encoding="utf-8") as f:
        f.write("// AUTO-GENERATED by tools/generate_assets.py - do not edit by hand.\n")
        f.write("#ifndef SLEEP_LOGO_H_\n#define SLEEP_LOGO_H_\n\n#include <Arduino.h>\n\n")
        f.write(f"#define SLEEP_LOGO_WIDTH {OLED_W}\n#define SLEEP_LOGO_HEIGHT {OLED_H}\n\n")
        f.write("// Full-panel (128x64), MSB-first packed 1-bit bitmap - lit pixel = logo,\n")
        f.write("// unlit = background. Shown in place of the face while ambient-sleepy\n")
        f.write("// (quiet hours / idle timeout). See assets/misc/sleep_logo.png.\n")
        f.write("extern const unsigned char sleepLogoBitmap[];\n\n")
        f.write("#endif // SLEEP_LOGO_H_\n")

    log(f"logo: {new_w}x{new_h} fitted into {OLED_W}x{OLED_H} -> {LOGO_OUT_SOURCE}")


generate_faces()
generate_audio()
generate_logo()
