#!/bin/sh
set -euo pipefail
usage() {
    echo "Usage: $0 <dir> <string>"
    exit 1
}

# Check for args
if [[ $# -ne 2 ]]; then
    usage
fi

if [[ ! -d $1 ]]; then
    usage
fi

filesdir=$1
searchstr=$2

files=$(grep $searchstr $filesdir -r -l | wc -l)
lines=$(grep $searchstr $filesdir -r | wc -l)
echo "The number of files are $files and the number of matching lines are $lines"
