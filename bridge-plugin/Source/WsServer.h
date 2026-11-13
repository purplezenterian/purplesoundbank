#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>

// A tiny WebSocket server on 127.0.0.1. One browser connection at a time.
// The browser (Purple SoundBank) connects out to us, so nothing needs to be opened in a firewall.
class WsServer : public juce::Thread
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void wsConnected() = 0;
        virtual void wsDisconnected() = 0;
        virtual void wsText (const juce::String& text) = 0;
        virtual void wsBinary (const uint8_t* data, size_t size) = 0;
    };

    WsServer (Listener& l, int firstPort, int lastPort);
    ~WsServer() override;

    void start() { startThread (juce::Thread::Priority::high); }
    void stop();
    void sendText (const juce::String& text);       // thread safe
    void sendBinary (const void* data, size_t size); // thread safe
    int getPort() const noexcept { return port.load(); }
    bool isConnected() const noexcept { return connected.load(); }

private:
    void run() override;
    bool handshake (juce::StreamingSocket& s);
    bool readExact (juce::StreamingSocket& s, uint8_t* dst, int n);
    void flushOutgoing (juce::StreamingSocket& s);
    void writeFrame (juce::StreamingSocket& s, int opcode, const void* data, size_t size);

    Listener& listener;
    const int firstPort, lastPort;
    std::atomic<int> port { 0 };
    std::atomic<bool> connected { false };

    juce::CriticalSection outLock;
    struct Out { int opcode; juce::MemoryBlock data; };
    juce::Array<Out> outgoing;
};
