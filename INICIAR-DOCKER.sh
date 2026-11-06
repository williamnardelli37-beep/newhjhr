#!/bin/sh
# Liga um site no Docker. Varios sites ao mesmo tempo: rode uma vez por pasta (cada um com a sua porta).
# Uso: sh INICIAR-DOCKER.sh [pasta-do-site] [porta]      Desligar: sh INICIAR-DOCKER.sh --parar [pasta-do-site]
cd "$(dirname "$0")" || exit 1
command -v docker >/dev/null 2>&1 || { echo "Instale o Docker: https://www.docker.com/products/docker-desktop"; exit 1; }
PARAR=0; [ "$1" = "--parar" ] && { PARAR=1; shift; }
SITE_DIR="$(cd "${1:-./site}" 2>/dev/null && pwd)" || { echo "Pasta do site nao encontrada: ${1:-./site}"; exit 1; }
NOME="nodo-$(basename "$SITE_DIR" | tr -c 'A-Za-z0-9_-\n' '-' | tr 'A-Z' 'a-z')"
PORTA="$2"
[ -z "$PORTA" ] && [ -f "$SITE_DIR/nodo.conf" ] && PORTA="$(grep '^porta=' "$SITE_DIR/nodo.conf" | head -n1 | cut -d= -f2 | tr -d ' \r')"
export SITE_DIR NODO_PORT="${PORTA:-7070}" NODO_UID="$(id -u)" NODO_GID="$(id -g)"
if [ "$PARAR" = 1 ]; then docker compose -p "$NOME" down; exit $?; fi
docker compose -p "$NOME" up -d --build && sleep 4 && docker compose -p "$NOME" logs --tail 15 nodo
echo "Abra http://localhost:$NODO_PORT   |   a chave do painel esta em $SITE_DIR/nodo.conf (linha chave=)"
echo "Desligar: sh INICIAR-DOCKER.sh --parar \"$SITE_DIR\""
