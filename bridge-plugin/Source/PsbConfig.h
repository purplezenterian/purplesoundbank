// Turns the page's messages (JSON) into engine settings.
#pragma once
#include <juce_core/juce_core.h>
#include "PsbEngine.h"

namespace psb
{
    inline double numOf (const juce::var& v, double d = 0.0) { return v.isVoid() ? d : (double) v; }
    inline bool boolOf (const juce::var& v) { return v.isVoid() ? false : (bool) v; }
    inline int toneType (const juce::String& t)
    {
        if (t == "lowshelf") return BQ_LSHELF;
        if (t == "highshelf") return BQ_HSHELF;
        if (t == "peaking") return BQ_PEAK;
        if (t == "highpass") return BQ_HP;
        return BQ_LP;
    }

    inline std::vector<SoundDef> parseSounds (const juce::var& sounds)
    {
        std::vector<SoundDef> defs;
        if (auto* arr = sounds.getArray())
            for (const auto& s : *arr)
            {
                SoundDef d;
                d.id = s["id"].toString().toStdString();
                if (d.id.empty()) continue;
                d.trim = numOf (s["trim"], 1.0);
                d.rel = numOf (s["rel"], 0.09);
                d.mod = s.hasProperty ("mod") ? s["mod"].toString().toStdString() : std::string ("vibrato");
                if (auto* tn = s["tone"].getArray())
                    for (const auto& t : *tn)
                    {
                        ToneBand b; b.type = toneType (t["type"].toString()); b.f = numOf (t["f"], 1000); b.g = numOf (t["g"]); b.q = numOf (t["q"], 0.7);
                        d.tone.push_back (b);
                    }
                d.human = boolOf (s["human"]); d.fat = boolOf (s["fat"]); d.drift = boolOf (s["drift"]); d.analog = boolOf (s["analog"]);
                d.ensemble = boolOf (s["ensemble"]); d.poly = boolOf (s["poly"]); d.tame = boolOf (s["tame"]); d.velDeep = boolOf (s["velDeep"]);
                d.monoVoice = boolOf (s["monoVoice"]); d.alone = boolOf (s["alone"]); d.atkMid = boolOf (s["atkMid"]);
                d.holdFix = boolOf (s["holdFix"]); d.fatOffset = boolOf (s["fatOffset"]);
                d.fatSpread = numOf (s["fatSpread"]); d.fatPan = numOf (s["fatPan"], 0.45); d.wide = numOf (s["wide"]);
                d.bite = numOf (s["bite"]); d.vib = numOf (s["vib"]); d.vibRate = numOf (s["vibRate"]); d.shift = numOf (s["shift"]);
                d.glide = numOf (s["glide"]); d.relPow = numOf (s["relPow"]); d.hiTrim = numOf (s["hiTrim"]); d.octCut = numOf (s["octCut"]);
                d.atk = numOf (s["atk"]); d.slide = numOf (s["slide"]); d.room = numOf (s["room"]);
                if (auto* lp = s["loop"].getDynamicObject())
                {
                    d.loop.has = true; d.loop.start = numOf (lp->getProperty ("start")); d.loop.end = numOf (lp->getProperty ("end"));
                    d.loop.xfade = numOf (lp->getProperty ("xfade")); d.loop.align = numOf (lp->getProperty ("align"));
                    d.loop.endFrac = numOf (lp->getProperty ("endFrac")); d.loop.flat = boolOf (lp->getProperty ("flat")); d.loop.lin = boolOf (lp->getProperty ("lin"));
                }
                if (auto* le = s["lowEnd"].getDynamicObject())
                {
                    d.lowEnd = true; d.leBelow = numOf (le->getProperty ("below")); d.leShelf = numOf (le->getProperty ("shelf"));
                    d.leSub = numOf (le->getProperty ("sub")); d.leDuck = numOf (le->getProperty ("duck"));
                    if (auto* cl = le->getProperty ("clean").getArray()) if (cl->size() >= 2) { d.leClean = true; d.leC0 = (double) (*cl)[0]; d.leC1 = (double) (*cl)[1]; }
                }
                if (auto* lg = s["legato"].getDynamicObject()) { d.legato = true; d.legIn = numOf (lg->getProperty ("in"), 0.03); d.legOut = numOf (lg->getProperty ("out"), 0.022); }
                if (auto* mk = s["modKnob"].getArray()) if (mk->size() >= 2) { d.hasModKnob = true; d.mk0 = (double) (*mk)[0]; d.mk1 = (double) (*mk)[1]; }
                defs.push_back (std::move (d));
            }
        return defs;
    }

    inline void applyState (Engine& engine, juce::DynamicObject* o)
    {
        engine.setMaster (numOf (o->getProperty ("master"), 0.8), boolOf (o->getProperty ("mute")));
        if (auto* fx = o->getProperty ("fx").getDynamicObject())
            engine.setGlobalFx (numOf (fx->getProperty ("cutoff"), 20000), numOf (fx->getProperty ("reso")), numOf (fx->getProperty ("env")), numOf (fx->getProperty ("glide")));
        if (auto* arr = o->getProperty ("ch").getArray())
            for (const auto& c : *arr)
            {
                const std::string id = c["id"].toString().toStdString();
                engine.setChannel (id, boolOf (c["on"]), numOf (c["vol"], 1.0), boolOf (c["solo"]));
                if (auto* e = c["env"].getDynamicObject())
                    engine.setChannelEnv (id, numOf (e->getProperty ("a")), numOf (e->getProperty ("d"), 0.5), numOf (e->getProperty ("s"), 1.0), numOf (e->getProperty ("r"), 0.4));
                if (auto* f = c["fxo"].getDynamicObject())
                    engine.setChannelFx (id, numOf (f->getProperty ("cutoff"), 20000), numOf (f->getProperty ("reso")), numOf (f->getProperty ("env")), numOf (f->getProperty ("glide")));
            }
    }
}
