#!/usr/bin/env bash
# Parse-check the example Lua script with the first available Lua compiler.
set -euo pipefail

cd "$(dirname "$0")/../.."

script=example/example.script
[ -f "$script" ] || { echo "error: missing $script" >&2; exit 1; }

# A .script file is a plain Lua chunk as far as the compiler is concerned.
compiler=""
for candidate in luac5.1 luac luajit; do
    if command -v "$candidate" >/dev/null 2>&1; then
        compiler="$candidate"
        break
    fi
done

if [ -z "$compiler" ]; then
    echo "error: no Lua compiler found. Tried: luac5.1, luac, luajit." >&2
    echo "       Install one with: sudo apt-get install -y lua5.1   or:  sudo apt-get install -y luajit" >&2
    exit 1
fi

echo "Using compiler: $compiler"
# luajit -bl parses; luac -p only parses. Both fail on syntax errors.
if [ "$compiler" = "luajit" ]; then
    luajit -bl "$script" >/dev/null
else
    "$compiler" -p "$script"
fi
echo "lua: $script parses cleanly"
