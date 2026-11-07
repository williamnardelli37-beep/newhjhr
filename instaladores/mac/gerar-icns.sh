#!/bin/sh
# Gera icones/Nodo.icns a partir dos PNGs (precisa do macOS: usa iconutil). Rode ANTES do cmake.
set -e
cd "$(dirname "$0")/../.."
S=build/Nodo.iconset; rm -rf "$S"; mkdir -p "$S"
cp icones/nodo-16.png "$S/icon_16x16.png";   cp icones/nodo-32.png "$S/icon_16x16@2x.png"
cp icones/nodo-32.png "$S/icon_32x32.png";   cp icones/nodo-64.png "$S/icon_32x32@2x.png"
cp icones/nodo-128.png "$S/icon_128x128.png"; cp icones/nodo-256.png "$S/icon_128x128@2x.png"
cp icones/nodo-256.png "$S/icon_256x256.png"; cp icones/nodo-512.png "$S/icon_256x256@2x.png"
cp icones/nodo-512.png "$S/icon_512x512.png"; cp icones/nodo-1024.png "$S/icon_512x512@2x.png"
iconutil -c icns "$S" -o icones/Nodo.icns
echo "Gerado icones/Nodo.icns"
