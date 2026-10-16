#!/usr/bin/env bash
# Syntax-check every C++ translation unit of the extension without an engine.
# Uses the minimal header stub in test/dmsdk (see test/README.md).
set -euo pipefail

cd "$(dirname "$0")/../.."

if ! command -v g++ >/dev/null 2>&1; then
    echo "error: g++ not found (install with: sudo apt-get install -y g++)" >&2
    exit 1
fi

if [ ! -f test/dmsdk/dmsdk/sdk.h ]; then
    echo "error: test/dmsdk/sdk.h is missing; the stub headers are required for this check" >&2
    exit 1
fi

failures=0
count=0
# keep filenames stable regardless of leading ./ noise
while IFS= read -r file; do
    [ -n "$file" ] || continue
    count=$((count + 1))
    if g++ -fsyntax-only -std=c++17 -Wno-attributes -Wno-unused-variable \
           -I defolddiscordsdk/include -I test/dmsdk "$file" 2> >(sed "s|^|  |" >&2); then
        echo "OK      $file"
    else
        echo "FAIL    $file" >&2
        failures=$((failures + 1))
    fi
done < <(find defolddiscordsdk/src -name '*.cpp' | sort)

echo
echo "syntax: checked $count file(s), failures: $failures"
if [ "$count" -eq 0 ]; then
    echo "error: no translation units found under defolddiscordsdk/src" >&2
    exit 1
fi
[ "$failures" -eq 0 ]
