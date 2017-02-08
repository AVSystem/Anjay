#!/bin/bash

set -e

TRUSTSTORE_PASSWORD=rootPass
KEYSTORE_PASSWORD=endPass

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$SCRIPT_DIR"
CLIENT_KEY_HEADER="$ROOT_DIR/demo/ssl_key.h"
CERTS_DIR="$ROOT_DIR/certs"

if [[ -d "$CERTS_DIR" ]]; then
    echo "WARNING: $CERTS_DIR already exists, its contents will be removed"
    echo -n "[ENTER to continue, CTRL-C to abort] "
    read _ || true # ignore EOF
    rm -rf "$CERTS_DIR"
fi
mkdir -p "$CERTS_DIR"
pushd "$CERTS_DIR" >/dev/null 2>&1

echo "* generating root cert"
openssl ecparam -name prime256v1 -genkey -out root.key
openssl req -batch -new -key root.key -x509 -sha256 -days 9999 -out root.crt
echo "* generating root cert - done"

for NAME in client server; do
    echo "* generating $NAME cert"
    openssl ecparam -name prime256v1 -genkey -out "${NAME}.key"
    openssl req -batch -new -subj '/CN=localhost' -key "${NAME}.key" -sha256 -out "${NAME}.csr"
    openssl x509 -sha256 -req -in "${NAME}.csr" -CA root.crt -CAkey root.key -out "${NAME}.crt" -days 9999 -CAcreateserial
    cat "${NAME}.crt" root.crt > "${NAME}-and-root.crt"
    echo "* generating $NAME cert - done"
done

"$TOOLS_DIR/import-certs.sh" --key client.key --cert client.crt --out "$CLIENT_KEY_HEADER"

if ! KEYTOOL="$(which keytool)" || [ -z "$KEYTOOL" ]; then
    echo ''
    echo "NOTE: keytool not found, not generating keystores for Java/Californium"
    echo ''
else
    echo "* creating trustStore.jks"
    yes | "$KEYTOOL" -importcert -alias root -file root.crt -keystore trustStore.jks -storepass "$TRUSTSTORE_PASSWORD" >/dev/null
    echo "* creating trustStore.jks - done"

    echo "* creating keyStore.jks"
    for NAME in client server; do
        openssl pkcs12 -export -in "${NAME}.crt" -inkey "${NAME}.key" -passin "pass:$KEYSTORE_PASSWORD" -out "${NAME}.p12" -name "${NAME}" -CAfile root.crt -caname root -password "pass:$KEYSTORE_PASSWORD"
        "$KEYTOOL" -importkeystore -deststorepass "$KEYSTORE_PASSWORD" -destkeystore keyStore.jks -srckeystore "${NAME}.p12" -srcstoretype PKCS12 -srcstorepass "$KEYSTORE_PASSWORD" -alias "${NAME}" -trustcacerts
    done
    echo "* creating keyStore.jks - done"

    echo ''
    echo "NOTE: To make demo successfully connect to Californium cf-secure server, copy contents of the $CERTS_DIR to the cf-secure/certs subdirectory and restart the server."
    echo ''
fi

popd >/dev/null 2>&1
