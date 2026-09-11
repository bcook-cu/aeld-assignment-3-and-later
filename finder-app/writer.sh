#!/bin/sh
set -euo pipefail

usage() {
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
}

if [[ $# -ne 2 ]]; then
    usage
fi

writefile="$1"
writestr="$2"

# Remove existing file
mkdir -p $(dirname $writefile)
echo "$writestr" > "$writefile" || exit 1
