#include "NukedEngine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

//==============================================================================
// Pimpl
//==============================================================================

class NukedEngine::Impl : private FrameSink
{
public:
    Impl()
    {
        // Audio ringbuffer, ~0.5 s at native rates.
        constexpr int kAudioSlots = 1 << 15; // 32768 stereo frames
        audioL.allocate ((size_t) kAudioSlots, true);
        audioR.allocate ((size_t) kAudioSlots, true);
        audioFifo = std::make_unique<juce::AbstractFifo> (kAudioSlots);

        // MIDI ringbuffer
        constexpr int kMidiSlots = 1 << 13; // 8192 bytes
        midiBuf.allocate ((size_t) kMidiSlots, true);
        midiFifo = std::make_unique<juce::AbstractFifo> (kMidiSlots);
    }

    ~Impl()
    {
        stop();
    }

    bool loadRoms (const juce::File& dir, NukedEngine::Model model, juce::String& err)
    {
        stop();
        ready.store (false, std::memory_order_release); // honest until proven ready
        core.reset();

        std::string error;
        const std::filesystem::path basePath = dir.getFullPathName().toStdString();
        core = createEmulatorCore (model, basePath, error);
        if (core == nullptr)
        {
            err = juce::String (error);
            return false;
        }

        totalSamples.store (0);
        totalMidiBytes.store (0);
        peak.store (0.0f);

        currentModel = model;
        nativeRate = core->nativeSampleRate();
        ready.store (true, std::memory_order_release);
        return true;
    }

    int getNativeSampleRate() const { return nativeRate; }
    bool isReady() const { return ready.load (std::memory_order_acquire); }
    NukedEngine::Model getCurrentModel() const { return currentModel; }

    void postSystemReset (bool gs)
    {
        if (! isReady()) return;
        pendingReset.store (gs ? 1 : 2, std::memory_order_release);
    }

    void postMidiMessage (const juce::MidiMessage& msg)
    {
        if (! isReady()) return;
        const auto* data = msg.getRawData();
        const int   size = msg.getRawDataSize();
        if (size <= 0) return;

        const auto scope = midiFifo->write (size);
        if (scope.blockSize1 + scope.blockSize2 < size)
            return; // queue full, drop message
        for (int i = 0; i < scope.blockSize1; ++i)
            midiBuf[scope.startIndex1 + i] = data[i];
        for (int i = 0; i < scope.blockSize2; ++i)
            midiBuf[scope.startIndex2 + i] = data[scope.blockSize1 + i];
    }

    void start()
    {
        if (! isReady())  return;
        if (running.load (std::memory_order_acquire)) return;

        running.store (true, std::memory_order_release);
        emuThread = std::thread ([this] { runLoop(); });
    }

    void stop()
    {
        running.store (false, std::memory_order_release);
        if (emuThread.joinable())
            emuThread.join();
    }

    bool isRunning() const { return running.load (std::memory_order_acquire); }

    uint64_t getTotalSamplesProduced() const   { return totalSamples.load (std::memory_order_relaxed); }
    uint64_t getTotalMidiBytesProcessed() const{ return totalMidiBytes.load (std::memory_order_relaxed); }
    float    getPeakLevel() const              { return peak.load (std::memory_order_relaxed); }
    int      getAudioFifoUsedPercent() const
    {
        const int total = audioFifo ? audioFifo->getTotalSize() : 1;
        const int used  = audioFifo ? audioFifo->getNumReady()  : 0;
        return (int) (100.0 * (double) used / (double) total);
    }

    int pullSamples (float* outL, float* outR, int numFrames)
    {
        if (! isReady())
        {
            juce::FloatVectorOperations::clear (outL, numFrames);
            juce::FloatVectorOperations::clear (outR, numFrames);
            return 0;
        }

        const auto scope = audioFifo->read (numFrames);
        const int delivered = scope.blockSize1 + scope.blockSize2;
        const float g = masterGain.load (std::memory_order_relaxed);

        for (int i = 0; i < scope.blockSize1; ++i)
        {
            outL[i] = audioL[scope.startIndex1 + i] * g;
            outR[i] = audioR[scope.startIndex1 + i] * g;
        }
        for (int i = 0; i < scope.blockSize2; ++i)
        {
            outL[scope.blockSize1 + i] = audioL[scope.startIndex2 + i] * g;
            outR[scope.blockSize1 + i] = audioR[scope.startIndex2 + i] * g;
        }

        if (delivered < numFrames)
        {
            juce::FloatVectorOperations::clear (outL + delivered, numFrames - delivered);
            juce::FloatVectorOperations::clear (outR + delivered, numFrames - delivered);
        }
        return delivered;
    }

    void  setMasterGain (float g) { masterGain.store (juce::jlimit (0.0f, 4.0f, g), std::memory_order_relaxed); }
    float getMasterGain() const   { return masterGain.load (std::memory_order_relaxed); }

private:
    void runLoop()
    {
        using namespace std::chrono_literals;
        constexpr int kHighWaterMark = (1 << 15) * 3 / 4;
        constexpr int kFramesPerSlice = 64; // ~1 ms: keeps MIDI timing tight

        while (running.load (std::memory_order_acquire))
        {
            drainMidi();

            if (auto resetFlag = pendingReset.exchange (0, std::memory_order_acq_rel))
                core->postSystemReset (resetFlag == 1);

            // Throttle: if the audio ringbuffer is mostly full, let the audio thread drain it.
            if (audioFifo->getNumReady() > kHighWaterMark)
            {
                std::this_thread::sleep_for (500us);
                continue;
            }

            core->run (*this, kFramesPerSlice);
        }
    }

    void drainMidi()
    {
        const int avail = midiFifo->getNumReady();
        if (avail <= 0) return;

        const auto scope = midiFifo->read (avail);
        if (scope.blockSize1 > 0) core->postMidi (midiBuf.getData() + scope.startIndex1, (size_t) scope.blockSize1);
        if (scope.blockSize2 > 0) core->postMidi (midiBuf.getData() + scope.startIndex2, (size_t) scope.blockSize2);

        totalMidiBytes.fetch_add ((uint64_t) avail, std::memory_order_relaxed);
    }

    // FrameSink, called on the emulator thread.
    void pushFrame (float l, float r) override
    {
        totalSamples.fetch_add (1, std::memory_order_relaxed);

        // Diagnostic: absolute peak ever observed.
        const float a = std::max (std::abs (l), std::abs (r));
        float prev = peak.load (std::memory_order_relaxed);
        while (a > prev
               && ! peak.compare_exchange_weak (prev, a, std::memory_order_relaxed, std::memory_order_relaxed))
        { /* retry */ }

        const auto scope = audioFifo->write (1);
        if (scope.blockSize1 == 1)
        {
            audioL[scope.startIndex1] = l;
            audioR[scope.startIndex1] = r;
        }
        else if (scope.blockSize2 == 1)
        {
            audioL[scope.startIndex2] = l;
            audioR[scope.startIndex2] = r;
        }
        // If FIFO is full the frame is dropped; the throttle in runLoop() prevents this in practice.
    }

    std::unique_ptr<EmulatorCore> core;
    NukedEngine::Model currentModel = NukedEngine::Model::SC55mk2;
    int  nativeRate = 0;
    std::atomic<bool> ready { false };
    std::atomic<bool> running { false };
    std::atomic<int>  pendingReset { 0 }; // 0 = none, 1 = GS, 2 = GM
    std::atomic<uint64_t> totalSamples { 0 };
    std::atomic<uint64_t> totalMidiBytes { 0 };
    std::atomic<float>    peak { 0.0f };
    std::atomic<float>    masterGain { 1.0f };

    juce::HeapBlock<float>  audioL, audioR;
    std::unique_ptr<juce::AbstractFifo> audioFifo;

    juce::HeapBlock<uint8_t> midiBuf;
    std::unique_ptr<juce::AbstractFifo> midiFifo;

    std::thread emuThread;
};

//==============================================================================
// Forwarding facade
//==============================================================================

NukedEngine::NukedEngine() : impl (std::make_unique<Impl>()) {}
NukedEngine::~NukedEngine() = default;

juce::StringArray NukedEngine::getModelDisplayNames()
{
    juce::StringArray names;
    for (int i = 0; i < emuModelCount(); ++i)
    {
        const auto m = emuModelFromIndex (i);
        names.add (juce::String (emuModelDisplayName (m))
                   + (emuModelIsBuilt (m) ? juce::String() : juce::String (" (not in this build)")));
    }
    return names;
}

bool NukedEngine::loadRoms (const juce::File& dir, Model model, juce::String& err)
{
    return impl->loadRoms (dir, model, err);
}

bool  NukedEngine::isReady() const             { return impl->isReady(); }
int   NukedEngine::getNativeSampleRate() const { return impl->getNativeSampleRate(); }
NukedEngine::Model NukedEngine::getCurrentModel() const { return impl->getCurrentModel(); }

void NukedEngine::postSystemReset (bool gs)             { impl->postSystemReset (gs); }
void NukedEngine::postMidiMessage (const juce::MidiMessage& m) { impl->postMidiMessage (m); }

void NukedEngine::start() { impl->start(); }
void NukedEngine::stop()  { impl->stop(); }
bool NukedEngine::isRunning() const { return impl->isRunning(); }

int NukedEngine::pullSamples (float* outL, float* outR, int numFrames)
{
    return impl->pullSamples (outL, outR, numFrames);
}

uint64_t NukedEngine::getTotalSamplesProduced() const     { return impl->getTotalSamplesProduced(); }
uint64_t NukedEngine::getTotalMidiBytesProcessed() const  { return impl->getTotalMidiBytesProcessed(); }
int      NukedEngine::getAudioFifoUsedPercent() const     { return impl->getAudioFifoUsedPercent(); }
float    NukedEngine::getPeakLevel() const                { return impl->getPeakLevel(); }
void     NukedEngine::setMasterGain (float g)             { impl->setMasterGain (g); }
float    NukedEngine::getMasterGain() const               { return impl->getMasterGain(); }
