"""Генератор тестовых битов с известным BPM и тональностью.

python3 tests/gen_signals.py OUT_DIR [count] [seed]
Пишет WAV-файлы и truth.csv (file,bpm,tonic,minor).
"""
import csv
import os
import sys

import numpy as np

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
MAJ = [0, 2, 4, 5, 7, 9, 11]
MIN = [0, 2, 3, 5, 7, 8, 10]


def midi_hz(m, tune_cents=0.0):
    return 440.0 * 2 ** ((m - 69 + tune_cents / 100.0) / 12.0)


class Mix:
    def __init__(self, seconds, sr):
        self.sr = sr
        self.buf = np.zeros(int(seconds * sr) + sr * 3)

    def add(self, t, sig, gain=1.0):
        i = int(t * self.sr)
        if i < 0 or i >= len(self.buf):
            return
        n = min(len(sig), len(self.buf) - i)
        self.buf[i:i + n] += gain * sig[:n]


def env(sr, dur, attack=0.005, decay=1.0):
    n = int(dur * sr)
    t = np.arange(n) / sr
    e = np.exp(-t / decay)
    a = int(attack * sr)
    if a > 0:
        e[:a] *= np.linspace(0, 1, a)
    return t, e


def tone(sr, f, dur, harmonics, decay, attack=0.005, inharm=0.0):
    t, e = env(sr, dur, attack, decay)
    s = np.zeros_like(t)
    for h, amp in harmonics:
        fh = f * h * np.sqrt(1 + inharm * h * h)
        if fh < sr / 2 - 200:
            s += amp * np.sin(2 * np.pi * fh * t + np.random.rand() * 6.28) * np.exp(-t * (h - 1) * 0.8)
    return s * e


def piano(sr, f, dur):
    return tone(sr, f, dur, [(h, 1.0 / h) for h in range(1, 9)], decay=0.9, inharm=0.0002)


def pad(sr, f, dur):
    t, _ = env(sr, dur, 0.08, 100)
    s = np.zeros_like(t)
    for h in range(1, 16):
        if f * h < sr / 2 - 200:
            s += (1.0 / h ** 1.6) * np.sin(2 * np.pi * f * h * t * (1 + 0.002 * np.sin(2 * np.pi * 0.3 * t)))
    rel = np.ones_like(t)
    r = int(0.15 * sr)
    rel[-r:] = np.linspace(1, 0, r)
    return s * rel * np.minimum(1, t / 0.08)


def pluck(sr, f, dur):
    return tone(sr, f, dur, [(1, 1), (2, 0.5), (3, 0.35), (4, 0.2), (5, 0.1)], decay=0.25)


def bell(sr, f, dur):
    return tone(sr, f, dur, [(1, 1), (2, 0.3), (3, 0.6), (4.2, 0.25), (5.4, 0.15)], decay=0.6)


def s808(sr, f, dur, slide_from=None):
    n = int(dur * sr)
    t = np.arange(n) / sr
    freq = f * (1 + 1.5 * np.exp(-t / 0.012))
    if slide_from is not None:
        freq = f + (slide_from - f) * np.exp(-t / 0.06)
    ph = 2 * np.pi * np.cumsum(freq) / sr
    s = np.tanh(2.5 * np.sin(ph)) * np.exp(-t / 0.9)
    rel = int(0.02 * sr)
    s[-rel:] *= np.linspace(1, 0, rel)
    return s


def kick(sr):
    n = int(0.35 * sr)
    t = np.arange(n) / sr
    freq = 45 + 110 * np.exp(-t / 0.03)
    ph = 2 * np.pi * np.cumsum(freq) / sr
    return np.sin(ph) * np.exp(-t / 0.12)


def noise_hp(n, order=2):
    x = np.random.randn(n + order)
    for _ in range(order):
        x = np.diff(x)
    return x[:n] / (2 ** order)


def snare(sr):
    n = int(0.25 * sr)
    t = np.arange(n) / sr
    return 0.8 * noise_hp(n, 1) * np.exp(-t / 0.07) + 0.5 * np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.05)


def clap(sr):
    n = int(0.25 * sr)
    t = np.arange(n) / sr
    e = np.exp(-t / 0.06)
    for d in (0.0, 0.01, 0.02):
        e += 0.6 * np.exp(-np.maximum(0, t - d) / 0.005) * (t >= d)
    return noise_hp(n, 1) * e * 0.7


def hat(sr, open_=False):
    n = int((0.25 if open_ else 0.06) * sr)
    t = np.arange(n) / sr
    return noise_hp(n, 3) * np.exp(-t / (0.08 if open_ else 0.015)) * 1.2


def chord_of(tonic, minor, degree, seventh=False, harmonic_v=False):
    scale = MIN if minor else MAJ
    notes = []
    for k in (0, 2, 4) + ((6,) if seventh else ()):
        idx = degree + k
        semis = scale[idx % 7] + 12 * (idx // 7)
        notes.append(semis)
    if minor and harmonic_v and degree == 4:
        notes[1] += 1  # мажорный V (гармонический минор)
    return [tonic + s for s in notes]


PROGRESSIONS_MIN = [[0, 5, 2, 6], [0, 3, 4, 0], [0, 5, 3, 4], [0, 0, 5, 6], [0, 3, 5, 4], [0, 6, 5, 6]]
PROGRESSIONS_MAJ = [[0, 4, 5, 3], [0, 3, 0, 4], [0, 5, 1, 4], [0, 3, 4, 3], [0, 5, 3, 4]]

STYLES = {
    "trap": dict(bpm=(130, 160), minor_p=0.85),
    "boombap": dict(bpm=(82, 98), minor_p=0.7),
    "drill": dict(bpm=(138, 146), minor_p=0.95),
    "rnb": dict(bpm=(62, 80), minor_p=0.4),
    "pop": dict(bpm=(95, 128), minor_p=0.4),
}


def make_track(style, seconds, sr, rng, bpm=None, tonic=None, minor=None, tune=0.0, drums=True, harmony=True):
    cfg = STYLES[style]
    if bpm is None:
        bpm = float(rng.uniform(*cfg["bpm"]))
        if rng.random() < 0.6:
            bpm = float(round(bpm))
    if tonic is None:
        tonic = int(rng.integers(12))
    if minor is None:
        minor = bool(rng.random() < cfg["minor_p"])

    beat = 60.0 / bpm
    bar = 4 * beat
    m = Mix(seconds, sr)
    prog = (PROGRESSIONS_MIN if minor else PROGRESSIONS_MAJ)[rng.integers(len(PROGRESSIONS_MIN if minor else PROGRESSIONS_MAJ))]
    harmonic_v = minor and rng.random() < 0.4
    inst = [piano, pad, pluck, bell][rng.integers(4)]
    lead = [pluck, bell, piano][rng.integers(3)]
    swing = 0.58 if style == "boombap" else 0.5
    nbars = int(seconds / bar) + 1
    scale = MIN if minor else MAJ
    mel_note = 4

    for b in range(nbars):
        t0 = b * bar
        deg = prog[b % len(prog)]
        ch = chord_of(tonic, minor, deg, seventh=rng.random() < 0.3, harmonic_v=harmonic_v)
        root = ch[0]

        if harmony:
            # аккорд
            base = 55 + ((root - 55) % 12)
            voicing = [base + (n - root) for n in ch]
            if inst is pad:
                for n in voicing:
                    m.add(t0, pad(sr, midi_hz(n, tune), bar), 0.10)
            else:
                hits = [0, 1.5, 2.5] if style in ("trap", "drill") else [0, 2]
                for h in hits:
                    for n in voicing:
                        m.add(t0 + h * beat, inst(sr, midi_hz(n, tune), 1.6), 0.10)
            # мелодия
            for s in range(8):
                if rng.random() < 0.55:
                    mel_note = int(np.clip(mel_note + rng.integers(-2, 3), 0, 9))
                    if rng.random() < 0.5:
                        cands = [i for i in range(10) if (scale[i % 7] + 12 * (i // 7)) % 12 in [(n - tonic) % 12 for n in ch]]
                        mel_note = min(cands, key=lambda c: abs(c - mel_note))
                    semis = scale[mel_note % 7] + 12 * (mel_note // 7)
                    if harmonic_v and deg == 4 and mel_note % 7 == 6:
                        semis += 1
                    n = 67 + ((tonic - 67) % 12) - 12 + semis + 12
                    st = t0 + s * beat / 2 + (swing - 0.5) * beat * (s % 2)
                    m.add(st, lead(sr, midi_hz(n, tune), 0.6), 0.09)

        if drums:
            if style in ("trap", "drill"):
                kicks = [0, 0.75, 2.5] if style == "trap" else [0, 1.75, 2.75]
                for k in kicks:
                    m.add(t0 + k * beat, kick(sr), 0.6)
                    f808 = midi_hz(28 + ((root - 28) % 12), tune)
                    m.add(t0 + k * beat, s808(sr, f808, beat * 1.4), 0.35)
                m.add(t0 + 2 * beat, clap(sr) if style == "trap" else snare(sr), 0.5)
                if style == "drill":
                    m.add(t0 + 3.5 * beat, snare(sr), 0.35)
                # хэты
                step = beat / 2
                pos = 0.0
                while pos < bar - 1e-6:
                    if style == "drill" and rng.random() < 0.3:
                        for j in range(3):
                            m.add(t0 + pos + j * step / 3, hat(sr), 0.25)
                    elif rng.random() < 0.25:
                        for j in range(4):
                            m.add(t0 + pos + j * step / 4, hat(sr), 0.2)
                    else:
                        m.add(t0 + pos, hat(sr), 0.3)
                    pos += step
            else:
                kicks = [0, 1.5, 2.5] if style == "boombap" else [0, 2, 2.5]
                for k in kicks:
                    j = rng.normal(0, 0.004) if style == "boombap" else 0
                    m.add(t0 + k * beat + j, kick(sr), 0.6)
                    if style != "boombap":
                        m.add(t0 + k * beat, s808(sr, midi_hz(28 + ((root - 28) % 12), tune), beat), 0.25)
                for sn in (1, 3):
                    j = rng.normal(0, 0.004) if style == "boombap" else 0
                    m.add(t0 + sn * beat + j, snare(sr), 0.5)
                sub = 4 if style == "rnb" else 2
                for i in range(4 * sub):
                    st = t0 + i * beat / sub
                    if sub == 2 and i % 2:
                        st += (swing - 0.5) * beat
                    m.add(st, hat(sr), 0.25)
                if style == "boombap":
                    # басовая линия вместо 808
                    m.add(t0, pluck(sr, midi_hz(40 + ((root - 40) % 12), tune), beat * 2), 0.3)

    x = m.buf[: int(seconds * sr)]
    x += 0.0005 * np.random.randn(len(x))
    x = x / (np.max(np.abs(x)) + 1e-9) * 0.9
    return x, bpm, tonic, minor


def write_wav(path, x, sr):
    import wave
    y = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(y.tobytes())


def main():
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(seed)
    np.random.seed(seed)
    styles = list(STYLES)
    rows = []
    for i in range(count):
        style = styles[i % len(styles)]
        sr = 48000 if i % 4 == 3 else 44100
        tune = float(rng.uniform(-40, 40)) if i % 5 == 4 else 0.0
        kind = "full"
        drums = harmony = True
        if i % 10 == 7:
            drums, kind = False, "nodrums"
        x, bpm, tonic, minor = make_track(style, 40.0, sr, rng, tune=tune, drums=drums, harmony=harmony)
        name = f"{i:03d}_{style}_{kind}.wav"
        write_wav(os.path.join(out, name), x, sr)
        rows.append([name, f"{bpm:.3f}" if drums else "", tonic, int(minor), f"{tune:.0f}"])
    with open(os.path.join(out, "truth.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "bpm", "tonic", "minor", "tune"])
        w.writerows(rows)


if __name__ == "__main__":
    main()
