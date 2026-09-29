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
extern uint8_t *soft_page(Graphics *g, int n);
extern uint32_t g_polyStat[3];
struct DrawRec { uint32_t key; uint32_t polys; std::vector<uint8_t> snap; };
static std::vector<DrawRec> g_list[4];
static int g_lastDraw = -1; static uint32_t g_lastKey, g_lastPoly;
static uint32_t bg_key(int p) { return ((uint32_t)g_bg.h[p] << 16) | g_bg.len[p]; }
static void bg_finalize() {
	if (g_searchMode) { g_lastDraw = -1; return; }
	if (g_lastDraw < 0) return;
	DrawRec r; r.key = g_lastKey; r.polys = g_polyStat[1] - g_lastPoly;
	uint8_t *pg = soft_page(g_gfx, g_lastDraw); r.snap.assign(pg, pg + 64000);
	g_list[g_lastDraw].push_back(r); g_lastDraw = -1;
}
static void bg_byte(int p, uint8_t b) { uint16_t h = g_bg.h[p]; h = (uint16_t)((h << 5) | (h >> 11)) ^ b; g_bg.h[p] = h; g_bg.len[p]++; }
void bg_fill(int p, uint8_t c) {
	bg_finalize(); g_list[p].clear();
	g_bg.h[p] = 0xACE1; g_bg.len[p] = 0; bg_byte(p, 0xF0); bg_byte(p, c);
}
static void bg_begin_draw(int p) { bg_finalize(); g_lastDraw = p; g_lastPoly = g_polyStat[1]; }
void bg_shape(int p, int seg, uint16_t off, int16_t x, int16_t y, uint16_t zoom) {
	bg_begin_draw(p);
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
		for (size_t i = 0; i < l.size(); ++i) { pre += l[i].polys; if (tot - pre <= LIVE_POLYS) { k = i; break; } }
		if ((k < 0 || pre < MIN_POLYS) && tot >= MIN_POLYS) { k = l.size() - 1; pre = tot; }
		if (k >= 0 && pre >= MIN_POLYS && g_e->_res._currentPart >= 16002 && !g_bgseen.count(l[k].key)) {
			g_bgseen.insert(l[k].key);
			TR("cache %d %08x polys%u/%u", p, l[k].key, pre, tot);
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
