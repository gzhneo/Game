# -*- coding: utf-8 -*-
# Copyright (C) 2026 GZH Neo
# Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
# (Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
# Plays the translator in a real office suite: opens a translation sheet of Babylon through UNO,
# checks layout, protection and formulas, fills the translation column and saves the sheet in the
# format the output name asks for. Runs with the Python that knows the suite's "uno" module
# (LibreOffice: python3 with python3-uno; Apache OpenOffice: program/python, Python 2.7), so the
# script is written for both Python versions.
#
#   python office_uno.py <port> <input sheet> <output sheet> <translations.tsv>
#
# translations.tsv: UTF-8, one "string ID<TAB>translation" per line; "\n" in a translation stands
# for a line break typed into the cell (Alt+Enter). The office must listen on the port
# (-accept="socket,host=127.0.0.1,port=<port>;urp;"). Exit code 0 if every check passed.
import io
import os
import sys
import time

import uno
from com.sun.star.beans import PropertyValue


def prop(name, value):
    p = PropertyValue()
    p.Name = name
    p.Value = value
    return p


def url(path):
    return uno.systemPathToFileUrl(os.path.abspath(path))


def connect(port):
    local = uno.getComponentContext()
    resolver = local.ServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", local)
    for _ in range(120):
        try:
            return resolver.resolve("uno:socket,host=127.0.0.1,port=%d;urp;StarOffice.ComponentContext" % port)
        except Exception:
            time.sleep(0.5)
    raise RuntimeError("office does not answer on port %d" % port)


def main():
    port, src, dst, tsv = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
    translations = {}
    with io.open(tsv, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line:
                key, text = line.split("\t", 1)
                translations[int(key)] = text.replace("\\n", "\n")

    ctx = connect(port)
    desktop = ctx.ServiceManager.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)
    load = [prop("Hidden", True)]
    if src.lower().endswith(".csv"):
        # what the translator picks in the import dialog: ',' '"' UTF-8
        load += [prop("FilterName", "Text - txt - csv (StarCalc)"), prop("FilterOptions", "44,34,76,1")]
    doc = desktop.loadComponentFromURL(url(src), "_blank", 0, tuple(load))
    if doc is None:
        print("FAIL cannot open %s" % src)
        return 1
    failures = []

    def expect(ok, what):
        if not ok:
            failures.append(what)

    sheet = doc.Sheets.getByIndex(0)
    cell = sheet.getCellByPosition
    language = cell(0, 0).getString()
    expect(language != "", "A1 holds the language")
    is_csv = src.lower().endswith(".csv")
    if not is_csv:
        expect(sheet.isProtected(), "sheet is protected")
        expect(cell(5, 2).CellProtection.IsLocked, "English column F is locked")
        expect(not cell(6, 2).CellProtection.IsLocked, "translation column G is editable")
        expect(sheet.Columns.getByIndex(6).IsVisible, "column G is visible")
        expect(not sheet.Columns.getByIndex(12).IsVisible, "revision column M is hidden")

    filled = 0
    row = 2
    while row < 100000:
        value = cell(4, row)
        string_id = int(value.getValue()) if value.getType().value == "VALUE" else int(value.getString() or "0")
        if string_id == -1:
            break
        if string_id in translations:
            cell(6, row).setString(translations[string_id])
            filled += 1
        row += 1
    expect(filled == len(translations), "filled %d of %d translations" % (filled, len(translations)))

    doc.calculateAll()
    if not is_csv:
        row = 2
        while cell(4, row).getValue() != -1 and row < 100000:
            text = cell(6, row).getString()
            length = cell(10, row).getValue()
            expect(int(length) == len(text), "K%d = LEN(G%d) is %d, expected %d" % (row + 1, row + 1, int(length), len(text)))
            row += 1

    lower = dst.lower()
    if lower.endswith(".xlsx"):
        options = (prop("FilterName", "Calc MS Excel 2007 XML"),)
    elif lower.endswith(".ods"):
        options = (prop("FilterName", "calc8"),)
    else:
        # field separator ',', text delimiter '"', UTF-8 (76), first line 1
        options = (prop("FilterName", "Text - txt - csv (StarCalc)"), prop("FilterOptions", "44,34,76,1"))
    doc.storeToURL(url(dst), options)
    doc.close(True)

    for f in failures:
        print("FAIL %s" % f.encode("ascii", "backslashreplace").decode("ascii"))
    print("%s: %s, %d translations filled -> %s" % ("OK" if not failures else "FAILED", language.encode("ascii", "backslashreplace").decode("ascii"), filled, os.path.basename(dst)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
