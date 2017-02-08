#!/bin/sh
FILE="$1"
shift
FIRST=1
PATTERN=''
while [ "$*" ]; do
    if [ -z "$FIRST" ]; then
        PATTERN="$PATTERN|"
    fi
    FIRST=''
    PATTERN="$PATTERN$1"
    shift
done
# readelf -s on a non-stripped .so file will show all symbols twice, but it's not a problem
readelf -sW "$FILE" | sed -e 's/^.*://;' | awk '{if ($2 > 0 && $4 == "GLOBAL") print $7}' | grep -Ev "^($PATTERN)"
test "$?" -eq 1
