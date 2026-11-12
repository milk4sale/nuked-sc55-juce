// EmulatorCore backend for Nuked-SC55 (SC-55 family, SC-155, JV-880).

#include "EmulatorCore.h"

#include "backend/emu.h"
#include "backend/rom.h"
#include "backend/rom_io.h"
#include "backend/audio.h"
#include "backend/pcm.h"

namespace
{
    constexpr float kSampleScale = 1.0f / 536870912.0f; // matches Normalize() in Nuked's audio.h

    Romset toNukedRomset (EmuModel m)
    {
        switch (m)
        {
            case EmuModel::SC55mk2:  return Romset::MK2;
            case EmuModel::SC55:     return Romset::MK1;
            case EmuModel::SC55st:   return Romset::ST;
            case EmuModel::CM300:    return Romset::CM300;
            case EmuModel::SCB55:    return Romset::SCB55;
            case EmuModel::RLP3237:  return Romset::RLP3237;
            case EmuModel::SC155:    return Romset::SC155;
            case EmuModel::SC155mk2: return Romset::SC155MK2;
            case EmuModel::JV880:    return Romset::JV880;
            case EmuModel::SC8820:   break;
        }
        return Romset::MK2;
    }

    class NukedCore final : public EmulatorCore
    {
    public:
        bool init (EmuModel model, const std::filesystem::path& romDir, std::string& error)
        {
            romset = toNukedRomset (model);

            SetRomsetFilenames (info, romDir, romset, ROMLOCATION_ALL);
            RomLoadStatusSet loadStatus {};
            LoadRomset (info, &loadStatus);

            // Every REQUIRED slot must have data. IsCompleteRomset only checks that paths are set,
            // which SetRomsetFilenames always does.
            std::string missing;
            for (size_t i = 0; i < ROMLOCATION_COUNT; ++i)
            {
                const auto loc = (RomLocation) i;
                if (! IsRequiredRom (romset, loc) || ! info.rom_data[i].empty())
                    continue;
                if (! missing.empty()) missing += ", ";
                missing += ToCString (loc);
            }
            if (! missing.empty())
            {
                error = "Required ROM file(s) not found in '" + romDir.string() + "': " + missing
                      + ". Did you pick a subfolder by mistake?";
                return false;
            }

            emu = std::make_unique<Emulator>();
            if (! emu->Init ({ /* lcd_backend */ nullptr, /* nvram_filename */ {} }))
            {
                error = "Nuked-SC55: Emulator::Init failed.";
                return false;
            }
            if (! emu->LoadRoms (romset, info))
            {
                error = "Nuked-SC55: Emulator::LoadRoms failed.";
                return false;
            }

            emu->Reset();
            emu->GetPCM().enable_oversampling = true;
            rate = (int) PCM_GetOutputFrequency (emu->GetPCM()); // 64000 or 66207 Hz
            emu->SetSampleCallback (&NukedCore::onSample, this);
            return true;
        }

        int nativeSampleRate() const override { return rate; }

        void postMidi (const uint8_t* data, size_t size) override
        {
            for (size_t i = 0; i < size; ++i)
                emu->PostMIDI (data[i]);
        }

        void postSystemReset (bool gs) override
        {
            emu->PostSystemReset (gs ? EMU_SystemReset::GS_RESET : EMU_SystemReset::GM_RESET);
        }

        void run (FrameSink& sink, int frames) override
        {
            // Step() produces a sample only every few calls; step until enough have come out.
            currentSink = &sink;
            produced = 0;
            while (produced < frames)
                emu->Step();
            currentSink = nullptr;
        }

    private:
        static void onSample (void* userdata, const AudioFrame<int32_t>& f)
        {
            auto* self = static_cast<NukedCore*> (userdata);
            ++self->produced;
            if (self->currentSink != nullptr)
                self->currentSink->pushFrame ((float) f.left * kSampleScale, (float) f.right * kSampleScale);
        }

        // info must outlive emu: it owns the ROM buffers the emulator references.
        RomsetInfo info;
        std::unique_ptr<Emulator> emu;
        Romset romset = Romset::MK2;
        int rate = 0;
        FrameSink* currentSink = nullptr;
        int produced = 0;
    };
}

std::unique_ptr<EmulatorCore> createNukedCore (EmuModel model, const std::filesystem::path& romDir, std::string& error)
{
    auto core = std::make_unique<NukedCore>();
    if (! core->init (model, romDir, error))
        return nullptr;
    return core;
}
