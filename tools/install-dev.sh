#!/bin/sh
# install-dev.sh -- prepara el entorno para cross-compilar bcl.exe desde Linux.
#
#   ./install-dev.sh            instala Zig (compilador C con mingw-w64 integrado)
#                               en ./toolchain y comprueba si hay Wine
#   ./install-dev.sh --wine     además intenta instalar Wine con el gestor de
#                               paquetes del sistema (pide sudo)
#   ./install-dev.sh --wine-portable
#                               descarga una compilación portable de Wine
#                               (Kron4ek/Wine-Builds, GitHub) en ./wine, sin root.
#                               No hay suma de verificación publicada: revisa el
#                               origen antes de usarla. Útil en Slackware.
#
# No toca nada fuera de este directorio salvo con --wine. El tarball de Zig se
# verifica contra el SHA-256 que publica https://ziglang.org/download/index.json.
set -eu

ZIG_VERSION=${ZIG_VERSION:-0.17.1}
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TC="$DIR/toolchain"
INDEX_URL=https://ziglang.org/download/index.json

case "$(uname -m)" in
    x86_64|amd64)  ZARCH=x86_64 ;;
    aarch64|arm64) ZARCH=aarch64 ;;
    *) echo "install-dev: arquitectura no soportada: $(uname -m)" >&2; exit 1 ;;
esac
KEY="$ZARCH-linux"

need() { command -v "$1" >/dev/null 2>&1 || { echo "install-dev: falta '$1'" >&2; exit 1; }; }
need curl; need tar; need sha256sum; need python3

if [ -x "$TC/zig" ] && [ "$("$TC/zig" version 2>/dev/null)" = "$ZIG_VERSION" ]; then
    echo "Zig $ZIG_VERSION ya instalado en $TC"
else
    WORK=$(mktemp -d "$DIR/.dl.XXXXXX")
    trap 'rm -rf "$WORK"' EXIT HUP INT TERM
    echo "Descargando el índice de Zig..."
    curl -fsSL "$INDEX_URL" -o "$WORK/index.json"
    # La salida del script es "URL SHA256"; el índice es datos, no instrucciones.
    set -- $(python3 -I - "$WORK/index.json" "$ZIG_VERSION" "$KEY" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
e = d[sys.argv[2]][sys.argv[3]]
print(e["tarball"], e["shasum"])
PY
)
    URL=$1; SHA=$2
    echo "Descargando $URL"
    curl -fL "$URL" -o "$WORK/zig.tar.xz"
    echo "$SHA  $WORK/zig.tar.xz" | sha256sum -c - >/dev/null \
        || { echo "install-dev: SHA-256 incorrecto, abortando" >&2; exit 1; }
    mkdir "$WORK/x"
    tar -xf "$WORK/zig.tar.xz" -C "$WORK/x"
    rm -rf "$TC"
    mv "$WORK"/x/zig-* "$TC"
    echo "Zig $("$TC/zig" version) instalado en $TC"
fi

if [ "${1:-}" = "--wine-portable" ] && [ ! -x "$DIR/wine/bin/wine64" ] && [ ! -x "$DIR/wine/bin/wine" ]; then
    WORK2=$(mktemp -d "$DIR/.dlw.XXXXXX")
    trap 'rm -rf "$WORK2"' EXIT HUP INT TERM
    echo "Buscando la última compilación portable de Wine..."
    curl -fsSL https://api.github.com/repos/Kron4ek/Wine-Builds/releases/latest -o "$WORK2/rel.json"
    WURL=$(python3 -I - "$WORK2/rel.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
for a in r["assets"]:
    n = a["name"]
    if n.endswith("-amd64.tar.xz") and "staging" not in n and "tkg" not in n and "proton" not in n and "wow64" not in n:
        print(a["browser_download_url"]); break
PY
)
    [ -n "$WURL" ] || { echo "install-dev: no encuentro un tarball de Wine" >&2; exit 1; }
    echo "Descargando $WURL"
    curl -fL "$WURL" -o "$WORK2/wine.tar.xz"
    mkdir "$WORK2/x"
    tar -xf "$WORK2/wine.tar.xz" -C "$WORK2/x"
    rm -rf "$DIR/wine"
    mv "$WORK2"/x/wine-* "$DIR/wine"
    echo "Wine portable instalado en $DIR/wine (ejecuta $DIR/wine/bin/wine64)"
fi

if [ -x "$DIR/wine/bin/wine64" ] || [ -x "$DIR/wine/bin/wine" ]; then
    echo "Wine portable: $DIR/wine"
elif command -v wine >/dev/null 2>&1 || command -v wine64 >/dev/null 2>&1; then
    echo "Wine: encontrado ($(command -v wine || command -v wine64))"
elif [ "${1:-}" = "--wine" ]; then
    echo "Instalando Wine con el gestor de paquetes (sudo)..."
    if   command -v apt-get >/dev/null 2>&1; then sudo apt-get install -y wine64
    elif command -v dnf     >/dev/null 2>&1; then sudo dnf install -y wine
    elif command -v pacman  >/dev/null 2>&1; then sudo pacman -S --needed wine
    elif command -v zypper  >/dev/null 2>&1; then sudo zypper install -y wine
    else
        echo "install-dev: no conozco el gestor de paquetes; instala Wine a mano" >&2
        echo "             (Slackware: SlackBuilds.org / sbopkg, paquete 'wine')." >&2
        exit 1
    fi
else
    echo "Wine: no está instalado (opcional, solo sirve para ejecutar bcl.exe aquí)."
    echo "      Ejecuta './install-dev.sh --wine', './install-dev.sh --wine-portable' o"
    echo "      instálalo con tu gestor de paquetes."
fi

cat <<EOF

Listo. Compilar con:
  $TC/zig cc -target x86_64-windows-gnu ...
EOF
