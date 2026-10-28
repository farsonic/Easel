#!/bin/sh
# package.sh version workdir: build the Easel-<version>.lha release from the current tree.
# The drawer holds Easel, README.md, Licenses.txt, Install-Easel, Example.jpg, the Fonts and
# TrueType drawers (from the previous package's workdir, prev/), and Source/ (the C source, the
# dav1d sources and the tests without their generated data). Then the icons, the archive, and a
# check that no certificate or key file went in.
set -e
V=$1; W=$2
[ -n "$V" ] && [ -n "$W" ] || { echo "usage: package.sh version workdir"; exit 2; }
E=$(cd "$(dirname "$0")/.." && pwd)
P=$W/Easel
mkdir -p "$P/Source"
cp "$E/Easel" "$E/EaselConvert" "$E/README.md" "$E/Licenses.txt" "$E/tools/Install-Easel" "$E/Example.jpg" "$P/"
[ -d "$P/Fonts" ] || cp -r "$W/../epkg/Easel/Fonts" "$W/../epkg/Easel/TrueType" "$P/"
sed -i "s/are not part of Quill's own code/are not part of Easel's own code/" "$P/TrueType/LICENSE-fonts.txt"
cd "$E"
cp -r amiga core tools build.sh README.md Licenses.txt "$P/Source/"
mkdir -p "$P/Source/tests" "$P/Source/av1"
cp tests/*.c tests/*.py "$P/Source/tests/"
cp -r av1/src av1/include av1/compat av1/tools av1/config.h av1/vcs_version.h av1/COPYING av1/build-av1.sh "$P/Source/av1/"
python3 "$E/tools/mkicons.py" "$W" >/dev/null
N=$E/Easel-$V.lha
T=$E/.Easel-$V.lha.new
(cd "$W" && lha -aq "$T" Easel Easel.info >/dev/null 2>&1)
mv "$T" "$N"
# the release must never carry a certificate or key (the sandbox CA is for testing only)
D=$(mktemp -d "$W/../echk.XXXX")
(cd "$D" && lha -xq "$N" >/dev/null 2>&1)
if grep -rlE "BEGIN CERT""IFICATE|BEGIN (RSA |EC )?PRIVATE KEY|sandbox-test""-only" "$D" || find "$D" -iname '*.crt' -o -iname '*.pem' -o -iname '*extra-ca*' | grep -q .; then
  echo "CERTIFICATE OR KEY FOUND IN $N"; exit 1
fi
echo "CA check clean"
ls -l "$N"
