/*
 * docinfo.c - File Info (0.6): a document's title, author, description, keywords, copyright and
 * URL (doc.h's DocInfo) in and out of picture files. Written from the format specifications:
 * Adobe's XMP Specification Part 1 (the packet and the Dublin Core properties), the PNG
 * specification (tEXt and iTXt), JPEG's COM and APP1 segments, and TIFF 6.0 / Exif 2.3 (the
 * ImageDescription, Artist, Copyright, DocumentName and XMP tags, and the EXIF Orientation).
 *
 * The same small XMP packet goes into JPEG (APP1), TIFF (tag 700), PSD (resource 1060) and PNG
 * (an iTXt "XML:com.adobe.xmp"), so the six fields come back from any of them; the plainer fields
 * of each format (PNG's Title, Author ... keywords, JPEG's comment, TIFF's ASCII tags, EXIF's
 * Artist and Copyright) are written too where they exist, for programs that don't read XMP.
 *
 * Every reader takes its input as hostile: each length and offset is checked against the data.
 */
#include <stdlib.h>
#include <string.h>
#include "codec.h"
#include "palette.h"
#include "zip.h"
#include "xml.h"

static u32 be16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static u32 be32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

/* ------------------------------------------------------------------ the fields */
/* the six fields by number, for the loops below: their buffers and sizes */
static char *field(DocInfo *f, int k, size_t *cap)
{
  switch (k) {
  case 0: *cap = sizeof f->title; return f->title;
  case 1: *cap = sizeof f->author; return f->author;
  case 2: *cap = sizeof f->description; return f->description;
  case 3: *cap = sizeof f->keywords; return f->keywords;
  case 4: *cap = sizeof f->copyright; return f->copyright;
  default: *cap = sizeof f->url; return f->url;
  }
}
void docinfo_clear(DocInfo *f) { memset(f, 0, sizeof *f); }
int docinfo_empty(const DocInfo *f)
{
  return !f || (!f->title[0] && !f->author[0] && !f->description[0] && !f->keywords[0] && !f->copyright[0] && !f->url[0]);
}
/* n bytes of UTF-8 into a field of cap bytes, cut only between characters; control characters
   other than newlines and tabs become spaces */
void docinfo_set(char *dst, size_t cap, const char *s, size_t n)
{
  size_t i;
  if (!cap) return;
  if (n >= cap) { n = cap - 1; while (n > 0 && ((u8)s[n] & 0xC0) == 0x80) n--; }
  for (i = 0; i < n; i++) { u8 c = (u8)s[i]; dst[i] = (char)(c < 32 && c != '\n' && c != '\t' ? ' ' : c); }
  dst[n] = 0;
}
/* Latin-1 text (PNG tEXt) as UTF-8 into a field */
static void set_latin1(char *dst, size_t cap, const u8 *s, size_t n)
{
  char tmp[600];
  size_t i, o = 0;
  for (i = 0; i < n && o + 2 < sizeof tmp; i++) o += (size_t)utf8_put(tmp + o, s[i]);
  docinfo_set(dst, cap, tmp, o);
}

/* text that is valid UTF-8 (so not Latin-1 that merely could be) */
static int utf8_valid(const u8 *s, size_t n)
{
  size_t i = 0;
  while (i < n) {
    int k = s[i] < 0x80 ? 0 : (s[i] & 0xE0) == 0xC0 ? 1 : (s[i] & 0xF0) == 0xE0 ? 2 : (s[i] & 0xF8) == 0xF0 ? 3 : -1;
    if (k < 0 || (size_t)k >= n - i) return 0;
    for (i++; k > 0; k--, i++) if ((s[i] & 0xC0) != 0x80) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ XMP */
/* one Alt (language alternative) property */
static void xmp_alt(Buf *b, const char *prop, const char *v)
{
  if (!v[0]) return;
  buf_printf(b, "   <%s><rdf:Alt><rdf:li xml:lang=\"x-default\">", prop);
  xml_escape(b, v, strlen(v));
  buf_printf(b, "</rdf:li></rdf:Alt></%s>\n", prop);
}
/* the packet: dc:title, dc:creator, dc:description, dc:subject (the keywords, split at commas and
   semicolons), dc:rights and xmpRights:WebStatement */
void xmp_write(Buf *b, const DocInfo *f)
{
  buf_adds(b, "<?xpacket begin=\"\357\273\277\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
              "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
              "  <rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
              " xmlns:xmpRights=\"http://ns.adobe.com/xap/1.0/rights/\">\n");
  xmp_alt(b, "dc:title", f->title);
  if (f->author[0]) {
    buf_adds(b, "   <dc:creator><rdf:Seq><rdf:li>");
    xml_escape(b, f->author, strlen(f->author));
    buf_adds(b, "</rdf:li></rdf:Seq></dc:creator>\n");
  }
  xmp_alt(b, "dc:description", f->description);
  if (f->keywords[0]) {
    const char *s = f->keywords;
    buf_adds(b, "   <dc:subject><rdf:Bag>");
    while (*s) {
      size_t l;
      while (*s == ' ' || *s == ',' || *s == ';') s++;
      for (l = 0; s[l] && s[l] != ',' && s[l] != ';'; l++) ;
      while (l > 0 && s[l - 1] == ' ') l--;
      if (l) { buf_adds(b, "<rdf:li>"); xml_escape(b, s, l); buf_adds(b, "</rdf:li>"); }
      s += l;
      while (*s == ' ') s++;
    }
    buf_adds(b, "</rdf:Bag></dc:subject>\n");
  }
  xmp_alt(b, "dc:rights", f->copyright);
  if (f->url[0]) {
    buf_adds(b, "   <xmpRights:WebStatement>");
    xml_escape(b, f->url, strlen(f->url));
    buf_adds(b, "</xmpRights:WebStatement>\n");
  }
  buf_adds(b, "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>");
}

/* which field a property's local name fills (-1 none) */
static int xmp_field(const char *name)
{
  static const char *const names[6] = { "title", "creator", "description", "subject", "rights", "WebStatement" };
  int k;
  for (k = 0; k < 6; k++) if (!strcmp(name, names[k])) return k;
  return -1;
}
/* a packet's six properties into f (fields it has replace what f held). Alt properties take their
   first entry; Seq and Bag entries are joined with commas */
int xmp_parse(const char *s, size_t n, DocInfo *f)
{
  Xml x;
  int prop = -1, pdepth = 0, inli = 0, nli = 0, got = 0;
  char val[600];
  size_t vl = 0;
  xml_init(&x, s, n);
  while (xml_next(&x) != XT_EOF) {
    if (x.type == XT_START) {
      int k = xmp_field(x.name);
      if (prop < 0 && !strcmp(x.name, "Description")) {
        const char *a = xml_attr(&x, "WebStatement");       /* the attribute form of a simple property */
        if (a) { docinfo_set(f->url, sizeof f->url, a, strlen(a)); got = 1; }
        if ((a = xml_attr(&x, "title")) != NULL) { docinfo_set(f->title, sizeof f->title, a, strlen(a)); got = 1; }
      }
      if (prop < 0 && k >= 0) { prop = k; pdepth = x.depth; vl = 0; nli = 0; inli = 0; }
      else if (prop >= 0 && !strcmp(x.name, "li")) { inli = 1; if (nli++ && vl + 2 < sizeof val && prop != 0 && prop != 2 && prop != 4) { val[vl++] = ','; val[vl++] = ' '; } }
    } else if (x.type == XT_TEXT && prop >= 0 && (inli || prop == 5)) {
      size_t l = x.text.len;
      if ((prop == 0 || prop == 2 || prop == 4) && nli > 1) continue;   /* other languages of an Alt */
      if (l > sizeof val - 1 - vl) l = sizeof val - 1 - vl;
      memcpy(val + vl, x.text.data, l);
      vl += l;
    } else if (x.type == XT_END && prop >= 0) {
      if (!strcmp(x.name, "li")) inli = 0;
      else if (x.depth < pdepth) {
        size_t cap;
        char *dst = field(f, prop, &cap);
        while (vl > 0 && (val[vl - 1] == ' ' || val[vl - 1] == '\n')) vl--;
        if (vl) { docinfo_set(dst, cap, val, vl); got = 1; }
        prop = -1;
      }
    }
  }
  xml_free(&x);
  return got;
}

/* ------------------------------------------------------------------ TIFF directories (TIFF and EXIF) */
typedef struct { const u8 *d; size_t n; int le; } Ifd;
static u32 g16(const Ifd *t, size_t o) { const u8 *p = t->d + o; return t->le ? p[0] | ((u32)p[1] << 8) : ((u32)p[0] << 8) | p[1]; }
static u32 g32(const Ifd *t, size_t o)
{
  const u8 *p = t->d + o;
  return t->le ? p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24) : ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
/* a classic TIFF header at d[0..n): the byte order and IFD0's entry table; 0 if not one */
static int ifd0(Ifd *t, const u8 *d, size_t n, size_t *first, u32 *count)
{
  u32 off;
  if (n < 8 || !((d[0] == 'I' && d[1] == 'I') || (d[0] == 'M' && d[1] == 'M'))) return 0;
  t->d = d; t->n = n; t->le = d[0] == 'I';
  if (g16(t, 2) != 42) return 0;
  off = g32(t, 4);
  if (off < 8 || off > n - 2) return 0;
  *count = g16(t, off);
  *first = (size_t)off + 2;
  if (*count > (n - *first) / 12) *count = (u32)((n - *first) / 12);   /* a cut-off table: as far as it goes */
  return 1;
}
/* entry i's value bytes (ASCII, BYTE or UNDEFINED) when they are all inside the data */
static const u8 *ifd_bytes(const Ifd *t, size_t e, u32 *len)
{
  u32 type = g16(t, e + 2), cnt = g32(t, e + 4), off;
  if (type != 1 && type != 2 && type != 7) return NULL;
  if (cnt <= 4) { *len = cnt; return t->d + e + 8; }
  off = g32(t, e + 8);
  if (off > t->n || cnt > t->n - off) return NULL;
  *len = cnt;
  return t->d + off;
}
/* an ASCII entry into a field, stopped at its NUL, if the field is still empty */
static void ifd_text(const Ifd *t, size_t e, char *dst, size_t cap)
{
  u32 len, l = 0;
  const u8 *p = ifd_bytes(t, e, &len);
  if (!p || dst[0]) return;
  while (l < len && p[l]) l++;
  while (l > 0 && p[l - 1] == ' ') l--;
  if (l && utf8_valid(p, l)) docinfo_set(dst, cap, (const char *)p, l);
  else if (l) set_latin1(dst, cap, p, l);
}
/* IFD0's XMP into f, then its text tags into the fields still empty; the Orientation (1-8, 0 if
   absent) returned. f may be NULL for just the orientation */
static int tiff_ifd0_info(const u8 *d, size_t n, DocInfo *f)
{
  Ifd t;
  size_t first;
  u32 cnt, i, len;
  int orient = 0, pass;
  if (!ifd0(&t, d, n, &first, &cnt)) return 0;
  for (pass = 0; pass < 2; pass++)
    for (i = 0; i < cnt; i++) {
      size_t e = first + (size_t)i * 12;
      u32 tag = g16(&t, e);
      const u8 *p;
      if (pass == 0) {
        if (tag == 0x0112 && g16(&t, e + 2) == 3) { u32 v = g16(&t, e + 8); if (v >= 1 && v <= 8) orient = (int)v; }
        if (tag == 700 && f && (p = ifd_bytes(&t, e, &len)) != NULL) xmp_parse((const char *)p, len, f);
      } else if (f)
        switch (tag) {
        case 269: ifd_text(&t, e, f->title, sizeof f->title); break;
        case 270: ifd_text(&t, e, f->description, sizeof f->description); break;
        case 315: ifd_text(&t, e, f->author, sizeof f->author); break;
        case 33432: ifd_text(&t, e, f->copyright, sizeof f->copyright); break;
        }
    }
  return orient;
}

/* ------------------------------------------------------------------ JPEG */
/* the segments before the scan: fn sees each marker with its data (len bytes) */
static void jpeg_segments(const u8 *d, size_t n, void (*fn)(int m, const u8 *p, size_t len, void *ctx), void *ctx)
{
  size_t o = 2;
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return;
  while (o + 4 <= n) {
    int m;
    u32 len;
    if (d[o] != 0xFF) return;
    m = d[o + 1];
    if (m == 0xFF) { o++; continue; }                     /* fill bytes */
    if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) { o += 2; continue; }
    if (m == 0xDA || m == 0xD9) return;
    len = be16(d + o + 2);
    if (len < 2 || len > n - o - 2) return;
    fn(m, d + o + 4, len - 2, ctx);
    o += 2 + len;
  }
}
typedef struct { DocInfo *f; int orient, xmp; char com[512]; } JpegScan;
static const char xmp_ns[] = "http://ns.adobe.com/xap/1.0/";
static void jpeg_seg(int m, const u8 *p, size_t len, void *ctx)
{
  JpegScan *s = (JpegScan *)ctx;
  if (m == 0xE1 && len >= 6 && !memcmp(p, "Exif\0", 5)) {
    /* EXIF's ImageDescription, Artist and Copyright fill what the XMP doesn't (an XMP segment
       after this one replaces them) */
    int o = tiff_ifd0_info(p + 6, len - 6, s->f);
    if (!s->orient) s->orient = o;
  } else if (m == 0xE1 && s->f && len > sizeof xmp_ns && !memcmp(p, xmp_ns, sizeof xmp_ns)) {
    if (xmp_parse((const char *)p + sizeof xmp_ns, len - sizeof xmp_ns, s->f)) s->xmp = 1;
  } else if (m == 0xFE && !s->com[0] && len) {
    size_t l = len;
    while (l > 0 && (p[l - 1] == 0 || p[l - 1] == ' ' || p[l - 1] == '\n')) l--;
    docinfo_set(s->com, sizeof s->com, (const char *)p, l);
  }
}
/* the EXIF Orientation of a JPEG (1-8), 1 when it has none */
int jpeg_orientation(const u8 *d, size_t n)
{
  JpegScan s;
  memset(&s, 0, sizeof s);
  jpeg_segments(d, n, jpeg_seg, &s);
  return s.orient ? s.orient : 1;
}
static int jpeg_info(const u8 *d, size_t n, DocInfo *f)
{
  JpegScan s;
  memset(&s, 0, sizeof s);
  s.f = f;
  jpeg_segments(d, n, jpeg_seg, &s);
  if (!f->description[0] && s.com[0]) memcpy(f->description, s.com, sizeof s.com);
  return !docinfo_empty(f);
}
/* the XMP (APP1) and, for programs that show only that, a COM of the description (or title) put
   in after the JFIF segment */
u8 *jpeg_add_info(u8 *jpg, size_t *len, const DocInfo *f)
{
  Buf b, x;
  size_t at = 2, l;
  const char *com;
  if (!jpg || docinfo_empty(f)) return jpg;
  if (*len < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) return jpg;
  if (*len >= 6 && jpg[2] == 0xFF && jpg[3] == 0xE0 && (l = be16(jpg + 4)) + 4 <= *len) at = 4 + l;
  buf_init(&b); buf_init(&x);
  xmp_write(&x, f);
  buf_add(&b, jpg, at);
  if (!x.oom && x.len + sizeof xmp_ns + 2 <= 65535) {
    l = x.len + sizeof xmp_ns + 2;
    buf_addc(&b, 0xFF); buf_addc(&b, 0xE1); buf_addc(&b, (int)(l >> 8)); buf_addc(&b, (int)(l & 255));
    buf_add(&b, xmp_ns, sizeof xmp_ns);
    buf_add(&b, x.data, x.len);
  }
  com = f->description[0] ? f->description : f->title;
  if (com[0]) {
    l = strlen(com) + 2;
    buf_addc(&b, 0xFF); buf_addc(&b, 0xFE); buf_addc(&b, (int)(l >> 8)); buf_addc(&b, (int)(l & 255));
    buf_add(&b, com, l - 2);
  }
  buf_add(&b, jpg + at, *len - at);
  buf_free(&x);
  free(jpg);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}

/* ------------------------------------------------------------------ PNG */
static const char *const png_keys[6] = { "Title", "Author", "Description", "Keywords", "Copyright", "URL" };
/* one chunk with its CRC */
static void png_chunk(Buf *b, const char *type, const u8 *p, size_t len)
{
  u32 crc = crc32_update(crc32_update(0, (const u8 *)type, 4), p, len);
  buf_addc(b, (int)(len >> 24)); buf_addc(b, (int)((len >> 16) & 255)); buf_addc(b, (int)((len >> 8) & 255)); buf_addc(b, (int)(len & 255));
  buf_add(b, type, 4);
  buf_add(b, p, len);
  buf_addc(b, (int)(crc >> 24)); buf_addc(b, (int)((crc >> 16) & 255)); buf_addc(b, (int)((crc >> 8) & 255)); buf_addc(b, (int)(crc & 255));
}
/* each field as tEXt when it is plain ASCII, else as iTXt (UTF-8, uncompressed), then the XMP as an
   iTXt; all straight after IHDR */
u8 *png_add_info(u8 *png, size_t *len, const DocInfo *f)
{
  Buf b, c, x;
  size_t at;
  int k;
  if (!png || docinfo_empty(f)) return png;
  if (*len < 33 || memcmp(png + 12, "IHDR", 4)) return png;
  at = 8 + 12 + be32(png + 8);
  if (at > *len) return png;
  buf_init(&b); buf_init(&c); buf_init(&x);
  buf_add(&b, png, at);
  for (k = 0; k < 6; k++) {
    size_t cap;
    const char *v = field((DocInfo *)f, k, &cap);
    const u8 *s;
    int ascii = 1;
    if (!v[0]) continue;
    for (s = (const u8 *)v; *s; s++) if (*s >= 128) ascii = 0;
    c.len = 0;
    buf_adds(&c, png_keys[k]); buf_addc(&c, 0);
    if (!ascii) { buf_addc(&c, 0); buf_addc(&c, 0); buf_addc(&c, 0); buf_addc(&c, 0); }   /* not compressed; no language */
    buf_adds(&c, v);
    if (!c.oom) png_chunk(&b, ascii ? "tEXt" : "iTXt", (const u8 *)c.data, c.len);
  }
  xmp_write(&x, f);
  c.len = 0;
  buf_adds(&c, "XML:com.adobe.xmp"); buf_addc(&c, 0); buf_addc(&c, 0); buf_addc(&c, 0); buf_addc(&c, 0); buf_addc(&c, 0);
  buf_add(&c, x.data, x.len);
  if (!c.oom && !x.oom) png_chunk(&b, "iTXt", (const u8 *)c.data, c.len);
  buf_add(&b, png + at, *len - at);
  buf_free(&c); buf_free(&x);
  free(png);
  if (b.oom) { buf_free(&b); return NULL; }
  *len = b.len;
  return (u8 *)buf_take(&b);
}
/* tEXt and uncompressed iTXt before the image data: the XMP on the first pass, then the named
   chunks over it on the second */
static int png_info(const u8 *d, size_t n, DocInfo *f)
{
  int pass, k;
  for (pass = 0; pass < 2; pass++) {
    size_t o = 8;
    while (o + 12 <= n) {
      u32 len = be32(d + o);
      const u8 *t = d + o + 4, *p = d + o + 8, *v;
      size_t kl = 0, vl, q;
      int utf;
      if (len > n - o - 12) break;
      if (!memcmp(t, "IDAT", 4) || !memcmp(t, "IEND", 4)) break;
      o += 12 + len;
      if (memcmp(t, "tEXt", 4) && memcmp(t, "iTXt", 4)) continue;
      while (kl < len && kl < 80 && p[kl]) kl++;
      if (kl >= len) continue;
      v = p + kl + 1; vl = len - kl - 1;
      utf = t[0] == 'i';
      if (utf) {                                         /* flag, method, language\0, translated keyword\0 */
        if (vl < 2 || v[0]) continue;                    /* compressed: skipped */
        for (q = 2; q < vl && v[q]; q++) ;
        for (q++; q < vl && v[q]; q++) ;
        if (++q > vl) continue;
        v += q; vl -= q;
      }
      if (kl == 17 && !memcmp(p, "XML:com.adobe.xmp", 17)) { if (pass == 0) xmp_parse((const char *)v, vl, f); continue; }
      if (pass == 1)
        for (k = 0; k < 6; k++)
          if (kl == strlen(png_keys[k]) && !memcmp(p, png_keys[k], kl)) {
            size_t cap;
            char *dst = field(f, k, &cap);
            if (utf) docinfo_set(dst, cap, (const char *)v, vl); else set_latin1(dst, cap, v, vl);
          }
    }
  }
  return !docinfo_empty(f);
}

/* ------------------------------------------------------------------ PSD and ORA */
/* PSD image resource 1060 (XMP) */
static int psd_info(const u8 *d, size_t n, DocInfo *f)
{
  size_t pos = 26, r, rend;
  u32 len;
  if (n < 30) return 0;
  len = be32(d + pos);
  if (len > n - pos - 4) return 0;
  pos += 4 + len;
  if (n - pos < 4 || (len = be32(d + pos)) > n - pos - 4) return 0;
  r = pos + 4; rend = r + len;
  while (rend - r >= 12) {
    u32 id = be16(d + r + 4), nl = d[r + 6], sz;
    size_t q = r + 6 + ((nl + 2) & ~1u);
    if (memcmp(d + r, "8BIM", 4) || q > rend || rend - q < 4) break;
    sz = be32(d + q);
    if (sz > rend - q - 4) break;
    if (id == 1060) return xmp_parse((const char *)d + q + 4, sz, f);
    r = q + 4 + sz + (sz & 1);
  }
  return 0;
}
/* the easel: attributes of an OpenRaster stack.xml's <image> */
static const char *const ora_keys[6] = { "title", "author", "description", "keywords", "copyright", "url" };
void ora_put_info(Buf *xml, const DocInfo *f)
{
  int k;
  for (k = 0; k < 6; k++) {
    size_t cap;
    const char *v = field((DocInfo *)f, k, &cap);
    if (!v[0]) continue;
    buf_printf(xml, " easel:%s=\"", ora_keys[k]);
    xml_escape(xml, v, strlen(v));
    buf_addc(xml, '"');
  }
}
static int ora_info(const u8 *d, size_t n, DocInfo *f)
{
  ZipReader z;
  int i, k;
  u8 *p;
  size_t len;
  if (!zip_open(&z, d, n)) return 0;
  if ((i = zip_find(&z, "stack.xml")) >= 0 && (p = zip_read(&z, i, &len))) {
    Xml x;
    xml_init(&x, (const char *)p, len);
    while (xml_next(&x) != XT_EOF)
      if (x.type == XT_START && !strcmp(x.name, "image")) {
        for (k = 0; k < 6; k++) {
          size_t cap;
          char *dst = field(f, k, &cap);
          const char *a = xml_attr(&x, ora_keys[k]);
          if (a) docinfo_set(dst, cap, a, strlen(a));
        }
        break;
      }
    xml_free(&x);
    free(p);
  }
  zip_close(&z);
  return !docinfo_empty(f);
}

/* ------------------------------------------------------------------ by format */
int codec_read_info(const u8 *d, size_t n, DocInfo *f)
{
  docinfo_clear(f);
  switch (codec_detect(d, n)) {
  case FMT_PNG: return png_info(d, n, f);
  case FMT_JPEG: return jpeg_info(d, n, f);
  case FMT_TIFF: tiff_ifd0_info(d, n, f); return !docinfo_empty(f);
  case FMT_PSD: return psd_info(d, n, f);
  case FMT_ORA: return ora_info(d, n, f);
  }
  return 0;
}

/* the document in its mode as palette.c writes it, with its File Info */
u8 *png_write_doc(Doc *d, size_t *len)
{
  u8 *p = doc_png_encode(d, d->dpi, len);
  return png_add_info(p, len, &d->info);
}
/* the composite over white as a JPEG, with its File Info */
u8 *jpeg_write_doc(Doc *d, int quality, size_t *len)
{
  Rgba im;
  u8 *p;
  im.w = d->w; im.h = d->h;
  if (!(im.px = doc_flat_rgba(d))) return NULL;
  rgba_flatten_white(&im);
  p = jpeg_write_progressive ? jpeg_encode_progressive(&im, quality, d->dpi, len) : NULL;   /* (1.2) */
  if (!p) p = jpeg_encode(&im, quality, d->dpi, len);
  free(im.px);
  return jpeg_add_info(p, len, &d->info);
}
