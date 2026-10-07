#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERT_DIR="${SCRIPT_DIR}/certs"

mkdir -p "${CERT_DIR}"
if [[ ! -f "${CERT_DIR}/tutorial-lab-ca.key" || ! -f "${CERT_DIR}/tutorial-lab-ca.crt" ]]; then
  openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days 365 \
    -config "${SCRIPT_DIR}/lab-ca-openssl.cnf" \
    -keyout "${CERT_DIR}/tutorial-lab-ca.key" \
    -out "${CERT_DIR}/tutorial-lab-ca.crt"
fi

openssl req -new -newkey rsa:3072 -sha256 -nodes \
  -config "${SCRIPT_DIR}/localhost-openssl.cnf" \
  -keyout "${CERT_DIR}/localhost.key" \
  -out "${CERT_DIR}/localhost.csr"
openssl x509 -req -sha256 -days 30 \
  -in "${CERT_DIR}/localhost.csr" \
  -CA "${CERT_DIR}/tutorial-lab-ca.crt" \
  -CAkey "${CERT_DIR}/tutorial-lab-ca.key" \
  -CAcreateserial \
  -extfile "${SCRIPT_DIR}/localhost-openssl.cnf" \
  -extensions v3_req \
  -out "${CERT_DIR}/localhost.crt"
chmod 600 "${CERT_DIR}/tutorial-lab-ca.key" "${CERT_DIR}/localhost.key"

openssl verify -CAfile "${CERT_DIR}/tutorial-lab-ca.crt" \
  -verify_hostname localhost "${CERT_DIR}/localhost.crt"

echo "Generated localhost certificate in ${CERT_DIR}. Trust tutorial-lab-ca.crt in the browser/OS before use."
