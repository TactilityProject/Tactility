#pragma once

#include <Tactility/app/terminal/TerminalRenderer.h>

/**
 * Draws the terminal directly in the display's native orientation - no rotation, so no PPA (or
 * other rotation hardware) needed. Works on any display; see TerminalRendererPpa for the
 * PPA-accelerated alternative used on panels (e.g. Tab5) that are natively portrait but want a
 * landscape terminal.
 */
class TerminalRendererGeneric : public TerminalRenderer {
public:
    ~TerminalRendererGeneric() override;

    bool begin(Device* display) override;
    void end() override;

protected:
    void present() override;
};
