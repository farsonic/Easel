#!/bin/sh
# package.sh version workdir: build the Easel-<version>.lha release from the current tree.
# The drawer holds Easel, README.md, Licenses.txt, Install-Easel, Example.jpg, the Fonts and
# TrueType drawers (from the previous package's workdir, prev/), the Dict drawer (round 4, from
# Quill's dict/), and Source/ (the C source, the
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
# round 4: the Dict drawer - Check Spelling's word list and the type's hyphenation patterns, Quill's
# (QUILL=<checkout>, else ../quill beside this one, else /home/user/quill)
if [ ! -d "$P/Dict" ]; then
  Q=${QUILL:-$E/../quill}; [ -d "$Q/dict" ] || Q=$E/../../quill; [ -d "$Q/dict" ] || Q=/home/user/quill
  mkdir -p "$P/Dict"
  for f in british-english LICENSE-dict.txt LICENSE-hyph.txt README_hyph_en_GB.txt hyph_en_GB.dic \
           hyph_de_DE.dic hyph_de_DE.txt hyph_fr_FR.dic hyph_fr_FR.txt hyph_es_ES.dic hyph_es_ES.txt hyph_it_IT.dic hyph_it_IT.txt; do
    cp "$Q/dict/$f" "$P/Dict/"
  done
  cat > "$P/Dict/README" <<'TXT'
british-english: the word list Edit > Check Spelling checks type layers with, one word a line (UTF-8):
SCOWL's British English list (size 50), as Quill ships it; see LICENSE-dict.txt. Words added with
Add go to Easel.words beside Easel, not here.

hyph_*.dic: hyphenation patterns for paragraph type (Paragraph > Hyphenate), in the language of the
interface (British English when there is none); see each .txt and LICENSE-hyph.txt.
TXT
fi
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
