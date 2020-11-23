#!/usr/bin/env bash
#
# Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

if [ -z "$OPENSSL" ]; then
    # default OpenSSL on macOS is outdated and buggy
    # use the one from Homebrew if available
    BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null || true)"
    if [ "$BREW_OPENSSL" ]; then
        OPENSSL="$BREW_OPENSSL/bin/openssl"
    else
        OPENSSL=openssl
    fi
fi

OPENSSL_VERSION="$("$OPENSSL" version | awk '{print $2}')"
OPENSSL_VERSION_MAJOR="${OPENSSL_VERSION%%.*}"
OPENSSL_VERSION_TMP="${OPENSSL_VERSION:${#OPENSSL_VERSION_MAJOR}+1}"
OPENSSL_VERSION_MINOR="${OPENSSL_VERSION_TMP%%.*}"
OPENSSL_VERSION_TMP="${OPENSSL_VERSION_TMP:${#OPENSSL_VERSION_MINOR}+1}"
OPENSSL_VERSION_PATCH="${OPENSSL_VERSION_TMP%%.*}"
# remove non-numeric suffix
OPENSSL_VERSION_PATCH="$(echo "$OPENSSL_VERSION_PATCH" | sed -e 's/[^0-9]*$//')"

openssl_packed_version() {
    echo "$(($3 + 256 * ($2 + 256 * $1)))"
}

OPENSSL_PACKED_VERSION="$(openssl_packed_version $OPENSSL_VERSION_MAJOR $OPENSSL_VERSION_MINOR $OPENSSL_VERSION_PATCH)"

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

echo "* generating self signed certs"
mkdir self-signed
cd self-signed

# MSYS translates arguments that start with "/" to Windows paths... but /CN= is not a path, so we disable it for this call
export MSYS2_ARG_CONV_EXCL='*'

"$OPENSSL" ecparam -name prime256v1 -genkey -out server.key
"$OPENSSL" ecparam -name prime256v1 -genkey -out client.key

if [ "$OPENSSL_PACKED_VERSION" -ge "$(openssl_packed_version 1 1 1)" ]; then
    # OpenSSL >= 1.1.1, we have -addext
    ADDEXT_OPT=(-addext 'subjectAltName = DNS:127.0.0.1,IP:127.0.0.1')
else
    # OpenSSL <= 1.1.0, skip -addext
    ADDEXT_OPT=()
fi
"$OPENSSL" req -batch -new -subj '/CN=127.0.0.1' "${ADDEXT_OPT[@]}" -key server.key -x509 -sha256 -days 9999 -out server.crt
"$OPENSSL" req -batch -new -subj '/CN=localhost' -key client.key -x509 -sha256 -days 9999 -out client.crt
"$OPENSSL" x509 -in server.crt -outform der > server.crt.der
"$OPENSSL" x509 -in client.crt -outform der > client.crt.der
"$OPENSSL" pkcs8 -topk8 -in client.key -inform pem -outform der -nocrypt > client.key.der
cd ..
echo "* generating self signed certs - done"

echo "* generating root cert"
"$OPENSSL" ecparam -name prime256v1 -genkey -out root.key
"$OPENSSL" req -batch -new -subj '/CN=root' -key root.key -x509 -sha256 -days 9999 -out root.crt
"$OPENSSL" x509 -in root.crt -outform der > root.crt.der
echo "* generating root cert - done"

echo "* generating client cert"
"$OPENSSL" ecparam -name prime256v1 -genkey -out client.key
"$OPENSSL" req -batch -new -subj '/CN=localhost' -key client.key -sha256 -out client.csr
"$OPENSSL" x509 -sha256 -req -in client.csr -CA root.crt -CAkey root.key -out client.crt -days 9999 -CAcreateserial
cat client.crt root.crt > client-and-root.crt
echo "* generating client cert - done"
"$OPENSSL" x509 -in client.crt -outform der > client.crt.der
"$OPENSSL" pkcs8 -topk8 -in client.key -inform pem -outform der \
    -passin "pass:$KEYSTORE_PASSWORD" -nocrypt > client.key.der

echo "* generating client2_ca cert"
"$OPENSSL" ecparam -name prime256v1 -genkey -out client2_ca.key
"$OPENSSL" req -batch -new -subj '/CN=intermediate' -key client2_ca.key -sha256 -out client2_ca.csr
"$OPENSSL" x509 -sha256 -req -in client2_ca.csr -CA root.crt -CAkey root.key -out client2_ca.crt -days 9999 -extfile <(echo 'basicConstraints = CA:TRUE') -CAcreateserial
cat client2_ca.crt root.crt > client2_ca-and-root.crt
echo "* generating client2_ca cert - done"
"$OPENSSL" x509 -in client2_ca.crt -outform der > client2_ca.crt.der

echo "* generating client2 cert"
"$OPENSSL" ecparam -name prime256v1 -genkey -out client2.key
"$OPENSSL" req -batch -new -subj '/CN=client2' -key client2.key -sha256 -out client2.csr
"$OPENSSL" x509 -sha256 -req -in client2.csr -CA client2_ca.crt -CAkey client2_ca.key -out client2.crt -days 9999 -CAcreateserial
cat client2.crt client2_ca.crt > client2-and-ca.crt
cat client2-and-ca.crt root.crt > client2-full-path.crt
echo "* generating client2 cert - done"
"$OPENSSL" x509 -in client2.crt -outform der > client2.crt.der
"$OPENSSL" pkcs8 -topk8 -in client2.key -inform pem -outform der \
    -passin "pass:$KEYSTORE_PASSWORD" -nocrypt > client2.key.der

echo "* generating server_ca cert"
"$OPENSSL" ecparam -name prime256v1 -genkey -out server_ca.key
"$OPENSSL" req -batch -new -subj '/CN=localhost' -key server_ca.key -sha256 -out server_ca.csr
"$OPENSSL" x509 -sha256 -req -in server_ca.csr -CA root.crt -CAkey root.key -out server_ca.crt -days 9999 -extfile <(echo 'basicConstraints = CA:TRUE') -CAcreateserial
cat server_ca.crt root.crt > server_ca-and-root.crt
echo "* generating server_ca cert - done"
"$OPENSSL" x509 -in server_ca.crt -outform der > server_ca.crt.der

echo "* generating server cert"
"$OPENSSL" ecparam -name prime256v1 -genkey -out server.key
"$OPENSSL" req -batch -new -subj '/CN=127.0.0.1' -key server.key -sha256 -out server.csr
"$OPENSSL" x509 -sha256 -req -in server.csr -CA server_ca.crt -CAkey server_ca.key -out server.crt -days 9999 -extfile <(echo 'subjectAltName = DNS:127.0.0.1,IP:127.0.0.1') -CAcreateserial
cat server.crt server_ca.crt > server-and-ca.crt
cat server-and-ca.crt root.crt > server-full-path.crt
echo "* generating server cert - done"
"$OPENSSL" x509 -in server.crt -outform der > server.crt.der

generate_java_keystores() {
    set -e
    if ! KEYTOOL="$(which keytool)" || [ -z "$KEYTOOL" ] || ! "$KEYTOOL" -help >/dev/null 2>/dev/null; then
        echo ''
        echo "NOTE: keytool not found, not generating keystores for Java/Californium"
        echo ''
    else
        echo "* creating trustStore.jks"
        yes | "$KEYTOOL" -importcert -alias root -file root.crt.der -keystore trustStore.jks -storetype PKCS12 -storepass "$TRUSTSTORE_PASSWORD" >/dev/null
        echo "* creating trustStore.jks - done"

        echo "* creating keyStore.jks"
        for NAME in client server; do
            "$OPENSSL" pkcs12 -export -in "${NAME}.crt" -inkey "${NAME}.key" -passin "pass:$KEYSTORE_PASSWORD" -out "${NAME}.p12" -name "${NAME}" -CAfile root.crt -caname root -password "pass:$KEYSTORE_PASSWORD"
            "$KEYTOOL" -importkeystore -deststorepass "$KEYSTORE_PASSWORD" -destkeystore keyStore.jks -deststoretype PKCS12 -srckeystore "${NAME}.p12" -srcstoretype PKCS12 -srcstorepass "$KEYSTORE_PASSWORD" -alias "${NAME}" -trustcacerts
        done
        echo "* creating keyStore.jks - done"

        echo ''
        echo "NOTE: To make demo successfully connect to Californium cf-secure server, copy contents of the $CERTS_DIR to the cf-secure/certs subdirectory and restart the server."
        echo ''
    fi
}

generate_java_keystores || {
    echo ''
    echo 'NOTE: Error while generating keystores for Java/Californium, ignoring'
    echo ''
}

popd >/dev/null 2>&1
