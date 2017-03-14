#!/bin/bash
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

. "$(dirname "$0")/utils.sh"

TRUSTSTORE_PASSWORD=rootPass
KEYSTORE_PASSWORD=endPass

SCRIPT_DIR="$(dirname "$(canonicalize "$0")")"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$SCRIPT_DIR"

if [[ "$#" < 1 ]]; then
    CERTS_DIR="$(pwd)/certs"
else
    CERTS_DIR="$1"
fi

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

openssl pkcs8 -topk8 -in client.key -inform pem -outform der \
    -passin "pass:$KEYSTORE_PASSWORD" -nocrypt > client.key.der
openssl x509 -in client.crt -outform der > client.crt.der

if ! KEYTOOL="$(which keytool)" || [ -z "$KEYTOOL" ] || ! "$KEYTOOL" -version >/dev/null 2>/dev/null; then
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
