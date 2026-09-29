// Headless oracle for rawgl: virtual clock, logs, frame dumps.
#include <stdlib.h>
#include <vector>
#include "engine.h"
#include "graphics.h"
#include "systemstub.h"
#include "util.h"
#include "trace.h"

FILE *g_trace = 0;
uint32_t g_vtime = 0;
uint32_t g_opcount[256];
uint32_t g_opsThisFrame = 0;
uint32_t g_polyStat[3];
static FILE *g_frames = 0;
FILE *g_vars = 0;
static uint32_t g_frameNo = 0;
static Engine *g_e = 0;
static double g_musicAcc; static bool g_musicOn;
struct BgState { uint8_t pend[4]; uint16_t h[4], len[4], nnew[4]; };
BgState g_bg;
uint32_t g_roomKey;
#include "search.inc"
#include "bgcap.inc"
static void varsHook();
void (*g_varsHook)() = varsHook;

struct InEv { uint32_t frame; uint8_t mask; };
static std::vector<InEv> g_inputs;

void trace_frame(const uint8_t *page, const Color *pal) {
	if (g_searchMode) analyse_frame(page);
	if (g_frames) {
		fwrite(&g_vtime, 4, 1, g_frames);
		uint8_t p[48];
		for (int i = 0; i < 16; ++i) { p[i*3] = pal[i].r; p[i*3+1] = pal[i].g; p[i*3+2] = pal[i].b; }
		fwrite(p, 48, 1, g_frames);
		fwrite(page, 320*200, 1, g_frames);
	}
	if (g_vars) fwrite(g_e->_script._scriptVars, 2, 256, g_vars);
	++g_frameNo;
}

static void advance(uint32_t ms) {
	for (uint32_t i = 0; i < ms; ++i) {
		++g_vtime;
		SfxPlayer &ply = g_e->_ply;
		if (g_musicOn && ply._playing && ply._delay) {
			g_musicAcc += 1.0;
			const double tick = ply._delay * 60.0 * 1000.0 / kPaulaFreq;
			while (g_musicAcc >= tick && ply._playing) {
				g_musicAcc -= tick;
				ply.handleEvents();
			}
		}
	}
}

struct NullStub : SystemStub {
	virtual void init(const char *, const DisplayMode *) {}
	virtual void fini() {}
	virtual void prepareScreen(int &w, int &h, float ar[4]) { w = 320; h = 200; }
	virtual void updateScreen() {}
	virtual void setScreenPixels555(const uint16_t *, int, int) {}
	virtual void processEvents() {
		uint8_t m = g_mask;
		if (!g_searchMode) for (size_t i = 0; i < g_inputs.size(); ++i) if (g_inputs[i].frame <= g_frameNo) m = g_inputs[i].mask;
		_pi.dirMask = 0;
		if (m & 1) _pi.dirMask |= PlayerInput::DIR_RIGHT;
		if (m & 2) _pi.dirMask |= PlayerInput::DIR_LEFT;
		if (m & 4) _pi.dirMask |= PlayerInput::DIR_DOWN;
		if (m & 8) _pi.dirMask |= PlayerInput::DIR_UP;
		_pi.action = (m & 0x80) != 0;
	}
	virtual void sleep(uint32_t d) { advance(d); }
	virtual uint32_t getTimeStamp() { return g_vtime; }
};

// ---- Mixer stub (logs) ----
static int resOf(const uint8_t *p) {
	for (int i = 0; i < g_e->_res._numMemList; ++i) if (g_e->_res._memList[i].bufPtr == p) return i;
	return -1;
}
Mixer::Mixer(SfxPlayer *sfx) : _aifc(0), _sfx(sfx), _impl(0) {}
void Mixer::init(MixerType) {}
void Mixer::quit() {}
void Mixer::update() {}
bool Mixer::hasMt32() const { return false; }
bool Mixer::hasMt32SoundMapping(int) { return false; }
void Mixer::playSoundRaw(uint8_t ch, const uint8_t *data, uint16_t freq, uint8_t vol) { TR("sfxplay ch%d res%d freq%d vol%d", ch, resOf(data), freq, vol); }
void Mixer::playSoundWav(uint8_t, const uint8_t *, uint16_t, uint8_t, uint8_t) {}
void Mixer::stopSound(uint8_t ch) { TR("sfxstop ch%d", ch); }
void Mixer::playSoundMt32(int) {}
void Mixer::setChannelVolume(uint8_t ch, uint8_t v) { TR("sfxvol ch%d %d", ch, v); }
void Mixer::playMusic(const char *, uint8_t) {}
void Mixer::stopMusic() {}
void Mixer::playAifcMusic(const char *, uint32_t) {}
void Mixer::stopAifcMusic() {}
void Mixer::playSfxMusic(int num) { TR("musplay %d delay%d order%d", num, _sfx->_delay, _sfx->_sfxMod.curOrder); _sfx->play(44100); g_musicOn = true; g_musicAcc = 0; }
void Mixer::stopSfxMusic() { TR("musstop"); _sfx->stop(); g_musicOn = false; }
void Mixer::stopAll() { TR("stopall"); _sfx->stop(); g_musicOn = false; }
void Mixer::preloadSoundAiff(uint8_t, const uint8_t *) {}
void Mixer::playSoundAiff(uint8_t, uint8_t, uint8_t) {}

// needed globals from main.cpp
bool Graphics::_is1991 = false;
bool Graphics::_use555 = false;
bool Video::_useEGA = false;
Difficulty Script::_difficulty = DIFFICULTY_NORMAL;
bool Script::_useRemasteredAudio = false;

static void varsHook() {
	if (!g_trace) return;
	int16_t *v = g_e->_script._scriptVars;
	fprintf(g_trace, "%u vars f%u scr%d v01=%d v02=%d v03=%d v06=%d\n", g_vtime, g_frameNo, v[0x67], v[1], v[2], v[3], v[6]);
}
int main(int argc, char *argv[]) {
	// oracle DATADIR PART NFRAMES [frames.bin|-] [trace.txt|-] [inputs.txt] [seed]
	if (argc < 4) { fprintf(stderr, "usage\n"); return 1; }
	const char *dataDir = argv[1];
	int part = atoi(argv[2]);
	uint32_t nframes = atoi(argv[3]);
	if (argc > 4 && strcmp(argv[4], "-")) g_frames = fopen(argv[4], "wb");
	if (argc > 5 && strcmp(argv[5], "-")) g_trace = fopen(argv[5], "w");
	if (argc > 6 && strcmp(argv[6], "-")) {
		FILE *f = fopen(argv[6], "r"); unsigned fr, m;
		while (f && fscanf(f, "%u %x", &fr, &m) == 2) { InEv e = { fr, (uint8_t)m }; g_inputs.push_back(e); }
		if (f) fclose(f);
	}
	if (getenv("BGCAP")) g_bgfile = fopen(getenv("BGCAP"), "ab");
	if (getenv("VARS")) g_vars = fopen(getenv("VARS"), "wb");
	g_debugMask = 0;
	Graphics::_is1991 = true;
	Graphics *graphics = GraphicsSoft_create(); g_gfx = graphics;
	NullStub stub;
	Engine *e = new Engine(dataDir, part);
	g_e = e;
	e->setSystemStub(&stub, graphics);
	e->setup(LANG_US, GRAPHICS_ORIGINAL, 0, 1, false);
	e->_script._scriptVars[Script::VAR_RANDOM_SEED] = (argc > 7) ? atoi(argv[7]) : 0x1234;
	if (getenv("OOTW_VARS")) { FILE *vf = fopen(getenv("OOTW_VARS"), "rb"); if (vf) { fread(e->_script._scriptVars, 2, 256, vf); fclose(vf); } }
	g_gfx = graphics;
	if (getenv("SEARCH")) { g_searchMode = 1; g_frames = 0; g_trace = 0; do_search(atoi(getenv("SEARCH")), argv[6]); return 0; }
	while (g_frameNo < nframes) {
		e->run();
	}
	if (g_trace) {
		for (int i = 0; i < 256; ++i) if (g_opcount[i] && i < 0x40) fprintf(g_trace, "# op %02x %u\n", i, g_opcount[i]);
		fclose(g_trace);
	}
	if (g_frames) fclose(g_frames);
	fprintf(stderr, "frames=%u vtime=%u part=%d\n", g_frameNo, g_vtime, e->_res._currentPart);
	return 0;
}
