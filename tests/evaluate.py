"""Оценка точности: python3 tests/evaluate.py CLI DIR [доп. флаги CLI]"""
import csv
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def short(t, m):
    return NOTES[t] + ("m" if m else "")


def rel(t, m):
    return ((t + 3) % 12, False) if m else ((t + 9) % 12, True)


def main():
    cli, d = sys.argv[1], sys.argv[2]
    flags = sys.argv[3:]
    rows = list(csv.DictReader(open(os.path.join(d, "truth.csv"))))

    def run(r):
        out = subprocess.run([cli, os.path.join(d, r["file"])] + flags, capture_output=True, text=True).stdout
        tok = out.split()
        return r, dict(zip(tok[0::2], tok[1::2]))

    with ThreadPoolExecutor(4) as ex:
        results = list(ex.map(run, rows))

    n = exact = pair = 0
    nb = bpm_ok = bpm_oct = 0
    errs = []
    for r, res in results:
        t, m = int(r["tonic"]), bool(int(r["minor"]))
        truth = short(t, m)
        relk = short(*rel(t, m))
        got = res["KEY"]
        n += 1
        exact += got == truth
        pair += got in (truth, relk)
        line = f"{r['file']:28s} key {truth:4s} -> {got:4s} conf {res['CONF']}"
        mark = "" if got == truth else (" (параллельная)" if got == relk else "  <<< ОШИБКА")
        line += mark
        if r["bpm"]:
            tb, gb = float(r["bpm"]), float(res["BPM"])
            nb += 1
            e = abs(gb - tb)
            ok = e <= 0.1
            octv = min(abs(gb - 2 * tb), abs(gb - tb / 2)) <= 0.1
            bpm_ok += ok
            bpm_oct += ok or octv
            if ok:
                errs.append(e)
            line += f" | bpm {tb:7.3f} -> {gb:7.3f} conf {res['TCONF']}" + ("" if ok else (" (x2 / ÷2)" if octv else "  <<< ОШИБКА"))
        print(line)

    print()
    print(f"Тональность точно: {exact}/{n}   с учётом параллельной: {pair}/{n}")
    if nb:
        mae = sum(errs) / max(1, len(errs))
        print(f"BPM ±0.1: {bpm_ok}/{nb}   с учётом x2/÷2: {bpm_oct}/{nb}   средняя ошибка (верные): {mae:.3f}")


if __name__ == "__main__":
    main()
