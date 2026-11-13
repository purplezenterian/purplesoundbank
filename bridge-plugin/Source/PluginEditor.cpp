#include "PluginEditor.h"

PSBBridgeEditor::PSBBridgeEditor (PSBBridgeProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setResizable (true, true);
    setResizeLimits (480, 300, 2400, 1600);
    setSize (900, 560);
    startTimerHz (30);
}
PSBBridgeEditor::~PSBBridgeEditor() { stopTimer(); }

juce::Rectangle<float> PSBBridgeEditor::frameArea (const juce::Image& img) const
{
    auto b = getLocalBounds().toFloat();
    const float s = juce::jmin (b.getWidth() / (float) img.getWidth(), b.getHeight() / (float) img.getHeight());
    const float w = (float) img.getWidth() * s, hh = (float) img.getHeight() * s;
    return { b.getCentreX() - w * 0.5f, b.getCentreY() - hh * 0.5f, w, hh };
}

void PSBBridgeEditor::sendView()
{
    if (proc.browserConnected())
        proc.sendToBrowser ("{\"t\":\"view\",\"w\":" + juce::String (getWidth() * 2) + ",\"h\":" + juce::String (getHeight() * 2) + "}");
}

void PSBBridgeEditor::resized() { sendView(); }

void PSBBridgeEditor::timerCallback()
{
    const bool c = proc.browserConnected();
    if (c && ! wasConnected) sendView();
    wasConnected = c;
    const auto seq = proc.frameCount();
    if (seq != lastSeq) { lastSeq = seq; shown = proc.latestFrame(); }
    repaint();
}

void PSBBridgeEditor::send (const char* kind, const juce::MouseEvent& e, float dy)
{
    if (! shown.isValid() || ! proc.browserConnected()) return;
    const auto r = frameArea (shown);
    const float x = (e.position.x - r.getX()) / r.getWidth(), y = (e.position.y - r.getY()) / r.getHeight();
    if (x < 0.0f || y < 0.0f || x > 1.0f || y > 1.0f) { if (juce::String (kind) != "up") return; }
    proc.sendToBrowser ("{\"t\":\"ptr\",\"k\":\"" + juce::String (kind) + "\",\"x\":" + juce::String (juce::jlimit (0.0f, 1.0f, x), 5)
                        + ",\"y\":" + juce::String (juce::jlimit (0.0f, 1.0f, y), 5) + ",\"dy\":" + juce::String (dy, 1) + "}");
}

void PSBBridgeEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastMove < 12) return;
    lastMove = now;
    send ("move", e);
}

void PSBBridgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff07040f));
    if (shown.isValid() && proc.browserConnected())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (shown, frameArea (shown));
        return;
    }
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
                           : "1. Open the Purple SoundBank hub in Chrome or Edge and start the instrument.\n2. Press Link to DAW, then Connect to plugin, then Show screen in plugin.";
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
