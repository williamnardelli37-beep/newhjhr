#!/bin/bash
set -euo pipefail

VERSION="3.0.7"
TAG="v${VERSION}"
REPO_URL="https://github.com/williamnardelli37-beep/newhjhr.git"
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="${TMPDIR:-/tmp}/NODO-PUBLICAR-${VERSION}"

printf '\nNODO COMERCIAL %s — PUBLICACAO NO GITHUB\n' "$VERSION"
printf 'Origem: %s\nDestino: %s\n\n' "$SOURCE_DIR" "$REPO_URL"

command -v git >/dev/null 2>&1 || { echo "ERRO: Git nao esta instalado."; exit 1; }
command -v rsync >/dev/null 2>&1 || { echo "ERRO: rsync nao encontrado (normalmente ja vem no macOS)."; exit 1; }

if [ -e "$WORK_DIR/.git" ]; then
  echo "Usando clone temporario existente: $WORK_DIR"
  git -C "$WORK_DIR" fetch origin --tags
  git -C "$WORK_DIR" checkout main
  git -C "$WORK_DIR" pull --ff-only origin main
else
  rm -rf "$WORK_DIR"
  git clone --branch main --single-branch "$REPO_URL" "$WORK_DIR"
fi

set +e
git -C "$WORK_DIR" ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1
TAG_STATUS=$?
set -e
if [ "$TAG_STATUS" -eq 0 ]; then
  echo "ERRO: a tag $TAG ja existe no GitHub. Nao vou sobrescreve-la."
  echo "Prepare uma nova versao antes de publicar."
  exit 1
elif [ "$TAG_STATUS" -ne 2 ]; then
  echo "ERRO: nao consegui consultar as tags remotas (codigo $TAG_STATUS). Verifique acesso/autenticacao."
  exit "$TAG_STATUS"
fi

rsync -a \
  --exclude='.git/' \
  --exclude='build/' \
  --exclude='dist/' \
  --exclude='.DS_Store' \
  "$SOURCE_DIR/" "$WORK_DIR/"

cd "$WORK_DIR"
git diff --check
git add -A
if git diff --cached --quiet; then
  echo "Nenhuma alteracao para publicar."
  exit 0
fi

printf '\nResumo das alteracoes que serao enviadas:\n'
git diff --cached --stat
printf '\nO commit sera enviado a main e a tag %s iniciara os builds.\n' "$TAG"
read -r -p "Para confirmar, digite PUBLICAR: " CONFIRM
if [ "$CONFIRM" != "PUBLICAR" ]; then
  echo "Cancelado. Nenhuma alteracao foi enviada."
  exit 0
fi

git commit -m "NODO ${VERSION} - compatibilidade multiplataforma"
git push origin main
git tag -a "$TAG" -m "NODO Comercial $VERSION"
git push origin "$TAG"

printf '\nPublicacao iniciada com sucesso.\n'
echo "Acompanhe: https://github.com/williamnardelli37-beep/newhjhr/actions"
echo "Releases: https://github.com/williamnardelli37-beep/newhjhr/releases"
