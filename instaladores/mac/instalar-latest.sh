#!/bin/bash
# Instalador do NODO para macOS. Baixa um app universal publicado no GitHub Releases.
# Uso: NODO_GITHUB_REPO="organizacao/repositorio" bash instalar-latest.sh
set -euo pipefail

REPO="${NODO_GITHUB_REPO:-}"
if [[ -z "$REPO" || "$REPO" == */*/* || "$REPO" != */* ]]; then
  echo "Defina NODO_GITHUB_REPO no formato usuario/repositorio."
  echo "Exemplo: NODO_GITHUB_REPO=empresa/nodo bash instalar-latest.sh"
  exit 2
fi

for cmd in curl ruby hdiutil ditto; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "Comando obrigatorio nao encontrado: $cmd"; exit 1; }
done

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64|arm64) ;;
  *) echo "Arquitetura macOS nao suportada: $ARCH"; exit 1 ;;
esac

API="https://api.github.com/repos/$REPO/releases/latest"
TMP="$(mktemp -d)"
MOUNT=""
cleanup() {
  if [[ -n "$MOUNT" ]]; then hdiutil detach "$MOUNT" -quiet -force >/dev/null 2>&1 || true; fi
  rm -rf "$TMP"
}
trap cleanup EXIT

echo "Consultando a ultima versao do NODO..."
JSON="$(curl -fsSL -H 'Accept: application/vnd.github+json' "$API")"
ASSET_URL="$(printf '%s' "$JSON" | ruby -rjson -e '
  j=JSON.parse(STDIN.read)
  a=j.fetch("assets", []).find { |x| x["name"].match?(/mac-universal\.dmg$/i) }
  abort("Nenhum instalador macOS universal .dmg encontrado nesta release.") unless a
  puts a.fetch("browser_download_url")
')"
VERSION="$(printf '%s' "$JSON" | ruby -rjson -e 'puts JSON.parse(STDIN.read).fetch("tag_name", "versao-desconhecida")')"
DMG="$TMP/Nodo.dmg"

echo "Baixando NODO $VERSION para $ARCH..."
curl -fL --progress-bar "$ASSET_URL" -o "$DMG"
MOUNT="$TMP/mount"
mkdir -p "$MOUNT"
hdiutil attach "$DMG" -nobrowse -readonly -noautoopen -mountpoint "$MOUNT" >/dev/null
[[ -d "$MOUNT/Nodo.app" ]] || { echo "Nao encontrei Nodo.app dentro do instalador."; exit 1; }

APP_DEST="/Applications/Nodo.app"
if [[ -e "$APP_DEST" ]]; then
  echo "Atualizando instalacao existente em /Applications..."
  sudo rm -rf "$APP_DEST"
fi
sudo ditto "$MOUNT/Nodo.app" "$APP_DEST"

mkdir -p "$HOME/.local/bin"
ln -sf "$APP_DEST/Contents/MacOS/nodo-cli" "$HOME/.local/bin/nodo"

# Inclui o diretorio de comandos do usuario no PATH do zsh sem duplicar a linha.
PROFILE="$HOME/.zprofile"
PATH_LINE='export PATH="$HOME/.local/bin:$PATH"'
 touch "$PROFILE"
grep -Fqx "$PATH_LINE" "$PROFILE" || printf '\n%s\n' "$PATH_LINE" >> "$PROFILE"

printf '\nNODO instalado em %s\n' "$APP_DEST"
printf 'Comando CLI: %s/.local/bin/nodo\n' "$HOME"
printf 'Abra o aplicativo com: open -a Nodo\n'
printf 'Para usar o comando nodo nesta janela: export PATH="$HOME/.local/bin:$PATH" && nodo --help\n'
open "$APP_DEST"
