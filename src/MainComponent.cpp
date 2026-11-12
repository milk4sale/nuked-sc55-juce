#include "MainComponent.h"
#include "PreferencesDialog.h"

namespace
{
    const juce::String kPropsRomDir   = "romDirectory";
    const juce::String kPropsModel    = "modelIndex";
    const juce::String kPropsSc55Model= "lastSc55ModelIndex";
    const juce::String kPropsMidiIn   = "midiInputIdentifier";
    const juce::String kPropsLastFile = "lastMidiFile";
    const juce::String kPropsGain     = "masterGain";
    const juce::String kPropsAudio    = "audioDeviceState";
    constexpr double   kDefaultOutputRate = 96000.0; // above the core's 66207 Hz: upsample, never decimate

    static std::unique_ptr<juce::FileLogger> gLog;
    static void initLogger()
    {
        if (gLog != nullptr) return;
        auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
        auto logFile = exeDir.getChildFile ("nuked-sc55-juce.log");
        logFile.deleteFile();
        gLog = std::make_unique<juce::FileLogger> (logFile, "Nuked SC-55/8820 JUCE log", 0);
    }
    static void L (const juce::String& s) { if (gLog) gLog->logMessage (s); }

    enum MenuId : int
    {
        FileOpenMidi = 1000,
        FileExit,
        EditPreferences = 2000,
        HelpAbout = 3000,
    };
}

MainComponent::MainComponent()
{
    initLogger();
    L ("=== App starting ===");

    juce::PropertiesFile::Options opts;
    opts.applicationName     = "NukedSC55JUCE";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
   #if JUCE_LINUX || JUCE_BSD
    opts.folderName          = ".config/NukedSC55JUCE"; // XDG-style, not ~/NukedSC55JUCE
   #else
    opts.folderName          = "NukedSC55JUCE";
   #endif
    props.setStorageParameters (opts);

    auto* user = props.getUserSettings();
    currentModelIndex = juce::jlimit (0, emuModelCount() - 1, user->getIntValue (kPropsModel, 0));
    lastSc55ModelIndex = user->getIntValue (kPropsSc55Model, 0);
    if (! emuModelUsesNuked (emuModelFromIndex (lastSc55ModelIndex))) lastSc55ModelIndex = 0;
    if (emuModelUsesNuked (emuModelFromIndex (currentModelIndex))) lastSc55ModelIndex = currentModelIndex;
    currentRomDir     = juce::File (user->getValue (kPropsRomDir, juce::String()));
    const auto savedGain  = (float) user->getDoubleValue (kPropsGain, 0.8);
    const auto savedMidi  = user->getValue (kPropsMidiIn, juce::String());
    const auto savedFile  = user->getValue (kPropsLastFile, juce::String());

    setSize (980, 264);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (menuBar);

    addAndMakeVisible (nowPlayingLabel);
    nowPlayingLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    nowPlayingLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nowPlayingLabel.setJustificationType (juce::Justification::centredLeft);
    nowPlayingLabel.setText ("(no file loaded)", juce::dontSendNotification);

    addAndMakeVisible (seekBar);
    seekBar.attachToPlayer (&midiFilePlayer);

    addAndMakeVisible (controls);
    controls.onPlayPause   = [this] { doPlayPause(); };
    controls.onStop        = [this] { doStop(); };
    controls.onGainChanged = [this] (float g)
    {
        engine.setMasterGain (g);
        props.getUserSettings()->setValue (kPropsGain, (double) g);
    };
    controls.setGain (savedGain);
    engine.setMasterGain (savedGain);

    addAndMakeVisible (levelMeterLabel);
    levelMeterLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    levelMeterLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (130, 130, 140));
    levelMeterLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (levelMeter);
    levelMeter.setPeakSource ([this] { return engine.getPeakLevel(); });

    addAndMakeVisible (modelSwitchLabel);
    modelSwitchLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    modelSwitchLabel.setColour (juce::Label::textColourId, juce::Colour::fromRGB (130, 130, 140));

    for (auto* b : { &sc55Button, &sc8820Button })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (false); // state follows the loaded model, see syncModelSwitch()
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour::fromRGB (70, 120, 200));
    }
    sc55Button.setConnectedEdges (juce::Button::ConnectedOnRight);
    sc8820Button.setConnectedEdges (juce::Button::ConnectedOnLeft);
    sc55Button.setTooltip ("Nuked-SC55 core. Pick the exact SC-55 variant in Edit > Preferences.");
    sc8820Button.setTooltip ("88emu core. Needs your own SC-8820 ROM dumps in the ROM folder.");
    sc55Button.onClick   = [this] { switchFamily (false); };
    sc8820Button.onClick = [this] { switchFamily (true); };
    syncModelSwitch();

    addAndMakeVisible (statusLabel);
    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);

    // MIDI in → engine
    midiFilePlayer.setMessageSink ([this] (const juce::MidiMessage& m)
    {
        engine.postMidiMessage (m);
    });

    // Restore the saved audio device setup; on first run, ask for 96 kHz so the
    // emulator's 66207 Hz output is only ever upsampled (no band-limiting).
    {
        std::unique_ptr<juce::XmlElement> savedAudio (user->getXmlValue (kPropsAudio));
        setAudioChannels (0, 2, savedAudio.get());
        if (savedAudio == nullptr)
        {
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.sampleRate = kDefaultOutputRate;
            const auto err = deviceManager.setAudioDeviceSetup (setup, true);
            if (err.isNotEmpty()) L ("Could not set default output rate: " + err);
        }
        deviceManager.addChangeListener (this);
    }

    // Restore MIDI input device (if previously selected and still present)
    if (savedMidi.isNotEmpty()) openMidiInput (savedMidi);

    // Restore last opened MIDI file
    if (savedFile.isNotEmpty())
    {
        juce::File f (savedFile);
        if (f.existsAsFile())
        {
            juce::String err;
            if (midiFilePlayer.loadFile (f, err))
            {
                lastMidiFile = f;
                nowPlayingLabel.setText (midiFilePlayer.getDescription(), juce::dontSendNotification);
                seekBar.attachToPlayer (&midiFilePlayer); // re-attach to refresh length
            }
            else
            {
                L ("Restore last file failed: " + err);
            }
        }
    }

    autoLoadRomsIfPossible();
    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    deviceManager.removeChangeListener (this);
    midiFilePlayer.stop();
    closeMidiInput();
    engine.stop();
    shutdownAudio();
}

void MainComponent::prepareToPlay (int samplesPerBlock, double sampleRate)
{
    outputSampleRate = sampleRate;
    resampleRatio = engine.isReady()
        ? (double) engine.getNativeSampleRate() / outputSampleRate
        : 1.0;
    interpL.reset();
    interpR.reset();

    const int needed = (int) std::ceil (samplesPerBlock * resampleRatio) + 16;
    scratchL.allocate ((size_t) needed, true);
    scratchR.allocate ((size_t) needed, true);
    scratchCapacity = needed;

    // Leftover buffer holds resampler "carry" between blocks — usually <8 samples.
    leftoverL.allocate (64, true);
    leftoverR.allocate (64, true);
    leftoverCount = 0;

    L ("prepareToPlay: samplesPerBlock=" + juce::String (samplesPerBlock)
       + " sampleRate=" + juce::String (sampleRate)
       + " engine.ready=" + juce::String ((int) engine.isReady())
       + " ratio=" + juce::String (resampleRatio, 4));
}

void MainComponent::releaseResources()
{
    scratchCapacity = 0;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    if (! engine.isReady() || ! engine.isRunning()) return;

    const int numOut = info.numSamples;
    const int needed = (int) std::ceil (numOut * resampleRatio) + 4;
    if (needed > scratchCapacity)
    {
        scratchL.allocate ((size_t) needed, true);
        scratchR.allocate ((size_t) needed, true);
        scratchCapacity = needed;
    }

    // Build the resampler input as [leftover from last block | fresh from engine].
    // Then ask process() to produce numOut and track how many inputs it consumed,
    // so the next block continues seamlessly.
    if (leftoverCount > 0)
    {
        std::memcpy (scratchL.getData(), leftoverL.getData(), (size_t) leftoverCount * sizeof (float));
        std::memcpy (scratchR.getData(), leftoverR.getData(), (size_t) leftoverCount * sizeof (float));
    }
    const int toFetch = juce::jmax (0, needed - leftoverCount);
    engine.pullSamples (scratchL.getData() + leftoverCount,
                        scratchR.getData() + leftoverCount,
                        toFetch);
    const int totalIn = leftoverCount + toFetch;

    auto* outL = info.buffer->getWritePointer (0, info.startSample);
    auto* outR = info.buffer->getNumChannels() > 1
                 ? info.buffer->getWritePointer (1, info.startSample)
                 : outL;

    const int consumedL = interpL.process (resampleRatio, scratchL.getData(), outL, numOut);
    const int consumedR = interpR.process (resampleRatio, scratchR.getData(), outR, numOut);
    const int consumed  = juce::jmin (consumedL, consumedR);

    const int newLeftover = juce::jmax (0, juce::jmin (totalIn - consumed, 64));
    if (newLeftover > 0)
    {
        std::memcpy (leftoverL.getData(), scratchL.getData() + consumed,
                     (size_t) newLeftover * sizeof (float));
        std::memcpy (leftoverR.getData(), scratchR.getData() + consumed,
                     (size_t) newLeftover * sizeof (float));
    }
    leftoverCount = newLeftover;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (28, 28, 32));

    // Subtle separator under the now-playing block
    g.setColour (juce::Colour::fromRGB (60, 60, 64));
    auto nb = nowPlayingLabel.getBounds();
    g.fillRect (nb.getX(), nb.getBottom() + 4, nb.getWidth(), 1);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (! dragHover) return;
    g.setColour (juce::Colour::fromRGB (90, 160, 255).withAlpha (0.15f));
    g.fillAll();
    g.setColour (juce::Colour::fromRGB (90, 160, 255));
    g.drawRect (getLocalBounds(), 3);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Drop MIDI file to play", getLocalBounds(), juce::Justification::centred);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    menuBar.setBounds (area.removeFromTop (24));

    area.reduce (16, 12);

    nowPlayingLabel.setBounds (area.removeFromTop (32));
    area.removeFromTop (16);

    seekBar.setBounds (area.removeFromTop (48));
    area.removeFromTop (12);

    controls.setBounds (area.removeFromTop (44));
    area.removeFromTop (8);

    {
        auto meterRow = area.removeFromTop (24);
        auto switchArea = meterRow.removeFromLeft (220);
        modelSwitchLabel.setBounds (switchArea.removeFromLeft (52));
        sc55Button.setBounds   (switchArea.removeFromLeft (80));
        sc8820Button.setBounds (switchArea.removeFromLeft (80));
        meterRow = meterRow.withSizeKeepingCentre (meterRow.getWidth(), 14);
        levelMeter.setBounds      (meterRow.removeFromRight (220));
        meterRow.removeFromRight (4);
        levelMeterLabel.setBounds (meterRow.removeFromRight (32));
    }
    area.removeFromTop (6);

    statusLabel.setBounds (area.removeFromBottom (20));
}

//==============================================================================
// Menu
//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu m;
    if (topLevelMenuIndex == 0) // File
    {
        m.addItem (FileOpenMidi, "Open MIDI...");
        m.addSeparator();
        m.addItem (FileExit, "Exit");
    }
    else if (topLevelMenuIndex == 1) // Edit
    {
        m.addItem (EditPreferences, "Preferences...");
    }
    else if (topLevelMenuIndex == 2) // Help
    {
        m.addItem (HelpAbout, "About");
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case FileOpenMidi:     pickMidiFile(); break;
        case FileExit:         juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case EditPreferences:  openPreferences(); break;
        case HelpAbout:
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::InfoIcon,
                "Nuked SC-55/8820 JUCE",
                "JUCE host for the Nuked-SC55 emulator, with SC-8820 support via 88emu.\n"
                "Based on github.com/depixeled-chris/nuked-sc55-juce");
            break;
    }
}

//==============================================================================
void MainComponent::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    engine.postMidiMessage (m);
}

void MainComponent::timerCallback()
{
    juce::String s;
    if (! engine.isReady())
    {
        s = statusOverride.isNotEmpty() ? statusOverride
                                        : juce::String ("Engine: not ready - load ROMs via Preferences.");
    }
    else
    {
        s << "Engine: " << NukedEngine::displayName (engine.getCurrentModel()) << " @ "
          << engine.getNativeSampleRate() << " Hz | output "
          << (int) outputSampleRate       << " Hz | peak "
          << juce::String (engine.getPeakLevel(), 3)
          << " | fifo " << engine.getAudioFifoUsedPercent() << "%";
    }
    statusLabel.setText (s, juce::dontSendNotification);

    const bool fileReady = engine.isReady() && midiFilePlayer.getLengthSeconds() > 0.0;
    controls.setEnabledTransport (fileReady);
    controls.setPlayingState (midiFilePlayer.isPlaying());
}

//==============================================================================
void MainComponent::pickMidiFile()
{
    auto initial = lastMidiFile.existsAsFile()
                   ? lastMidiFile.getParentDirectory()
                   : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    // Linux file dialogs match patterns case-sensitively, so list both cases
    // (lots of MIDI files in the wild are named FOO.MID).
    auto chooser = std::make_shared<juce::FileChooser> (
        "Open MIDI file", initial,
        "*.mid;*.MID;*.Mid;*.midi;*.MIDI;*.kar;*.KAR;*.smf;*.SMF");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;

        L ("pickMidiFile selected: " + f.getFullPathName());
        loadMidiFile (f);
    });
}

bool MainComponent::loadMidiFile (const juce::File& f)
{
    juce::String err;
    if (! midiFilePlayer.loadFile (f, err))
    {
        L ("loadFile failed: " + err);
        statusLabel.setText ("Failed to load: " + err, juce::dontSendNotification);
        return false;
    }
    lastMidiFile = f;
    props.getUserSettings()->setValue (kPropsLastFile, f.getFullPathName());
    nowPlayingLabel.setText (midiFilePlayer.getDescription(), juce::dontSendNotification);
    seekBar.attachToPlayer (&midiFilePlayer); // refresh length
    L ("loadFile OK: " + midiFilePlayer.getDescription());
    return true;
}

bool MainComponent::isMidiFile (const juce::File& f)
{
    return f.hasFileExtension ("mid;midi;kar;smf"); // case-insensitive
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isMidiFile (juce::File (path)))
            return true;
    return false;
}

void MainComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void MainComponent::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();

    for (const auto& path : files)
    {
        const juce::File f (path);
        if (! isMidiFile (f) || ! f.existsAsFile())
            continue;

        L ("file dropped: " + f.getFullPathName());
        const bool wasPlaying = midiFilePlayer.isPlaying();
        if (wasPlaying) midiFilePlayer.stop();
        if (loadMidiFile (f))
            midiFilePlayer.play(); // dropping a song = "play this"
        return;
    }
}

void MainComponent::doPlayPause()
{
    if (midiFilePlayer.isPlaying())
    {
        L ("pause");
        midiFilePlayer.pause();
    }
    else
    {
        L ("play (from " + juce::String (midiFilePlayer.getPositionSeconds(), 2) + "s)");
        midiFilePlayer.play();
    }
}

void MainComponent::doStop()
{
    L ("stop");
    midiFilePlayer.stop();
}

void MainComponent::openPreferences()
{
    PreferencesDialog::Initial init {
        deviceManager,
        NukedEngine::getModelDisplayNames(),
        currentModelIndex,
        currentRomDir,
        currentMidiInIdentifier
    };

    PreferencesDialog::launch (
        this, init,
        [this] (int modelIdx) { selectModel (modelIdx); },
        [this] (const juce::File& dir)
        {
            currentRomDir = dir;
            juce::String err;
            props.getUserSettings()->setValue (kPropsRomDir, dir.getFullPathName());
            if (! loadRomsForCurrentSettings (err))
                showRomError (err);
        },
        [this] (const juce::String& id)
        {
            if (id.isEmpty())
            {
                closeMidiInput();
                props.getUserSettings()->setValue (kPropsMidiIn, juce::String());
            }
            else
            {
                openMidiInput (id);
            }
        });
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Persist output device / sample rate / buffer size picked in Preferences.
    if (auto xml = deviceManager.createStateXml())
        props.getUserSettings()->setValue (kPropsAudio, xml.get());
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        L ("Audio device: " + dev->getName() + " @ " + juce::String (dev->getCurrentSampleRate())
           + " Hz, " + juce::String (dev->getCurrentBufferSizeSamples()) + " samples");
}

void MainComponent::autoLoadRomsIfPossible()
{
    if (! currentRomDir.isDirectory()) return;
    juce::String err;
    if (! loadRomsForCurrentSettings (err))
    {
        L ("Auto-load ROMs failed: " + err);
        statusOverride = "ROMs missing for " + NukedEngine::displayName (NukedEngine::modelFromIndex (currentModelIndex))
                       + ". Pick another model or add the ROMs.";
    }
}

void MainComponent::selectModel (int modelIdx)
{
    currentModelIndex = juce::jlimit (0, emuModelCount() - 1, modelIdx);
    auto* user = props.getUserSettings();
    user->setValue (kPropsModel, currentModelIndex);
    if (emuModelUsesNuked (NukedEngine::modelFromIndex (currentModelIndex)))
    {
        lastSc55ModelIndex = currentModelIndex;
        user->setValue (kPropsSc55Model, lastSc55ModelIndex);
    }
    syncModelSwitch();

    juce::String err;
    if (! loadRomsForCurrentSettings (err))
        showRomError (err);
}

void MainComponent::switchFamily (bool sc8820)
{
    const int target = sc8820 ? NukedEngine::indexFromModel (NukedEngine::Model::SC8820) : lastSc55ModelIndex;
    if (target == currentModelIndex && engine.isReady())
        return;
    selectModel (target);
}

void MainComponent::syncModelSwitch()
{
    const bool is8820 = NukedEngine::modelFromIndex (currentModelIndex) == NukedEngine::Model::SC8820;
    sc55Button.setToggleState (! is8820, juce::dontSendNotification);
    sc8820Button.setToggleState (is8820, juce::dontSendNotification);
}

void MainComponent::showRomError (const juce::String& err)
{
    const auto name = NukedEngine::displayName (NukedEngine::modelFromIndex (currentModelIndex));
    statusOverride = "ROMs missing for " + name + ". See the message for which files.";
    L ("ROM load failed: " + err);
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::WarningIcon,
        "Can't start " + name,
        err + "\n\nROM folder: "
            + (currentRomDir.isDirectory() ? currentRomDir.getFullPathName() : juce::String ("(not set)"))
            + "\nSet it in Edit > Preferences. ROMs are not included with this app; use dumps of your own hardware.");
}

bool MainComponent::loadRomsForCurrentSettings (juce::String& err)
{
    midiFilePlayer.stop();
    engine.stop();

    const auto model = NukedEngine::modelFromIndex (currentModelIndex);
    L ("Loading ROMs: " + currentRomDir.getFullPathName() + ", model=" + juce::String (currentModelIndex));

    statusOverride.clear();
    if (! engine.loadRoms (currentRomDir, model, err))
    {
        L ("loadRoms failed: " + err);
        return false;
    }

    engine.start();
    resampleRatio = (double) engine.getNativeSampleRate() / juce::jmax (1.0, outputSampleRate);
    interpL.reset();
    interpR.reset();
    L ("loadRoms OK, native=" + juce::String (engine.getNativeSampleRate()));
    return true;
}

void MainComponent::openMidiInput (const juce::String& identifier)
{
    closeMidiInput();
    currentMidiIn = juce::MidiInput::openDevice (identifier, this);
    if (currentMidiIn != nullptr)
    {
        currentMidiIn->start();
        currentMidiInIdentifier = identifier;
        props.getUserSettings()->setValue (kPropsMidiIn, identifier);
    }
}

void MainComponent::closeMidiInput()
{
    if (currentMidiIn != nullptr)
    {
        currentMidiIn->stop();
        currentMidiIn.reset();
    }
    currentMidiInIdentifier.clear();
}
