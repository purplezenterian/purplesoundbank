#include "WsServer.h"

namespace
{
    // SHA-1 (needed once per connection for the WebSocket handshake).
    juce::MemoryBlock sha1 (const juce::String& text)
    {
        auto data = text.toStdString();
        uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
        std::vector<uint8_t> m (data.begin(), data.end());
        const uint64_t bits = (uint64_t) data.size() * 8;
        m.push_back (0x80);
        while (m.size() % 64 != 56) m.push_back (0);
        for (int i = 7; i >= 0; --i) m.push_back ((uint8_t) (bits >> (i * 8)));
        auto rol = [] (uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };
        for (size_t off = 0; off < m.size(); off += 64)
        {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = ((uint32_t) m[off + i * 4] << 24) | ((uint32_t) m[off + i * 4 + 1] << 16) | ((uint32_t) m[off + i * 4 + 2] << 8) | (uint32_t) m[off + i * 4 + 3];
            for (int i = 16; i < 80; ++i) w[i] = rol (w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            for (int i = 0; i < 80; ++i)
            {
                uint32_t f, k;
                if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
                uint32_t t = rol (a, 5) + f + e + k + w[i];
                e = d; d = c; c = rol (b, 30); b = a; a = t;
            }
            h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        }
        uint8_t out[20];
        const uint32_t hs[5] = { h0, h1, h2, h3, h4 };
        for (int i = 0; i < 5; ++i) for (int j = 0; j < 4; ++j) out[i * 4 + j] = (uint8_t) (hs[i] >> (24 - j * 8));
        return juce::MemoryBlock (out, 20);
    }
}

WsServer::WsServer (Listener& l, int first, int last) : juce::Thread ("PSB Bridge socket"), listener (l), firstPort (first), lastPort (last) {}
WsServer::~WsServer() { stop(); }

void WsServer::stop()
{
    signalThreadShouldExit();
    stopThread (2000);
}

void WsServer::sendText (const juce::String& text)
{
    juce::ScopedLock sl (outLock);
    if (! connected.load()) return;
    if (outgoing.size() > 512) return;
    auto utf8 = text.toStdString();
    outgoing.add ({ 1, juce::MemoryBlock (utf8.data(), utf8.size()) });
}

void WsServer::sendBinary (const void* data, size_t size)
{
    juce::ScopedLock sl (outLock);
    if (! connected.load()) return;
    if (outgoing.size() > 512) return;
    outgoing.add ({ 2, juce::MemoryBlock (data, size) });
}

bool WsServer::readExact (juce::StreamingSocket& s, uint8_t* dst, int n)
{
    int got = 0;
    while (got < n)
    {
        if (threadShouldExit()) return false;
        const int ready = s.waitUntilReady (true, 5);
        if (ready < 0) return false;
        if (ready == 0) { flushOutgoing (s); continue; }
        const int r = s.read (dst + got, n - got, false);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

void WsServer::writeFrame (juce::StreamingSocket& s, int opcode, const void* data, size_t size)
{
    std::vector<uint8_t> f;
    f.push_back ((uint8_t) (0x80 | opcode));
    if (size < 126) f.push_back ((uint8_t) size);
    else if (size < 65536) { f.push_back (126); f.push_back ((uint8_t) (size >> 8)); f.push_back ((uint8_t) size); }
    else { f.push_back (127); for (int i = 7; i >= 0; --i) f.push_back ((uint8_t) ((uint64_t) size >> (i * 8))); }
    f.insert (f.end(), (const uint8_t*) data, (const uint8_t*) data + size);
    s.write (f.data(), (int) f.size());
}

void WsServer::flushOutgoing (juce::StreamingSocket& s)
{
    juce::Array<Out> batch;
    { juce::ScopedLock sl (outLock); batch.swapWith (outgoing); }
    for (auto& o : batch) writeFrame (s, o.opcode, o.data.getData(), o.data.getSize());
}

bool WsServer::handshake (juce::StreamingSocket& s)
{
    juce::String req;
    for (int guard = 0; guard < 200 && ! req.contains ("\r\n\r\n"); ++guard)
    {
        if (threadShouldExit()) return false;
        if (s.waitUntilReady (true, 50) <= 0) continue;
        char buf[1024];
        const int r = s.read (buf, sizeof (buf), false);
        if (r <= 0) return false;
        req += juce::String::fromUTF8 (buf, r);
        if (req.length() > 16384) return false;
    }
    auto lines = juce::StringArray::fromLines (req);
    juce::String key;
    for (auto& l : lines)
        if (l.startsWithIgnoreCase ("sec-websocket-key:")) key = l.fromFirstOccurrenceOf (":", false, false).trim();
    if (key.isEmpty()) return false;
    auto digest = sha1 (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    auto accept = juce::Base64::toBase64 (digest.getData(), digest.getSize());
    juce::String resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n";
    auto u = resp.toStdString();
    return s.write (u.data(), (int) u.size()) == (int) u.size();
}

void WsServer::run()
{
    // Each plugin instance takes the next free port in the range, so several can be open at once.
    juce::StreamingSocket server;
    int bound = 0;
    for (int p = firstPort; p <= lastPort; ++p)
        if (server.createListener (p, "127.0.0.1")) { bound = p; break; }
    if (bound == 0) return;
    port.store (bound);

    while (! threadShouldExit())
    {
        if (server.waitUntilReady (true, 100) <= 0) continue;   // wake up regularly so the plugin can close quickly
        std::unique_ptr<juce::StreamingSocket> client (server.waitForNextConnection());
        if (client == nullptr) { wait (50); continue; }
        if (! handshake (*client)) continue;

        connected.store (true);
        listener.wsConnected();
        std::vector<uint8_t> payload;
        for (;;)
        {
            uint8_t h[2];
            bool dead = false;
            for (;;)   // idle: push notes out to the browser every millisecond
            {
                if (threadShouldExit()) { dead = true; break; }
                const int rdy = client->waitUntilReady (true, 1);
                if (rdy < 0) { dead = true; break; }
                if (rdy > 0) break;
                flushOutgoing (*client);
            }
            if (dead || ! readExact (*client, h, 2)) break;
            const int opcode = h[0] & 0x0F;
            uint64_t len = h[1] & 0x7F;
            const bool masked = (h[1] & 0x80) != 0;
            if (len == 126) { uint8_t e[2]; if (! readExact (*client, e, 2)) break; len = ((uint64_t) e[0] << 8) | e[1]; }
            else if (len == 127) { uint8_t e[8]; if (! readExact (*client, e, 8)) break; len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | e[i]; }
            if (len > (16u << 20)) break;
            uint8_t mask[4] = { 0, 0, 0, 0 };
            if (masked && ! readExact (*client, mask, 4)) break;
            payload.resize ((size_t) len);
            if (len > 0 && ! readExact (*client, payload.data(), (int) len)) break;
            if (masked) for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i & 3];

            if (opcode == 1) listener.wsText (juce::String::fromUTF8 ((const char*) payload.data(), (int) payload.size()));
            else if (opcode == 2) listener.wsBinary (payload.data(), payload.size());
            else if (opcode == 9) writeFrame (*client, 10, payload.data(), payload.size());
            else if (opcode == 8) break;
            flushOutgoing (*client);
        }
        connected.store (false);
        { juce::ScopedLock sl (outLock); outgoing.clear(); }
        listener.wsDisconnected();
    }
    server.close();
}
