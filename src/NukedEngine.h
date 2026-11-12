#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <memory>

#include "EmulatorCore.h"

// JUCE-facing realtime wrapper around an EmulatorCore (Nuked-SC55 or 88emu, picked by model).
//
// Threading:
//   - Construction and loadRoms() should be called on the message thread.
//   - postMidiMessage() is safe to call from any thread (lock-free queue).
//   - pullSamples() is the realtime audio thread entry point (lock-free).
//   - start()/stop() spin up a dedicated emulator thread, the only thread that touches the core.
//
// Output is at the core's native rate (see getNativeSampleRate()); MainComponent resamples it.

class NukedEngine
{
public:
    using Model = EmuModel;

    static juce::StringArray getModelDisplayNames();
    static Model modelFromIndex (int index)  { return emuModelFromIndex (index); }
    static int   indexFromModel (Model m)    { return emuModelToIndex (m); }
    static juce::String displayName (Model m){ return emuModelDisplayName (m); }

    NukedEngine();
    ~NukedEngine();

    NukedEngine (const NukedEngine&) = delete;
    NukedEngine& operator= (const NukedEngine&) = delete;

    // Loads ROMs for `model` from `romDirectory`. Returns true on success.
    // On failure, errorOut is populated with a user-displayable message.
    // Stops any running emulator thread before reloading.
    bool loadRoms (const juce::File& romDirectory, Model model, juce::String& errorOut);

    bool  isReady() const;
    Model getCurrentModel() const;

    // The core's native output sample rate for the loaded model. 0 until loadRoms() succeeds.
    int getNativeSampleRate() const;

    // Send a GS or GM system reset. Safe to call any time after loadRoms() succeeds.
    void postSystemReset (bool gs);

    // Push a MIDI message into the emulator's input queue. Thread-safe,
    // non-blocking. Drops the message if the queue is full.
    void postMidiMessage (const juce::MidiMessage& msg);

    // Start / stop the background emulator thread. Has no effect if the
    // emulator is not yet ready.
    void start();
    void stop();
    bool isRunning() const;

    // Audio thread entry. Pulls up to numFrames stereo float frames out of
    // the ringbuffer into outL / outR, applying the current master gain.
    // Returns the number of frames actually written; the remainder of the
    // buffer is zeroed.
    int pullSamples (float* outL, float* outR, int numFrames);

    // Set master gain (linear, 0.0–1.0+). Realtime-safe. Default 1.0.
    void  setMasterGain (float linearGain);
    float getMasterGain() const;

    // Diagnostics (cheap atomic reads).
    uint64_t getTotalSamplesProduced() const;
    uint64_t getTotalMidiBytesProcessed() const;
    int      getAudioFifoUsedPercent() const;
    float    getPeakLevel() const; // absolute peak as float (~0.0–1.0)

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
