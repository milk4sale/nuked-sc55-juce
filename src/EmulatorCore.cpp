#include "EmulatorCore.h"

#include <array>

namespace
{
    struct ModelInfo
    {
        EmuModel    model;
        const char* name;
        bool        nuked;
    };

    // Order = settings index. Append only, so saved settings keep their meaning.
    constexpr std::array<ModelInfo, 10> kModels {{
        { EmuModel::SC55mk2,  "Roland SC-55mk2 (v1.01)", true },
        { EmuModel::SC55,     "Roland SC-55 (v1.21)",    true },
        { EmuModel::SC55st,   "Roland SC-55st (v1.01)",  true },
        { EmuModel::CM300,    "Roland CM-300 / SCC-1",   true },
        { EmuModel::SCB55,    "Roland SCB-55",           true },
        { EmuModel::RLP3237,  "Roland RLP-3237",         true },
        { EmuModel::SC155,    "Roland SC-155",           true },
        { EmuModel::SC155mk2, "Roland SC-155mk2",        true },
        { EmuModel::JV880,    "Roland JV-880",           true },
        { EmuModel::SC8820,   "Roland SC-8820 (88emu)",  false },
    }};
}

int emuModelCount() { return (int) kModels.size(); }

EmuModel emuModelFromIndex (int index)
{
    if (index < 0 || index >= (int) kModels.size())
        return EmuModel::SC55mk2;
    return kModels[(size_t) index].model;
}

int emuModelToIndex (EmuModel model)
{
    for (size_t i = 0; i < kModels.size(); ++i)
        if (kModels[i].model == model)
            return (int) i;
    return 0;
}

const char* emuModelDisplayName (EmuModel model) { return kModels[(size_t) emuModelToIndex (model)].name; }
bool        emuModelUsesNuked (EmuModel model)   { return kModels[(size_t) emuModelToIndex (model)].nuked; }

bool emuModelIsBuilt (EmuModel model)
{
    if (emuModelUsesNuked (model))
        return true;
   #if NUKED_HAVE_88EMU
    return true;
   #else
    return false;
   #endif
}

std::unique_ptr<EmulatorCore> createEmulatorCore (EmuModel model,
                                                  const std::filesystem::path& romDir,
                                                  std::string& error)
{
    std::error_code ec;
    if (! std::filesystem::is_directory (romDir, ec))
    {
        error = "ROM folder does not exist: " + romDir.string();
        return nullptr;
    }

    if (emuModelUsesNuked (model))
        return createNukedCore (model, romDir, error);

   #if NUKED_HAVE_88EMU
    return createEmu88Core (model, romDir, error);
   #else
    error = std::string (emuModelDisplayName (model))
          + " needs the 88emu core, which this build left out. Rebuild with -DNUKED_WITH_88EMU=ON.";
    return nullptr;
   #endif
}
