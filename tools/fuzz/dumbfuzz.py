#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Einfacher Mutations-Fuzzer.

Braucht weder libFuzzer noch Windows: nimmt Saatdateien, veraendert sie zufaellig und ruft damit
ein Fuzzing-Ziel auf (die Ziele aus `tools/fuzz/` sind auch als normale Programme benutzbar).
Jeder Absturz -- Rueckgabecode ungleich 0, Signal oder Zeitlimit -- landet als Datei im
Fundordner, zusammen mit der Ausgabe.

    python tools/fuzz/dumbfuzz.py --target build/FuzzCompression --seeds tools/fuzz/seeds/compression \\
        --runs 20000 --out fuzz-funde

Mit einem ASan-Build des Ziels (`-fsanitize=address` bzw. `/fsanitize=address`) findet der Lauf
auch Zugriffe ueber Puffergrenzen, die sonst unbemerkt bleiben.

Exit-Code 0 = keine Funde, 1 = Funde, 2 = Aufruffehler.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
import time


def load_seeds(paths):
    seeds = []
    for path in paths:
        if os.path.isdir(path):
            for name in sorted(os.listdir(path)):
                full = os.path.join(path, name)
                if os.path.isfile(full):
                    with open(full, "rb") as f:
                        seeds.append(bytearray(f.read()))
        elif os.path.isfile(path):
            with open(path, "rb") as f:
                seeds.append(bytearray(f.read()))
    return seeds


def mutate(data, rng):
    """Ein paar einfache, wirksame Mutationen -- Bits kippen, Bytes ersetzen, kuerzen, anhaengen."""
    out = bytearray(data)
    if not out:
        out = bytearray(rng.randbytes(16))
    for _ in range(rng.randint(1, 8)):
        choice = rng.randint(0, 4)
        if choice == 0 and out:                      # Bit kippen
            i = rng.randrange(len(out))
            out[i] ^= 1 << rng.randrange(8)
        elif choice == 1 and out:                    # Byte ersetzen
            out[rng.randrange(len(out))] = rng.randrange(256)
        elif choice == 2 and len(out) > 4:           # kuerzen
            del out[rng.randrange(len(out)):]
        elif choice == 3:                            # anhaengen
            out += rng.randbytes(rng.randint(1, 32))
        elif choice == 4 and len(out) >= 4:          # interessante Zahl einsetzen
            i = rng.randrange(len(out) - 3)
            value = rng.choice([0, 1, 0x7FFFFFFF, 0xFFFFFFFF, 0x10FB, 0x11FB, 0x90FB, 0x91FB])
            out[i:i + 4] = value.to_bytes(4, rng.choice(["little", "big"]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--target", required=True, help="Fuzzing-Ziel (nimmt Dateinamen als Argumente)")
    ap.add_argument("--seeds", nargs="+", required=True, help="Saatdateien oder -ordner")
    ap.add_argument("--runs", type=int, default=5000, help="Anzahl Aufrufe")
    ap.add_argument("--minutes", type=float, help="alternativ: so lange laufen")
    ap.add_argument("--out", default="fuzz-funde", help="Ordner fuer Funde")
    ap.add_argument("--timeout", type=float, default=10.0, help="Sekunden je Aufruf")
    ap.add_argument("--seed", type=int, default=0, help="Zufalls-Seed (0 = Zeit)")
    a = ap.parse_args()

    if not os.path.isfile(a.target):
        print(f"FEHLER: Ziel {a.target} nicht gefunden")
        return 2
    seeds = load_seeds(a.seeds)
    if not seeds:
        print("FEHLER: keine Saatdateien gefunden")
        return 2

    os.makedirs(a.out, exist_ok=True)
    rng = random.Random(a.seed if a.seed else None)
    deadline = time.time() + a.minutes * 60 if a.minutes else None

    findings, run = 0, 0
    started = time.time()
    while True:
        if deadline is None and run >= a.runs:
            break
        if deadline is not None and time.time() >= deadline:
            break
        run += 1

        data = mutate(rng.choice(seeds), rng)
        fd, path = tempfile.mkstemp(suffix=".bin")
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        try:
            proc = subprocess.run([os.path.abspath(a.target), path], capture_output=True,
                                  timeout=a.timeout)
            code, output = proc.returncode, proc.stdout + proc.stderr
        except subprocess.TimeoutExpired:
            code, output = "Zeitlimit", b""

        if code != 0:
            findings += 1
            name = os.path.join(a.out, f"fund{findings:04d}")
            with open(name + ".bin", "wb") as f:
                f.write(data)
            with open(name + ".txt", "wb") as f:
                f.write(f"Rueckgabe: {code}\nLauf: {run}\n\n".encode("latin-1") + output)
            print(f"[{run}] FUND {code} -> {name}.bin ({len(data)} Byte)")
        os.remove(path)

        if run % 1000 == 0:
            print(f"[{run}] {findings} Funde, {time.time() - started:.0f} s")

    print(f"\n{run} Aufrufe, {findings} Funde in {time.time() - started:.0f} s -- Ordner {a.out}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
