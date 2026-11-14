#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "WsServer.h"

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
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Shown in the plugin window
    bool browserConnected() const { return server.isConnected(); }
    int  port() const { return server.getPort(); }
    juce::String browserName() const;
    float level() const { return meter.load(); }

private:
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
    bool primed = false, wasOffline = false, offlinePrimed = false;
    static constexpr double offlineLatencySeconds = 0.045;

    std::atomic<float> meter { 0.0f };
    juce::CriticalSection nameLock;
    juce::String browserLabel;

    WsServer server;
    juce::int64 lastTransport = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PSBBridgeProcessor)
};
