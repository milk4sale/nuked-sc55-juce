// TestRunner.cpp
//
// Headless validation runner. Reads the same settings the GUI app saves
// (ROM dir, model, last MIDI file) and renders the MIDI file offline
// through the selected core (Nuked-SC55 or 88emu), writing a WAV and a verbose log.
//
// Run from the command line — no UI, no audio device, fully deterministic.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>

#include "EmulatorCore.h"

namespace
{
    juce::String                     g_logPath;
    std::unique_ptr<juce::FileLogger> g_logger;

    void L (const juce::String& s)
    {
        std::cout << s << std::endl;
        if (g_logger != nullptr) g_logger->logMessage (s);
    }
}

// --- offline sample collector --------------------------------------------------

struct SampleCollector final : FrameSink
{
    std::vector<float> left;
    std::vector<float> right;
    uint64_t count = 0;
    double minL =  1e30, maxL = -1e30, minR = 1e30, maxR = -1e30;
    double sumAbs = 0.0;

    void pushFrame (float L, float R) override
    {
        left.push_back (L);
        right.push_back (R);
        ++count;
        if (L < minL) minL = L;
        if (L > maxL) maxL = L;
        if (R < minR) minR = R;
        if (R > maxR) maxR = R;
        sumAbs += std::abs (L) + std::abs (R);
    }
};

// --- main --------------------------------------------------------------------

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit; // for FileLogger etc.

    auto exeDir  = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    auto logFile = exeDir.getChildFile ("test-runner.log");
    logFile.deleteFile();
    g_logger = std::make_unique<juce::FileLogger> (logFile, "TestRunner", 0);
    g_logPath = logFile.getFullPathName();

    L ("=== TestRunner start ===");
    L ("Log file: " + g_logPath);

    // --- read same settings the GUI app saves -----------------------------
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "NukedSC55JUCE";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
   #if JUCE_LINUX || JUCE_BSD
    opts.folderName          = ".config/NukedSC55JUCE"; // XDG-style, not ~/NukedSC55JUCE
   #else
    opts.folderName          = "NukedSC55JUCE";
   #endif

    juce::ApplicationProperties props;
    props.setStorageParameters (opts);
    auto* user = props.getUserSettings();

    juce::File romDir   (user->getValue ("romDirectory"));
    int        modelIdx = user->getIntValue ("modelIndex", 0);
    juce::File midiFile (user->getValue ("lastMidiFile"));

    juce::String midiArg, romArg;
    int modelArg = -1;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a (argv[i]);
        if (a.startsWith ("--midi=")) midiArg = a.fromFirstOccurrenceOf ("=", false, false);
        else if (a.startsWith ("--rom-dir=")) romArg = a.fromFirstOccurrenceOf ("=", false, false);
        else if (a.startsWith ("--model="))   modelArg = a.fromFirstOccurrenceOf ("=", false, false).getIntValue();
    }
    if (midiArg.isNotEmpty()) midiFile = juce::File (midiArg);
    if (romArg.isNotEmpty())  romDir   = juce::File (romArg);
    if (modelArg >= 0)         modelIdx = modelArg;

    L ("Settings file: " + user->getFile().getFullPathName());
    L ("ROM dir:   " + (romDir.isDirectory() ? romDir.getFullPathName() : "(not set / not a directory)"));
    L ("Model idx: " + juce::String (modelIdx));
    L ("MIDI file: " + (midiFile.existsAsFile() ? midiFile.getFullPathName() : "(not set / does not exist)"));

    if (! romDir.isDirectory())
    {
        L ("FATAL: ROM directory not set or invalid. Run the GUI app first to pick one, or pass --rom-dir=...");
        return 2;
    }
    if (! midiFile.existsAsFile())
    {
        L ("FATAL: MIDI file not set or invalid. Run the GUI app first to load one, or pass --midi=...");
        return 2;
    }

    // --- load ROMs + boot the core ----------------------------------------
    const auto model = emuModelFromIndex (modelIdx);
    L ("Model: " + juce::String (emuModelDisplayName (model))
       + (emuModelUsesNuked (model) ? " (Nuked-SC55)" : " (88emu)"));

    std::string loadError;
    const auto t0 = std::chrono::steady_clock::now();
    auto core = createEmulatorCore (model, romDir.getFullPathName().toStdString(), loadError);
    if (core == nullptr)
    {
        L ("FATAL: " + juce::String (loadError));
        return 3;
    }
    const auto bootMs = std::chrono::duration_cast<std::chrono::milliseconds> (
        std::chrono::steady_clock::now() - t0).count();
    const int nativeRate = core->nativeSampleRate();
    L ("Core ready in " + juce::String ((juce::int64) bootMs) + " ms, native sample rate "
       + juce::String (nativeRate) + " Hz");

    SampleCollector collector;
    collector.left.reserve ((size_t) nativeRate * 32);
    collector.right.reserve ((size_t) nativeRate * 32);

    auto runFrames = [&] (uint64_t frames)
    {
        const uint64_t target = collector.count + frames;
        while (collector.count < target)
            core->run (collector, (int) std::min<uint64_t> (64, target - collector.count));
    };

    // Warm up ~250 ms so the PCM state settles before playing.
    L ("Warming up for " + juce::String (nativeRate / 4) + " frames...");
    runFrames ((uint64_t) nativeRate / 4);

    // GS reset covers MIDI files that omit it.
    L ("Sending GS reset...");
    core->postSystemReset (true);
    runFrames ((uint64_t) nativeRate / 10);
    L ("GS reset processed.");

    // --- parse the MIDI file ----------------------------------------------
    juce::MidiFile mf;
    {
        std::unique_ptr<juce::FileInputStream> is (midiFile.createInputStream());
        if (is == nullptr || ! is->openedOk()) { L ("FATAL: open MIDI failed"); return 6; }
        if (! mf.readFrom (*is))                { L ("FATAL: parse MIDI failed"); return 7; }
    }
    mf.convertTimestampTicksToSeconds();
    juce::MidiMessageSequence merged;
    for (int t = 0; t < mf.getNumTracks(); ++t) merged.addSequence (*mf.getTrack (t), 0.0);
    merged.updateMatchedPairs();
    L ("MIDI parsed: " + juce::String (merged.getNumEvents())
          + " events, length " + juce::String (merged.getEndTime(), 2) + " s");

    // --- offline render ---------------------------------------------------
    const double renderSeconds = juce::jmin (merged.getEndTime() + 1.0, 30.0);
    const uint64_t totalSamplesNeeded = (uint64_t) (renderSeconds * nativeRate);
    L ("Rendering " + juce::String (renderSeconds, 2) + " s ("
          + juce::String ((juce::int64) totalSamplesNeeded) + " samples)...");

    const uint64_t startCount = collector.count;
    uint64_t midiBytesPosted  = 0;
    int     nextEvent          = 0;
    const int numEvents        = merged.getNumEvents();

    while (collector.count - startCount < totalSamplesNeeded)
    {
        const uint64_t relSamples = collector.count - startCount;
        const double   nowSec     = (double) relSamples / nativeRate;

        // Dispatch any events whose time has come
        while (nextEvent < numEvents)
        {
            auto* evt = merged.getEventPointer (nextEvent);
            if (evt->message.getTimeStamp() > nowSec) break;
            if (! evt->message.isMetaEvent())
            {
                const auto* data = evt->message.getRawData();
                const int    n    = evt->message.getRawDataSize();
                core->postMidi (data, (size_t) n);
                midiBytesPosted += (uint64_t) n;
            }
            ++nextEvent;
        }
        core->run (collector, 64);
    }
    L ("Render done. Events sent: " + juce::String (nextEvent) + "/" + juce::String (numEvents)
          + ", MIDI bytes: " + juce::String ((juce::int64) midiBytesPosted)
          + ", samples produced: " + juce::String ((juce::int64) (collector.count - startCount)));

    // --- statistics -------------------------------------------------------
    const uint64_t n = collector.left.size();
    const double meanAbs = collector.sumAbs / juce::jmax ((uint64_t) 1, n * 2);
    L ("Output statistics:");
    L ("  total stereo frames: " + juce::String ((juce::int64) n));
    L ("  L range: [" + juce::String (collector.minL, 6) + ", " + juce::String (collector.maxL, 6) + "]");
    L ("  R range: [" + juce::String (collector.minR, 6) + ", " + juce::String (collector.maxR, 6) + "]");
    L ("  mean |sample|: " + juce::String (meanAbs, 8));

    const bool nonSilent = (collector.maxL > 1e-5) || (-collector.minL > 1e-5)
                        || (collector.maxR > 1e-5) || (-collector.minR > 1e-5);
    L (nonSilent ? "==> AUDIO PRODUCED (non-silent output)"
                 : "==> SILENCE — emulator generated zero-magnitude output");

    // --- write WAV --------------------------------------------------------
    auto wavFile = exeDir.getChildFile ("test-output.wav");
    wavFile.deleteFile();
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::FileOutputStream> fos (wavFile.createOutputStream());
    if (fos == nullptr) { L ("WARN: could not create WAV output stream"); return 0; }
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFmt.createWriterFor (fos.get(), nativeRate, 2, 16, {}, 0));
    if (writer == nullptr) { L ("WARN: WAV writer creation failed"); return 0; }
    fos.release(); // ownership transferred

    juce::AudioBuffer<float> buf (2, (int) n);
    juce::FloatVectorOperations::copy (buf.getWritePointer (0), collector.left.data(),  (int) n);
    juce::FloatVectorOperations::copy (buf.getWritePointer (1), collector.right.data(), (int) n);
    writer->writeFromAudioSampleBuffer (buf, 0, (int) n);
    writer.reset();
    L ("Wrote WAV: " + wavFile.getFullPathName() + " (" + juce::String ((juce::int64) wavFile.getSize()) + " bytes)");

    L ("=== TestRunner end ===");
    g_logger.reset();
    return nonSilent ? 0 : 1;
}
