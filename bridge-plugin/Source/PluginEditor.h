#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class PSBBridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PSBBridgeEditor (PSBBridgeProcessor&);
    ~PSBBridgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override {}
private:
    void timerCallback() override { repaint(); }
    PSBBridgeProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PSBBridgeEditor)
};
