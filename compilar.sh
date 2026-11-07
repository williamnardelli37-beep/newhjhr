#!/bin/sh
# Compilacao do NODO Desktop comercial. Qt 6 e obrigatorio para a interface.
#
# Uso:  ./compilar.sh [universal|intel|arm|native]
#   (so no Mac; padrao = universal: um unico app que roda em Mac Intel e em Mac Apple Silicon)
#   universal  Intel + Apple Silicon      intel  so Intel (x86_64)
#   arm        so Apple Silicon (arm64)   native so a maquina atual
#
# Para o app rodar em Intel, o Qt tambem precisa ter x86_64. O Qt do Homebrew num Mac Apple Silicon e so arm64,
# entao este script procura (nesta ordem): $NODO_QT, ~/Qt/6.*/macos (instalador oficial) e o Homebrew; se nenhum
# servir, instala o Qt oficial (universal) em ~/Qt sozinho.
set -eu
cd "$(dirname "$0")"
SO="$(uname -s)"
QT_VERSAO_AQT="${QT_VERSAO_AQT:-6.8.3}"

if [ "$SO" = "Darwin" ]; then
  ALVO="${1:-${NODO_MAC_ARCH:-universal}}"
  case "$ALVO" in
    universal) ARCHS="x86_64 arm64" ;;
    intel)     ARCHS="x86_64" ;;
    arm)       ARCHS="arm64" ;;
    native)    ARCHS="$(uname -m)" ;;
    *) echo "Uso: ./compilar.sh [universal|intel|arm|native]"; exit 1 ;;
  esac
  command -v cmake >/dev/null 2>&1 || {
    command -v brew >/dev/null 2>&1 || { echo "Instale Homebrew (https://brew.sh) ou o CMake (https://cmake.org/download)"; exit 1; }
    brew install cmake
  }
  command -v lipo >/dev/null 2>&1 || { echo "Faltam as ferramentas de linha de comando da Apple. Rode: xcode-select --install"; exit 1; }

  # o Qt serve se tiver TODAS as arquiteturas pedidas
  qt_serve() {
    [ -d "$1/lib/QtCore.framework" ] || return 1
    A="$(lipo -archs "$1/lib/QtCore.framework/QtCore" 2>/dev/null)" || return 1
    for a in $ARCHS; do
      case " $A " in *" $a "*) ;; *) return 1 ;; esac
    done
    return 0
  }

  QT=""
  OFICIAL="$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -1 || true)"
  BREWQT=""
  if command -v brew >/dev/null 2>&1; then BREWQT="$(brew --prefix qt 2>/dev/null || true)"; fi
  for c in "${NODO_QT:-}" "$OFICIAL" "$BREWQT"; do
    if [ -n "$c" ] && qt_serve "$c"; then QT="$c"; break; fi
  done

  if [ -z "$QT" ]; then
    echo "Nenhum Qt 6 com suporte a: $ARCHS."
    echo "Instalando o Qt $QT_VERSAO_AQT oficial (universal) em $HOME/Qt (download grande, so na primeira vez)..."
    command -v python3 >/dev/null 2>&1 || { echo "Falta o Python 3. Rode: xcode-select --install"; exit 1; }
    python3 -m venv "$HOME/.nodo-aqt"
    "$HOME/.nodo-aqt/bin/pip" install --quiet --upgrade aqtinstall
    "$HOME/.nodo-aqt/bin/aqt" install-qt mac desktop "$QT_VERSAO_AQT" clang_64 -O "$HOME/Qt"
    QT="$HOME/Qt/$QT_VERSAO_AQT/macos"
    qt_serve "$QT" || { echo "O Qt instalado em $QT nao tem: $ARCHS. Me mande esta mensagem."; exit 1; }
  fi
  echo "Qt: $QT  |  Arquiteturas: $ARCHS"

  # trocar de arquitetura/Qt exige comecar do zero (o cache do CMake guarda a escolha antiga)
  MARCA="$ALVO|$QT"
  if [ -d build ] && [ "$(cat build/.nodo-alvo 2>/dev/null || true)" != "$MARCA" ]; then
    echo "Build antigo de outra configuracao: apagando a pasta build/ para refazer."
    rm -rf build dist
  fi
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNODO_BUILD_GUI=ON -DNODO_MAC_ARCH="$ALVO" -DCMAKE_PREFIX_PATH="$QT"
  echo "$MARCA" > build/.nodo-alvo
elif [ "$SO" = "Linux" ]; then
  command -v cmake >/dev/null 2>&1 || { echo "Instale cmake e um compilador C++17."; exit 1; }
  if ! pkg-config --exists Qt6Widgets 2>/dev/null; then
    echo "Qt 6 Widgets nao encontrado. Debian/Ubuntu: sudo apt install qt6-base-dev qt6-tools-dev cmake g++"; exit 1
  fi
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNODO_BUILD_GUI=ON
else
  echo "Sistema nao suportado por este script: $SO"; exit 1
fi

cmake --build build --config Release --parallel 4
cmake --install build --prefix "$PWD/dist"
if [ "$SO" = "Darwin" ]; then
  if [ -d build/Nodo.app ]; then
    echo "Aplicativo compilado: build/Nodo.app  (arquiteturas: $(lipo -archs build/Nodo.app/Contents/MacOS/Nodo))"
  fi
else
  echo "Aplicativo compilado em dist/"
fi
echo "Compilacao concluida. Para pacote no Mac: sh instaladores/mac/montar-dmg.sh   (outros sistemas: cd build && cpack)"
