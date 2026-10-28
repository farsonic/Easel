/*
 * convert.c - EaselConvert, Easel's Shell command for converting pictures in batches: any picture
 * Easel reads (and, as a last resort, any the system's datatypes read) to PNG, JPEG, BMP, IFF ILBM
 * (24-bit or a number of colours), GIF or OpenRaster, resized if asked, with a Workbench icon if
 * asked. The work for each file is core/batch.c's; this is the Shell side: the arguments, the
 * wildcards (MatchFirst/MatchNext), the output names, and a big stack for the AVIF decoder.
 *
 * Template:
 *   FROM/M/A,TO/K,FORMAT/K,WIDTH/N,HEIGHT/N,SCALE/N,QUALITY/N,COLORS/N,DITHER/S,ICON/S,ICONTYPE/K,
 *   DEFTOOL/K,QUIET/S
 * FROM   the pictures (wildcards allowed; .info files are skipped when matching a pattern)
 * TO     a drawer for the results, or (one picture in) the file to write; without it each
 *        result goes beside its picture with the new extension
 * FORMAT PNG (default), JPEG, BMP, ILBM, GIF or ORA; without it, from TO's extension
 * WIDTH, HEIGHT  the new size (one alone keeps the proportions); SCALE  per cent instead
 * QUALITY  JPEG quality 1-100 (85); COLORS  ILBM and GIF colours 2-256 (ILBM default 24-bit);
 * DITHER  Floyd-Steinberg when reducing colours; ICON  write a .info beside each result;
 * ICONTYPE  CLASSIC, NEWICONS, GLOW (default) or PNG; DEFTOOL  the icon's default tool
 * (default SYS:Utilities/MultiView); QUIET  say nothing but errors
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosasl.h>
#include <dos/rdargs.h>
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "../core/batch.h"
#include "../core/iconwrite.h"
#include "../core/adjust.h"
#include "dtload.h"

const char verstag[] = "$VER: EaselConvert 1.0 (28.10.2026) converts pictures (Easel's codecs)";

struct GfxBase *GfxBase;                 /* for dtload.c's older-datatype path */
size_t __stack = 16384;                  /* vbcc's startup gives main this much when the Shell gives less */

#define TEMPLATE "FROM/M/A,TO/K,FORMAT/K,WIDTH/N,HEIGHT/N,SCALE/N,QUALITY/N,COLORS/N,DITHER/S,ICON/S,ICONTYPE/K,DEFTOOL/K,QUIET/S"
enum { A_FROM, A_TO, A_FORMAT, A_WIDTH, A_HEIGHT, A_SCALE, A_QUALITY, A_COLORS, A_DITHER, A_ICON, A_ICONTYPE, A_DEFTOOL, A_QUIET, A_N };

/* the conversion runs on a stack of its own: the AVIF decoder (dav1d) wants far more than a
   Shell command gets (see amiga/bigstack.s and main.c's load_doc_big_stack) */
#define DECODE_STACK (384L * 1024)
long run_on_stack(struct StackSwapStruct *ss, long (*fn)(void *), void *arg);
typedef struct { const char *in, *out; const BatchOpts *o; char err[200]; int ok; } Job;
static long job_run(void *arg)
{
  Job *j = (Job *)arg;
  j->ok = batch_convert(j->in, j->out, j->o, j->err, sizeof j->err);
  return 0;
}
static u8 *stack;
static int convert_one(const char *in, const char *out, const BatchOpts *o, char *err, size_t errcap)
{
  struct StackSwapStruct ss;
  Job j;
  j.in = in; j.out = out; j.o = o; j.err[0] = 0; j.ok = 0;
  ss.stk_Lower = stack;
  ss.stk_Upper = (ULONG)(stack + DECODE_STACK);
  ss.stk_Pointer = (APTR)(stack + DECODE_STACK);
  run_on_stack(&ss, job_run, &j);
  strncpy(err, j.err, errcap - 1); err[errcap - 1] = 0;
  return j.ok;
}

static int is_dir(const char *path)
{
  BPTR l = Lock((STRPTR)path, ACCESS_READ);
  struct FileInfoBlock *fib;
  int dir = 0;
  if (!l) return 0;
  if ((fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL))) {
    if (Examine(l, fib)) dir = fib->fib_DirEntryType > 0;
    FreeDosObject(DOS_FIB, fib);
  }
  UnLock(l);
  return dir;
}
/* the output for in: into the drawer todir (or beside in when NULL), its extension made ext */
static void out_name(const char *in, const char *todir, const char *ext, char *out, size_t cap)
{
  char name[256], *dot;
  strncpy(name, (const char *)FilePart((STRPTR)in), sizeof name - 1); name[sizeof name - 1] = 0;
  if ((dot = strrchr(name, '.')) && dot != name) *dot = 0;
  strncat(name, ext, sizeof name - 1 - strlen(name));
  if (todir) { strncpy(out, todir, cap - 1); out[cap - 1] = 0; }
  else { size_t n = (size_t)((const char *)PathPart((STRPTR)in) - in); if (n >= cap) n = cap - 1; memcpy(out, in, n); out[n] = 0; }
  AddPart((STRPTR)out, (STRPTR)name, cap);
}

static int quiet, nok, nfail;
static void convert_file(const char *in, const char *to, int todir, const BatchOpts *o)
{
  char out[600], err[200];
  if (to && !todir) { strncpy(out, to, sizeof out - 1); out[sizeof out - 1] = 0; }
  else out_name(in, to, batch_ext(o->format), out, sizeof out);
  if (str_ieq(in, out)) { printf("%s: not converted onto itself\n", in); nfail++; return; }
  if (!quiet) { printf("%s -> %s ", in, out); fflush(stdout); }
  if (convert_one(in, out, o, err, sizeof err)) { nok++; if (!quiet) printf("done\n"); }
  else { nfail++; if (quiet) printf("%s: %s\n", in, err); else printf("failed: %s\n", err); }
}

int main(void)
{
  LONG args[A_N];
  struct RDArgs *rd;
  BatchOpts o;
  STRPTR *from;
  const char *to;
  int todir = 0, nin = 0, rc = RETURN_FAIL, i;
  struct AnchorPath *ap = NULL;
  memset(args, 0, sizeof args);
  if (!(rd = ReadArgs((STRPTR)TEMPLATE, args, NULL))) { PrintFault(IoErr(), (STRPTR)"EaselConvert"); return RETURN_FAIL; }
  batch_opts_default(&o);
  o.dither = args[A_DITHER] != 0;
  if (args[A_FORMAT] && (o.format = batch_format_named((const char *)args[A_FORMAT])) == FMT_UNKNOWN) {
    printf("EaselConvert: FORMAT is PNG, JPEG, BMP, ILBM, GIF or ORA\n");
    goto done;
  }
  to = (const char *)args[A_TO];
  if (!o.format && to && !is_dir(to)) o.format = batch_format_of_path(to);
  if (!o.format) o.format = FMT_PNG;
  if (args[A_WIDTH]) o.width = (i32)*(LONG *)args[A_WIDTH];
  if (args[A_HEIGHT]) o.height = (i32)*(LONG *)args[A_HEIGHT];
  if (args[A_SCALE]) o.scale = (int)*(LONG *)args[A_SCALE];
  if (args[A_QUALITY]) o.quality = (int)*(LONG *)args[A_QUALITY];
  if (args[A_COLORS]) o.colors = (int)*(LONG *)args[A_COLORS];
  if (o.width < 0 || o.height < 0 || o.scale < 0 || o.quality < 0 || o.quality > 100 || (o.colors && (o.colors < 2 || o.colors > 256))) {
    printf("EaselConvert: WIDTH, HEIGHT and SCALE can't be negative, QUALITY is 1-100, COLORS 2-256\n");
    goto done;
  }
  if (args[A_ICON]) {
    const char *t = (const char *)args[A_ICONTYPE];
    o.icon = ICON_GLOW + 1;
    if (t) {
      if (str_ieq(t, "CLASSIC")) o.icon = ICON_CLASSIC + 1;
      else if (str_ieq(t, "NEWICONS")) o.icon = ICON_NEWICONS + 1;
      else if (str_ieq(t, "GLOW") || str_ieq(t, "GLOWICONS")) o.icon = ICON_GLOW + 1;
      else if (str_ieq(t, "PNG")) o.icon = ICON_PNG + 1;
      else { printf("EaselConvert: ICONTYPE is CLASSIC, NEWICONS, GLOW or PNG\n"); goto done; }
    }
    o.deftool = args[A_DEFTOOL] ? (const char *)args[A_DEFTOOL] : ICON_TOOL_VIEWER;
  }
  quiet = args[A_QUIET] != 0;
  from = (STRPTR *)args[A_FROM];
  for (i = 0; from[i]; i++) nin++;
  /* TO is a drawer when it is one, or when more than one picture (or a pattern) goes in */
  if (to) {
    LONG wild = 0;
    char pat[512];
    for (i = 0; from[i] && !wild; i++) wild = ParsePatternNoCase(from[i], (STRPTR)pat, sizeof pat) == 1;
    todir = is_dir(to) || nin > 1 || wild;
    if (todir && !is_dir(to)) { printf("EaselConvert: %s isn't a drawer\n", to); goto done; }
  }
  if (!(stack = (u8 *)AllocVec(DECODE_STACK, MEMF_ANY)) || !(ap = (struct AnchorPath *)AllocVec(sizeof(struct AnchorPath) + 600, MEMF_CLEAR))) {
    printf("EaselConvert: not enough memory\n");
    goto done;
  }
  GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39);
  if (GfxBase) o.fallback = dt_load;
  adjlayer_init();
  for (i = 0; from[i]; i++) {
    char pat[512];
    LONG wild = ParsePatternNoCase(from[i], (STRPTR)pat, sizeof pat) == 1, e;
    memset(ap, 0, sizeof(struct AnchorPath) + 600);
    ap->ap_Strlen = 600;
    ap->ap_BreakBits = SIGBREAKF_CTRL_C;
    for (e = MatchFirst(from[i], ap); !e; e = MatchNext(ap)) {
      const char *name = (const char *)ap->ap_Buf;
      size_t l = strlen(name);
      if (ap->ap_Info.fib_DirEntryType > 0) continue;
      if (wild && l > 5 && str_ieq(name + l - 5, ".info")) continue;
      convert_file(name, to, todir, &o);
    }
    MatchEnd(ap);
    if (e == ERROR_BREAK) { PrintFault(e, (STRPTR)"EaselConvert"); break; }
    if (e != ERROR_NO_MORE_ENTRIES) { printf("%s: ", from[i]); fflush(stdout); PrintFault(e, NULL); nfail++; }
  }
  if (!quiet && nok + nfail > 1) printf("%d converted, %d failed\n", nok, nfail);
  rc = nfail ? (nok ? RETURN_WARN : RETURN_ERROR) : nok ? RETURN_OK : RETURN_WARN;
done:
  if (ap) FreeVec(ap);
  if (stack) FreeVec(stack);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  FreeArgs(rd);
  return rc;
}
