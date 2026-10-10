#!/bin/sh
# Copia los ajustes guardados de la versión anterior (nemo-mac) a Fynder.
#
# Fynder guarda sus ajustes en /io/github/josemalena/fynder/ de dconf; antes
# estaban en /io/github/josemalena/nemo-mac/. Los nombres de las claves no
# cambiaron, así que basta copiar el árbol.
#
# Uso:  utils/migrar-ajustes-a-fynder.sh [--forzar]
#
# Sin --forzar no toca nada si Fynder ya tiene ajustes propios, para no pisar
# lo que se haya cambiado después. Antes de escribir deja un respaldo del
# destino en ~/.local/state/fynder-migracion/. No borra los ajustes viejos.

set -eu

ORIGEN=/io/github/josemalena/nemo-mac/
DESTINO=/io/github/josemalena/fynder/
RESPALDO_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/fynder-migracion"

if ! command -v dconf >/dev/null 2>&1; then
    echo "No encuentro el comando dconf." >&2
    exit 1
fi

viejos=$(dconf dump "$ORIGEN")
nuevos=$(dconf dump "$DESTINO")

if [ -z "$viejos" ]; then
    echo "No hay ajustes en $ORIGEN: no hay nada que migrar."
    exit 0
fi

if [ -n "$nuevos" ] && [ "${1:-}" != "--forzar" ]; then
    echo "Fynder ya tiene ajustes en $DESTINO; no los piso."
    echo "Si quieres sobrescribirlos con los viejos, repite con --forzar."
    exit 0
fi

mkdir -p "$RESPALDO_DIR"
marca=$(date +%Y%m%d-%H%M%S)
printf '%s\n' "$nuevos" > "$RESPALDO_DIR/destino-antes-$marca.ini"
printf '%s\n' "$viejos" > "$RESPALDO_DIR/origen-$marca.ini"

printf '%s\n' "$viejos" | dconf load "$DESTINO"

echo "Listo: ajustes copiados de $ORIGEN a $DESTINO."
echo "Respaldo en $RESPALDO_DIR (destino-antes-$marca.ini y origen-$marca.ini)."
