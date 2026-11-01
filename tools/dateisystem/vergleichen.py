#!/usr/bin/env python3
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
"""Vergleicht die Liste des Spiels (-dateiIndexAusgeben) mit der von VfsDump.

    vergleichen.py <Liste des Spiels> <Liste von VfsDump>

Rückgabewert 0, wenn beide Listen jeden Pfad demselben Archiv zuordnen. Sonst nennt das Skript die
Zahl der Abweichungen je Art und die ersten Beispiele. Ein Pfad, den das Spiel mit `-` führt, war im
alten Code nicht auffindbar (Pfad ohne Endung); das neue Dateisystem findet
ihn. Solche Pfade werden getrennt genannt und zählen nicht als Abweichung. Die Listen werden bytegenau gelesen (Latin-1),
Zeilenenden CRLF und LF gelten gleich.
"""
import sys


def lesen(pfad):
    eintraege = {}
    with open(pfad, "rb") as f:
        for zeile in f.read().decode("latin-1").splitlines():
            if not zeile:
                continue
            schluessel, _, archiv = zeile.partition("\t")
            eintraege[schluessel] = archiv
    return eintraege


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    spiel, neu = lesen(sys.argv[1]), lesen(sys.argv[2])
    nur_spiel = sorted(set(spiel) - set(neu))
    nur_neu = sorted(set(neu) - set(spiel))
    alt_unerreichbar = sorted(k for k in set(spiel) & set(neu) if spiel[k] == "-")
    anders = sorted(k for k in set(spiel) & set(neu) if spiel[k] != neu[k] and spiel[k] != "-")
    print(f"Spiel {len(spiel)} Pfade, VfsDump {len(neu)} Pfade")
    print(f"nur im Spiel {len(nur_spiel)}, nur in VfsDump {len(nur_neu)}, anderes Archiv {len(anders)}")
    for k in alt_unerreichbar:
        print(f"  im alten Code nicht auffindbar, jetzt aus {neu[k]}: {k}")
    for titel, liste, zeigen in (("nur im Spiel", nur_spiel, lambda k: spiel[k]),
                                 ("nur in VfsDump", nur_neu, lambda k: neu[k]),
                                 ("anderes Archiv", anders, lambda k: f"Spiel {spiel[k]}, VfsDump {neu[k]}")):
        for k in liste[:10]:
            print(f"  {titel}: {k}  ({zeigen(k)})")
    gleich = not (nur_spiel or nur_neu or anders)
    print("gleich" if gleich else "ABWEICHUNG")
    return 0 if gleich else 1


if __name__ == "__main__":
    sys.exit(main())
