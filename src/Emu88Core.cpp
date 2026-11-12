// EmulatorCore backend for 88emu (The Usual Suspects, part of gearmulator, GPL-3.0).
// Used for the SC-8820: SH-2 CPU running the original firmware, XP6 + LSP sound chips.
//
// 88emu finds ROMs by content (MD5 + size) in the ROM folder and its subfolders, so the
// SC-8820 dumps can sit in their own subfolder, e.g. <rom folder>/SC-8820/.

#include "EmulatorCore.h"

#include "c_interface.h"

#include <vector>

namespace
{
    emu88_device_id toEmu88Device (EmuModel m)
    {
        switch (m)
        {
            case EmuModel::SC8820: return EMU88_DEVICE_SC8820;
            default:               return -1;
        }
    }

    class Emu88Core final : public EmulatorCore
    {
    public:
        ~Emu88Core() override { emu88_free_context (ctx); }

        bool init (EmuModel model, const std::filesystem::path& romDir, std::string& error)
        {
            const auto device = toEmu88Device (model);
            if (device < 0)
            {
                error = std::string ("88emu: no device for ") + emuModelDisplayName (model);
                return false;
            }

            // The search path is process-wide; point it at the user's ROM folder only.
            if (emu88_set_rom_path (romDir.string().c_str()) != EMU88_RC_OK)
            {
                error = "88emu: cannot read ROM folder " + romDir.string();
                return false;
            }

            if (! emu88_is_device_available (device))
            {
                error = std::string (emuModelDisplayName (model)) + ": ROMs not found in '"
                      + romDir.string() + "' (subfolders are searched too).\n" + describeRoms (device);
                return false;
            }

            ctx = emu88_create_context();
            if (ctx == nullptr
                || emu88_select_device (ctx, device) != EMU88_RC_OK)
            {
                error = "88emu: could not create the device context.";
                return false;
            }

            // Default boot: factory init + skip the power-on intro, so MIDI plays at once.
            const auto rc = emu88_open_synth (ctx);
            if (rc != EMU88_RC_OK)
            {
                error = rc == EMU88_RC_MISSING_ROMS
                      ? "88emu: ROMs incomplete.\n" + describeRoms (device)
                      : "88emu: device failed to start (code " + std::to_string ((int) rc) + ").";
                return false;
            }

            rate = (int) emu88_get_actual_stereo_output_samplerate (ctx);
            if (rate <= 0)
            {
                error = "88emu: device reported no sample rate.";
                return false;
            }
            return true;
        }

        int nativeSampleRate() const override { return rate; }

        void postMidi (const uint8_t* data, size_t size) override
        {
            emu88_parse_stream (ctx, data, (uint32_t) size);
        }

        void postSystemReset (bool gs) override
        {
            static constexpr uint8_t kGsReset[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
            static constexpr uint8_t kGmReset[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
            if (gs) emu88_play_sysex (ctx, kGsReset, (uint32_t) sizeof (kGsReset));
            else    emu88_play_sysex (ctx, kGmReset, (uint32_t) sizeof (kGmReset));
        }

        void run (FrameSink& sink, int frames) override
        {
            if ((int) buffer.size() < frames * 2)
                buffer.resize ((size_t) frames * 2);
            emu88_render_float (ctx, buffer.data(), (uint32_t) frames);
            for (int i = 0; i < frames; ++i)
                sink.pushFrame (buffer[(size_t) i * 2], buffer[(size_t) i * 2 + 1]);
        }

    private:
        static std::string describeRoms (emu88_device_id device)
        {
            std::string text (2048, '\0');
            const size_t len = emu88_describe_device_roms (device, text.data(), text.size());
            if (len >= text.size())
            {
                text.assign (len + 1, '\0');
                emu88_describe_device_roms (device, text.data(), text.size());
            }
            text.resize (std::min (len, text.size()));
            return text;
        }

        emu88_context ctx = nullptr;
        int rate = 0;
        std::vector<float> buffer;
    };
}

std::unique_ptr<EmulatorCore> createEmu88Core (EmuModel model, const std::filesystem::path& romDir, std::string& error)
{
    auto core = std::make_unique<Emu88Core>();
    if (! core->init (model, romDir, error))
        return nullptr;
    return core;
}
