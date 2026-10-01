// ---- page-state cache capture (mirrors the SMS engine protocol exactly) ----
// Every page carries a 16-bit rolling hash + byte length of what produced it:
//   fill p,c        -> H=0xACE1, L=0, feed F0 c
//   copy src->dst   -> H[dst]=H[src]^0xC0C0, L[dst]=L[src]+1        (plain)
//                      H[dst]=rotl5(H[dst])^H[src]^vs^0x5C5C, L+=L[src]+1   (vertical-scroll copy)
//   shape/string    -> feed its operands (A0|seg off x y zoom / B0 id x y col)
// key = H<<16 | L. When a page is used (copy source / displayed) the draws since its last reset are
// examined: the page right after the heavy prefix (suffix <= LIVE_POLYS polygons) is stored if the prefix
// has >= MIN_POLYS polygons. The SMS looks up the longest cached prefix and renders the rest live.
#include <set>
#include <vector>
#define MIN_POLYS 8
#define LIVE_POLYS 6
static FILE *g_bgfile = 0;
static std::set<uint32_t> g_bgseen;
static FILE *g_seenfile = 0;   /* BGSEEN: keys already captured by earlier runs (read at start, appended) */
static void bg_want_load();
static void bg_seen_load() {
	bg_want_load();
	const char *fn = getenv("BGSEEN"); if (!fn) return;
	FILE *f = fopen(fn, "rb"); uint32_t k;
	if (f) { while (fread(&k, 4, 1, f) == 1) g_bgseen.insert(k); fclose(f); }
	g_seenfile = fopen(fn, "ab");
}
static FILE *g_keylog = 0;     /* BGKEYS: every candidate state key, repeats included (for frequency counts) */
static void bg_keylog(uint32_t k) { if (!g_keylog && getenv("BGKEYS")) g_keylog = fopen(getenv("BGKEYS"), "ab"); if (g_keylog) { fwrite(&k, 4, 1, g_keylog); fflush(g_keylog); } }
static std::set<uint32_t> g_want;
/* FLOATSET: tiny shapes (seg u8, off u16 records) kept out of the page hash like Lester */
static std::set<uint32_t> g_float; static int g_floatLoaded = 0;
int bg_is_floating(int seg, int off, int zoom) {
	if (!g_floatLoaded) { g_floatLoaded = 1; const char *fn = getenv("FLOATSET");
		if (fn) { FILE *f = fopen(fn, "rb"); uint8_t r[3]; if (f) { while (fread(r, 3, 1, f) == 1) g_float.insert((r[0] << 16) | r[1] | (r[2] << 8)); fclose(f); } } }
	return zoom == 64 && g_float.count((seg << 16) | (off & 0xFFFF)) ? 1 : 0;
}
static FILE *g_prefixlog = 0;
static void bg_prefixlog(uint32_t k, uint32_t polys) {
	if (!g_prefixlog && getenv("BGPREFIX")) g_prefixlog = fopen(getenv("BGPREFIX"), "ab");
	if (g_prefixlog) { fwrite(&k, 4, 1, g_prefixlog); fwrite(&polys, 4, 1, g_prefixlog); }
}
static void bg_want_load() {
	const char *fn = getenv("BGWANT"); if (!fn) return;
	FILE *f = fopen(fn, "rb"); uint32_t k;
	if (f) { while (fread(&k, 4, 1, f) == 1) g_want.insert(k); fclose(f); }
}
static void bg_seen_add(uint32_t k) { if (g_seenfile) { fwrite(&k, 4, 1, g_seenfile); fflush(g_seenfile); } }
extern uint8_t *soft_page(Graphics *g, int n);
extern uint32_t g_polyStat[3];
extern int g_heroDraw;
struct DrawRec { uint32_t key; uint32_t polys; int hero; std::vector<uint8_t> snap; };
static std::vector<DrawRec> g_list[4];
static int g_lastDraw = -1, g_lastHero = 0; static uint32_t g_lastKey, g_lastPoly;
static uint32_t bg_key(int p) { return ((uint32_t)g_bg.h[p] << 16) | g_bg.len[p]; }
static void bg_finalize() {
	if (g_searchMode) { g_lastDraw = -1; return; }
	if (g_lastDraw < 0) return;
	DrawRec r; r.key = g_lastKey; r.polys = g_polyStat[1] - g_lastPoly; r.hero = g_lastHero;
	uint8_t *pg = soft_page(g_gfx, g_lastDraw); r.snap.assign(pg, pg + 64000);
	g_list[g_lastDraw].push_back(r); g_lastDraw = -1;
}
static void bg_byte(int p, uint8_t b) { uint16_t h = g_bg.h[p]; h = (uint16_t)((h << 5) | (h >> 11)) ^ b; g_bg.h[p] = h; g_bg.len[p]++; }
void bg_fill(int p, uint8_t c) {
	bg_finalize(); g_list[p].clear();
	g_bg.h[p] = 0xACE1; g_bg.len[p] = 0; bg_byte(p, 0xF0); bg_byte(p, c);
}
static void bg_begin_draw(int p) { bg_finalize(); g_lastDraw = p; g_lastPoly = g_polyStat[1]; g_lastHero = g_heroDraw; }
void bg_shape(int p, int seg, uint16_t off, int16_t x, int16_t y, uint16_t zoom) {
	bg_begin_draw(p);
	if (g_heroDraw) { g_lastKey = bg_key(p); return; }     /* Lester is not part of the page hash */
	bg_byte(p, 0xA0 | seg); bg_byte(p, off >> 8); bg_byte(p, off & 255); bg_byte(p, (uint16_t)x >> 8); bg_byte(p, x & 255);
	bg_byte(p, (uint16_t)y >> 8); bg_byte(p, y & 255); bg_byte(p, zoom >> 8); bg_byte(p, zoom & 255);
	g_lastKey = bg_key(p);
}
void bg_string(int p, uint16_t id, uint8_t x, uint8_t y, uint8_t c) {
	bg_begin_draw(p);
	bg_byte(p, 0xB0); bg_byte(p, id >> 8); bg_byte(p, id & 255); bg_byte(p, x); bg_byte(p, y); bg_byte(p, c);
	g_lastKey = bg_key(p);
}
void bg_use(int p) {
	bg_finalize();
	if (g_searchMode) { g_list[p].clear(); return; }
	std::vector<DrawRec> &l = g_list[p];
	if (!l.empty()) TR("use %d %08x n%zu", p, l.back().key, l.size());
	if (!l.empty()) {
		uint32_t tot = 0; for (auto &r : l) tot += r.polys;
		uint32_t pre = 0; int k = -1;
		/* state just before Lester, when other shapes follow him: used when one of them overlaps him */
		{ size_t hf = 0; while (hf < l.size() && !l[hf].hero) ++hf;
		  if (hf > 0 && hf + 1 < l.size()) {
			uint32_t ph = 0; for (size_t q = 0; q < hf; ++q) ph += l[q].polys;
			if (ph >= MIN_POLYS && g_e->_res._currentPart >= 16002) bg_keylog(l[hf - 1].key);
			if (ph >= MIN_POLYS && g_e->_res._currentPart >= 16002 && !g_bgseen.count(l[hf - 1].key)) {
				g_bgseen.insert(l[hf - 1].key);
				TR("cache %d %08x polys%u prehero", p, l[hf - 1].key, ph);
				bg_seen_add(l[hf - 1].key);
				if (g_bgfile) { fwrite(&l[hf - 1].key, 4, 1, g_bgfile); fwrite(&l[hf - 1].snap[0], 64000, 1, g_bgfile); fflush(g_bgfile); }
			} } }
		/* BGPREFIX: log (key, prefix polys) of every prefix worth caching; BGWANT: store any prefix whose key is wanted */
		if (g_e->_res._currentPart >= 16002 && (getenv("BGPREFIX") || !g_want.empty())) {
			uint32_t pp = 0;
			for (size_t i = 0; i < l.size(); ++i) {
				pp += l[i].polys;
				if (pp < MIN_POLYS || l[i].hero) continue;
				bg_prefixlog(l[i].key, pp);
				if (g_want.count(l[i].key) && !g_bgseen.count(l[i].key)) {
					g_bgseen.insert(l[i].key); bg_seen_add(l[i].key);
					if (g_bgfile) { fwrite(&l[i].key, 4, 1, g_bgfile); fwrite(&l[i].snap[0], 64000, 1, g_bgfile); fflush(g_bgfile); }
				}
			}
		}
		for (size_t i = 0; i < l.size(); ++i) { pre += l[i].polys; if (tot - pre <= LIVE_POLYS) { k = i; break; } }
		if ((k < 0 || pre < MIN_POLYS) && tot >= MIN_POLYS) { k = l.size() - 1; pre = tot; }
		if (k >= 0 && pre >= MIN_POLYS && g_e->_res._currentPart >= 16002) bg_keylog(l[k].key);
		if (k >= 0 && pre >= MIN_POLYS && g_e->_res._currentPart >= 16002 && !g_bgseen.count(l[k].key)) {
			g_bgseen.insert(l[k].key);
			TR("cache %d %08x polys%u/%u", p, l[k].key, pre, tot);
			bg_seen_add(l[k].key);
			if (g_bgfile) { fwrite(&l[k].key, 4, 1, g_bgfile); fwrite(&l[k].snap[0], 64000, 1, g_bgfile); fflush(g_bgfile); }
		}
		if (p == 0 && k >= 0 && pre >= MIN_POLYS) g_roomKey = l[k].key;
	}
	l.clear();
}
void bg_copy(int src, int dst, int scroll, int16_t vs) {
	bg_finalize();
	if (src == dst) return;
	g_list[dst].clear();
	if (!scroll) { g_bg.h[dst] = g_bg.h[src] ^ 0xC0C0; g_bg.len[dst] = g_bg.len[src] + 1; }
	else { uint16_t h = g_bg.h[dst]; g_bg.h[dst] = (uint16_t)((h << 5) | (h >> 11)) ^ g_bg.h[src] ^ (uint16_t)vs ^ 0x5C5C; g_bg.len[dst] += g_bg.len[src] + 1; }
}
