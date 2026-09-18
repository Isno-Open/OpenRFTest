#!/usr/bin/env python3
"""Engendre board_pins.h depuis une declaration de carte.

    python3 tools/gen_board.py boards/isno-super.json build/board_pins.h

Meme format que les declarations d'isno-launcher : les broches, les radios et,
par radio, la plage autorisee, la porteuse et la puissance conduite maximale.
Le firmware lit ces valeurs, il n'en connait aucune. Appele par CMake a la
configuration : rien d'engendre n'est commis.
"""
import json, sys

def header(d):
    ui = d.get("ui", {})
    bid = d.get("board_id", {})
    radios = d["radios"]
    spi = {(r["pins"]["sck"], r["pins"]["miso"], r["pins"]["mosi"]) for r in radios}
    if len(spi) != 1:
        raise SystemExit("les radios de %s ne partagent pas le meme bus SPI" % d["id"])
    bands = {b["radio"]: b for b in d["bands_allowed_khz"]}
    for r in radios:
        if r["band"] not in bands:
            raise SystemExit("radio %s sans plage declaree dans %s" % (r["band"], d["id"]))
    L = [
        "/* ENGENDRE par tools/gen_board.py depuis boards/%s.json. Ne pas modifier. */" % d["id"],
        "#ifndef OPENRFTEST_BOARD_PINS_H",
        "#define OPENRFTEST_BOARD_PINS_H",
        "",
        '#define BOARD_ID            "%s"' % d["id"],
        '#define BOARD_NAME          "%s"' % d["name"],
        '#define BOARD_AP_SLUG       "%s"' % d.get("ap_slug", d["id"]),
        "#define BOARD_PIN_BUTTON    %d" % ui.get("button", -1),
        "#define BOARD_PIN_LED       %d" % ui.get("led", -1),
        "#define BOARD_LED_ACTIVE_HIGH %d" % (1 if ui.get("led_active_high", True) else 0),
        "#define BOARD_PIN_ID0       %d" % bid.get("id0", -1),
        "#define BOARD_PIN_ID1       %d" % bid.get("id1", -1),
        "#define BOARD_PIN_SPI_SCK   %d" % radios[0]["pins"]["sck"],
        "#define BOARD_PIN_SPI_MISO  %d" % radios[0]["pins"]["miso"],
        "#define BOARD_PIN_SPI_MOSI  %d" % radios[0]["pins"]["mosi"],
        "#define BOARD_RADIO_COUNT   %d" % len(radios),
        "typedef struct {",
        "    const char *band;       /* « 868 », « 433 » */",
        "    int cs, gdo0, gdo2;     /* GPIO */",
        "    int low_khz, high_khz;  /* plage autorisee, bornes incluses */",
        "    int carrier_khz;        /* porteuse par defaut */",
        "    int max_dbm;            /* puissance conduite maximale declaree */",
        "} board_radio_t;",
        "#define BOARD_RADIOS_INIT { \\",
    ]
    for r in radios:
        b = bands[r["band"]]
        L.append('    { "%s", %d, %d, %d, %d, %d, %d, %d }, \\' % (
            r["band"], r["pins"]["cs"], r["pins"]["gdo0"], r["pins"]["gdo2"],
            b["low"], b["high"], b["carrier_khz"], b["max_dbm"]))
    L += ["}", "", "#endif"]
    return "\n".join(L) + "\n"

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    d = json.load(open(sys.argv[1], encoding="utf-8"))
    text = header(d)
    try:
        if open(sys.argv[2], encoding="utf-8").read() == text:
            sys.exit(0)   # inchange : ne pas toucher au fichier, sinon tout recompile
    except FileNotFoundError:
        pass
    open(sys.argv[2], "w", encoding="utf-8").write(text)
