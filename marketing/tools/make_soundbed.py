#!/usr/bin/env python3
"""Generate the original sample-free Looksy review-film soundbed."""
import hashlib
import json
import math
import struct
import wave
from pathlib import Path


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / "media/audio/optical-pulse.wav"
    output.parent.mkdir(parents=True, exist_ok=True)
    rate, duration = 48000, 24
    with wave.open(str(output), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(rate)
        frames = bytearray()
        for index in range(rate * duration):
            time = index / rate
            envelope = math.exp(-(time % 2) * 2.8)
            fade = min(1, time / 1.4, (duration - time) / 2)
            frequencies = (130.8128, 195.9977, 261.6256)
            tone = sum(math.sin(2 * math.pi * frequency * time) * 0.022 / (n + 1)
                       for n, frequency in enumerate(frequencies))
            bell = math.sin(2 * math.pi * 523.251 * time) * envelope * 0.032
            sample = int(max(-1, min(1, (tone + bell) * fade)) * 32767)
            frames.extend(struct.pack("<h", sample))
        audio.writeframes(frames)
    receipt = {
        "id": "soundbed", "origin": "Original deterministic sine-wave composition authored for this marketing kit",
        "duration_s": duration, "sample_rate": rate, "channels": 1,
        "source_code": "tools/make_soundbed.py", "rights_status": "cleared",
        "third_party_samples": False, "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
    }
    output.with_name("provenance.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(output)


if __name__ == "__main__":
    main()
