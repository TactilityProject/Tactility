#pragma once

#include <Tactility/app/terminal/TerminalRenderer.h>

/**
 * Draws the terminal directly in the display's native orientation: no rotation, so no PPA needed.
 * See TerminalRendererPpa for the PPA-accelerated alternative for a portrait panel that wants a
 * landscape terminal.
 */
class TerminalRendererGeneric : public TerminalRenderer {
public:
    ~TerminalRendererGeneric() override;

    bool begin(Device* display) override;
    void end() override;

protected:
    void present(int yStart, int yEnd) override;
};
