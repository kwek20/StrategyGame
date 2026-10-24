"""Generate original deterministic PCM effects; no downloaded audio or runtime synthesis."""
from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parents[1] / "assets/audio/combat"
RATE = 48000

def write(name, seconds, seed, bass, decay, noise_gain):
    rng = random.Random(seed)
    low_noise = 0.0
    samples = []
    for i in range(int(RATE * seconds)):
        t = i / RATE
        noise = rng.uniform(-1, 1)
        low_noise += (noise - low_noise) * .12
        attack = min(1, t / .002)
        thump = math.sin(2 * math.pi * (bass * t + 18 * (1 - math.exp(-12*t))))
        body = thump * math.exp(-decay*t) * .6
        crack = noise * math.exp(-65*t) * .42
        tail = low_noise * math.exp(-3.5*t) * noise_gain
        metal = math.sin(2 * math.pi * 730*t) * math.exp(-24*t) * .09
        fade = min(1, (seconds-t) / .03)
        samples.append(max(-.93, min(.93, attack * fade * (body + crack + tail + metal))))
    ROOT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(ROOT / name), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(struct.pack("<" + "h" * len(samples), *(int(s * 32767) for s in samples)))

if __name__ == "__main__":
    write("cannon_fire.wav", .65, 31, 62, 12, .7)
    write("cannon_impact.wav", .38, 51, 130, 20, 1.1)
    write("vehicle_destroyed.wav", 1.6, 71, 34, 4.5, 1.7)
