#include <math.h>
// ---- Go-Explore search (included by oracle.cpp) ----
#include <map>
#include <string>
extern uint8_t *soft_page(Graphics *g, int n);
extern size_t soft_snap(Graphics *g, uint8_t *out, bool save);
int g_dead = 0; int g_lastCopySrc = 0;
static int g_searchMode = 0; static uint8_t g_mask = 0;
static uint32_t g_room = 0; static int g_cx = -1, g_cy = -1;
static Graphics *g_gfx;
static void analyse_frame(const uint8_t *disp) {
	const uint8_t *bg = soft_page(g_gfx, g_lastCopySrc);
	g_room = (uint16_t)g_e->_script._scriptVars[0x67];
	long sx = 0, sy = 0, n = 0;
	for (int y = 0; y < 200; y += 2) for (int x = 0; x < 320; x += 2) if (disp[y*320+x] != bg[y*320+x]) { sx += x; sy += y; ++n; }
	if (n > 8) { g_cx = sx / n; g_cy = sy / n; } else { g_cx = g_cy = -1; }
}
struct Snap {
	std::vector<uint8_t> eng, mem, gfx; uint32_t vtime, frameNo; double musicAcc; bool musicOn;
	std::vector<uint8_t> path; // one mask per chunk
	int maxRank;
	BgState bg; uint32_t room;
};
static void snap_save(Snap &s) {
	s.eng.assign((uint8_t*)g_e, (uint8_t*)g_e + sizeof(Engine));
	s.mem.assign(g_e->_res._memPtrStart, g_e->_res._memPtrStart + Resource::MEM_BLOCK_SIZE);
	s.gfx.resize(soft_snap(g_gfx, 0, true)); soft_snap(g_gfx, &s.gfx[0], true);
	memcpy(&s.bg, &g_bg, sizeof(s.bg)); s.room = g_roomKey;
	s.vtime = g_vtime; s.frameNo = g_frameNo; s.musicAcc = g_musicAcc; s.musicOn = g_musicOn;
}
static void snap_load(const Snap &s) {
	memcpy((uint8_t*)g_e, &s.eng[0], sizeof(Engine));
	memcpy(g_e->_res._memPtrStart, &s.mem[0], Resource::MEM_BLOCK_SIZE);
	soft_snap(g_gfx, (uint8_t*)&s.gfx[0], false);
	memcpy(&g_bg, &s.bg, sizeof(s.bg)); g_roomKey = s.room;
	g_vtime = s.vtime; g_frameNo = s.frameNo; g_musicAcc = s.musicAcc; g_musicOn = s.musicOn;
}
static const int CHUNK = 6;
static const uint8_t ACTS[] = { 0, 1, 2, 4, 8, 0x80, 0x81, 0x82, 0x09, 0x0A, 0x84, 1, 2, 0x81, 0x82 };
// returns 0 ok, 1 dead, 2 part done
static int run_chunk(uint8_t m) {
	g_mask = m; g_dead = 0;
	uint32_t target = g_frameNo + CHUNK;
	int part = g_e->_res._currentPart;
	while (g_frameNo < target) {
		g_e->run();
		if (g_dead) return 1;
		if (g_e->_res._currentPart != part || g_e->_res._nextPart) return 2;
	}
	return 0;
}
static void do_search(int iters, const char *outPath) {
	std::map<std::string, Snap> arch; std::map<std::string, int> chosen;
	srand(1);
	// start state
	Snap s0; snap_save(s0); s0.maxRank = 0; arch["start"] = s0;
	std::map<uint32_t, int> roomOrder;
	for (int it = 0; it < iters; ++it) {
		// pick cell: weight = (1+roomRank) / sqrt(1+chosen)
		double tot = 0; std::vector<std::pair<double, const std::string*> > w;
		for (auto &kv : arch) {
			int rr = kv.second.maxRank;
			const char *us = strrchr(kv.first.c_str(), '_'); int al = us ? atoi(us + 1) : 0;
			double ww = pow(12.0, rr) * pow(0.5, al) / sqrt(1.0 + chosen[kv.first]);
			w.push_back(std::make_pair(ww, &kv.first)); tot += ww;
		}
		double pick = tot * (rand() / (double)RAND_MAX); const std::string *key = w.back().second;
		for (auto &p : w) { pick -= p.first; if (pick <= 0) { key = p.second; break; } }
		chosen[*key]++;
		Snap base = arch[*key]; int maxRank = base.maxRank;
		snap_load(base);
		std::vector<uint8_t> path = base.path;
		uint8_t act = ACTS[rand() % sizeof(ACTS)];
		for (int k = 0; k < 24; ++k) {
			if (rand() % 8 == 0) act = ACTS[rand() % sizeof(ACTS)];
			int r = run_chunk(act); path.push_back(act);
			if (r == 1) break;
			if (r == 2) {
				FILE *f = fopen(outPath, "w");
				for (size_t i = 0; i < path.size(); ++i) fprintf(f, "%u %x\n", (unsigned)(i * CHUNK), path[i]);
				fclose(f);
				fprintf(stderr, "SOLVED it=%d chunks=%zu frames=%u next=%d\n", it, path.size(), g_frameNo, g_e->_res._nextPart);
				return;
			}
			if (g_cx < 0) continue;
			if (!roomOrder.count(g_room)) { int n = roomOrder.size(); roomOrder[g_room] = n; fprintf(stderr, "new room %08x #%d it=%d frame=%u prog=%d\n", g_room, n, it, g_frameNo, g_e->_script._scriptVars[0x2A]);
				char fn[128]; snprintf(fn, sizeof fn, "../cap/room_%d.txt", g_room); FILE *pf = fopen(fn, "w");
				if (pf) { for (size_t q = 0; q < path.size(); ++q) fprintf(pf, "%u %x\n", (unsigned)(q * CHUNK), path[q]); fclose(pf); } }
			int prog = g_e->_script._scriptVars[0x2A];
			if (prog > maxRank) maxRank = prog;
			{ static int bestProg = -1; if (prog > bestProg) { bestProg = prog; fprintf(stderr, "progress %d it=%d frame=%u\n", prog, it, g_frameNo);
				char fn[128]; snprintf(fn, sizeof fn, "../cap/prog_%d.txt", prog); FILE *pf = fopen(fn, "w");
				if (pf) { for (size_t q = 0; q < path.size(); ++q) fprintf(pf, "%u %x\n", (unsigned)(q * CHUNK), path[q]); fclose(pf); } } }
			if (prog >= 6) {
				FILE *f = fopen(outPath, "w");
				for (size_t i = 0; i < path.size(); ++i) fprintf(f, "%u %x\n", (unsigned)(i * CHUNK), path[i]);
				fclose(f); fprintf(stderr, "SOLVED (0x2A=6) it=%d frames=%u\n", it, g_frameNo); return;
			}
			char buf[64]; int alive = 0; for (int k = 0; k < 8; ++k) if (g_e->_script._scriptVars[0x47 + 3 * k] == 183) ++alive;
			if (g_room < 3) alive = 0;   /* slug slots are reused by other screens */
			snprintf(buf, sizeof buf, "%08x_%d_%d_%d_%d", g_room, g_cx / 32, g_cy / 40, g_e->_script._scriptVars[0x2A], alive);
			std::string c(buf);
			auto f = arch.find(c);
			if (f == arch.end() || f->second.path.size() > path.size() + 4) {
				if (arch.size() < 900 || f != arch.end()) { Snap s; snap_save(s); s.path = path; s.maxRank = maxRank; arch[c] = s; }
			}
		}
		if (it % 500 == 0) fprintf(stderr, "it=%d cells=%zu rooms=%zu\n", it, arch.size(), roomOrder.size());
	}
	fprintf(stderr, "not solved\n");
}
