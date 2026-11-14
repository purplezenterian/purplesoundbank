#include "PluginEditor.h"

PSBBridgeEditor::PSBBridgeEditor (PSBBridgeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
#if PSB_EMBED
    // The real Purple SoundBank lives in this window. Members sign in here and play exactly as on the website.
    setResizable (true, true);
    setResizeLimits (640, 420, 3000, 2000);
    setSize (1180, 760);
    setWantsKeyboardFocus (true);
    web = std::make_unique<juce::WebBrowserComponent> (juce::WebBrowserComponent::Options{}
            .withNativeIntegrationEnabled()
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withEventListener ("psb", [this] (const juce::var& v) { onPage (v); }));
    addAndMakeVisible (*web);
    web->setBounds (getLocalBounds());
    proc.setPageSink ([safe = juce::Component::SafePointer<PSBBridgeEditor> (this)] (const juce::String& s)
                      { juce::MessageManager::callAsync ([safe, s] { if (safe != nullptr) safe->emit (s); }); });
    web->goToURL ("https://purplezenterian.github.io/purplesoundbank/site/?plugin=1#/play");
#else
    setSize (520, 300);
#endif
    startTimerHz (30);
}
PSBBridgeEditor::~PSBBridgeEditor()
{
    stopTimer();
#if PSB_EMBED
    proc.setPageSink ({});
    web.reset();
#endif
}

void PSBBridgeEditor::resized()
{
#if PSB_EMBED
    if (web) web->setBounds (getLocalBounds());
#endif
}

#if PSB_EMBED
void PSBBridgeEditor::timerCallback()
{
    // What the host plays is shown on the page's keys and wheels (the sound itself is made by the plugin).
    PSBBridgeProcessor::UiMidi m;
    int guard = 0;
    while (guard++ < 64 && proc.popUiMidi (m))
    {
        juce::String s = "{\"t\":\"midi\",\"d\":[" + juce::String ((int) m.b[0]);
        for (int i = 1; i < m.n; ++i) s << "," << (int) m.b[i];
        s << "]}";
        emit (s);
    }
    bool pl; double bpm;
    if (proc.popTransport (pl, bpm)) emit ("{\"t\":\"transport\",\"playing\":" + juce::String (pl ? "true" : "false") + ",\"bpm\":" + juce::String (bpm) + "}");
    repaint();
}

void PSBBridgeEditor::emit (const juce::String& json)
{
    if (web) web->emitEventIfBrowserIsVisible ("psb", juce::var (json));
}

void PSBBridgeEditor::onPage (const juce::var& v)
{
    if (auto* o = v.getDynamicObject())
    {
        const auto k = o->getProperty ("k").toString();
        const auto d = o->getProperty ("d").toString();
        if (k == "t") proc.pageText (d);
        else if (k == "snd") proc.nativeSound (o->getProperty ("id").toString(), (int) o->getProperty ("key"), d);
        else if (k == "sndEnd") proc.nativeSoundEnd (o->getProperty ("id").toString(), (int) o->getProperty ("n"));
        else if (k == "b")
        {
            juce::MemoryOutputStream m;
            if (juce::Base64::convertFromBase64 (m, d))
                proc.pageBinary (static_cast<const uint8_t*> (m.getData()), m.getDataSize());
        }
    }
}
#endif

void PSBBridgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff07040f));
    const auto purple = juce::Colour (0xff9b5cff);
    g.setColour (purple);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PSB Bridge", 24, 22, getWidth() - 48, 34, juce::Justification::left);
    g.setColour (juce::Colour (0xffa894d8));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Purple SoundBank, inside your DAW", 24, 56, getWidth() - 48, 20, juce::Justification::left);

    const bool on = proc.browserConnected() && proc.browserName().isNotEmpty();
    g.setColour (on ? juce::Colour (0xff4cf0a6) : juce::Colour (0xfff0b64c));
    g.fillEllipse (24.0f, 108.0f, 12.0f, 12.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (on ? "Linked to " + proc.browserName() : "Waiting for Purple SoundBank", 46, 102, getWidth() - 70, 24, juce::Justification::left);

    g.setColour (juce::Colour (0xffa894d8));
    g.setFont (juce::FontOptions (13.0f));
    juce::String help = on ? "Play this track's MIDI and the sounds on your hub come out of this track."
                           : "1. Open the Purple SoundBank hub in Chrome or Edge and start the instrument.\n2. Press the DAW button at the top right, then Connect to plugin.";
    g.drawFittedText (help, 24, 140, getWidth() - 48, 60, juce::Justification::topLeft, 4);
    if (proc.port() == 0)
        g.drawText ("Could not open a port. Close other PSB Bridge windows and try again.", 24, 210, getWidth() - 48, 20, juce::Justification::left);
    else
        g.drawText ("Port " + juce::String (proc.port()), 24, 210, getWidth() - 48, 20, juce::Justification::left);

    // Level meter
    const float lvl = juce::jlimit (0.0f, 1.0f, proc.level());
    g.setColour (juce::Colour (0xff1a1230));
    g.fillRoundedRectangle (24.0f, 250.0f, (float) getWidth() - 48.0f, 10.0f, 5.0f);
    g.setColour (purple);
    g.fillRoundedRectangle (24.0f, 250.0f, ((float) getWidth() - 48.0f) * lvl, 10.0f, 5.0f);
}
