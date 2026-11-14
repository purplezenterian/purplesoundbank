#include "PsbEngine.h"
#include <algorithm>
#include <cstring>
#include <set>
#include <mutex>
#include <atomic>

namespace psb {

static const double PI = 3.14159265358979323846;
static double gAttK = 0.12, gRelK = 0.5;   // time-constant factors fitted against the web compressor
#include <cstdlib>
struct EnvInit { EnvInit() { if (const char* a = std::getenv("PSB_ATTK")) gAttK = std::atof(a); if (const char* r = std::getenv("PSB_RELK")) gRelK = std::atof(r); } } gEnvInit;
static inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
static inline double linToDb(double x) { return 20.0 * std::log10(x); }

// ---------------------------------------------------------------------------------------------
// Biquad: same coefficient formulas the browser uses, so the tone matches the web instrument.
// ---------------------------------------------------------------------------------------------
void Biquad::set(int type, double sr, double f, double q, double gdb) {
    const double cutoff = std::max(0.0, std::min(1.0, f / (sr * 0.5)));
    double B0 = 1, B1 = 0, B2 = 0, A1 = 0, A2 = 0;
    bypass = false;
    switch (type) {
        case BQ_LP:
        case BQ_HP: {
            if (cutoff >= 1.0 || cutoff <= 0.0) { B0 = (type == BQ_LP) ? cutoff : (cutoff >= 1.0 ? 0.0 : 1.0); break; }
            const double Q = std::pow(10.0, q / 20.0);   // lowpass/highpass Q is in dB
            const double w0 = PI * cutoff, alpha = std::sin(w0) / (2 * Q), k = std::cos(w0);
            double b0, b1, b2;
            if (type == BQ_LP) { b0 = (1 - k) / 2; b1 = 1 - k; b2 = b0; }
            else { b0 = (1 + k) / 2; b1 = -(1 + k); b2 = b0; }
            const double a0 = 1 + alpha;
            B0 = b0 / a0; B1 = b1 / a0; B2 = b2 / a0; A1 = -2 * k / a0; A2 = (1 - alpha) / a0;
            break;
        }
        case BQ_PEAK: {
            if (std::fabs(gdb) < 1e-6 || cutoff <= 0.0 || cutoff >= 1.0) { bypass = true; break; }
            const double A = std::pow(10.0, gdb / 40.0), w0 = PI * cutoff;
            const double alpha = std::sin(w0) / (2 * std::max(1e-4, q)), k = std::cos(w0);
            const double b0 = 1 + alpha * A, b1 = -2 * k, b2 = 1 - alpha * A, a0 = 1 + alpha / A, a1 = -2 * k, a2 = 1 - alpha / A;
            B0 = b0 / a0; B1 = b1 / a0; B2 = b2 / a0; A1 = a1 / a0; A2 = a2 / a0;
            break;
        }
        case BQ_LSHELF:
        case BQ_HSHELF: {
            if (std::fabs(gdb) < 1e-6) { bypass = true; break; }
            const double A = std::pow(10.0, gdb / 40.0);
            if (cutoff >= 1.0) { B0 = (type == BQ_LSHELF) ? A * A : 1.0; break; }
            if (cutoff <= 0.0) { B0 = (type == BQ_LSHELF) ? 1.0 : A * A; break; }
            const double w0 = PI * cutoff, alpha = std::sin(w0) / 2.0 * std::sqrt(2.0), k = std::cos(w0);
            const double k2 = 2 * std::sqrt(A) * alpha, ap = A + 1, am = A - 1;
            double b0, b1, b2, a0, a1, a2;
            if (type == BQ_LSHELF) {
                b0 = A * (ap - am * k + k2); b1 = 2 * A * (am - ap * k); b2 = A * (ap - am * k - k2);
                a0 = ap + am * k + k2; a1 = -2 * (am + ap * k); a2 = ap + am * k - k2;
            } else {
                b0 = A * (ap + am * k + k2); b1 = -2 * A * (am + ap * k); b2 = A * (ap + am * k - k2);
                a0 = ap - am * k + k2; a1 = 2 * (am - ap * k); a2 = ap - am * k - k2;
            }
            B0 = b0 / a0; B1 = b1 / a0; B2 = b2 / a0; A1 = a1 / a0; A2 = a2 / a0;
            break;
        }
    }
    b0 = B0; b1 = B1; b2 = B2; a1 = A1; a2 = A2;
}

// ---------------------------------------------------------------------------------------------
// Compressor / limiter
// ---------------------------------------------------------------------------------------------
double Comp::kneeCurve(double x, double kk) const {
    if (x < linThr) return x;
    return linThr + (1.0 - std::exp(-kk * (x - linThr))) / kk;
}
double Comp::slopeAt(double x, double kk) const {
    if (x < linThr) return 1.0;
    const double x2 = x * 1.001;
    const double xd = linToDb(x), x2d = linToDb(x2);
    const double yd = linToDb(kneeCurve(x, kk)), y2d = linToDb(kneeCurve(x2, kk));
    return (y2d - yd) / (x2d - xd);
}
double Comp::saturate(double x, double kk) const {
    if (x < kneeThr) return kneeCurve(x, kk);
    const double xd = linToDb(x);
    const double yd = ymaxDb + slope * (xd - kneeThrDb);
    return dbToLin(yd);
}
void Comp::prepare(double sampleRate, double thr, double knee, double rat, double a, double r) {
    sr = sampleRate; thrDb = thr; kneeDb = knee; ratio = rat; att = a; rel = r;
    linThr = dbToLin(thrDb);
    kneeThrDb = thrDb + kneeDb;
    kneeThr = dbToLin(kneeThrDb);
    slope = 1.0 / ratio;
    // find k so the knee curve meets the ratio slope at the end of the knee
    {
        double mn = 0.1, mx = 10000, kk = 5;
        for (int i = 0; i < 15; i++) {
            const double s = slopeAt(kneeThr, kk);
            if (s < slope) mx = kk; else mn = kk;
            kk = std::sqrt(mn * mx);
        }
        k = kk;
    }
    ymaxDb = linToDb(kneeCurve(kneeThr, k));
    const double full = saturate(1.0, k);
    makeup = std::pow(1.0 / full, 0.6);
    attC = 1.0 - std::exp(-1.0 / (std::max(1e-4, att) * gAttK * sr));
    relC = 1.0 - std::exp(-1.0 / (std::max(1e-4, rel) * gRelK * sr));
    dlen = (int) std::lround(0.006 * sr);
    for (auto& v : dl) v.assign((size_t) dlen + 1, 0.f);
    w = 0; g = 1;
}
void Comp::reset() { for (auto& v : dl) std::fill(v.begin(), v.end(), 0.f); w = 0; g = 1; }
void Comp::process(float* L, float* R, int n) {
    const int len = dlen + 1;
    for (int i = 0; i < n; i++) {
        const float inL = L[i], inR = R[i];
        const double pk = std::max(std::fabs((double) inL), std::fabs((double) inR));
        double target = 1.0;
        if (pk > 1e-9 && pk > linThr) target = std::min(1.0, saturate(pk, k) / pk);
        if (target < g) g += (target - g) * attC; else g += (target - g) * relC;
        if (g > 1.0) g = 1.0;
        dl[0][w] = inL; dl[1][w] = inR;
        int rd = w - dlen; if (rd < 0) rd += len;
        L[i] = (float) (dl[0][rd] * g * makeup);
        R[i] = (float) (dl[1][rd] * g * makeup);
        if (++w >= len) w = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Engine internals
// ---------------------------------------------------------------------------------------------
struct Voice;
struct Channel;

struct Hv { double amp = 1, cut = 20000, smack = 0, at = 0.002, swell = 1, spread = 8, detune = 0; };

struct Layer {
    bool used = false, dead = false, isSub = false;
    Channel* ch = nullptr;
    Voice* voice = nullptr;
    const Pcm* pcm = nullptr;
    double pos = 0, ratio = 1, srRatio = 1, staticCents = 0;
    double glideC = 0, glideDur = 0, glideT = 0, slideC = 0, slideDur = 0, slideT = 0;
    bool loop = false; double loopS = 0, loopE = 0; int frames = 0;
    // amp envelope
    int ph = 0;   // 0 linear attack, 1 settle, 2 release
    double g = 0, atkStep = 0, atkTo = 0; int atkLeft = 0;
    bool settle = false; double setTarget = 0, setCoef = 0;
    double relCoef = 0;
    // filters
    Biquad vf, sm, ls, m1, m2, f;
    bool hasLs = false, hasMid = false;
    bool vfRamp = false; double vfFrom = 0, vfTo = 0; double vfT = 0, vfDur = 0, vfCur = 0;
    double smBase = 0, smBoost = 0; bool smDecay = false;
    double fTarget = 20000, fCur = 20000, fCoefPerSec = 0, fQ = 0; bool fMove = false;
    bool fat = false; double pan = 0, panGl = 1, panGr = 0;
    bool polyCounted = false, acct = false;
    double age = 0;   // seconds
    double driftPhaseOff = 0;
};

struct Voice {
    bool used = false;
    int src = 0, note = 0;
    bool up = false, unheld = false;
    std::vector<Layer*> parts;
};

struct Channel {
    SoundDef def;
    bool ready = false;
    std::map<int, Pcm> pcm;
    std::vector<int> keys;
    bool mute = false, off = false, solo = false, cut = false;
    double vol = 1;
    struct { double a = 0, d = 0.5, s = 1, r = 0.4; } env, env0;
    struct { double cutoff = 20000, reso = 0, env = 0, glide = 0; } fxo;
    Smooth outG, polyG, vibGain;
    int held = 0, alive = 0, live = 0;
    std::vector<Voice*> vox;
    std::vector<Biquad> tone;
    Biquad chHP, chHS, chEQ[5];
    bool legatoNow = false;
    double vibPhase = 0;
    std::vector<float> bl, br;
    // ensemble
    struct Dl { std::vector<float> l, r; int w = 0; double ph = 0, rate = 0, dep = 0, t = 0, pan = 0; } ens[3];
    bool ensOn() const { return def.ensemble; }
};

// A control message for the audio thread. Everything from the page goes through a lock-free queue and is applied
// by the audio thread itself at the start of a block, so the audio thread never waits for anyone.
struct Cmd {
    enum { CH, ENV, FX, MASTER, GFX, NOTEON, NOTEOFF, CC, BEND, ALLOFF } t = CH;
    char id[48] = {0};
    double a = 0, b = 0, c = 0, d = 0;
    int i = 0, j = 0;
};

struct Engine::Impl {
    std::mutex mtx;    // only for rare structural changes (loading sounds, prepare); the audio thread only ever try-locks it
    std::mutex dmtx;   // guards defs for the non-audio threads
    std::mutex rmtx;   // guards readyIds for the non-audio threads
    std::set<std::string> readyIds;
    static constexpr uint32_t QN = 16384, HN = 512;
    std::vector<Cmd> q = std::vector<Cmd>(QN);
    std::atomic<uint32_t> qr{0}, qw{0};
    std::atomic_flag qlock = ATOMIC_FLAG_INIT;
    Cmd hq[HN]; uint32_t hr = 0, hw = 0;   // audio thread's own host-MIDI queue (no sharing at all)
    std::vector<Channel*> scratchCh;
    std::vector<Voice*> scratchA, scratchB;
    bool push(const Cmd& cm) {
        while (qlock.test_and_set(std::memory_order_acquire)) {}   // producers only: the message thread and loader threads
        const uint32_t w = qw.load(std::memory_order_relaxed);
        const bool ok = (w - qr.load(std::memory_order_acquire)) < QN;
        if (ok) { q[w % QN] = cm; qw.store(w + 1, std::memory_order_release); }
        qlock.clear(std::memory_order_release);
        return ok;
    }
    void pushRT(const Cmd& cm) { if (hw - hr < HN) { hq[hw % HN] = cm; ++hw; } }
    void apply(const Cmd& cm);
    void drain() {
        uint32_t r = qr.load(std::memory_order_relaxed);
        const uint32_t w = qw.load(std::memory_order_acquire);
        while (r != w) { apply(q[r % QN]); ++r; qr.store(r, std::memory_order_release); }
        while (hr != hw) { apply(hq[hr % HN]); ++hr; }
    }
    static Cmd mk(int t, const std::string& id = std::string()) {
        Cmd cm; cm.t = (decltype(cm.t)) t;
        const size_t n = std::min<size_t>(id.size(), sizeof(cm.id) - 1);
        std::memcpy(cm.id, id.data(), n); cm.id[n] = 0;
        return cm;
    }
    std::vector<SoundDef> defs;
    double sr = 48000;
    std::vector<std::unique_ptr<Channel>> chs;
    std::vector<Layer> layers;
    std::vector<Voice> voices;
    std::vector<Voice*> active, sustained;   // active: voices whose key is down
    struct Held { int src, note, vel; };
    std::vector<Held> monoHeld;
    bool pedalUi = false, midiPedal = false;
    int lastGlideNote = -1000;
    double bendCents = 0, masterWant = 0.8, muteWant = 1.0;
    Smooth bendS, modS, vibModS, masterG, outMute;
    double modVal = 0;
    double lfoPh = 0, driftPh = 0;
    struct { double cutoff = 20000, reso = 0, env = 0, glide = 0; } fx;
    // master chain
    Biquad hp;
    Comp comp, lim;
    double limFix = 1;
    std::vector<float> mL, mR;
    uint64_t rng = 88172645463325252ULL;
    double clockT = 0;
    static constexpr int BLK = 32;

    bool testMode = false;
    double rnd() {   // -1..1
        if (testMode) return 0.0;
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return ((double) (rng >> 11) / 9007199254740992.0) * 2.0 - 1.0;
    }
    double rnd01() { return testMode ? 0.5 : (rnd() + 1.0) * 0.5; }

    Layer* allocLayer() {
        for (auto& l : layers) if (!l.used) { l = Layer(); l.used = true; return &l; }
        return nullptr;
    }
    Voice* allocVoice() {
        for (auto& v : voices) if (!v.used) { v.used = true; v.parts.clear(); v.up = v.unheld = false; return &v; }
        return nullptr;
    }
    bool pedalOn() const { return pedalUi || midiPedal; }
    Channel* find(const std::string& id) { for (auto& c : chs) if (c->def.id == id) return c.get(); return nullptr; }

    static double glideSecs(double n) { return n < 0.02 ? 0.0 : 0.02 * std::pow(75.0, n); }

    struct Shape { double amp, cut, smack; };
    static Shape velShape(int note, int vel) {
        const double v = std::min(127, std::max(1, vel));
        Shape s;
        s.amp = std::min(1.45, std::pow(v / 100.0, 1.7));
        const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double track = std::min(6.0, std::max(0.6, std::pow(hz / 261.6, 0.6)));
        s.cut = v >= 100 ? 20000.0 : std::min(20000.0, std::max(600.0, 20000.0 * std::pow(v / 100.0, 2.4) * track));
        s.smack = v > 100 ? (v - 100) / 27.0 * 5.0 : 0.0;
        return s;
    }
    Hv humanize(int vel, const Shape& sh, const Channel& ch) {
        const double x = std::min(1.0, vel / 127.0);
        const bool deep = ch.def.velDeep;
        const double soft = vel < 100 ? (deep ? 0.28 + 0.72 * std::pow(vel / 100.0, 0.9) : 0.55 + 0.45 * std::min(1.0, vel / 100.0)) : 1.0;
        Hv h;
        h.amp = sh.amp * std::pow(10.0, rnd() * 0.5 / 20.0);
        h.cut = std::min(20000.0, sh.cut * soft * (1 + rnd() * 0.05));
        h.smack = vel > 90 ? (vel - 90) / 37.0 * (deep ? 11 : 8) * (ch.def.tame ? 0.35 : 1.0) : 0.0;
        h.at = ch.def.fat ? 0.002 + 0.025 * std::pow(1 - x, 2.2) + std::fabs(rnd()) * 0.004
                          : (vel < 90 ? 0.001 + 0.013 * std::pow(1 - vel / 90.0, 2) + std::fabs(rnd()) * 0.0015 : 0.001);
        h.swell = deep ? 0.42 + 0.58 * x : 1.0;
        h.spread = ch.def.fatSpread > 0 ? ch.def.fatSpread : 4 + 11 * x;
        h.detune = rnd() * 2;
        return h;
    }

    static int nearestKey(const std::vector<int>& keys, int note) {
        int best = keys[0];
        for (int k : keys) if (std::abs(k - note) < std::abs(best - note)) best = k;
        return best;
    }

    void polyUpdate(Channel& ch) {
        if (!ch.def.poly) return;
        const double nn = std::max(1.0, (ch.held + 0.03 * std::max(0, ch.alive - ch.held)) / 2.0);
        const double g = std::pow(nn, -0.5);
        ch.polyG.target = g;
        ch.polyG.setTau(g < ch.polyG.cur ? 0.006 : 0.25, sr);
    }

    void unhold(Voice* v) {
        if (v->unheld) return;
        v->unheld = true;
        for (Layer* p : v->parts) if (p->ch->def.poly && p->polyCounted) p->ch->held = std::max(0, p->ch->held - 1);
        for (Layer* p : v->parts) polyUpdate(*p->ch);
    }

    void startRelease(Layer* p, double tau) {
        p->ph = 2;
        p->relCoef = 1.0 - std::exp(-1.0 / (std::max(0.001, tau) * sr));
    }

    void releaseVoice(Voice* v) {
        unhold(v);
        for (Layer* p : v->parts) {
            if (p->dead || p->ph == 2) continue;
            Channel& c = *p->ch;
            const double rEff = c.def.relPow > 0 ? std::min(30.0, c.env0.r * std::pow(c.env.r / c.env0.r, c.def.relPow)) : c.env.r;
            startRelease(p, std::max(0.005, rEff / 4.0));
        }
        sustained.erase(std::remove(sustained.begin(), sustained.end(), v), sustained.end());
    }

    void forget(Voice* v) {
        active.erase(std::remove(active.begin(), active.end(), v), active.end());
    }

    int layersLive() const {
        int n = 0;
        for (auto* v : active) n += (int) v->parts.size();
        for (auto* v : sustained) n += (int) v->parts.size();
        return n;
    }

    bool monoActive() const {
        for (auto& c : chs) if (c->def.monoVoice && c->ready && !c->off && !c->mute) return true;
        return false;
    }

    std::vector<std::unique_ptr<Channel>> makeChannels(const std::vector<SoundDef>& defs);
    void adopt(std::vector<std::unique_ptr<Channel>> fresh);
    std::map<std::string, std::unique_ptr<Channel>> retired;
    void allOff();
    void doNoteOn(int src, int note, int vel, bool monoOnly);
    void doNoteOff(int src);
    void updateFilters();
    void syncPedal() { if (!pedalOn()) { scratchB.assign(sustained.begin(), sustained.end()); for (auto* v : scratchB) releaseVoice(v); } }
    void gcLayers();
    void applyMix();
};

// ---------------------------------------------------------------------------------------------
// Loop builder (port of the web version's makeLoop)
// ---------------------------------------------------------------------------------------------
static bool makeLoop(Pcm& b, const LoopCfg& lp) {
    const double sr = b.sr;
    const int N = b.frames();
    const double len = N / sr;
    const int ls = (int) std::lround(std::min(lp.start, len * 0.45) * sr);
    int le = std::min((int) std::lround(lp.end * sr), N);
    if (lp.endFrac > 0) le = std::min(le, (int) std::lround(N * lp.endFrac));
    if ((le - ls) / sr < 0.3) return false;
    const int c = std::min({(int) std::lround(lp.xfade * sr), ls, (int) std::floor((le - ls) * 0.6)});
    if (c < 1) return false;
    if (lp.align > 0) {
        const float* x = b.l.data();
        double best = -2; int bestEnd = le;
        const int lo = std::max(ls + (int) std::lround(0.3 * sr) + c, le - (int) std::lround(lp.align * sr));
        for (int e = le; e >= lo; e -= 2) {
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < c; i += 6) { const double u = x[e - c + i], v = x[ls - c + i]; sxy += u * v; sxx += u * u; syy += v * v; }
            const double r = sxy / std::sqrt(sxx * syy + 1e-12);
            if (r > best) { best = r; bestEnd = e; }
        }
        le = bestEnd;
    }
    float* chn[2] = {b.l.data(), b.r.data()};
    if (lp.flat) {
        const int w = std::max(64, (int) std::lround(0.08 * sr));
        auto rmsAt = [&](const float* a, int i0) { double e = 0; for (int i = i0; i < i0 + w && i < N; i++) e += (double) a[i] * a[i]; return std::sqrt(e / w) + 1e-9; };
        const double rs = rmsAt(chn[0], ls), re = rmsAt(chn[0], std::max(ls, le - c - w));
        const double ratio = std::min(6.0, std::max(1.0, rs / re));
        const double span = std::max(1, le - ls);
        for (int n = 0; n < 2; n++) for (int i = ls; i < le; i++) chn[n][i] = (float) (chn[n][i] * std::pow(ratio, (i - ls) / span));
    }
    for (int n = 0; n < 2; n++) {
        for (int i = 0; i < c; i++) {
            const double w = (i + 0.5) / c;
            if (lp.lin) chn[n][le - c + i] = (float) (chn[n][le - c + i] * (1 - w) + chn[n][ls - c + i] * w);
            else chn[n][le - c + i] = (float) (chn[n][le - c + i] * std::cos(w * PI / 2) + chn[n][ls - c + i] * std::sin(w * PI / 2));
        }
    }
    b.looped = true; b.loopS = (double) ls / sr; b.loopE = (double) le / sr;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Engine API
// ---------------------------------------------------------------------------------------------
Engine::Engine() : d(new Impl) {}
Engine::~Engine() {}

void Engine::prepare(double sampleRate) {
    Impl& E = *d;
    std::lock_guard<std::mutex> lk(E.mtx);
    E.sr = sampleRate;
    if (E.layers.size() != 640) E.layers.assign(640, Layer()); else for (auto& l : E.layers) l = Layer();
    if (E.voices.size() != 320) { E.voices.assign(320, Voice()); for (auto& v : E.voices) v.parts.reserve(48); }
    else for (auto& v : E.voices) { v.used = false; v.parts.clear(); }
    E.active.clear(); E.sustained.clear(); E.monoHeld.clear();
    E.active.reserve(512); E.sustained.reserve(512); E.monoHeld.reserve(128);
    E.scratchA.reserve(512); E.scratchB.reserve(512); E.scratchCh.reserve(128);
    E.bendS.init(0, 0.008, sampleRate); E.modS.init(0, 0.02, sampleRate); E.vibModS.init(0, 0.015, sampleRate);
    E.masterG.init(E.masterWant, 0.005, sampleRate); E.outMute.init(E.muteWant, 0.02, sampleRate);
    E.hp.set(BQ_HP, sampleRate, 1.0, 0.7, 0); E.hp.reset();
    E.comp.prepare(sampleRate, -10, 12, 4, 0.003, 0.2);
    E.lim.prepare(sampleRate, -3, 0, 20, 0.001, 0.12);
    E.limFix = 1.0 / std::pow(1.0 / std::pow(10.0, (-3 + 3.0 / 20) / 20), 0.6);
    E.mL.assign(Impl::BLK, 0.f); E.mR.assign(Impl::BLK, 0.f);
    if (!E.defs.empty()) { auto fresh = E.makeChannels(E.defs); E.adopt(std::move(fresh)); }
}

int Engine::latencySamples() const { return d->comp.latency() + d->lim.latency(); }

// Builds the channels (all the memory) without touching the running engine, so no lock is needed for it.
std::vector<std::unique_ptr<Channel>> Engine::Impl::makeChannels(const std::vector<SoundDef>& defs) {
    Impl& E = *this;
    std::vector<std::unique_ptr<Channel>> out;
    for (const auto& s : defs) {
        auto c = std::make_unique<Channel>();
        c->vox.reserve(160);
        c->def = s;
        c->env0.r = std::round((s.rel > 0 ? s.rel : 0.09) * 4 * 100.0) / 100.0;
        c->env = c->env0;
        c->tone.resize(s.tone.size());
        c->outG.init(0, 0.015, E.sr); c->polyG.init(1, 0.25, E.sr); c->vibGain.init(0, 0.015, E.sr);
        c->bl.assign(Impl::BLK, 0.f); c->br.assign(Impl::BLK, 0.f);
        const double rates[3] = {0.31, 0.47, 0.23}, deps[3] = {0.0026, 0.0032, 0.0022}, ts[3] = {0.017, 0.025, 0.031}, pans[3] = {-0.85, 0.85, 0.0};
        for (int i = 0; i < 3; i++) {
            c->ens[i].l.assign((size_t) (0.1 * E.sr) + 4, 0.f); c->ens[i].r = c->ens[i].l;
            c->ens[i].rate = rates[i]; c->ens[i].dep = deps[i]; c->ens[i].t = ts[i]; c->ens[i].pan = pans[i];
        }
        for (size_t i = 0; i < s.tone.size(); i++) c->tone[i].set(s.tone[i].type, E.sr, s.tone[i].f, s.tone[i].type == BQ_LP || s.tone[i].type == BQ_HP ? 0 : s.tone[i].q, s.tone[i].g);
        for (size_t i = 0; i < s.tone.size(); i++) if (s.tone[i].type == BQ_HP) c->tone[i].set(BQ_HP, E.sr, s.tone[i].f, 0.7, 0);
        c->chHP.set(BQ_HP, E.sr, 1.0, 0.7, 0);
        c->chHS.set(BQ_HSHELF, E.sr, 5000, 0, 0);
        const double bf[5] = {60, 250, 1000, 4000, 12000};
        const int bt[5] = {BQ_LSHELF, BQ_PEAK, BQ_PEAK, BQ_PEAK, BQ_HSHELF};
        for (int i = 0; i < 5; i++) c->chEQ[i].set(bt[i], E.sr, bf[i], i == 0 || i == 4 ? 0.7 : 0.9, 0);
        if (s.hasModKnob) {   // the mod wheel sits at the bottom to start: its knob value sets this sound's own cutoff
            const double n = s.mk0;
            if (n <= 0.5) c->fxo.cutoff = 80.0 * std::pow(20000.0 / 80.0, n * 2.0);
        }
        out.push_back(std::move(c));
    }
    return out;
}
// Quick part, under the lock: moves what was loaded across and swaps the new channels in.
void Engine::Impl::adopt(std::vector<std::unique_ptr<Channel>> fresh) {
    Impl& E = *this;
    std::map<std::string, std::unique_ptr<Channel>> old;
    for (auto& c : E.chs) old[c->def.id] = std::move(c);
    E.chs.clear();
    for (auto& l : E.layers) l = Layer();
    for (auto& v : E.voices) { v.used = false; v.parts.clear(); }
    E.active.clear(); E.sustained.clear(); E.monoHeld.clear();
    for (auto& c : fresh) {
        auto it = old.find(c->def.id);
        if (it != old.end()) {   // keep what was loaded and how it was set
            Channel& o = *it->second;
            c->pcm = std::move(o.pcm); c->keys = std::move(o.keys); c->ready = o.ready;
            c->mute = o.mute; c->off = o.off; c->solo = o.solo; c->vol = o.vol; c->env = o.env;
            if (!c->def.hasModKnob) c->fxo = o.fxo;
        }
        E.chs.push_back(std::move(c));
    }
    E.applyMix();
    for (auto& c : E.chs) { c->outG.cur = c->outG.target; }
    retired.swap(old);   // freed later, off the lock
}
void Engine::defineSounds(const std::vector<SoundDef>& defs) {
    Impl& E = *d;
    { std::lock_guard<std::mutex> lk(E.dmtx); E.defs = defs; }
    auto fresh = E.makeChannels(defs);   // the memory is built here, outside the lock
    std::map<std::string, std::unique_ptr<Channel>> gone;
    {
        std::lock_guard<std::mutex> lk(E.mtx);
        E.adopt(std::move(fresh));
        gone.swap(E.retired);
    }
    { std::lock_guard<std::mutex> lk(E.rmtx); E.readyIds.clear(); for (auto& ch : E.chs) if (ch->ready) E.readyIds.insert(ch->def.id); }
}

bool Engine::prepareSamples(const std::string& id, std::map<int, Pcm>& samples) const {
    const SoundDef* def = nullptr;
    SoundDef D;
    { std::lock_guard<std::mutex> lk(d->dmtx); for (auto& x : d->defs) if (x.id == id) def = &x; if (!def) return false; D = *def; }
    if (D.loop.has) for (auto& kv : samples) makeLoop(kv.second, D.loop);
    return !samples.empty();
}
void Engine::installSamples(const std::string& id, std::map<int, Pcm>&& samples) {
    std::map<int, Pcm> old;
    {
        std::lock_guard<std::mutex> lk(d->mtx);
        Channel* c = d->find(id); if (!c) return;
        c->ready = false;
        for (auto& l : d->layers) if (l.used && l.ch == c) l.dead = true;   // voices still using the old audio stop
        d->gcLayers();
        old.swap(c->pcm);
        c->pcm = std::move(samples);
        c->keys.clear();
        for (auto& kv : c->pcm) c->keys.push_back(kv.first);
        std::sort(c->keys.begin(), c->keys.end());
        c->ready = !c->keys.empty();
    }
    { Channel* cc = nullptr; bool rd = false; { std::lock_guard<std::mutex> lk(d->mtx); cc = d->find(id); rd = cc && cc->ready; }
      std::lock_guard<std::mutex> lk(d->rmtx); if (rd) d->readyIds.insert(id); else d->readyIds.erase(id); }
}
void Engine::setSamples(const std::string& id, std::map<int, Pcm>&& samples) { prepareSamples(id, samples); installSamples(id, std::move(samples)); }
bool Engine::hasSamples(const std::string& id) const { std::lock_guard<std::mutex> lk(d->rmtx); return d->readyIds.count(id) > 0; }

void Engine::setChannel(const std::string& id, bool on, double vol, bool solo) {
    Cmd m = Impl::mk(Cmd::CH, id); m.i = on ? 1 : 0; m.j = solo ? 1 : 0; m.a = vol; d->push(m);
}
void Engine::setChannelEnv(const std::string& id, double a, double dd, double s, double r) {
    Cmd m = Impl::mk(Cmd::ENV, id); m.a = a; m.b = dd; m.c = s; m.d = r; d->push(m);
}
void Engine::setChannelFx(const std::string& id, double cutoff, double reso, double env, double glide) {
    Cmd m = Impl::mk(Cmd::FX, id); m.a = cutoff; m.b = reso; m.c = env; m.d = glide; d->push(m);
}
void Engine::setMaster(double gain, bool mute) { Cmd m = Impl::mk(Cmd::MASTER); m.a = gain; m.i = mute ? 1 : 0; d->push(m); }
void Engine::setGlobalFx(double cutoff, double reso, double env, double glide) {
    Cmd m = Impl::mk(Cmd::GFX); m.a = cutoff; m.b = reso; m.c = env; m.d = glide; d->push(m);
}

void Engine::Impl::applyMix() {
    bool anySolo = false;
    for (auto& c : chs) if (c->solo) anySolo = true;
    for (auto& c : chs) {
        if (c->off) { c->mute = true; c->solo = false; }
        if (!c->mute) c->cut = false;
        const bool audible = (!c->mute && (!anySolo || c->solo)) || (c->live > 0 && !c->cut);
        c->outG.target = audible ? c->vol * c->def.trim : 0.0;
    }
}

void Engine::Impl::updateFilters() {
    for (auto* list : {&active, &sustained}) for (Voice* v : *list) for (Layer* p : v->parts) {
        Channel& c = *p->ch;
        const double wm = c.def.mod == "cutoff" ? modVal : 0.0;
        const double fc = std::min(20000.0, std::min(fx.cutoff, c.fxo.cutoff) * std::pow(2.0, wm * 6));
        p->fTarget = fc; p->fMove = true; p->fCoefPerSec = 1.0 / 0.03;
        p->fQ = std::min(1.0, std::max(fx.reso, c.fxo.reso) + wm * 0.15) * 20.0;
    }
}

// MIDI ---------------------------------------------------------------------------------------
void Engine::noteOn(int note, int vel) { Cmd m = Impl::mk(Cmd::NOTEON); m.i = note; m.j = vel; d->push(m); }
void Engine::noteOff(int note) { Cmd m = Impl::mk(Cmd::NOTEOFF); m.i = note; d->push(m); }
void Engine::controller(int cc, int val) { Cmd m = Impl::mk(Cmd::CC); m.i = cc; m.j = val; d->push(m); }
void Engine::pitchBend(int v14) { Cmd m = Impl::mk(Cmd::BEND); m.i = v14; d->push(m); }
void Engine::allNotesOff() { d->push(Impl::mk(Cmd::ALLOFF)); }
void Engine::noteOnRT(int note, int vel) { Cmd m = Impl::mk(Cmd::NOTEON); m.i = note; m.j = vel; d->pushRT(m); }
void Engine::noteOffRT(int note) { Cmd m = Impl::mk(Cmd::NOTEOFF); m.i = note; d->pushRT(m); }
void Engine::controllerRT(int cc, int val) { Cmd m = Impl::mk(Cmd::CC); m.i = cc; m.j = val; d->pushRT(m); }
void Engine::pitchBendRT(int v14) { Cmd m = Impl::mk(Cmd::BEND); m.i = v14; d->pushRT(m); }
void Engine::allNotesOffRT() { d->pushRT(Impl::mk(Cmd::ALLOFF)); }

// Runs on the audio thread only.
void Engine::Impl::apply(const Cmd& m) {
    Impl& E = *this;
    switch (m.t) {
        case Cmd::CH: { Channel* c = E.find(m.id); if (!c) break; c->off = !m.i; c->mute = !m.i; c->solo = m.j != 0; c->vol = m.a; E.applyMix(); break; }
        case Cmd::ENV: { Channel* c = E.find(m.id); if (!c) break; c->env.a = m.a; c->env.d = m.b; c->env.s = m.c; c->env.r = m.d; break; }
        case Cmd::FX: {
            Channel* c = E.find(m.id); if (!c) break;
            if (!c->def.hasModKnob) c->fxo.cutoff = m.a;   // a sound with a mod-wheel cutoff follows the wheel here
            c->fxo.reso = m.b; c->fxo.env = m.c; c->fxo.glide = m.d;
            E.updateFilters(); break;
        }
        case Cmd::MASTER: E.masterWant = m.a; E.muteWant = m.i ? 0.0 : 1.0; E.masterG.target = m.a; E.outMute.target = E.muteWant; break;
        case Cmd::GFX: E.fx.cutoff = m.a; E.fx.reso = m.b; E.fx.env = m.c; E.fx.glide = m.d; E.updateFilters(); break;
        case Cmd::NOTEON: E.doNoteOn(m.i, m.i, m.j, false); break;
        case Cmd::NOTEOFF: E.doNoteOff(m.i); break;
        case Cmd::ALLOFF: E.allOff(); break;
        case Cmd::BEND: E.bendS.target = ((double) m.i / 8192.0 - 1.0) * 200.0; break;
        case Cmd::CC: {
            const int cc = m.i, val = m.j;
            if (cc == 1) { E.modVal = val / 127.0; E.modS.target = E.modVal; E.vibModS.target = E.modVal;
                for (auto& c : E.chs) if (c->def.hasModKnob) {   // the wheel drives this sound's own cutoff knob
                    const double n = c->def.mk0 + (c->def.mk1 - c->def.mk0) * E.modVal;
                    c->fxo.cutoff = n <= 0.5 ? 80.0 * std::pow(20000.0 / 80.0, n * 2.0) : 20000.0;
                }
                E.updateFilters();
            }
            else if (cc == 64) { E.midiPedal = val >= 64; E.syncPedal(); }
            else if (cc == 123 || cc == 120) E.allOff();
            else if (cc == 121) { E.bendS.target = 0; E.modVal = 0; E.modS.target = 0; E.midiPedal = false; E.syncPedal(); }
            break;
        }
    }
}
void Engine::Impl::allOff() {
    scratchA.assign(active.begin(), active.end());
    for (Voice* v : scratchA) doNoteOff(v->src);
    monoHeld.clear();
}

void Engine::Impl::doNoteOn(int src, int note, int vel, bool monoOnly) {
    if (!monoOnly && monoActive()) {
        monoHeld.erase(std::remove_if(monoHeld.begin(), monoHeld.end(), [&](const Held& h) { return h.src == src; }), monoHeld.end());
        monoHeld.push_back({src, note, vel});
    }
    const int glideFrom = lastGlideNote; lastGlideNote = note;
    for (Voice* v : active) if (v->src == src && !v->up) { doNoteOff(src); break; }

    // mono sounds: the new note takes over from the one still sounding (legato)
    for (auto& mcp : chs) {
        Channel& mc = *mcp; mc.legatoNow = false;
        if (mc.def.monoVoice && mc.ready && !mc.off && !mc.mute) {
            for (auto* list : {&active, &sustained}) for (Voice* o : *list) for (Layer* p : o->parts) if (p->ch == &mc && !p->dead && p->ph != 2) {
                mc.legatoNow = true;
                startRelease(p, mc.def.legato ? mc.def.legOut : 0.022);
            }
        }
    }
    const Shape shape = velShape(note, vel);
    Voice* v = allocVoice();
    if (!v) return;
    v->src = src; v->note = note;
    bool anySolo = false; for (auto& c : chs) if (c->solo) anySolo = true;

    for (auto& chp : chs) {
        Channel& ch = *chp;
        if (!ch.ready || ch.off || ch.mute || (anySolo && !ch.solo) || (monoOnly && !ch.def.monoVoice)) continue;
        const SoundDef& D = ch.def;
        const int nt = note + (int) D.shift;
        const int k = nearestKey(ch.keys, nt);
        const Pcm& pcm = ch.pcm.find(k)->second;
        int lay[3], nlay = 0; // -1, 1 | 0 | 2 = sub
        if (D.fat) { lay[nlay++] = -1; lay[nlay++] = 1; } else lay[nlay++] = 0;
        const double leDepth = D.lowEnd ? std::min(1.0, std::max(0.0, (D.leBelow - nt) / 12.0)) : 0.0;
        const double subFade = std::min(1.0, std::max(0.0, (nt - 30) / 6.0));
        if (leDepth > 0.02 && D.lowEnd && D.leSub > 0 && subFade > 0.02) lay[nlay++] = 2;
        for (int li = 0; li < nlay; ++li) {
            const int dOff = lay[li];
            const bool isSub = dOff == 2;
            Layer* L = allocLayer();
            if (!L) break;
            Hv hv; const bool human = D.human;
            if (human) hv = humanize(vel, shape, ch);
            L->ch = &ch; L->voice = v; L->pcm = &pcm; L->isSub = isSub;
            L->frames = pcm.frames();
            L->srRatio = pcm.sr / sr;
            L->ratio = std::pow(2.0, (nt - k) / 12.0) * (isSub ? 0.5 : 1.0);
            const double spread = human ? hv.spread : 8.0;
            double cents = (isSub ? 0 : dOff) * spread + (human ? hv.detune : 0.0) + ((D.analog || D.drift) ? rnd() * 3.0 : 0.0);
            L->staticCents = cents;
            const bool lg = D.monoVoice && ch.legatoNow;
            if (D.slide > 0 && D.monoVoice && lg && glideFrom > -999 && glideFrom != note) { L->slideC = (glideFrom - note) * 100.0; L->slideDur = D.slide; }
            { const double gs = glideSecs(std::max({fx.glide, ch.fxo.glide, D.glide})); if (gs > 0 && glideFrom > -999 && glideFrom != note) { L->glideC = (glideFrom - note) * 100.0; L->glideDur = gs; } }
            L->loop = pcm.looped; L->loopS = pcm.loopS * pcm.sr; L->loopE = pcm.loopE * pcm.sr;
            // start position
            double startSec = 0;
            if (lg && pcm.looped) startSec = pcm.loopS * 0.4;
            else if (D.fatOffset && dOff > 0 && pcm.looped) startSec = pcm.loopS + 0.15 + rnd01() * std::max(0.1, pcm.loopE - pcm.loopS - 0.4);
            L->pos = startSec * pcm.sr;
            // amp envelope
            const auto& e = ch.env;
            const double at = lg ? std::max(D.legato ? D.legIn : 0.03, e.a) : std::max({human ? hv.at : 0.002, e.a, D.atk});
            const double ampBase = human ? hv.amp : shape.amp;
            const double ampc = (D.tame ? std::min(ampBase, 1.0) : ampBase) * (D.fat ? 0.72 : 1.0);
            const double hiG = D.hiTrim > 0 ? 1 - (1 - D.hiTrim) * std::min(1.0, std::max(0.0, (note - 58) / 6.0)) : 1.0;
            const double sub0 = isSub ? D.leSub * leDepth * subFade : (leDepth > 0 ? 1 - D.leDuck * leDepth : 1.0);
            const double A = ampc * sub0 * hiG;
            const double biteHi = D.bite > 0 && D.atkMid ? std::max(0.15, std::min(1.0, 1 - (note - 48) / 16.0)) : 1.0;
            const double biteDb = D.bite * biteHi + (D.atkMid && e.a < 0 ? -e.a * 8 * std::max(0.35, biteHi) : 0.0);
            L->ph = 0; L->g = 0;
            if (biteDb != 0 && e.s >= 0.999 && !lg && !(D.atkMid && e.a > 0.004)) {
                const double ta = std::min(at, 0.004);
                L->atkTo = A * std::pow(10.0, biteDb / 20.0); L->atkLeft = std::max(1, (int) std::lround(ta * sr)); L->atkStep = L->atkTo / L->atkLeft;
                L->settle = true; L->setTarget = A; L->setCoef = 1.0 - std::exp(-1.0 / (0.11 * sr));
            } else {
                L->atkTo = A; L->atkLeft = std::max(1, (int) std::lround(at * sr)); L->atkStep = A / L->atkLeft;
                if (e.s < 0.999) { L->settle = true; L->setTarget = A * e.s; L->setCoef = 1.0 - std::exp(-1.0 / ((std::max(0.01, e.d) / 3.0) * sr)); }
            }
            // filters
            const double wm = D.mod == "cutoff" ? modVal : 0.0;
            const double fc = std::min(20000.0, std::min(fx.cutoff, ch.fxo.cutoff) * std::pow(2.0, wm * 6));
            const double fr = std::min(1.0, std::max(fx.reso, ch.fxo.reso) + wm * 0.15);
            const double fe = std::max({fx.env, ch.fxo.env, wm});
            L->fQ = fr * 20.0;
            const double peak = std::min(20000.0, fc * std::pow(2.0, fe * 5));
            L->fTarget = fc; L->fCur = fc;
            if (fe > 0.01 && peak > fc * 1.01) { L->fCur = peak; L->fMove = true; L->fCoefPerSec = 1.0 / (std::max(0.02, ch.env.d) / 3.0); }
            L->f.set(BQ_LP, sr, L->fCur, L->fQ, 0);
            const double vcut = human ? hv.cut : shape.cut;
            L->vf.set(BQ_LP, sr, vcut, 0, 0);
            if (human && hv.swell < 1) { L->vfRamp = true; L->vfFrom = hv.cut * hv.swell; L->vfTo = hv.cut; L->vfDur = std::max(0.05, at * 1.6); L->vfT = 0; L->vf.set(BQ_LP, sr, L->vfFrom, 0, 0); }
            const double smBase = human ? hv.smack : shape.smack;
            L->smBase = smBase; L->smBoost = biteDb != 0 ? biteDb * 0.9 : 0.0; L->smDecay = L->smBoost != 0;
            L->sm.set(BQ_PEAK, sr, 3500, 0.9, smBase + L->smBoost);
            if (D.lowEnd && leDepth > 0.02 && D.leShelf != 0 && D.octCut <= 0) {
                const double cl = D.leClean ? (nt >= D.leC0 ? std::min(1.0, std::max(0.0, (D.leC1 + 3 - nt) / 3.0)) : 0.0) : 0.0;
                L->hasLs = true; L->ls.set(BQ_LSHELF, sr, 140, 0, D.leShelf * leDepth * (1 - cl));
                if (cl > 0.02) { L->hasMid = true; L->m1.set(BQ_PEAK, sr, 230, 0.9, -6 * cl); L->m2.set(BQ_PEAK, sr, 420, 1.1, -3 * cl); }
            }
            if (D.fat) { L->fat = true; L->pan = (dOff < 0 ? -1 : 1) * (D.fatPan > 0 ? D.fatPan : 0.45); const double p = L->pan; const double dp = p <= 0 ? p + 1 : p; L->panGl = std::cos(dp * PI / 2); L->panGr = std::sin(dp * PI / 2); }
            L->driftPhaseOff = 0;
            ch.live++;
            if (D.poly) { ch.alive++; ch.held++; L->polyCounted = true; polyUpdate(ch); }
            v->parts.push_back(L);
        }
    }
    active.push_back(v);
    // poly sound caps (held + ringing tails)
    scratchCh.clear();
    for (Layer* p : v->parts) if (p->ch->def.poly && std::find(scratchCh.begin(), scratchCh.end(), p->ch) == scratchCh.end()) scratchCh.push_back(p->ch);
    for (size_t pi = 0; pi < scratchCh.size(); ++pi) {
        Channel* ch = scratchCh[pi];
        ch->vox.push_back(v);
        if (ch->vox.size() > 80) ch->vox.erase(ch->vox.begin(), ch->vox.begin() + (ch->vox.size() - 80));
        while (ch->alive > 110 && ch->vox.size() > 1) {
            Voice* o = ch->vox.front(); ch->vox.erase(ch->vox.begin());
            if (o == v) { ch->vox.push_back(o); break; }
            if (std::find(active.begin(), active.end(), o) != active.end()) { o->up = true; forget(o); }
            sustained.erase(std::remove(sustained.begin(), sustained.end(), o), sustained.end());
            unhold(o);
            for (Layer* p : o->parts) if (p->ch == ch && !p->dead) startRelease(p, 0.04);
        }
    }
    // polyphony: past 48 notes / 200 layers the oldest is let go
    while ((active.size() + sustained.size() > 48 || layersLive() > 200) && active.size() + sustained.size() > 1) {
        Voice* old = !sustained.empty() ? sustained.front() : active.front();
        if (!old || old == v) break;
        if (std::find(sustained.begin(), sustained.end(), old) != sustained.end()) releaseVoice(old);
        else { old->up = true; forget(old); releaseVoice(old); }
    }
}

void Engine::Impl::doNoteOff(int src) {
    {   // a mono helper voice for this key
        for (Voice* v : active) if (v->src == src + 1000) { doNoteOff(src + 1000); break; }
    }
    auto mi = std::find_if(monoHeld.begin(), monoHeld.end(), [&](const Held& h) { return h.src == src; });
    if (mi != monoHeld.end()) {
        const bool wasTop = (mi + 1) == monoHeld.end();
        monoHeld.erase(mi);
        if (wasTop && !monoHeld.empty() && monoActive()) { const Held pv = monoHeld.back(); doNoteOn(pv.src + 1000, pv.note, pv.vel, true); }
    }
    Voice* v = nullptr;
    for (Voice* x : active) if (x->src == src) { v = x; break; }
    if (!v || v->up) return;
    v->up = true;
    forget(v);
    if (pedalOn()) sustained.push_back(v); else releaseVoice(v);
}

void Engine::Impl::gcLayers() {
    for (auto& l : layers) if (l.used && l.dead && !l.acct) {
        l.acct = true;
        Channel& c = *l.ch;
        c.live = std::max(0, c.live - 1);
        if (c.def.poly) { c.alive = std::max(0, c.alive - 1); polyUpdate(c); }
    }
    for (auto& v : voices) if (v.used) {
        if (std::find(active.begin(), active.end(), &v) != active.end()) continue;
        if (std::find(sustained.begin(), sustained.end(), &v) != sustained.end()) continue;
        bool all = true;
        for (Layer* p : v.parts) if (!p->dead) { all = false; break; }
        if (!all) continue;
        for (Layer* p : v.parts) p->used = false;
        for (auto& c : chs) c->vox.erase(std::remove(c->vox.begin(), c->vox.end(), &v), c->vox.end());
        v.used = false; v.parts.clear();
    }
}

void Engine::testChain(float* L, float* R, int n) {
    Impl& E = *d;
    for (int i = 0; i < n; i++) { L[i] *= 0.3f; R[i] *= 0.3f; }
    E.comp.process(L, R, n); E.lim.process(L, R, n);
    const float k = 0.9f;
    for (int i = 0; i < n; i++) {
        auto guard = [&](float x) { const float a = std::fabs(x); const float y = a <= k ? a : k + (1 - k) * std::tanh((a - k) / (1 - k)); return x < 0 ? -y : y; };
        L[i] = guard(L[i] * (float) E.limFix); R[i] = guard(R[i] * (float) E.limFix);
    }
}
void Engine::setTestMode(bool on) { d->testMode = on; }
int Engine::activeLayers() const { int n = 0; for (auto& l : d->layers) if (l.used && !l.dead) n++; return n; }

// ---------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------
static inline float hermite(const float* a, int n, double p) {
    const int i = (int) p; const float f = (float) (p - i);
    auto at = [&](int j) { return a[j < 0 ? 0 : (j >= n ? n - 1 : j)]; };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

void Engine::render(float* outL, float* outR, int n) {
    Impl& E = *d;
    // The audio thread never waits: if a rare structural change (loading a sound) holds the lock, wait a few
    // microseconds, and only then give up on this one block.
    std::unique_lock<std::mutex> lk(E.mtx, std::try_to_lock);
    for (int spin = 0; !lk.owns_lock() && spin < 4000; ++spin) (void) lk.try_lock();
    if (!lk.owns_lock()) { std::fill(outL, outL + n, 0.f); std::fill(outR, outR + n, 0.f); return; }
    E.drain();
    const double sr = E.sr;
    for (int s0 = 0; s0 < n; s0 += Impl::BLK) {
        const int m = std::min(Impl::BLK, n - s0);
        const double dt = (double) m / sr;
        // control-rate shared values
        auto stepN = [&](Smooth& s) { s.cur += (s.target - s.cur) * (1.0 - std::pow(1.0 - s.coef, (double) m)); };
        stepN(E.bendS); stepN(E.modS); stepN(E.vibModS);
        E.lfoPh += 2 * PI * 5.4 * dt; if (E.lfoPh > 2 * PI) E.lfoPh -= 2 * PI;
        E.driftPh += 2 * PI * 0.27 * dt; if (E.driftPh > 2 * PI) E.driftPh -= 2 * PI;
        const double lfo = std::sin(E.lfoPh), driftV = std::sin(E.driftPh) * 1.5;
        for (auto& c : E.chs) {
            stepN(c->outG); stepN(c->polyG); stepN(c->vibGain);
            c->vibPhase += 2 * PI * (c->def.vibRate > 0 ? c->def.vibRate : 0) * dt; if (c->vibPhase > 2 * PI) c->vibPhase -= 2 * PI;
            std::fill(c->bl.begin(), c->bl.begin() + m, 0.f); std::fill(c->br.begin(), c->br.begin() + m, 0.f);
            c->vibGain.target = E.modVal * (c->def.vib > 0 ? c->def.vib : 45);
        }
        // layers
        for (auto& Lr : E.layers) {
            if (!Lr.used || Lr.dead) continue;
            Layer& L = Lr; Channel& C = *L.ch; const SoundDef& D = C.def;
            // pitch
            double cents = L.staticCents + E.bendS.cur;
            if (D.mod == "vibrato") {
                if (D.vibRate > 0) cents += std::sin(C.vibPhase) * C.vibGain.cur;
                else if (D.vib > 0) cents += lfo * E.modVal * D.vib;
                else cents += lfo * E.modVal * 45.0;
            }
            if (D.analog || D.drift) cents += driftV;
            if (L.glideDur > 0 && L.glideT < L.glideDur) { cents += L.glideC * (1.0 - L.glideT / L.glideDur); L.glideT += dt; }
            if (L.slideDur > 0 && L.slideT < L.slideDur) { cents += L.slideC * (1.0 - L.slideT / L.slideDur); L.slideT += dt; }
            const double inc = L.ratio * L.srRatio * std::pow(2.0, cents / 1200.0);
            // filter updates
            if (L.fMove) {
                const double a = 1.0 - std::exp(-L.fCoefPerSec * dt);
                L.fCur += (L.fTarget - L.fCur) * a;
                if (std::fabs(L.fCur - L.fTarget) < L.fTarget * 0.002) { L.fCur = L.fTarget; L.fMove = L.fCoefPerSec > 0 && false; }
                L.f.set(BQ_LP, sr, L.fCur, L.fQ, 0);
            }
            if (L.vfRamp) {
                L.vfT += dt; const double k = std::min(1.0, L.vfT / L.vfDur);
                L.vf.set(BQ_LP, sr, L.vfFrom + (L.vfTo - L.vfFrom) * k, 0, 0);
                if (k >= 1.0) L.vfRamp = false;
            }
            if (L.smDecay) {
                L.age += dt;
                const double t = std::max(0.0, L.age - 0.004);
                const double gdb = L.smBase + L.smBoost * std::exp(-t / 0.09);
                L.sm.set(BQ_PEAK, sr, 3500, 0.9, gdb);
                if (t > 0.6) { L.smDecay = false; L.sm.set(BQ_PEAK, sr, 3500, 0.9, L.smBase); }
            }
            float* bl = C.bl.data(); float* br = C.br.data();
            const float* sl = L.pcm->l.data(); const float* sR = L.pcm->r.data();
            for (int i = 0; i < m; i++) {
                if (L.pos >= L.frames - 1 && !L.loop) { L.dead = true; break; }
                float xl = hermite(sl, L.frames, L.pos), xr = hermite(sR, L.frames, L.pos);
                L.pos += inc;
                if (L.loop && L.pos >= L.loopE) L.pos -= (L.loopE - L.loopS);
                // envelope
                if (L.ph == 0) { L.g += L.atkStep; if (--L.atkLeft <= 0) { L.g = L.atkTo; L.ph = 1; } }
                else if (L.ph == 1) { if (L.settle) L.g += (L.setTarget - L.g) * L.setCoef; }
                else { L.g -= L.g * L.relCoef; if (L.g < 1e-6) { L.dead = true; break; } }
                const float g = (float) L.g;
                xl *= g; xr *= g;
                xl = L.vf.run(0, xl); xr = L.vf.run(1, xr);
                if (!L.sm.bypass) { xl = L.sm.run(0, xl); xr = L.sm.run(1, xr); }
                if (L.hasLs && !L.ls.bypass) { xl = L.ls.run(0, xl); xr = L.ls.run(1, xr); }
                if (L.hasMid) { if (!L.m1.bypass) { xl = L.m1.run(0, xl); xr = L.m1.run(1, xr); } if (!L.m2.bypass) { xl = L.m2.run(0, xl); xr = L.m2.run(1, xr); } }
                xl = L.f.run(0, xl); xr = L.f.run(1, xr);
                if (L.fat) {
                    if (L.pan <= 0) { const float ol = xl + xr * (float) L.panGl, or_ = xr * (float) L.panGr; xl = ol; xr = or_; }
                    else { const float ol = xl * (float) L.panGl, or_ = xr + xl * (float) L.panGr; xl = ol; xr = or_; }
                }
                bl[i] += xl; br[i] += xr;
            }
        }
        // channel chains -> master sum
        std::fill(E.mL.begin(), E.mL.begin() + m, 0.f); std::fill(E.mR.begin(), E.mR.begin() + m, 0.f);
        for (auto& cp : E.chs) {
            Channel& C = *cp; const SoundDef& D = C.def;
            float* bl = C.bl.data(); float* br = C.br.data();
            for (int i = 0; i < m; i++) {
                float xl = bl[i], xr = br[i];
                const float pg = D.poly ? (float) C.polyG.cur : 1.f, og = (float) C.outG.cur;
                xl *= pg * og; xr *= pg * og;
                for (auto& t : C.tone) if (!t.bypass) { xl = t.run(0, xl); xr = t.run(1, xr); }
                if (!C.chHP.bypass && false) {}
                if (D.wide > 0) {
                    const float M = 0.5f * (xl + xr), S = 0.5f * (float) D.wide * (xl - xr);
                    xl = M + S; xr = M - S;
                }
                if (D.ensemble) {
                    float wl = 0, wr = 0;
                    for (auto& e : C.ens) {
                        e.l[e.w] = xl; e.r[e.w] = xr;
                        const int N = (int) e.l.size();
                        e.ph += 2 * PI * e.rate / sr; if (e.ph > 2 * PI) e.ph -= 2 * PI;
                        const double del = (e.t + e.dep * std::sin(e.ph)) * sr;
                        double rp = e.w - del; while (rp < 0) rp += N;
                        const int i0 = (int) rp; const double fr = rp - i0; const int i1 = (i0 + 1) % N;
                        const float dl = (float) (e.l[i0] * (1 - fr) + e.l[i1] * fr), dr = (float) (e.r[i0] * (1 - fr) + e.r[i1] * fr);
                        const double dp = e.pan <= 0 ? e.pan + 1 : e.pan; const float gl = (float) std::cos(dp * PI / 2), gr = (float) std::sin(dp * PI / 2);
                        if (e.pan <= 0) { wl += dl + dr * gl; wr += dr * gr; } else { wl += dl * gl; wr += dr + dl * gr; }
                        if (++e.w >= N) e.w = 0;
                    }
                    E.mL[i] += xl + 0.45f * wl; E.mR[i] += xr + 0.45f * wr;
                } else { E.mL[i] += xl; E.mR[i] += xr; }
            }
        }
        // master chain
        for (int i = 0; i < m; i++) {
            E.masterG.cur += (E.masterG.target - E.masterG.cur) * E.masterG.coef;
            const float mg = (float) E.masterG.cur;
            float xl = E.mL[i] * mg, xr = E.mR[i] * mg;
            xl = E.hp.run(0, xl); xr = E.hp.run(1, xr);
            E.mL[i] = xl * 0.3f; E.mR[i] = xr * 0.3f;
        }
        E.comp.process(E.mL.data(), E.mR.data(), m);
        E.lim.process(E.mL.data(), E.mR.data(), m);
        for (int i = 0; i < m; i++) {
            E.outMute.cur += (E.outMute.target - E.outMute.cur) * E.outMute.coef;
            const float k = 0.9f;
            auto guard = [&](float x) { const float a = std::fabs(x); const float y = a <= k ? a : k + (1 - k) * std::tanh((a - k) / (1 - k)); return x < 0 ? -y : y; };
            outL[s0 + i] = guard(E.mL[i] * (float) E.limFix) * (float) E.outMute.cur;
            outR[s0 + i] = guard(E.mR[i] * (float) E.limFix) * (float) E.outMute.cur;
        }
        E.gcLayers();
    }
}

}  // namespace psb
