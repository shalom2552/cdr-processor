#!/usr/bin/env bash
# Regenerates the class diagrams under docs/diagrams and the page that shows them.
# Pass --rebuild-db to force a fresh compilation database.

set -euo pipefail

cd "$(dirname "$0")/.."

PAGE=docs/Class_Diagrams.md
CONFIG=scripts/uml.yml
CLANG_UML=0.6.3

require() {
    if ! command -v "$1" > /dev/null 2>&1; then
        echo "error: $1 not found. $2" >&2
        exit 1
    fi
}

require make "install it: sudo pacman -S make"
require clang "install it: sudo pacman -S clang"
require bear "records the compilation database. install it: sudo pacman -S bear"
require clang-uml "reads the sources and writes the diagrams. the AUR package caps llvm
    at 22, so build it from source:

    sudo pacman -S --needed cmake yaml-cpp
    curl -sL https://github.com/bkryza/clang-uml/archive/refs/tags/$CLANG_UML.tar.gz | tar xz -C /tmp
    cmake -S /tmp/clang-uml-$CLANG_UML -B /tmp/clang-uml-$CLANG_UML/build -G Ninja \\
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=\$HOME/.local -DBUILD_TESTS=OFF
    cmake --build /tmp/clang-uml-$CLANG_UML/build -j3
    cmake --install /tmp/clang-uml-$CLANG_UML/build

    keep the job count low, the parser eats a few GB per file. \$HOME/.local/bin has to
    be on PATH."

if [ ! -f "$CONFIG" ]; then
    echo "error: $CONFIG not found. it holds the diagram list" >&2
    exit 1
fi

if [ ! -f compile_commands.json ] || [ "${1:-}" = "--rebuild-db" ]; then
    make clean
    if ! bear -- make build; then
        echo "error: the build failed, so there is no compilation database to read" >&2
        exit 1
    fi
fi

clang-uml -c "$CONFIG" -d . -o docs/diagrams -g mermaid \
    --add-compile-flag "-resource-dir=$(clang -print-resource-dir)"

sed -i -e '/^classDiagram$/i %%{init: {"theme": "dark", "themeVariables": {"lineColor": "#a0a8b4"}}}%%' \
       -e 's/+~/+\&#126;/g' docs/diagrams/*.mmd

{
    echo "# Class Diagrams"
    echo
    echo "Generated from the sources by \`scripts/uml.sh\`. Edit \`$CONFIG\`, not this file."

    for mmd in docs/diagrams/overview.mmd $(ls docs/diagrams/*.mmd | grep -v overview); do
        echo
        echo "## $(basename "$mmd" .mmd)"
        echo
        echo '```mermaid'
        grep -v -e '^%% Generated' -e '^%% LLVM' "$mmd"
        echo '```'
    done
} > "$PAGE"

echo "diagrams written to docs/diagrams, page written to $PAGE"
