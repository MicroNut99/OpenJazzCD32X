/*
 * OpenJazz CD32X - pack baking (PC build only). T32X_BAKE=<file>: every game file the game
 * opens and every picture it builds (Video::createSurface with pixels, finished font atlases)
 * is recorded; at the end of the run they are written as one pack (pack.h).
 * T32X_BAKE_INFO=1 also lists them on stderr.
 */
#include "pack.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

void *__real_malloc(size_t size);   /* bake memory is not game memory: keep it out of the count */
void __real_free(void *p);

typedef struct { char name[16]; uint8_t *data; uint32_t size; } BFile;
typedef struct { uint32_t hash; uint16_t w, h; uint8_t *data; } BPic;
typedef struct { char name[16]; uint32_t start, end, hash; uint16_t w, h; } BFPic;

static BFile *bfiles; static int nfiles, capfiles;
static BPic *bpics;   static int npics, cappics;
static BFPic *bfp;    static int nfp, capfp;
static const char *bake_path;
static int bake_state = -1;   /* -1 not checked, 0 off, 1 on */

static void bake_write(void);

static int bake_on(void)
{
	if (bake_state < 0)
	{
		bake_path = getenv("T32X_BAKE");
		bake_state = bake_path && *bake_path;
		if (bake_state)
			atexit(bake_write);
	}
	return bake_state;
}

static void *grow(void *p, int *cap, int need, size_t elem)
{
	if (need <= *cap) return p;
	int n = *cap ? *cap * 2 : 64;
	while (n < need) n *= 2;
	void *q = __real_malloc((size_t)n * elem);
	if (p) { memcpy(q, p, (size_t)*cap * elem); __real_free(p); }
	*cap = n;
	return q;
}

static void norm(const char *name, char out[16])
{
	const char *b = name;
	for (const char *p = name; *p; ++p)
		if (*p == '/' || *p == '\\') b = p + 1;
	int i = 0;
	for (; b[i] && i < 15; ++i) out[i] = (char)((b[i] >= 'a' && b[i] <= 'z') ? b[i] - 32 : b[i]);
	for (; i < 16; ++i) out[i] = 0;
}

/* T32X_BAKE_SKIP=A,B,...: files left out of the pack (and its catalog) */
static int skipped(const char n[16])
{
	const char *sk = getenv("T32X_BAKE_SKIP");
	if (!sk) return 0;
	size_t l = strlen(n);
	for (const char *p = sk; *p; )
	{
		const char *e = strchr(p, ',');
		size_t k = e ? (size_t)(e - p) : strlen(p);
		if (k == l && strncasecmp(p, n, l) == 0) return 1;
		p += k + (e ? 1 : 0);
	}
	return 0;
}

void pack_bake_file(const char *name, const char *path)
{
	if (!bake_on()) return;
	char n[16];
	norm(name, n);
	if (skipped(n)) return;
	for (int i = 0; i < nfiles; ++i)
		if (memcmp(bfiles[i].name, n, 16) == 0) return;
	FILE *f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = __real_malloc(len > 0 ? (size_t)len : 1);
	if (len > 0 && fread(d, 1, (size_t)len, f) != (size_t)len) { fclose(f); __real_free(d); return; }
	fclose(f);
	bfiles = grow(bfiles, &capfiles, nfiles + 1, sizeof *bfiles);
	memcpy(bfiles[nfiles].name, n, 16);
	bfiles[nfiles].data = d;
	bfiles[nfiles].size = (uint32_t)len;
	nfiles++;
}

void pack_bake_picture(const uint8_t *pixels, int w, int h)
{
	if (!bake_on()) return;
	const uint32_t hv = pack_hash(pixels, w, h);
	const size_t n = (size_t)w * h;
	for (int i = 0; i < npics; ++i)
		if (bpics[i].hash == hv && bpics[i].w == w && bpics[i].h == h && memcmp(bpics[i].data, pixels, n) == 0)
			return;
	bpics = grow(bpics, &cappics, npics + 1, sizeof *bpics);
	bpics[npics].hash = hv;
	bpics[npics].w = (uint16_t)w;
	bpics[npics].h = (uint16_t)h;
	bpics[npics].data = __real_malloc(n ? n : 1);
	memcpy(bpics[npics].data, pixels, n);
	npics++;
}

void pack_bake_file_picture(const char *name, uint32_t start, uint32_t end,
                            const uint8_t *pixels, int w, int h)
{
	if (!bake_on()) return;
	pack_bake_picture(pixels, w, h);
	char n[16];
	norm(name, n);
	for (int i = 0; i < nfp; ++i)
		if (bfp[i].start == start && memcmp(bfp[i].name, n, 16) == 0) return;
	bfp = grow(bfp, &capfp, nfp + 1, sizeof *bfp);
	memcpy(bfp[nfp].name, n, 16);
	bfp[nfp].start = start;
	bfp[nfp].end = end;
	bfp[nfp].hash = pack_hash(pixels, w, h);
	bfp[nfp].w = (uint16_t)w;
	bfp[nfp].h = (uint16_t)h;
	nfp++;
}

static int cmp_pic(const void *a, const void *b)
{
	uint32_t x = ((const BPic *)a)->hash, y = ((const BPic *)b)->hash;
	return x < y ? -1 : x > y;
}

static void put32(FILE *f, uint32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; fwrite(b, 1, 4, f); }
static void put16(FILE *f, uint16_t v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; fwrite(b, 1, 2, f); }
static void pad4(FILE *f, uint32_t *pos) { while (*pos & 3) { fputc(0, f); (*pos)++; } }

/* J4d: T32X_BAKE_CATALOG=<data folder>: every game file there is listed in the pack as well
 * (entry with offset 0 = "on the disc, not in this pack"), so File::exists knows the game is
 * complete - the episode menu greys out episodes whose files it cannot find. */
static char (*catalog)[16];
static uint32_t *catalog_size;
static int ncat;

static void read_catalog(void)
{
	const char *dir = getenv("T32X_BAKE_CATALOG");
	if (!dir) return;
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d)) != NULL)
	{
		if (strlen(e->d_name) > 15 || e->d_name[0] == '.') continue;
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		char n[16];
		norm(e->d_name, n);
		if (skipped(n)) continue;
		int dup = 0;
		for (int i = 0; i < nfiles && !dup; ++i) dup = memcmp(bfiles[i].name, n, 16) == 0;
		if (dup) continue;
		int cap = 0;
		(void)cap;
		char (*nc)[16] = __real_malloc(sizeof *nc * (size_t)(ncat + 1));
		uint32_t *ns = __real_malloc(sizeof *ns * (size_t)(ncat + 1));
		if (ncat) { memcpy(nc, catalog, sizeof *nc * (size_t)ncat); memcpy(ns, catalog_size, sizeof *ns * (size_t)ncat); __real_free(catalog); __real_free(catalog_size); }
		catalog = nc; catalog_size = ns;
		memcpy(catalog[ncat], n, 16);
		catalog_size[ncat] = (uint32_t)st.st_size;
		ncat++;
	}
	closedir(d);
}

static void bake_write(void)
{
	read_catalog();
	qsort(bpics, (size_t)npics, sizeof *bpics, cmp_pic);
	uint32_t pos = 24 + (uint32_t)(nfiles + ncat) * 24 + (uint32_t)npics * 12 + (uint32_t)nfp * 32;
	uint32_t *foff = __real_malloc(sizeof(uint32_t) * (size_t)(nfiles + 1));
	uint32_t *poff = __real_malloc(sizeof(uint32_t) * (size_t)(npics + 1));
	for (int i = 0; i < nfiles; ++i) { pos = (pos + 3) & ~3u; foff[i] = pos; pos += bfiles[i].size; }
	for (int i = 0; i < npics; ++i)  { pos = (pos + 3) & ~3u; poff[i] = pos; pos += (uint32_t)bpics[i].w * bpics[i].h; }
	const uint32_t total = (pos + 3) & ~3u;

	FILE *f = fopen(bake_path, "wb");
	if (!f) { fprintf(stderr, "[bake] cannot write %s\n", bake_path); return; }
	fwrite("OJPK", 1, 4, f); put32(f, 2); put32(f, (uint32_t)(nfiles + ncat)); put32(f, (uint32_t)npics); put32(f, total); put32(f, (uint32_t)nfp);
	for (int i = 0; i < nfiles; ++i) { fwrite(bfiles[i].name, 1, 16, f); put32(f, foff[i]); put32(f, bfiles[i].size); }
	for (int i = 0; i < ncat; ++i)   { fwrite(catalog[i], 1, 16, f); put32(f, 0); put32(f, catalog_size[i]); }
	for (int i = 0; i < npics; ++i)  { put32(f, bpics[i].hash); put16(f, bpics[i].w); put16(f, bpics[i].h); put32(f, poff[i]); }
	for (int i = 0; i < nfp; ++i)
	{
		int j = 0;   /* its picture (recorded too, so it is there) */
		while (j < npics && !(bpics[j].hash == bfp[i].hash && bpics[j].w == bfp[i].w && bpics[j].h == bfp[i].h)) ++j;
		fwrite(bfp[i].name, 1, 16, f); put32(f, bfp[i].start); put32(f, bfp[i].end);
		put16(f, bfp[i].w); put16(f, bfp[i].h); put32(f, j < npics ? poff[j] : 0);
	}
	uint32_t at = 24 + (uint32_t)(nfiles + ncat) * 24 + (uint32_t)npics * 12 + (uint32_t)nfp * 32;
	uint32_t fbytes = 0, pbytes = 0;
	for (int i = 0; i < nfiles; ++i) { pad4(f, &at); fwrite(bfiles[i].data, 1, bfiles[i].size, f); at += bfiles[i].size; fbytes += bfiles[i].size; }
	for (int i = 0; i < npics; ++i)  { uint32_t n = (uint32_t)bpics[i].w * bpics[i].h; pad4(f, &at); fwrite(bpics[i].data, 1, n, f); at += n; pbytes += n; }
	pad4(f, &at);
	fclose(f);
	fprintf(stderr, "[bake] %s: %u bytes - %d files (%u bytes), %d pictures (%u bytes), %d file pictures, %d more files listed\n",
	        bake_path, total, nfiles, fbytes, npics, pbytes, nfp, ncat);
	if (getenv("T32X_BAKE_INFO"))
	{
		for (int i = 0; i < nfiles; ++i) fprintf(stderr, "[bake] file %-15s %8u\n", bfiles[i].name, bfiles[i].size);
		for (int i = 0; i < npics; ++i)  fprintf(stderr, "[bake] picture %4u x %-4u %08X\n", bpics[i].w, bpics[i].h, bpics[i].hash);
	}
}
