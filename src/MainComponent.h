#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "NukedEngine.h"
#include "MidiFilePlayer.h"
#include "SeekBar.h"
#include "PlayerControls.h"
#include "LevelMeter.h"

class MainComponent : public juce::AudioAppComponent,
                      public juce::MenuBarModel,
                      public juce::FileDragAndDropTarget,
                      private juce::MidiInputCallback,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // juce::AudioAppComponent
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // juce::Component
    void paint (juce::Graphics& g) override;
    void resized() override;
    void paintOverChildren (juce::Graphics& g) override;

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;

    void pickMidiFile();
    bool loadMidiFile (const juce::File& f);
    static bool isMidiFile (const juce::File& f);
    void doPlayPause();
    void doStop();
    void openPreferences();
    void autoLoadRomsIfPossible();
    bool loadRomsForCurrentSettings (juce::String& errorOut);
    void openMidiInput (const juce::String& identifier);
    void closeMidiInput();
    void onPlayerStateChange (const juce::MidiMessage& m);

    // Engine + transport core
    NukedEngine engine;
    MidiFilePlayer midiFilePlayer;

    // Resampler state (Nuked native rate → audio device rate)
    juce::LagrangeInterpolator interpL, interpR;
    double outputSampleRate = 0.0;
    double resampleRatio    = 1.0;
    juce::HeapBlock<float> scratchL, scratchR;
    int scratchCapacity = 0;
    juce::HeapBlock<float> leftoverL, leftoverR;
    int leftoverCount = 0;

    // UI
    juce::MenuBarComponent menuBar { this };
    juce::Label   nowPlayingLabel;
    juce::Label   statusLabel;
    SeekBar       seekBar;
    PlayerControls controls;
    juce::Label   levelMeterLabel { {}, "OUT" };
    LevelMeter    levelMeter;

    // Subsystems
    std::unique_ptr<juce::MidiInput> currentMidiIn;
    juce::String                   currentMidiInIdentifier;
    juce::File   currentRomDir;
    int          currentModelIndex = 0;
    juce::File   lastMidiFile;
    juce::ApplicationProperties props;
    bool dragHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
