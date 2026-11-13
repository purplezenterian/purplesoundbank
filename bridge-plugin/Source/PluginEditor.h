#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class PSBBridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PSBBridgeEditor (PSBBridgeProcessor&);
    ~PSBBridgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override { send ("down", e); }
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override { send ("up", e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { send ("wheel", e, -w.deltaY * 300.0f); }
private:
    void timerCallback() override;
    juce::Rectangle<float> frameArea (const juce::Image&) const;
    void send (const char* kind, const juce::MouseEvent&, float dy = 0.0f);
    void sendView();
    juce::Image shown;
    uint32_t lastSeq = 0xffffffff;
    bool wasConnected = false;
    juce::int64 lastMove = 0;
    PSBBridgeProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PSBBridgeEditor)
};
