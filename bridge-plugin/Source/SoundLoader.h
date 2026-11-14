// Decodes one of the instrument's sound files (mp3) into floats, trimmed exactly like the browser trims it,
// so notes start and loop at the same moment as they do on the website.
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "PsbEngine.h"

namespace psb
{
    inline uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

    // Reads the encoder delay / padding from the Xing/Info tag (what the browser uses to trim the file).
    inline bool readGapless (const uint8_t* d, size_t n, int& delay, int& padding, int& frames)
    {
        size_t lim = std::min<size_t> (n, 4096);
        size_t at = 0;
        bool found = false;
        for (size_t i = 0; i + 8 < lim; ++i)
            if ((d[i] == 'I' && d[i + 1] == 'n' && d[i + 2] == 'f' && d[i + 3] == 'o') || (d[i] == 'X' && d[i + 1] == 'i' && d[i + 2] == 'n' && d[i + 3] == 'g'))
            { at = i; found = true; break; }
        if (! found) return false;
        size_t p = at + 4;
        const uint32_t flags = be32 (d + p); p += 4;
        frames = 0;
        if (flags & 1) { frames = (int) be32 (d + p); p += 4; }
        if (flags & 2) p += 4;
        if (flags & 4) p += 100;
        if (flags & 8) p += 4;
        if (p + 24 > n) return false;
        const uint8_t* e = d + p;   // encoder name (9 bytes), then the tag body
        delay = (e[21] << 4) | (e[22] >> 4);
        padding = ((e[22] & 0x0F) << 8) | e[23];
        return frames > 0 && delay >= 0 && delay < 4000 && padding >= 0 && padding < 4000;
    }

    inline bool decodeMp3 (const void* data, size_t size, Pcm& out)
    {
        juce::MP3AudioFormat fmt;
        std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
        if (! r || r->lengthInSamples <= 0) return false;
        const int total = (int) r->lengthInSamples;
        juce::AudioBuffer<float> b ((int) juce::jmax<unsigned> (2u, r->numChannels), total);
        if (! r->read (&b, 0, total, 0, true, true)) return false;
        if (r->numChannels < 2) b.copyFrom (1, 0, b, 0, 0, total);
        int delay = 0, padding = 0, frames = 0, skip = 0, len = total;
        if (readGapless (static_cast<const uint8_t*> (data), size, delay, padding, frames))
        {
            skip = delay + 529;
            len = juce::jmin (total - skip, frames * 1152 - delay - padding);
        }
        if (len <= 0) return false;
        out.sr = r->sampleRate;
        out.l.assign (b.getReadPointer (0) + skip, b.getReadPointer (0) + skip + len);
        out.r.assign (b.getReadPointer (1) + skip, b.getReadPointer (1) + skip + len);
        return true;
    }
}
