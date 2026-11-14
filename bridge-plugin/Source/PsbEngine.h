// Purple SoundBank native sound engine (plain C++, no framework needed).
// A port of the web instrument's audio engine, so the plugin makes the sound itself,
// sample-accurate inside the DAW's own audio callback (no browser audio in the path).
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace psb {

enum BqType { BQ_LP, BQ_HP, BQ_PEAK, BQ_LSHELF, BQ_HSHELF };

struct ToneBand { int type = BQ_PEAK; double f = 1000, g = 0, q = 0.7; };

struct LoopCfg { bool has = false; double start = 0, end = 0, xfade = 0, align = 0, endFrac = 0; bool flat = false, lin = false; };

// One decoded recording (stereo, floats). sr is the file's own rate.
struct Pcm {
    std::vector<float> l, r;
    double sr = 44100;
    bool looped = false;
    double loopS = 0, loopE = 0;   // seconds
    int frames() const { return (int) l.size(); }
};

struct SoundDef {
    std::string id;
    double trim = 1, rel = 0.09;
    std::string mod = "vibrato";   // vibrato | none | knob | cutoff | lp | sweep
    std::vector<ToneBand> tone;
    bool human = false, fat = false, drift = false, analog = false, ensemble = false, poly = false, tame = false;
    bool velDeep = false, monoVoice = false, alone = false, atkMid = false, holdFix = false, fatOffset = false;
    double fatSpread = 0, fatPan = 0.45, wide = 0, bite = 0, vib = 0, vibRate = 0, shift = 0, glide = 0;
    double relPow = 0, hiTrim = 0, octCut = 0, atk = 0, slide = 0, room = 0;
    bool lowEnd = false; double leBelow = 0, leShelf = 0, leSub = 0, leDuck = 0;
    bool leClean = false; double leC0 = 0, leC1 = 0;
    bool legato = false; double legIn = 0.03, legOut = 0.022;
    bool hasModKnob = false; double mk0 = 0, mk1 = 0;
    LoopCfg loop;
};

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {0, 0}, z2[2] = {0, 0};
    bool bypass = false;
    void set(int type, double sr, double f, double qOrRes, double gainDb);
    inline float run(int c, float x) {
        const double y = b0 * x + z1[c];
        z1[c] = b1 * x - a1 * y + z2[c];
        z2[c] = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

struct Smooth {   // exponential approach, like setTargetAtTime
    double cur = 0, target = 0, coef = 1;
    void init(double v, double tauSec, double sr) { cur = target = v; coef = 1.0 - std::exp(-1.0 / (tauSec * sr)); }
    void setTau(double tauSec, double sr) { coef = 1.0 - std::exp(-1.0 / (tauSec * sr)); }
    inline double step() { cur += (target - cur) * coef; return cur; }
};

// A compressor / limiter with the same static curve and make-up gain as the browser's one,
// with a short look-ahead so attacks are caught before they arrive.
struct Comp {
    double thrDb = -10, kneeDb = 12, ratio = 4, att = 0.003, rel = 0.2;
    double linThr = 0, kneeThr = 0, kneeThrDb = 0, ymaxDb = 0, slope = 0.25, k = 5, makeup = 1;
    std::vector<float> dl[2];
    int dlen = 0, w = 0;
    double g = 1, attC = 0, relC = 0, sr = 48000;
    void prepare(double sampleRate, double thr, double knee, double rat, double a, double r);
    double kneeCurve(double x, double kk) const;
    double saturate(double x, double kk) const;
    double slopeAt(double x, double kk) const;
    void process(float* L, float* R, int n);
    int latency() const { return dlen; }
    void reset();
};

struct Channel;
struct Layer;
struct Voice;

class Engine {
public:
    Engine();
    ~Engine();
    void prepare(double sampleRate);
    int latencySamples() const;
    void defineSounds(const std::vector<SoundDef>& defs);
    // decoded recordings for one sound: note number -> audio. The loops are built here.
    // Heavy part (loop building) can run on any thread; installing is quick.
    bool prepareSamples(const std::string& id, std::map<int, Pcm>& samples) const;
    void installSamples(const std::string& id, std::map<int, Pcm>&& samples);
    void setSamples(const std::string& id, std::map<int, Pcm>&& samples);
    bool hasSamples(const std::string& id) const;

    // state from the page (the instrument's own controls)
    void setChannel(const std::string& id, bool on, double vol, bool solo);
    void setChannelEnv(const std::string& id, double a, double d, double s, double r);
    void setChannelFx(const std::string& id, double cutoff, double reso, double env, double glide);
    void setMaster(double gain, bool mute);
    void setGlobalFx(double cutoff, double reso, double env, double glide);

    // MIDI
    void noteOn(int note, int vel);
    void noteOff(int note);
    void controller(int cc, int val);
    void pitchBend(int v14);
    void allNotesOff();

    void render(float* outL, float* outR, int n);   // call between events
    int activeLayers() const;
    void setTestMode(bool on);
    void testChain(float* L, float* R, int n);   // master compressor/limiter only (for comparing with the web version)   // fixed random numbers, for comparing against the web version

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace psb
