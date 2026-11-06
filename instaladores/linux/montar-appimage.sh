#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
VERSION="${VERSION:-3.0.5}"
QT_ROOT_DIR="${QT_ROOT_DIR:-${QT_ROOT:-}}"
if [[ -z "$QT_ROOT_DIR" || ! -x "$QT_ROOT_DIR/bin/qmake" ]]; then
  echo "ERRO: QT_ROOT_DIR não aponta para uma instalação Qt válida." >&2
  exit 1
fi
[[ -x build/nodo-app && -x build/nodo ]] || { echo "ERRO: compile build/nodo-app e build/nodo primeiro." >&2; exit 1; }

TOOLS="$ROOT/build/linuxdeploy-tools"
APPDIR="$ROOT/build/Nodo.AppDir"
mkdir -p "$TOOLS" "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/512x512/apps"

BASE_TAG="1-alpha-20251107-1"
QT_TAG="1-alpha-20250213-1"
APPIMAGE_TAG="1-alpha-20250213-1"
fetch() {
  local url="$1" output="$2"
  if [[ ! -s "$output" ]]; then curl --fail --location --retry 3 --retry-delay 2 "$url" -o "$output"; fi
  chmod +x "$output"
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/${BASE_TAG}/linuxdeploy-x86_64.AppImage" "$TOOLS/linuxdeploy-x86_64.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${QT_TAG}/linuxdeploy-plugin-qt-x86_64.AppImage" "$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/${APPIMAGE_TAG}/linuxdeploy-plugin-appimage-x86_64.AppImage" "$TOOLS/linuxdeploy-plugin-appimage-x86_64.AppImage"

cp build/nodo-app "$APPDIR/usr/bin/nodo-app"
cp build/nodo "$APPDIR/usr/bin/nodo"
cp instaladores/linux/nodo.desktop "$APPDIR/usr/share/applications/nodo.desktop"
cp instaladores/linux/nodo.png "$APPDIR/usr/share/icons/hicolor/512x512/apps/nodo.png"
cat > "$APPDIR/LEIA-ME.txt" <<'INFO'
NODO COMERCIAL - LINUX x86_64
Distribuição AppImage e AppDir portátil.
Requisito de sistema: Linux x86_64 com glibc 2.35 ou superior.
O pacote leva o runtime Qt e os plugins usados pelo aplicativo.
Para executar o AppImage: chmod +x NODO-*.AppImage && ./NODO-*.AppImage
INFO

cd "$TOOLS"
export QMAKE="$QT_ROOT_DIR/bin/qmake"
export VERSION
export LDAI_OUTPUT="$ROOT/build/NODO-${VERSION}-Linux-x86_64.AppImage"
"$TOOLS/linuxdeploy-x86_64.AppImage" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/nodo-app" \
  --executable "$APPDIR/usr/bin/nodo" \
  --desktop-file "$APPDIR/usr/share/applications/nodo.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/512x512/apps/nodo.png" \
  --plugin qt \
  --output appimage

[[ -s "$LDAI_OUTPUT" ]] || { echo "ERRO: linuxdeploy não gerou o AppImage esperado: $LDAI_OUTPUT" >&2; exit 1; }
chmod +x "$LDAI_OUTPUT"

tar -czf "$ROOT/build/NODO-${VERSION}-Linux-x86_64-AppDir.tar.gz" -C "$ROOT/build" Nodo.AppDir

echo "AppImage gerado e AppDir compactado em build/."
