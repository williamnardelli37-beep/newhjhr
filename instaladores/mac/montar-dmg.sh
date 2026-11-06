#!/bin/sh
# Gera um DMG com janela visual de arrastar para Aplicativos e um PKG opcional.
# Uso: VERSAO=3.0.6 sh instaladores/mac/montar-dmg.sh
# O app precisa estar compilado em build/Nodo.app.
set -eu
cd "$(dirname "$0")/../.."

VERSAO="${VERSAO:-3.0.6}"
# Assinatura: usa CODESIGN_IDENTITY se voce definir; senao cria o certificado gratuito "Loop";
# se isso falhar, assina sem identidade ("ad hoc"), que tambem e gratis.
if [ -n "${CODESIGN_IDENTITY:-}" ]; then
  ID="$CODESIGN_IDENTITY"
else
  ID="$(sh instaladores/mac/certificado-loop.sh || true)"
  [ -n "$ID" ] || ID="-"
fi
if [ "$ID" = "-" ]; then echo "Assinatura: ad hoc (sem certificado)."; else echo "Assinatura: certificado $ID"; fi
APP="build/Nodo.app"
BACKGROUND="instaladores/mac/dmg-background.png"

[ -d "$APP" ] || { echo "ERRO: build/Nodo.app nao encontrado. Compile o projeto antes."; exit 1; }
[ -f "$BACKGROUND" ] || { echo "ERRO: imagem do instalador nao encontrada: $BACKGROUND"; exit 1; }

# Usa o macdeployqt da mesma instalacao Qt usada pelo CMake.
QTPREFIX="${NODO_QT:-$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' build/CMakeCache.txt 2>/dev/null | head -1)}"
QTBIN="$QTPREFIX/bin"
if [ -x "$QTBIN/macdeployqt" ]; then
  "$QTBIN/macdeployqt" "$APP"
elif command -v macdeployqt >/dev/null 2>&1; then
  macdeployqt "$APP"
else
  echo "ERRO: macdeployqt nao encontrado. Use o mesmo Qt usado na compilacao." >&2
  exit 1
fi

# Garante que cada binario do bundle ainda pode iniciar no macOS Monterey 12.
python3 scripts/verificar_macos_minimo.py "$APP" --minimum 12.0 --archs x86_64,arm64

# Confere se o executavel e todas as bibliotecas distribuidas possuem as mesmas arquiteturas.
ARQ="$(lipo -archs "$APP/Contents/MacOS/Nodo" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')"
case "$ARQ" in
  "arm64 x86_64") SUF="universal" ;;
  "x86_64") SUF="intel" ;;
  "arm64") SUF="arm" ;;
  *) echo "ERRO: arquitetura inesperada: $ARQ"; exit 1 ;;
esac
RUIM=0
find "$APP" -type f \( -name '*.dylib' -o -perm -u+x \) ! -name '*.sh' ! -name '*.py' -print | while IFS= read -r f; do
  A="$(lipo -archs "$f" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//' || true)"
  [ -z "$A" ] || [ "$A" = "$ARQ" ] || { echo "ARQUITETURA DIFERENTE ($A, esperado $ARQ): $f"; exit 23; }
done || RUIM=$?
[ "$RUIM" -eq 0 ] || { echo "ERRO: ha bibliotecas com arquitetura diferente."; exit 1; }
echo "App $SUF ($ARQ): arquiteturas conferidas."

mkdir -p "$APP/Contents/Resources"
cp LEIA-ME.txt SEGURANCA.md guia.html COMO-ABRIR.txt "$APP/Contents/Resources/" 2>/dev/null || true
codesign --force --deep --options runtime --sign "$ID" "$APP" 2>/dev/null || codesign --force --deep --sign "$ID" "$APP"
codesign --verify --deep --strict "$APP"

OUT="${OUT:-$PWD}"
mkdir -p "$OUT"
ST="$(mktemp -d)"
MOUNT="$ST/mount"
cleanup() {
  hdiutil detach "$MOUNT" -quiet >/dev/null 2>&1 || true
  rm -rf "$ST"
}
trap cleanup EXIT HUP INT TERM

# Conteudo do volume: app real, atalho para /Applications com nome em portugues e fundo.
mkdir -p "$ST/dmg/.background" "$MOUNT"
ditto "$APP" "$ST/dmg/Nodo.app"
cp "$BACKGROUND" "$ST/dmg/.background/background.png"
ln -s /Applications "$ST/dmg/Aplicativos"
[ -f "COMO-ABRIR.txt" ] && cp "COMO-ABRIR.txt" "$ST/dmg/Como abrir na primeira vez.txt"

# DMG gravavel temporario para que o Finder grave o layout e o fundo no .DS_Store.
RWDMG="$ST/Nodo-layout-rw.dmg"
hdiutil create -volname "NODO" -srcfolder "$ST/dmg" -ov -format UDRW "$RWDMG" >/dev/null
hdiutil attach -quiet -readwrite -noverify -noautoopen -mountpoint "$MOUNT" "$RWDMG"

# A janela usa o app e o alias reais; apenas o titulo, seta e instrucoes estao na imagem de fundo.
osascript <<APPLESCRIPT
tell application "Finder"
  tell disk "NODO"
    open
    delay 1
    set current view of container window to icon view
    set theViewOptions to the icon view options of container window
    set background picture of theViewOptions to file ".background:background.png"
    set arrangement of theViewOptions to not arranged
    set icon size of theViewOptions to 112
    set text size of theViewOptions to 14
    close
    delay 1
    open
    update without registering applications
    tell container window
      set sidebar width to 0
      set toolbar visible to false
      set statusbar visible to false
      set bounds to {120, 90, 910, 660}
      set position of item "Nodo.app" to {185, 270}
      set position of item "Aplicativos" to {605, 270}
    end tell
    update without registering applications
    delay 2
    close
  end tell
end tell
APPLESCRIPT

sync
hdiutil detach "$MOUNT" -quiet
hdiutil convert "$RWDMG" -ov -format UDZO -imagekey zlib-level=9 -o "$OUT/Nodo-$VERSAO-mac-$SUF.dmg" >/dev/null
echo "DMG visual gerado: $OUT/Nodo-$VERSAO-mac-$SUF.dmg"

# PKG opcional: instala em /Applications e cria o comando de terminal nodo.
mkdir -p "$ST/scripts"
cat > "$ST/scripts/postinstall" <<'POSTINSTALL'
#!/bin/sh
mkdir -p /usr/local/bin
ln -sf /Applications/Nodo.app/Contents/MacOS/nodo-cli /usr/local/bin/nodo
exit 0
POSTINSTALL
chmod 0755 "$ST/scripts/postinstall"
pkgbuild --component "$APP" --install-location /Applications --scripts "$ST/scripts" --identifier app.nodo.servidor --version "$VERSAO" "$ST/Nodo-componente.pkg" >/dev/null
productbuild --package "$ST/Nodo-componente.pkg" "$OUT/Nodo-$VERSAO-mac-$SUF.pkg" >/dev/null
echo "PKG gerado: $OUT/Nodo-$VERSAO-mac-$SUF.pkg"
