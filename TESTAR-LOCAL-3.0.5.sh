#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
BUILD="${TMPDIR:-/tmp}/nodo-3.0.5-cli-validation"

echo '[1/5] Validando YAML do GitHub Actions...'
python3 - <<'PY'
import yaml
from pathlib import Path
p = Path('.github/workflows/build-installers.yml')
data = yaml.safe_load(p.read_text())
required = {'macos', 'windows-modern', 'windows-legacy', 'linux', 'publish-release'}
missing = required - set(data['jobs'])
if missing:
    raise SystemExit(f'Jobs ausentes: {sorted(missing)}')
print('YAML OK:', ', '.join(sorted(data['jobs'])))
PY

echo '[2/5] Validando scripts shell...'
bash -n compilar.sh instaladores/mac/*.sh instaladores/linux/*.sh PUBLICAR-GITHUB-3.0.5.sh

echo '[3/5] Validando scripts Python...'
python3 -m py_compile testes/*.py scripts/*.py

echo '[4/5] Compilando CLI local sem Qt...'
cmake -S . -B "$BUILD" -DNODO_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel 2

echo '[5/5] Executando teste funcional HTTP/autenticacao...'
python3 testes/platform_smoke.py "$BUILD/nodo"

echo
 echo 'VALIDACAO LOCAL APROVADA.'
echo 'Nota: builds GUI e pacotes macOS/Windows/Linux sao executados nos runners do GitHub Actions.'
