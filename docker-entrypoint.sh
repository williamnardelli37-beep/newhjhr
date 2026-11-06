#!/bin/sh
# Prepara a pasta do site na primeira vez e liga o Nodo em primeiro plano (os logs saem em: docker compose logs)
set -e
cd /site
[ -f nodo.conf ] || nodo integrar /site >/dev/null
exec nodo rodar /site
