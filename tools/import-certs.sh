#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$ROOT_DIR/tools"
CERTS_DIR="$ROOT_DIR/certs"

DEFAULT_PRIVATE_KEY_PATH=$CERTS_DIR/client.key
DEFAULT_CERTIFICATE_PATH=$CERTS_DIR/client.crt
DEFAULT_OUTPUT_PATH="$ROOT_DIR/demo/ssl_key.h"

die() {
    echo "$@" >&2
    exit 1
}

print_help() {
    cat <<EOF >&2
NAME
    $0 - Generate C header with hard-coded certificate/private key pair for Anjay demo.

SYNOPSIS
    $0 [ OPTIONS... ]

OPTIONS
    -k, --key FILE
            - read private key from FILE.
              Default: $DEFAULT_PRIVATE_KEY_PATH
    -c, --cert FILE
            - read certificate from FILE.
              Default: $DEFAULT_CERTIFICATE_PATH
    -o, --out FILE
            - save output to FILE.
              Default: $DEFAULT_OUTPUT_PATH
    -h, --help
            - print this message and exit.

EOF
}

while [ "$#" -ge 1 ]; do
    case "$1" in
        -k|--key)
            PRIVATE_KEY_PATH="$2"
            shift
            ;;
        -c|--cert)
            CERTIFICATE_PATH="$2"
            shift
            ;;
        -o|--out)
            OUTPUT_PATH="$2"
            shift
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        *)
            die "Unrecognized option: $1"
            ;;
    esac

    shift
done

[ "$PRIVATE_KEY_PATH" ] || PRIVATE_KEY_PATH=$DEFAULT_PRIVATE_KEY_PATH
PRIVATE_KEY_PATH="$(readlink -f "$PRIVATE_KEY_PATH")"

[ "$CERTIFICATE_PATH" ] || CERTIFICATE_PATH=$DEFAULT_CERTIFICATE_PATH
CERTIFICATE_PATH="$(readlink -f "$CERTIFICATE_PATH")"

[ "$OUTPUT_PATH" ] || OUTPUT_PATH=$DEFAULT_OUTPUT_PATH
if [ "$OUTPUT_PATH" == "-" ]; then
    OUTPUT_PATH="/dev/stdout"
else
    OUTPUT_PATH="$(readlink -f "$OUTPUT_PATH")"
fi

function cut_bytes() {
    dd if=/dev/stdin of=/dev/stdout bs=1 skip="$1" count="$2" 2>/dev/null
}

[ -f "$PRIVATE_KEY_PATH" ] || die "$PRIVATE_KEY_PATH does not exist or is not a file"
[ -f "$CERTIFICATE_PATH" ] || die "$CERTIFICATE_PATH does not exist or is not a file"

echo "* generating $CLIENT_KEY_HEADER"
cat >"$OUTPUT_PATH" <<EOF
#ifndef ANJAY_SSL_KEY_H
#define ANJAY_SSL_KEY_H

#define ANJAY_DEMO_CLIENT_PKCS8_PRIVATE_KEY \\
$(openssl pkcs8 -topk8 -in "$PRIVATE_KEY_PATH" -inform pem -outform der \
    -passin "pass:endPass" -nocrypt | "$TOOLS_DIR/chex")

#define ANJAY_DEMO_CLIENT_X509_CERTIFICATE \\
$(openssl x509 -in "$CERTIFICATE_PATH" -outform der | "$TOOLS_DIR/chex")

#endif /* ANJAY_SSL_KEY_H */
EOF
echo "* generating $OUTPUT_PATH - done"
