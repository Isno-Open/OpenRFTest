#!/usr/bin/env bash
# Les temoins d'OpenRFTest, sur poste, sans materiel ni docker : les registres
# recalcules contre des valeurs connues, le nom et le mot de passe du reseau,
# et le generateur d'en-tete sur les deux cartes.
set -uo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
FAIL=0
essai() {
  local nom=$1; shift
  if cc -Wall -Wextra -Werror -Imain "$@" "test/test_$nom.c" -lm -o "$T/$nom" 2>"$T/err" && "$T/$nom" >"$T/out" 2>&1; then
    echo "ok    $nom  $(tail -1 "$T/out")"
  else
    echo "ECHEC $nom"; sed 's/^/      /' "$T/err" "$T/out" 2>/dev/null | head -12; FAIL=1
  fi
}
essai rf_regs main/rf_regs.c
essai ap main/ap.c
for b in boards/*.json; do
  if python3 tools/gen_board.py "$b" "$T/h.h" && grep -q BOARD_RADIOS_INIT "$T/h.h"; then echo "ok    en-tete $(basename "$b")"; else echo "ECHEC en-tete $b"; FAIL=1; fi
done
# Le generateur doit REFUSER une carte dont les radios ne partagent pas le bus.
python3 - "$T/mut.json" <<'PY'
import json,sys
d=json.load(open('boards/isno-super.json')); d['radios'][1]['pins']['sck']=40
json.dump(d,open(sys.argv[1],'w'))
PY
if python3 tools/gen_board.py "$T/mut.json" "$T/m.h" 2>/dev/null; then echo "ECHEC le generateur accepte deux bus SPI"; FAIL=1; else echo "ok    le generateur refuse deux bus SPI"; fi
exit $FAIL
