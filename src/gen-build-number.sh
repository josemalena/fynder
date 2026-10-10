#!/bin/sh
# Genera fynder-build-number.h con un número de compilación que sube en cada
# build (para comprobar de un vistazo que corre la versión recién instalada).
# El contador vive fuera del repositorio, en ~/.local/state/fynder/build-number
# (o donde diga FYNDER_BUILD_COUNTER).
out="$1"
counter="${FYNDER_BUILD_COUNTER:-${XDG_STATE_HOME:-${HOME:-/tmp}/.local/state}/fynder/build-number}"
mkdir -p "$(dirname "$counter")" 2>/dev/null
n=$(cat "$counter" 2>/dev/null || echo 0)
n=$((n + 1))
echo "$n" > "$counter" 2>/dev/null
cat > "$out" <<EOT
#define FYNDER_BUILD_NUMBER $n
#define FYNDER_BUILD_DATE "$(date '+%Y-%m-%d %H:%M')"
EOT
