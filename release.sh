#!/bin/bash
# Pack a release archive: the p96cts binary, its documentation and the golden
# reference set, as an LHA named for the tag. Run from the repository root
# with the binary already built:
#
#   make docker-build && ./release.sh v0.1
#
# The CI release workflow runs the same two steps, so what it publishes can be
# reproduced (and inspected) locally before a tag is ever pushed.

# bash, not sh: -E and pipefail are not POSIX, as in third_party/build.sh.
set -Eeuxo pipefail

TAG=${1-}
if [ -z "$TAG" ]; then
    echo "usage: $0 <tag>" >&2
    exit 2
fi

if [ ! -f p96cts ]; then
    echo "$0: no p96cts binary; run 'make docker-build' first" >&2
    exit 1
fi

# The git tag is "v0.1"; the archive is "p96cts-0.1.lha", matching both the
# Aminet convention and the program's own "p96cts 0.1" banner.
DIR="p96cts-${TAG#v}"
rm -rf "$DIR" "$DIR.lha"
mkdir -p "$DIR"
cp p96cts README.md LICENSE "$DIR/"
cp -r golden "$DIR/"
lha a "$DIR.lha" "$DIR/"
rm -rf "$DIR"

echo "$0: wrote $DIR.lha"
