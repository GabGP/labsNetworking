#!/bin/bash
# Corre nuestro traceroute y el traceroute del sistema con los mismos
# argumentos, guarda ambas salidas en tests/logs/ y muestra el diff lado a lado.
#
#   uso: tests/compare.sh [HOST] [ARGS...]
#   por defecto: HOST=8.8.8.8  ARGS=-n -m 20 -w 2
#
# Ambos necesitan root porque abren sockets RAW.

set -u

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$DIR/traceroute"

HOST="${1:-8.8.8.8}"
[ $# -gt 0 ] && shift
ARGS=("$@")
[ ${#ARGS[@]} -eq 0 ] && ARGS=(-n -m 20 -w 2)

if [ ! -x "$BIN" ]; then
    echo "compare.sh: falta $BIN, compila primero con 'make'" >&2
    exit 1
fi
if ! command -v traceroute >/dev/null 2>&1; then
    echo "compare.sh: el traceroute del sistema no esta instalado" >&2
    echo "            (sudo apt-get install traceroute) - solo se usa para comparar" >&2
    exit 1
fi

LOGS="$DIR/tests/logs"
mkdir -p "$LOGS"
SAFE="${HOST//[^A-Za-z0-9._-]/_}"
OWN="$LOGS/own_$SAFE.log"
SYS="$LOGS/system_$SAFE.log"

echo "=== propio:  sudo ./traceroute ${ARGS[*]} $HOST"
sudo "$BIN" "${ARGS[@]}" "$HOST" | tee "$OWN"

echo
echo "=== sistema: sudo traceroute ${ARGS[*]} $HOST"
sudo traceroute "${ARGS[@]}" "$HOST" | tee "$SYS"

echo
echo "=== diff lado a lado (izquierda = propio, derecha = sistema)"
echo "    logs: $OWN"
echo "          $SYS"
diff -y --width=150 "$OWN" "$SYS"

exit 0
