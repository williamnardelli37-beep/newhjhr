#!/bin/sh
# Gera um DMG com janela visual de arrastar para Aplicativos e um PKG opcional.
# Uso: VERSAO=3.0.6 sh instaladores/mac/montar-dmg.sh
# O app precisa estar compilado em build/Nodo.app.
set -eu
cd "$(dirname "$0")/../.."

VERSAO="${VERSAO:-3.0.6}"
# Assinatura: Ad Hoc por padrao (gratuita, sem certificado, "codesign --sign -").
# Se voce tiver um certificado de verdade (Apple Developer ID), defina CODESIGN_IDENTITY
# com o nome dele para usar em vez do Ad Hoc.
ID="${CODESIGN_IDENTITY:--}"
if [ "$ID" = "-" ]; then echo "Assinatura: Ad Hoc (gratuita, sem certificado)."; else echo "Assinatura: certificado $ID"; fi
APP="build/Nodo.app"
BACKGROUND="instaladores/mac/dmg-background.png"

[ -d "$APP" ] || { echo "ERRO: build/Nodo.app nao encontrado. Compile o projeto antes."; exit 1; }
[ -f "$BACKGROUND" ] || { echo "ERRO: imagem do instalador nao encontrada: $BACKGROUND"; exit 1; }

# Pasta temporaria usada tanto para emagrecer bibliotecas quanto para montar o DMG mais abaixo.
ST="$(mktemp -d)"
cleanup() {
  hdiutil detach "$MOUNT" -quiet >/dev/null 2>&1 || true
  rm -rf "$ST"
}
trap cleanup EXIT HUP INT TERM

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

# Confere se o executavel e todas as bibliotecas distribuidas possuem as mesmas arquiteturas.
ARQ="$(lipo -archs "$APP/Contents/MacOS/Nodo" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')"
case "$ARQ" in
  "arm64 x86_64") SUF="universal"; ARCHS_ESPERADOS="x86_64,arm64"; ARCH_UNICA="" ;;
  "x86_64") SUF="intel"; ARCHS_ESPERADOS="x86_64"; ARCH_UNICA="x86_64" ;;
  "arm64") SUF="arm"; ARCHS_ESPERADOS="arm64"; ARCH_UNICA="arm64" ;;
  *) echo "ERRO: arquitetura inesperada: $ARQ"; exit 1 ;;
esac

# O Qt oficial (macdeployqt) e universal (Intel+ARM). Quando o app e so uma
# arquitetura (build separado Intel/ARM), "emagrece" cada biblioteca do Qt
# para a mesma arquitetura do app, senao a checagem abaixo recusa o pacote.
# Escreve num arquivo temporario (o lipo pode recusar -output igual ao arquivo
# de entrada), preserva o modo/permissao original e confere se cada lipo deu certo.
if [ -n "$ARCH_UNICA" ]; then
  find "$APP" -type f \( -name '*.dylib' -o -perm -u+x \) ! -name '*.sh' ! -name '*.py' -print > "$ST/emagrecer-lista.txt"
  EMAGRECEU=0
  while IFS= read -r f; do
    A="$(lipo -archs "$f" 2>/dev/null || true)"
    case "$A" in
      *" "*)
        MODO="$(stat -f '%Lp' "$f")"
        TMP="$ST/emagrecido.tmp"
        rm -f "$TMP"
        if ! lipo -thin "$ARCH_UNICA" -output "$TMP" "$f"; then
          echo "ERRO: falha ao emagrecer para $ARCH_UNICA: $f" >&2
          exit 1
        fi
        chmod "$MODO" "$TMP"
        mv -f "$TMP" "$f"
        EMAGRECEU=$((EMAGRECEU + 1))
        ;;
    esac
  done < "$ST/emagrecer-lista.txt"
  echo "Emagrecidas $EMAGRECEU bibliotecas universais para $ARCH_UNICA."
fi

# Garante que cada binario do bundle ainda pode iniciar no macOS Monterey 12,
# conferindo so a(s) arquitetura(s) que este build realmente tem (Intel ou ARM, separados).
python3 scripts/verificar_macos_minimo.py "$APP" --minimum 12.0 --archs "$ARCHS_ESPERADOS"
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
MOUNT="$ST/mount"

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

# O hdiutil termina antes do Finder "enxergar" o volume novo; sem essa espera o
# AppleScript abaixo falha as vezes com "Can't get disk NODO" (-1728), so em CI.
# Confere de verdade (em vez de so dormir um tempo fixo), ate 30s.
i=0
until osascript -e 'tell application "Finder" to get name of disk "NODO"' >/dev/null 2>&1; do
  i=$((i + 1))
  [ "$i" -le 30 ] || { echo "ERRO: Finder nunca reconheceu o disco NODO."; exit 1; }
  sleep 1
done

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
