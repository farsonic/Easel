#!/bin/sh
# build-av1.sh - compile dav1d for Easel. "amiga": vbcc objects in av1/obj (68040 with its FPU);
# "native": a sanitizer-checked library at $2 for the native tests. Only out-of-date objects are
# rebuilt, several at once (the vbcc ones take a while: decode.c alone is a minute or two).
set -e
cd "$(dirname "$0")"
CORE="cdf cpu ctx data decode dequant_tables getbits intra_edge itx_1d lf_mask lib log mem msac obu pal picture qm ref refmvs scan tables thread_task warpmv wedge"
TMPL="cdef_apply cdef fg_apply filmgrain ipred_prepare ipred itx lf_apply loopfilter looprestoration lr_apply mc recon"
jobs() { for f in $CORE; do echo "$f -"; done; for t in $TMPL; do echo "$t 8"; echo "$t 16"; done; }
if [ "$1" = "native" ]; then
  out=${2:-/tmp/easel_dav1d}; mkdir -p "$out"
  jobs | while read f b; do
    if [ "$b" = "-" ]; then src=src/$f.c; o=$out/$f.o; d=; else src=src/${f}_tmpl.c; o=$out/${f}_$b.o; d=-DBITDEPTH=$b; fi
    echo "cc -std=c99 -O1 -g -w -fsanitize=address,undefined -include config.h -Icompat -I. -Iinclude -Isrc $d -c $src -o $o"
  done | xargs -P 8 -I{} sh -c {}
  ar rcs "$out/libdav1d.a" "$out"/*.o
  exit 0
fi
export VBCC=${VBCC:-/opt/amiga/vbcc}
export PATH=$VBCC/bin:$PATH
mkdir -p obj
CFG=$(cd .. && pwd)/tools/vbcc-aos68k-fpu
jobs | while read f b; do
  if [ "$b" = "-" ]; then src=src/$f.c; o=obj/$f.o; d=; else src=src/${f}_tmpl.c; o=obj/${f}_$b.o; d=-DBITDEPTH=$b; fi
  stale=0
  [ -f "$o" ] || stale=1
  [ "$stale" = 0 ] && [ -n "$(find "$src" src include config.h compat -newer "$o" -name '*.[ch]' 2>/dev/null | head -1)" ] && stale=1
  [ "$stale" = 1 ] && echo "vc +$CFG -c99 -O2 -cpu=68040 -fpu=68040 -Icompat -I. -Iinclude -Isrc $d -c $src -o $o >obj/\$(basename $o .o).log 2>&1 || { echo FAILED $src; grep -A1 '^error' obj/\$(basename $o .o).log; exit 1; }"
done | xargs -P 8 -I{} sh -c {}
