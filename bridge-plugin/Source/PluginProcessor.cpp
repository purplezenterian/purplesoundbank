#include "PluginProcessor.h"
#include "PluginEditor.h"

PSBBridgeProcessor::PSBBridgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      ringL ((size_t) ringSize, 0.0f), ringR ((size_t) ringSize, 0.0f),
      server (*this, 48210, 48219)
{
    server.start();
}

PSBBridgeProcessor::~PSBBridgeProcessor() { server.stop(); }

void PSBBridgeProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
    flushRequest.store (true);
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

void PSBBridgeProcessor::wsConnected()
{
    flushRequest.store (true);
    server.sendText ("{\"t\":\"hello\",\"v\":1,\"plugin\":\"PSB Bridge\",\"host\":\"" + juce::SystemStats::getOperatingSystemName().replace ("\"", "") + "\"}");
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
    if (size < 16 || d[0] != 1) return;
    uint32_t sr, frames;
    std::memcpy (&sr, d + 4, 4);
    std::memcpy (&frames, d + 8, 4);
    if (sr >= 8000 && sr <= 384000) srcRate.store ((double) sr);
    if (size < 16 + (size_t) frames * 8) return;
    const float* f = reinterpret_cast<const float*> (d + 16);   // interleaved L,R
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
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;

    // The DAW's notes and tempo go to the browser
    if (server.isConnected())
    {
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.getRawDataSize() >= 1 && m.getRawDataSize() <= 3)
            {
                const auto* r = m.getRawData();
                juce::String s = "{\"t\":\"midi\",\"d\":[" + juce::String ((int) r[0]);
                for (int i = 1; i < m.getRawDataSize(); ++i) s << "," << (int) r[i];
                s << "]}";
                server.sendText (s);
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
                    server.sendText (s);
                }
        }
    }
    midi.clear();

    // The browser's audio comes out of the track. Resampling and a gentle drift correction keep it in time.
    const double ratioBase = srcRate.load() / hostRate;
    const double target = 0.040 * srcRate.load();      // keep about 40 ms in hand
    const uint64_t w = writePos.load (std::memory_order_acquire);

    if (flushRequest.exchange (false)) { primed = false; readPos = (double) w; }

    double fill = (double) w - readPos;
    if (! primed)
    {
        if (server.isConnected() && fill >= target) { primed = true; readPos = (double) w - target; }
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

juce::AudioProcessorEditor* PSBBridgeProcessor::createEditor() { return new PSBBridgeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PSBBridgeProcessor(); }
