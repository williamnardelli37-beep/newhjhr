#!/bin/sh
# Prepara o Nodo.app (Qt embutido + assinatura) e monta o .dmg (arrastar para Aplicativos) e o .pkg (assistente de instalacao).
# Uso (na raiz do projeto, depois do ./compilar.sh):   VERSAO=3.0.0 sh instaladores/mac/montar-dmg.sh
# O nome do arquivo diz a arquitetura: -universal (Intel + Apple Silicon), -intel ou -arm.
# Para assinar com certificado Apple: CODESIGN_IDENTITY="Developer ID Application: Seu Nome (TEAMID)" ... (sem isso, assinatura local "ad hoc").
set -e
cd "$(dirname "$0")/../.."
VERSAO="${VERSAO:-3.0.1}"
ID="${CODESIGN_IDENTITY:--}"
APP="build/Nodo.app"
[ -d "$APP" ] || { echo "Compile antes: cmake -S . -B build && cmake --build build"; exit 1; }
# usa o macdeployqt do MESMO Qt com que o app foi compilado (o do cache do CMake), senao o do Homebrew/PATH
QTPREFIX="${NODO_QT:-$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' build/CMakeCache.txt 2>/dev/null | head -1)}"
QTBIN="$QTPREFIX/bin"
if [ -x "$QTBIN/macdeployqt" ]; then
  "$QTBIN/macdeployqt" "$APP"
elif command -v macdeployqt >/dev/null 2>&1; then
  macdeployqt "$APP"
else
  echo "ERRO: macdeployqt nao encontrado. Use o mesmo Qt usado para compilar e configure NODO_QT ou CMAKE_PREFIX_PATH."
  exit 1
fi

# arquitetura do app e conferencia: TODO binario/biblioteca dentro do .app precisa ter as mesmas arquiteturas do executavel
# (uma unica .dylib so-arm64 faz o app abrir no Apple Silicon e falhar no Intel)
ARQ="$(lipo -archs "$APP/Contents/MacOS/Nodo" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')"
case "$ARQ" in
  "arm64 x86_64") SUF="universal" ;;
  "x86_64") SUF="intel" ;;
  "arm64") SUF="arm" ;;
  *) echo "Arquitetura inesperada: $ARQ"; exit 1 ;;
esac
RUIM=0
for f in $(find "$APP" -type f \( -name '*.dylib' -o -perm -u+x \) ! -name '*.sh' ! -name '*.py'); do
  A="$(lipo -archs "$f" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')" || continue
  [ -n "$A" ] || continue
  if [ "$A" != "$ARQ" ]; then echo "ARQUITETURA DIFERENTE ($A, esperado $ARQ): $f"; RUIM=1; fi
done
[ "$RUIM" = 0 ] || { echo "Corrija acima (normalmente: compile com o Qt oficial universal usando ./compilar.sh) e refaca."; exit 1; }
echo "App $SUF ($ARQ): todas as bibliotecas conferem."
cp -r LEIA-ME.txt SEGURANCA.md guia.html "$APP/Contents/Resources/" 2>/dev/null || true
codesign --force --deep --options runtime --sign "$ID" "$APP" 2>/dev/null || codesign --force --deep --sign "$ID" "$APP"
codesign --verify --deep --strict "$APP"
OUT="${OUT:-$PWD}"
ST="$(mktemp -d)"; trap 'rm -rf "$ST"' EXIT
mkdir -p "$ST/dmg"; cp -R "$APP" "$ST/dmg/"; ln -s /Applications "$ST/dmg/Applications"
hdiutil create -volname "Nodo $VERSAO" -srcfolder "$ST/dmg" -ov -format UDZO "$OUT/Nodo-$VERSAO-mac-$SUF.dmg" >/dev/null
echo "Gerado: $OUT/Nodo-$VERSAO-mac-$SUF.dmg"
# .pkg: instala em /Applications e deixa o comando "nodo" em /usr/local/bin
mkdir -p "$ST/scripts"
cat > "$ST/scripts/postinstall" <<'EOT'
#!/bin/sh
mkdir -p /usr/local/bin
ln -sf /Applications/Nodo.app/Contents/MacOS/nodo-cli /usr/local/bin/nodo
exit 0
EOT
chmod 0755 "$ST/scripts/postinstall"
pkgbuild --component "$APP" --install-location /Applications --scripts "$ST/scripts" --identifier app.nodo.servidor --version "$VERSAO" "$ST/Nodo-componente.pkg" >/dev/null
productbuild --package "$ST/Nodo-componente.pkg" "$OUT/Nodo-$VERSAO-mac-$SUF.pkg" >/dev/null
echo "Gerado: $OUT/Nodo-$VERSAO-mac-$SUF.pkg"
