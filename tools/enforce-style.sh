#!/bin/bash
#
# This script uses clang-format-6 to enforce a given coding style for the SAT
# solver source code, i.e. for all .h and .cc files.
#

# Get to repository base directory
cd $(dirname "${BASH_SOURCE[0]}")/..

if [ ! -r .clang-format ]
then
    echo "error: cannot find file .clang-format"
    exit 1
fi

declare -r TOOL="clang-format-10"

if ! command -v "$TOOL" &> /dev/null
then
    echo "error: could not find $TOOL, abort"
    exit 1
fi

echo "info: use clang format, version: $("$TOOL" --version)"

# iterate over all files and check style
declare -i STATUS=0
for file in $(find . -name "*.cc" -o -name "*.h" -type f)
do
	echo "formatting $file ..."
	"$TOOL" -i "$file" || STATUS=$?
done

if [ "$STATUS" -ne 0 ]; then
    echo "error: failed formatting with $STATUS"
fi
exit $STATUS
