# Copyright 2017-2025 AVSystem <avsystem@avsystem.com>
# AVSystem Anjay LwM2M SDK
# All rights reserved.
#
# Licensed under AVSystem Anjay LwM2M Client SDK - Non-Commercial License.
# See the attached LICENSE file for details.

log() {
    local FG_BOLD="\033[1m"
    local FG_DEFAULT="\033[0m"

    echo -e "$(date) ${FG_BOLD}$0: ${FUNCNAME[1]}:${FG_DEFAULT} $@" >&2
}

warn() {
    local COL_YELLOW="\033[33m"
    local COL_DEFAULT="\033[0m"

    log "${COL_YELLOW}$@${COL_DEFAULT}"
}

die() {
    local COL_RED="\033[31m"
    local COL_YELLOW="\033[33m"
    local COL_DEFAULT="\033[0m"

    echo -ne "$COL_RED"
    log "$@"

    read LINE FUNC FILE <<<$(caller 0)
    echo -e "${COL_YELLOW}at $FILE:$LINE ($FUNC)$COL_DEFAULT"

    local FRAME=1
    while [[ $FRAME -le 10 ]]; do
        if ! caller $FRAME >/dev/null; then
            exit 1
        fi

        read LINE FUNC FILE <<<$(caller $FRAME)
        echo -e "$COL_YELLOW  called from $FILE:$LINE ($FUNC)$COL_DEFAULT"
        ((FRAME++))
    done

    echo -e "$COL_YELLOW  (more frames follow)"
    exit 1
}

canonicalize() {
    echo "$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1")"
}

current_git_branch_name() {
    if git branch | grep --quiet 'HEAD detached at'; then
        return 1
    else
        git branch | grep '*' | cut --fields=2- --delimiter=' '
    fi
}

current_git_branch_hash() {
    git rev-parse --short HEAD
}

num_processors() {
    if command -v nproc > /dev/null; then
        nproc
    else
        sysctl -n hw.ncpu
    fi
}

if [[ ! "$ATEXIT_SETUP" ]]; then
    ATEXIT_SETUP=1
    ATEXIT_SCHEDULED=()

    atexit() {
        for EXPR in "$@"; do
            ATEXIT_SCHEDULED+=("$@")
        done
    }

    atexit_handler() {
        for CMD in "${ATEXIT_SCHEDULED[@]}"; do
            log "$CMD"

            # when `set -e` is used, any error causes the script to exit.
            # trap EXIT handlers are still executed, but if any command
            # inside one fails as well, the handler will be terminated too.
            # `|| true` below ensures that failure of one scheduled command
            # does not prevent other ones from executing.
            eval "$CMD" || true
        done

        ATEXIT_SCHEDULED=()
    }

    [[ "$(trap -p EXIT)" ]] && die "trap EXIT already set"

    trap atexit_handler EXIT
fi
