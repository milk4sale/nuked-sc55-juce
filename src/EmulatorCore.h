#pragma once

// Model abstraction: one interface over the two emulation cores this app can drive.
//
//   - Nuked-SC55 (external/Nuked-SC55): SC-55 family, SC-155, JV-880.
//   - 88emu      (external/gearmulator): SC-8820. Optional, see NUKED_WITH_88EMU in CMakeLists.txt.
//
// Neither core ships ROM data. Both read the user's own ROM dumps from a folder at runtime.
//
// Threading: create a core on any thread, then use it from one thread at a time.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

enum class EmuModel
{
    SC55mk2,
    SC55,
    SC55st,
    CM300,
    SCB55,
    RLP3237,
    SC155,
    SC155mk2,
    JV880,
    SC8820,
};

class FrameSink
{
public:
    virtual ~FrameSink() = default;
    // One stereo frame at the core's native rate, nominally in [-1, 1].
    virtual void pushFrame (float left, float right) = 0;
};

class EmulatorCore
{
public:
    virtual ~EmulatorCore() = default;

    virtual int  nativeSampleRate() const = 0;
    // Raw MIDI wire bytes; running status and messages split across calls are fine.
    virtual void postMidi (const uint8_t* data, size_t size) = 0;
    virtual void postSystemReset (bool gs) = 0;
    // Advances the emulation by about `frames` stereo frames, pushing each into `sink`.
    virtual void run (FrameSink& sink, int frames) = 0;
};

// Model table helpers. Indices are what the settings file stores ("modelIndex").
int          emuModelCount();
EmuModel     emuModelFromIndex (int index);
int          emuModelToIndex (EmuModel model);
const char*  emuModelDisplayName (EmuModel model);
bool         emuModelUsesNuked (EmuModel model);
// False when the model's core was left out of this build.
bool         emuModelIsBuilt (EmuModel model);

// Loads the ROMs for `model` from `romDir` and boots the core. On failure returns nullptr and
// sets `error` to a message for the user (missing files are listed by name).
std::unique_ptr<EmulatorCore> createEmulatorCore (EmuModel model,
                                                  const std::filesystem::path& romDir,
                                                  std::string& error);

// Per-core factories, used by createEmulatorCore().
std::unique_ptr<EmulatorCore> createNukedCore (EmuModel model, const std::filesystem::path& romDir, std::string& error);
std::unique_ptr<EmulatorCore> createEmu88Core (EmuModel model, const std::filesystem::path& romDir, std::string& error);
