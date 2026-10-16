#!/usr/bin/env bash
# Cheap consistency check between the extension's C API binding table and the
# vendored Discord header, without compiling anything.
#
# Every X(...) row in defolddiscordsdk/src/discordsdk_capi.h must reference
# a function name that also appears in defolddiscordsdk/include/cdiscourd.h.
set -euo pipefail

cd "$(dirname "$0")/../.."

capi_h=defolddiscordsdk/src/discordsdk_capi.h
vendored_h=defolddiscordsdk/include/cdiscourd.h

for f in "$capi_h" "$vendored_h"; do
    [ -f "$f" ] || { echo "error: missing $f" >&2; exit 1; }
done

missing=0
checked=0
while IFS= read -r row; do
    # X(return_type, FunctionName, (params)) -> take field 2 of the comma split
    name=$(sed 's/^[[:space:]]*X(//' <<<"$row" | awk -F',' '{print $2}' | tr -d '[:space:]')
    [ -n "$name" ] || continue
    checked=$((checked + 1))
    # names are plain identifiers, so this regex is injection-safe
    if ! grep -Eq "\b$name[[:space:]]*\(" "$vendored_h"; then
        echo "MISSING $name (in $capi_h but not declared in $vendored_h)" >&2
        missing=$((missing + 1))
    fi
done < <(grep -E '^[[:space:]]*X\(' "$capi_h")

echo
echo "capi-consistency: checked $checked binding row(s), missing: $missing"
if [ "$checked" -eq 0 ]; then
    echo "error: found no X(...) rows in $capi_h" >&2
    exit 1
fi
[ "$missing" -eq 0 ]
