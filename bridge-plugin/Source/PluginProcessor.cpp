#include "PluginProcessor.h"
#include "PluginEditor.h"
#if PSB_EMBED
#include "SoundLoader.h"
#include "PsbConfig.h"
#endif

PSBBridgeProcessor::PSBBridgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      ringL ((size_t) ringSize, 0.0f), ringR ((size_t) ringSize, 0.0f),
      server (*this, 48210, 48219)
{
#if PSB_EMBED
    engine.prepare (48000.0);
#else
    server.start();
#endif
}

PSBBridgeProcessor::~PSBBridgeProcessor()
{
#if PSB_EMBED
    loaderPool.removeAllJobs (true, 8000);
#else
    server.stop();
#endif
}

void PSBBridgeProcessor::prepareToPlay (double sampleRate, int)
{
#if PSB_EMBED
    engine.prepare (sampleRate);
    setLatencySamples (engine.latencySamples());
    hostRate = sampleRate;
    return;
#endif
    hostRate = sampleRate;
    flushRequest.store (true);
    // While the host bounces (offline), this plugin runs at real time and the browser's sound arrives a little late.
    // Tell the host about that delay so it lines the bounce up.
    setLatencySamples (isNonRealtime() ? (int) (renderLatencySeconds * sampleRate) : 0);
    renderSynced = false;
    fastLatched = false;
}

bool PSBBridgeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::String PSBBridgeProcessor::browserName() const
{
    juce::ScopedLock sl (nameLock);
    return browserLabel;
}

void PSBBridgeProcessor::sendToBrowser (const juce::String& json)
{
    server.sendText (json);
    if (pageSinkSet.load())
    {
        juce::ScopedLock sl (sinkLock);
        if (pageSink) pageSink (json);
    }
}

void PSBBridgeProcessor::setPageSink (std::function<void (const juce::String&)> f)
{
    juce::ScopedLock sl (sinkLock);
    pageSink = std::move (f);
    pageSinkSet.store ((bool) pageSink);
    flushRequest.store (true);
}

// Messages from the page when it lives inside the plugin window (same messages the socket carries).
void PSBBridgeProcessor::pageText (const juce::String& text)
{
    lastPageMs.store (juce::Time::getMillisecondCounter());
#if PSB_EMBED
    if (text.startsWith ("{\"n\":")) { nativeText (text); return; }
#endif
    wsText (text);
    if (text.contains ("\"hello\""))
        sendToBrowser ("{\"t\":\"hello\",\"v\":2,\"plugin\":\"PSB Bridge\",\"embedded\":true" + juce::String (PSB_EMBED ? ",\"native\":true" : "") + "}");
}

void PSBBridgeProcessor::pageBinary (const uint8_t* d, size_t size)
{
    lastPageMs.store (juce::Time::getMillisecondCounter());
    wsBinary (d, size);
}

void PSBBridgeProcessor::wsConnected()
{
    flushRequest.store (true);
    server.sendText ("{\"t\":\"hello\",\"v\":2,\"plugin\":\"PSB Bridge\",\"host\":\"" + juce::SystemStats::getOperatingSystemName().replace ("\"", "") + "\"}");
}

void PSBBridgeProcessor::wsDisconnected()
{
    flushRequest.store (true);
    juce::ScopedLock sl (nameLock);
    browserLabel = {};
}

void PSBBridgeProcessor::wsText (const juce::String& text)
{
    auto v = juce::JSON::parse (text);
    if (auto* o = v.getDynamicObject())
    {
        if (o->getProperty ("t").toString() == "base")
        {
            baseSec.store ((double) o->getProperty ("base"));
            baseReady.store (true);
            return;
        }
        if (o->getProperty ("t").toString() == "hello")
        {
            juce::ScopedLock sl (nameLock);
            browserLabel = o->getProperty ("app").toString();
            const double sr = (double) o->getProperty ("sampleRate");
            if (sr >= 8000.0 && sr <= 384000.0) srcRate.store (sr);
        }
    }
}

void PSBBridgeProcessor::wsBinary (const uint8_t* d, size_t size)
{
    if (size < 16 || (d[0] != 1 && d[0] != 3)) return;
    const size_t hdr = d[0] == 3 ? 24 : 16;
    if (size < hdr) return;
    uint32_t sr, frames;
    std::memcpy (&sr, d + 4, 4);
    std::memcpy (&frames, d + 8, 4);
    if (sr >= 8000 && sr <= 384000) srcRate.store ((double) sr);
    if (size < hdr + (size_t) frames * 8) return;
    const float* f = reinterpret_cast<const float*> (d + hdr);   // interleaved L,R
    if (d[0] == 3)
    {
        double t; std::memcpy (&t, d + 16, 8);
        chunkOffset.store (t * (double) sr - (double) writePos.load (std::memory_order_relaxed));
    }
    uint64_t w = writePos.load (std::memory_order_relaxed);
    for (uint32_t i = 0; i < frames; ++i)
    {
        const size_t idx = (size_t) (w & (uint64_t) (ringSize - 1));
        ringL[idx] = f[i * 2];
        ringR[idx] = f[i * 2 + 1];
        ++w;
    }
    writePos.store (w, std::memory_order_release);
}

void PSBBridgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
#if PSB_EMBED
    nativeProcess (buffer, midi);
    return;
#endif
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;

    const uint64_t blockStart = sampleClock;
    sampleClock += (uint64_t) n;

    // Is the host rendering faster than real time (Bounce in Place, Export)? It says so itself, and we also check the clock.
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (nowMs - lastBlockMs > 500.0 || blockStart < anchorSample) { anchorMs = nowMs; anchorSample = blockStart; fastLatched = false; }
    lastBlockMs = nowMs;
    const bool flagged = isNonRealtime();
    if (! flagged && ! fastLatched)
    {
        const double expected = (double) (blockStart - anchorSample) / hostRate * 1000.0;
        if (expected > 800.0 && (nowMs - anchorMs) < expected * 0.5) fastLatched = true;
    }
    const bool render = flagged || fastLatched;
    const bool connected = browserConnected();
    if (! render) renderSynced = false;
    if (render != wasRender) { wasRender = render; flushRequest.store (true); }

    // Agree with the browser where this plugin's sample clock sits on the page's clock.
    if (render && connected && ! renderSynced)
    {
        baseReady.store (false);
        sendToBrowser ("{\"t\":\"sync\",\"s\":" + juce::String ((juce::int64) blockStart) + ",\"rate\":" + juce::String (hostRate, 3)
                         + ",\"look\":" + juce::String (lookAheadSeconds, 3) + "}");
        const auto until = juce::Time::getMillisecondCounter() + 1500;
        while (! baseReady.load() && juce::Time::getMillisecondCounter() < until) juce::Thread::sleep (1);
        renderSynced = true;
    }

    // The DAW's notes and tempo go to the browser. While rendering, each note carries its exact sample position.
    if (connected)
    {
        const bool stamp = render && baseReady.load();
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.getRawDataSize() >= 1 && m.getRawDataSize() <= 3)
            {
                const auto* r = m.getRawData();
                juce::String s = "{\"t\":\"midi\",\"d\":[" + juce::String ((int) r[0]);
                for (int i = 1; i < m.getRawDataSize(); ++i) s << "," << (int) r[i];
                s << "]";
                if (stamp) s << ",\"s\":" << juce::String ((juce::int64) (blockStart + (uint64_t) meta.samplePosition));
                s << "}";
                sendToBrowser (s);
            }
        }
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastTransport > 100)
        {
            lastTransport = now;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                {
                    juce::String s = "{\"t\":\"transport\",\"playing\":" + juce::String (pos->getIsPlaying() ? "true" : "false");
                    if (auto bpm = pos->getBpm()) s << ",\"bpm\":" << *bpm;
                    if (auto ppq = pos->getPpqPosition()) s << ",\"ppq\":" << *ppq;
                    s << "}";
                    sendToBrowser (s);
                }
        }
    }
    midi.clear();

    const double sr = srcRate.load();
    const double ratioBase = sr / hostRate;
    const uint64_t w = writePos.load (std::memory_order_acquire);

    if (render)
    {
        // Exact render: read the page's audio by timestamp. The host is told about the fixed delay, so the notes land where they were played.
        if (! connected || ! baseReady.load()) { buffer.clear(); return; }
        const double D = (double) getLatencySamples();
        const double t0 = baseSec.load() + ((double) blockStart - D) / hostRate;     // page time of this block's first sample
        const bool sameRate = std::abs (ratioBase - 1.0) < 1e-6;
        const double posStart = t0 * sr - chunkOffset.load();
        const double posEndNeeded = posStart + (double) n * ratioBase + 2.0;
        const auto deadline = juce::Time::getMillisecondCounter() + 4000;
        while ((double) writePos.load (std::memory_order_acquire) < posEndNeeded && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep (1);
        const uint64_t wNow = writePos.load (std::memory_order_acquire);
        if ((double) wNow < posEndNeeded || posStart < 0.0 || posStart < (double) wNow - (double) ringSize * 0.9) { buffer.clear(); return; }

        float pk = 0.0f;
        const int64_t base0 = (int64_t) std::llround (posStart);
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            if (sameRate)
            {
                const size_t a = (size_t) ((uint64_t) (base0 + i) & (uint64_t) (ringSize - 1));
                l = ringL[a]; r = ringR[a];
            }
            else
            {
                const double p = posStart + (double) i * ratioBase;
                const uint64_t i0 = (uint64_t) p;
                const float fr = (float) (p - (double) i0);
                const size_t a = (size_t) (i0 & (uint64_t) (ringSize - 1)), b = (size_t) ((i0 + 1) & (uint64_t) (ringSize - 1));
                l = ringL[a] + (ringL[b] - ringL[a]) * fr;
                r = ringR[a] + (ringR[b] - ringR[a]) * fr;
            }
            outL[i] = l; outR[i] = r;
            pk = juce::jmax (pk, std::abs (l), std::abs (r));
        }
        meter.store (juce::jmax (pk, meter.load() * 0.9f));
        return;
    }

    // Live playing: the browser's audio comes out of the track. Resampling and a gentle drift correction keep it in time.
    const double target = 0.040 * sr;      // keep about 40 ms in hand
    if (flushRequest.exchange (false)) { primed = false; readPos = (double) w; }

    double fill = (double) w - readPos;
    if (! primed)
    {
        if (connected && fill >= target) { primed = true; readPos = (double) w - target; }
        else { buffer.clear(); meter.store (meter.load() * 0.9f); return; }
    }
    if (fill < 0 || fill > (double) ringSize * 0.75) { primed = false; buffer.clear(); return; }

    // Nudge the speed so the amount in hand drifts back to the target (limited to +/- 0.5 percent)
    const double err = (fill - target) / target;
    ratioTrim += 0.00002 * juce::jlimit (-1.0, 1.0, err);
    ratioTrim = juce::jlimit (0.995, 1.005, ratioTrim);
    const double step = ratioBase * ratioTrim;

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        if (readPos + 2.0 >= (double) w) { outL[i] = outR[i] = 0.0f; primed = false; for (int j = i; j < n; ++j) outL[j] = outR[j] = 0.0f; break; }
        const uint64_t i0 = (uint64_t) readPos;
        const float fr = (float) (readPos - (double) i0);
        const size_t a = (size_t) (i0 & (uint64_t) (ringSize - 1)), b = (size_t) ((i0 + 1) & (uint64_t) (ringSize - 1));
        const float l = ringL[a] + (ringL[b] - ringL[a]) * fr;
        const float r = ringR[a] + (ringR[b] - ringR[a]) * fr;
        outL[i] = l; outR[i] = r;
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
        readPos += step;
    }
    meter.store (juce::jmax (peak, meter.load() * 0.9f));
}

#if PSB_EMBED
// ================================================================================================
// Native engine: the plugin makes the sound itself, inside the host's own audio callback.
// ================================================================================================
juce::File PSBBridgeProcessor::cacheRoot() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Purple SoundBank").getChildFile ("PSB Bridge").getChildFile ("sounds");
}

void PSBBridgeProcessor::applyConfig (const juce::var& sounds)
{
    auto defs = psb::parseSounds (sounds);
    if (defs.empty()) return;
    engine.defineSounds (defs);
    soundIds.clear();
    for (auto& d : defs) soundIds.push_back (juce::String (d.id));
    nativeReady = true;
    for (auto& id : soundIds) loadCached (id);
    sendHave();
}

void PSBBridgeProcessor::sendHave()
{
    juce::String s = "{\"t\":\"have\",\"ids\":[";
    {
        juce::ScopedLock sl (incomingLock);
        bool first = true;
        for (auto& id : haveIds) { if (! first) s << ","; first = false; s << "\"" << id << "\""; }
    }
    s << "]}";
    sendToBrowser (s);
}

void PSBBridgeProcessor::loadCached (const juce::String& id)
{
    loaderPool.addJob ([this, id]
    {
        const auto dir = cacheRoot().getChildFile (id);
        if (! dir.getChildFile ("ok").existsAsFile()) return;
        std::map<int, psb::Pcm> m;
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.mp3"))
        {
            juce::MemoryBlock mb;
            if (! f.loadFileAsData (mb)) continue;
            psb::Pcm p;
            if (psb::decodeMp3 (mb.getData(), mb.getSize(), p)) m[f.getFileNameWithoutExtension().getIntValue()] = std::move (p);
        }
        if (m.empty()) return;
        const std::string sid = id.toStdString();
        engine.prepareSamples (sid, m);
        engine.installSamples (sid, std::move (m));
        { juce::ScopedLock sl (incomingLock); haveIds.insert (id); }
        juce::MessageManager::callAsync ([this] { sendHave(); });
        return;
    });
}

void PSBBridgeProcessor::nativeSound (const juce::String& id, int key, const juce::String& base64)
{
    juce::MemoryOutputStream out;
    if (! juce::Base64::convertFromBase64 (out, base64)) return;
    juce::ScopedLock sl (incomingLock);
    incoming[id].mp3[key] = out.getMemoryBlock();
}

void PSBBridgeProcessor::nativeSoundEnd (const juce::String& id, int)
{
    Incoming got;
    {
        juce::ScopedLock sl (incomingLock);
        auto it = incoming.find (id);
        if (it == incoming.end()) return;
        got = std::move (it->second);
        incoming.erase (it);
    }
    auto shared = std::make_shared<Incoming> (std::move (got));
    loaderPool.addJob ([this, id, shared]
    {
        std::map<int, psb::Pcm> m;
        const auto dir = cacheRoot().getChildFile (id);
        dir.createDirectory();
        dir.getChildFile ("ok").deleteFile();
        for (auto& kv : shared->mp3)
        {
            dir.getChildFile (juce::String (kv.first) + ".mp3").replaceWithData (kv.second.getData(), kv.second.getSize());
            psb::Pcm p;
            if (psb::decodeMp3 (kv.second.getData(), kv.second.getSize(), p)) m[kv.first] = std::move (p);
        }
        if (m.empty()) return;
        dir.getChildFile ("ok").replaceWithText ("1");
        const std::string sid = id.toStdString();
        engine.prepareSamples (sid, m);
        engine.installSamples (sid, std::move (m));
        { juce::ScopedLock sl (incomingLock); haveIds.insert (id); }
        juce::MessageManager::callAsync ([this] { sendHave(); });
        return;
    });
}

void PSBBridgeProcessor::nativeText (const juce::String& json)
{
    const auto v = juce::JSON::parse (json);
    auto* o = v.getDynamicObject();
    if (! o) return;
    const auto n = o->getProperty ("n").toString();
    if (n == "cfg") { { juce::ScopedLock sl (stateLock); lastCfg = json; } applyConfig (o->getProperty ("sounds")); return; }
    if (n == "on") { engine.noteOn ((int) o->getProperty ("note"), (int) o->getProperty ("vel")); return; }
    if (n == "off") { engine.noteOff ((int) o->getProperty ("note")); return; }
    if (n == "bend") { engine.pitchBend ((int) o->getProperty ("v")); return; }
    if (n == "cc") { engine.controller ((int) o->getProperty ("c"), (int) o->getProperty ("v")); return; }
    if (n == "st") { { juce::ScopedLock sl (stateLock); lastSt = json; } psb::applyState (engine, o); return; }
}

bool PSBBridgeProcessor::popUiMidi (UiMidi& m)
{
    int s1, n1, s2, n2;
    uiFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 + n2 <= 0) return false;
    m = uiBuf[n1 > 0 ? s1 : s2];
    uiFifo.finishedRead (1);
    return true;
}

bool PSBBridgeProcessor::popTransport (bool& p, double& bpm)
{
    if (! transportDirty.exchange (false)) return false;
    p = playing.load(); bpm = bpmNow.load();
    return true;
}

void PSBBridgeProcessor::nativeProcess (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 1) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;

    int pos = 0;
    for (const auto meta : midi)
    {
        const int sp = juce::jlimit (0, n, meta.samplePosition);
        if (sp > pos) { engine.render (L + pos, R + pos, sp - pos); pos = sp; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine.noteOnRT (m.getNoteNumber(), m.getVelocity());
        else if (m.isNoteOff()) engine.noteOffRT (m.getNoteNumber());
        else if (m.isController()) engine.controllerRT (m.getControllerNumber(), m.getControllerValue());
        else if (m.isPitchWheel()) engine.pitchBendRT (m.getPitchWheelValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOffRT();
        // the page shows what the host plays (keys, wheels): it does not make sound
        if (m.getRawDataSize() >= 1 && m.getRawDataSize() <= 3)
        {
            int s1, n1, s2, n2;
            uiFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 > 0)
            {
                UiMidi& u = uiBuf[n1 > 0 ? s1 : s2];
                u.n = (uint8_t) m.getRawDataSize();
                for (int i = 0; i < u.n; ++i) u.b[i] = m.getRawData()[i];
                uiFifo.finishedWrite (1);
            }
        }
    }
    if (pos < n) engine.render (L + pos, R + pos, n - pos);
    midi.clear();

    float pk = 0.0f;
    for (int i = 0; i < n; i += 4) pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
    nativePeak.store (juce::jmax (pk, nativePeak.load() * 0.9f));
    meter.store (nativePeak.load());
    if (auto* ph = getPlayHead())
        if (auto p = ph->getPosition())
        {
            const bool pl = p->getIsPlaying();
            const double b = p->getBpm().orFallback (0.0);
            if (pl != playing.load() || std::abs (b - bpmNow.load()) > 0.01) { playing.store (pl); bpmNow.store (b); transportDirty.store (true); }
        }
}
#endif

juce::AudioProcessorEditor* PSBBridgeProcessor::createEditor() { return new PSBBridgeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PSBBridgeProcessor(); }

// The host saves the project with the sound set-up inside, so it comes back the way it was (no window needed).
void PSBBridgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
#if PSB_EMBED
    juce::ScopedLock sl (stateLock);
    auto* o = new juce::DynamicObject();
    o->setProperty ("cfg", lastCfg);
    o->setProperty ("st", lastSt);
    const auto s = juce::JSON::toString (juce::var (o));
    dest.append (s.toRawUTF8(), s.getNumBytesAsUTF8());
#else
    juce::ignoreUnused (dest);
#endif
}

void PSBBridgeProcessor::setStateInformation (const void* data, int size)
{
#if PSB_EMBED
    const auto v = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (data), size));
    if (auto* o = v.getDynamicObject())
    {
        const auto cfg = o->getProperty ("cfg").toString(), st = o->getProperty ("st").toString();
        if (cfg.isNotEmpty()) nativeText (cfg);
        if (st.isNotEmpty()) nativeText (st);
    }
#else
    juce::ignoreUnused (data, size);
#endif
}
