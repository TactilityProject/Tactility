#pragma once

#include <string>
#include <vector>

struct Device;
struct PixelBuffer;

namespace tt::app::boot {

/**
 * Draws boot screens directly to the display, for use while LVGL isn't running.
 * Content is drawn in the orientation from the display settings.
 */
class BootScreen {

    Device* display = nullptr;
    int panelWidth = 0;
    int panelHeight = 0;
    /** 0, 1, 2 or 3 for a rotation of 0, 90, 180 or 270 degrees */
    int rotation = 0;
    PixelBuffer* target = nullptr;
    /** Second hardware frame buffer, kept in sync with target */
    PixelBuffer* secondFrameBuffer = nullptr;
    int bandHeight = 0;

    int logicalWidth() const { return (rotation % 2 == 0) ? panelWidth : panelHeight; }
    int logicalHeight() const { return (rotation % 2 == 0) ? panelHeight : panelWidth; }

public:

    ~BootScreen();

    /** @return false when there is no usable display */
    bool begin();

    /** Releases the display, so LVGL can take over. */
    void end();

    /** @return the smallest logical display dimension, or 0 when there is no display */
    int getSmallestDimension() const;

    /**
     * Shows a logo with text lines below it, centered on a black background.
     * @param[in] logoPath path to a PNG file, or empty for no logo
     * @param[in] lines text lines, wrapped when they're too long for the display
     */
    void show(const std::string& logoPath, const std::vector<std::string>& lines);
};

/**
 * @param[in] action what happens on input, e.g. "reboot"
 * @return "Press any key to <action>" when a keyboard is available, otherwise
 * "Touch the screen to <action>" when a pointer is available, otherwise an empty string
 */
std::string getInputPrompt(const std::string& action);

/** Blocks until any key is pressed on a keyboard or the screen is touched. */
void waitForInput();

}
