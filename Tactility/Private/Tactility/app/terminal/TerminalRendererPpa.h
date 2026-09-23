#pragma once

#include <Tactility/app/terminal/TerminalRenderer.h>

/**
 * Landscape terminal on a portrait panel (e.g. Tab5's 720x1280 native panel), rotated onto the
 * panel by the PPA (Pixel Processing Accelerator) hardware, the same way the Doom app does it.
 */
class TerminalRendererPpa : public TerminalRenderer {
public:
    ~TerminalRendererPpa() override;

    /** True on targets with a PPA unit this renderer can use (see SOC_PPA_SUPPORTED). */
    static bool isSupported();

    bool begin(Device* display) override;
    void end() override;

protected:
    void present() override;

private:
    // ppa_client_handle_t, kept opaque here so driver/ppa.h stays confined to the .cpp.
    void* ppaClient = nullptr;

    // Output side when the panel has no hw double buffer: one PSRAM buffer rotated into and
    // pushed manually.
    uint16_t* rotatedBuffer = nullptr;
};
