#!/usr/bin/env bash
# Fetch and build nauty, then link it as ./nauty so the Makefile finds it.
#
# nauty is by Brendan McKay and Adolfo Piperno and is distributed under the
# Apache License 2.0. GraphCrunch does not bundle it. See NOTICE.
set -euo pipefail

VERSION="${NAUTY_VERSION:-nauty2_8_8}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [ -e nauty/nauty.a ]; then
    echo "nauty already built at $ROOT/nauty"; exit 0
fi

# McKay's ANU page first. The Rome mirror is kept as a fallback because its TLS
# chain is not trusted on every machine.
MIRRORS=(
    "https://users.cecs.anu.edu.au/~bdm/nauty/${VERSION}.tar.gz"
    "https://pallini.di.uniroma1.it/${VERSION}.tar.gz"
)

if [ ! -d "${VERSION}" ]; then
    if [ ! -f "${VERSION}.tar.gz" ]; then
        ok=0
        for url in "${MIRRORS[@]}"; do
            echo "downloading ${url}"
            if curl -fsSL -o "${VERSION}.tar.gz" "${url}"; then ok=1; break; fi
            echo "  failed, trying next mirror"
        done
        if [ "$ok" -ne 1 ]; then
            echo "Could not download ${VERSION}.tar.gz." >&2
            echo "Fetch it manually from https://pallini.di.uniroma1.it/ and" >&2
            echo "place it in $ROOT, then rerun this script." >&2
            exit 1
        fi
    fi
    tar xzf "${VERSION}.tar.gz"
fi

cd "${VERSION}"
./configure
make -j"$(nproc 2>/dev/null || echo 2)" nauty.a
cd "$ROOT"
ln -sfn "${VERSION}" nauty
echo
echo "nauty built. ./nauty -> ${VERSION}"
echo "Now run: make"
