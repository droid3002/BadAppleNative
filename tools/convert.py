#!/usr/bin/env python3

import os
import subprocess
import struct
import sys

INPUT = "bad.mp4"
OUTPUT = "badapple.dat"
AUDIO_OUTPUT = "badapple_audio.dat"

WIDTH = 160
HEIGHT = 120
FPS = 30

AUDIO_SAMPLE_RATE = 22050
AUDIO_CHANNELS = 1
AUDIO_BITS = 16

if not os.path.exists(INPUT):
    print(f"'{INPUT}' not found.")
    sys.exit(1)

print("=" * 50)
print("Bad Apple!! Media Converter")
print("=" * 50)

# ============================================================
# Step 1: Extract and convert video frames
# ============================================================
print("\n[Video] Extracting frames with ffmpeg...")

cmd = [
    "ffmpeg",
    "-y",
    "-i", INPUT,
    "-vf", f"fps={FPS},scale={WIDTH}:{HEIGHT},format=gray",
    "-f", "rawvideo",
    "-"
]

proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)

frame_size = WIDTH * HEIGHT

frames = 0

with open(OUTPUT, "wb") as out:

    # Header: "BAPL" + width(u16) + height(u16) + fps(u16) + frameCount(u32)
    out.write(b"BAPL")
    out.write(struct.pack("<H", WIDTH))
    out.write(struct.pack("<H", HEIGHT))
    out.write(struct.pack("<H", FPS))

    # Placeholder for frame count
    out.write(struct.pack("<I", 0))

    while True:

        frame = proc.stdout.read(frame_size)

        if len(frame) != frame_size:
            break

        packed = bytearray()

        for i in range(0, frame_size, 8):

            b = 0

            for bit in range(8):

                if frame[i + bit] >= 128:
                    b |= 1 << (7 - bit)

            packed.append(b)

        out.write(packed)

        frames += 1

        if frames % 100 == 0:
            print(f"  [Video] {frames} frames")

    proc.wait()

    # Write frame count back into header
    out.seek(10)
    out.write(struct.pack("<I", frames))

print(f"  [Video] Done: {frames} frames")
print(f"  [Video] Output: {OUTPUT} ({os.path.getsize(OUTPUT)} bytes)")

# ============================================================
# Step 2: Extract audio track as raw PCM
# ============================================================
print(f"\n[Audio] Extracting audio ({AUDIO_BITS}bit {AUDIO_SAMPLE_RATE}Hz mono)...")

audio_cmd = [
    "ffmpeg",
    "-y",
    "-i", INPUT,
    "-vn",
    "-f", "s16le",
    "-acodec", "pcm_s16le",
    "-ar", str(AUDIO_SAMPLE_RATE),
    "-ac", str(AUDIO_CHANNELS),
    "-"
]

audio_proc = subprocess.Popen(audio_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
audio_data, audio_err = audio_proc.communicate()

if audio_proc.returncode != 0:
    print(f"  [Audio] ffmpeg error: {audio_err.decode()}")
    print("  [Audio] Continuing without audio...")
    audio_data = b""

if audio_data:
    with open(AUDIO_OUTPUT, "wb") as out:
        out.write(audio_data)

    duration = len(audio_data) / (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS // 8))
    print(f"  [Audio] Done: {len(audio_data)} bytes")
    print(f"  [Audio] Duration: {duration:.1f}s")
    print(f"  [Audio] Output: {AUDIO_OUTPUT}")
else:
    print("  [Audio] No audio data extracted")

# ============================================================
# Summary
# ============================================================
print("\n" + "=" * 50)
print("Conversion complete!")
print(f"  Video: {OUTPUT} ({os.path.getsize(OUTPUT)} bytes, {frames} frames)")
if os.path.exists(AUDIO_OUTPUT):
    print(f"  Audio: {AUDIO_OUTPUT} ({os.path.getsize(AUDIO_OUTPUT)} bytes)")
print(f"  FPS: {FPS}")
print(f"  Resolution: {WIDTH}x{HEIGHT}")
print("=" * 50)
