#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <map>
#include "WsServer.h"
#if PSB_EMBED
#include "PsbEngine.h"
#endif

// PSB Bridge: no sounds of its own. The sounds live on the Purple SoundBank hub in the browser.
// The browser streams its audio here; this plugin plays it into the DAW track and sends the track's MIDI back.
class PSBBridgeProcessor : public juce::AudioProcessor, private WsServer::Listener
{
public:
    PSBBridgeProcessor();
    ~PSBBridgeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PSB Bridge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Shown in the plugin window
    // The page can be linked two ways: through the local socket (Chrome) or by living inside the plugin window.
    bool pageLinked() const { return pageSinkSet.load() && (juce::Time::getMillisecondCounter() - lastPageMs.load()) < 2500; }
    bool browserConnected() const { return server.isConnected() || pageLinked(); }
    void sendToBrowser (const juce::String& json);
    void setPageSink (std::function<void (const juce::String&)> f);
    void pageText (const juce::String& text);
    void pageBinary (const uint8_t* d, size_t size);
    int  port() const { return server.getPort(); }
    juce::String browserName() const;
    float level() const { return meter.load(); }

#if PSB_EMBED
    // The sound engine lives in the plugin: the page only controls it. Messages from the page arrive here.
    void nativeText (const juce::String& json);
    void nativeSound (const juce::String& id, int key, const juce::String& base64);
    void nativeSoundEnd (const juce::String& id, int count);
    // MIDI the host plays, handed to the page (for its keys and wheels). Drained on the message thread.
    struct UiMidi { uint8_t b[3]; uint8_t n; };
    bool popUiMidi (UiMidi& m);
    bool popTransport (bool& playing, double& bpm);
    void sendHave();
#endif

private:
#if PSB_EMBED
    psb::Engine engine;
    void nativeProcess (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void applyConfig (const juce::var& sounds);
    void queueDecode (const juce::String& id);
    void loadCached (const juce::String& id);
    juce::File cacheRoot() const;
    struct Incoming { std::map<int, juce::MemoryBlock> mp3; };
    juce::CriticalSection incomingLock;
    std::map<juce::String, Incoming> incoming;
    std::set<juce::String> haveIds;
    std::vector<juce::String> soundIds;
    juce::ThreadPool loaderPool { 1 };
    juce::AbstractFifo uiFifo { 2048 };
    UiMidi uiBuf[2048];
    std::atomic<bool> playing { false }, transportDirty { false };
    std::atomic<double> bpmNow { 0.0 };
    std::atomic<float> nativePeak { 0.0f };
    bool nativeReady = false;
    juce::CriticalSection stateLock;
    juce::String lastCfg, lastSt;
#endif
    void wsConnected() override;
    void wsDisconnected() override;
    void wsText (const juce::String&) override;
    void wsBinary (const uint8_t* data, size_t size) override;

    static constexpr int ringSize = 1 << 18;   // frames, about 5 s
    std::vector<float> ringL, ringR;
    std::atomic<uint64_t> writePos { 0 };
    std::atomic<double> srcRate { 48000.0 };
    std::atomic<bool> flushRequest { false };

    // Only touched on the audio thread
    double readPos = 0.0, ratioTrim = 1.0, hostRate = 48000.0;
    bool primed = false;

    // Exact-timing render (Bounce in Place / Export): notes are tagged with their sample position and the browser
    // says which moment on its own clock each one lands on, so the audio can be read back to the exact sample.
    uint64_t sampleClock = 0;
    bool renderSynced = false, fastLatched = false, wasRender = false;
    double anchorMs = 0.0, lastBlockMs = 0.0; uint64_t anchorSample = 0;
    std::atomic<bool> baseReady { false };
    juce::CriticalSection sinkLock;
    std::function<void (const juce::String&)> pageSink;
    std::atomic<bool> pageSinkSet { false };
    std::atomic<juce::uint32> lastPageMs { 0 };
    std::atomic<double> baseSec { 0.0 };
    std::atomic<double> chunkOffset { 0.0 };      // browser frame index minus ring position, from the newest chunk
    int64_t renderShift = 0;                      // whole-frame shift used when both sides run at the same rate
    static constexpr double renderLatencySeconds = 0.12;
    static constexpr double lookAheadSeconds = 0.08;

    std::atomic<float> meter { 0.0f };
    juce::CriticalSection nameLock;
    juce::String browserLabel;

    WsServer server;
    juce::int64 lastTransport = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PSBBridgeProcessor)
};
