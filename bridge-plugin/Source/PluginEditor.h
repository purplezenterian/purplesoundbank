#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#if PSB_EMBED
 #include <juce_gui_extra/juce_gui_extra.h>
#endif
#include "PluginProcessor.h"

class PSBBridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PSBBridgeEditor (PSBBridgeProcessor&);
    ~PSBBridgeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
#if PSB_EMBED
    void emit (const juce::String& json);
#endif
private:
#if PSB_EMBED
    void onPage (const juce::var&);
    std::unique_ptr<juce::WebBrowserComponent> web;
#endif
    void timerCallback() override { repaint(); }
    PSBBridgeProcessor& proc;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PSBBridgeEditor)
};
