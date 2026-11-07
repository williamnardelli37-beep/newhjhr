#!/bin/sh
# Cria o certificado GRATUITO "Loop" (autoassinado) para assinar o Nodo.app no macOS.
# Imprime na saida padrao so o identificador (hash) do certificado; mensagens vao para stderr.
# Uso: ID="$(sh instaladores/mac/certificado-loop.sh)"
set -eu

NOME="${LOOP_CERT_NAME:-Loop}"
KC="${HOME}/Library/Keychains/loop-assinatura.keychain-db"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

command -v openssl >/dev/null 2>&1 || { echo "openssl nao encontrado" >&2; exit 1; }
command -v security >/dev/null 2>&1 || { echo "security (macOS) nao encontrado" >&2; exit 1; }

SENHA="$(openssl rand -hex 16)"

cat > "$TMP/loop.cnf" <<CNF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NOME
O = Loop Web
C = BR
[ext]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
CNF

openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
  -keyout "$TMP/loop.key" -out "$TMP/loop.crt" -config "$TMP/loop.cnf" >/dev/null 2>&1

# OpenSSL 3 usa cifras novas que o "security import" do macOS nao le; forca as compativeis.
openssl pkcs12 -export -inkey "$TMP/loop.key" -in "$TMP/loop.crt" -out "$TMP/loop.p12" \
  -name "$NOME" -passout "pass:$SENHA" \
  -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES -macalg sha1 >/dev/null 2>&1 \
|| openssl pkcs12 -export -inkey "$TMP/loop.key" -in "$TMP/loop.crt" -out "$TMP/loop.p12" \
  -name "$NOME" -passout "pass:$SENHA" >/dev/null 2>&1

# Chaveiro proprio, so para a assinatura (nao mexe no seu chaveiro de login).
security delete-keychain "$KC" >/dev/null 2>&1 || true
security create-keychain -p "$SENHA" "$KC"
security set-keychain-settings -lut 21600 "$KC"
security unlock-keychain -p "$SENHA" "$KC"
security import "$TMP/loop.p12" -k "$KC" -P "$SENHA" -T /usr/bin/codesign -T /usr/bin/security >/dev/null
security set-key-partition-list -S apple-tool:,apple: -s -k "$SENHA" "$KC" >/dev/null 2>&1 || true

# Coloca o chaveiro na lista de busca para o codesign achar o certificado.
LISTA="$(security list-keychains -d user | tr -d '"' | tr '\n' ' ')"
# shellcheck disable=SC2086
security list-keychains -d user -s "$KC" $LISTA

# Guarda a parte publica para quem quiser confiar no certificado (opcional).
[ -n "${LOOP_CERT_PUBLIC_OUT:-}" ] && cp "$TMP/loop.crt" "$LOOP_CERT_PUBLIC_OUT" 2>/dev/null || true

HASH="$(security find-identity -p codesigning "$KC" | grep "\"$NOME\"" | head -1 | awk '{print $2}')"
[ -n "$HASH" ] || { echo "Nao consegui criar o certificado $NOME" >&2; exit 1; }
echo "Certificado '$NOME' criado: $HASH" >&2
echo "$HASH"
