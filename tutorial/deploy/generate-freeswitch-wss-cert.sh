#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERT_DIR="${SCRIPT_DIR}/certs"
CA_KEY="${CERT_DIR}/tutorial-lab-ca.key"
CA_CERT="${CERT_DIR}/tutorial-lab-ca.crt"

if [[ ! -f "${CA_KEY}" || ! -f "${CA_CERT}" ]]; then
  echo "Lab CA is missing; run ${SCRIPT_DIR}/generate-local-cert.sh first." >&2
  exit 1
fi

openssl req -new -newkey rsa:3072 -sha256 -nodes \
  -config "${SCRIPT_DIR}/freeswitch-wss-openssl.cnf" \
  -keyout "${CERT_DIR}/freeswitch-wss.key" \
  -out "${CERT_DIR}/freeswitch-wss.csr"
openssl x509 -req -sha256 -days 30 \
  -in "${CERT_DIR}/freeswitch-wss.csr" \
  -CA "${CA_CERT}" \
  -CAkey "${CA_KEY}" \
  -CAcreateserial \
  -extfile "${SCRIPT_DIR}/freeswitch-wss-openssl.cnf" \
  -extensions v3_req \
  -out "${CERT_DIR}/freeswitch-wss.crt"

# mod_sofia reads certificate chain and private key from the same wss.pem file.
cp "${CERT_DIR}/freeswitch-wss.crt" "${CERT_DIR}/wss.pem"
printf '\n' >> "${CERT_DIR}/wss.pem"
sed -n '/-----BEGIN PRIVATE KEY-----/,/-----END PRIVATE KEY-----/p' \
  "${CERT_DIR}/freeswitch-wss.key" >> "${CERT_DIR}/wss.pem"
printf '\n' >> "${CERT_DIR}/wss.pem"
cat "${CA_CERT}" >> "${CERT_DIR}/wss.pem"
chmod 600 "${CERT_DIR}/freeswitch-wss.key" "${CERT_DIR}/wss.pem"

openssl verify -CAfile "${CA_CERT}" -verify_ip 10.100.212.8 \
  "${CERT_DIR}/freeswitch-wss.crt"
echo "Generated FreeSWITCH WSS certificate for 10.100.212.8 in ${CERT_DIR}/wss.pem."
