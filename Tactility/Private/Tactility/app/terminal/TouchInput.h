#pragma once

struct Device;

/** Touch-to-exit gesture: a fallback for a keyboard-less terminal, disabled while a keyboard is
 * present so a touch can't close the terminal by accident. Re-evaluated on every call. */
class TouchInput {
public:
    TouchInput();
    ~TouchInput();

    /** @return true if the gesture just fired */
    bool touched(bool keyboardPresent);

private:
    Device* device_ = nullptr;
    bool probed_ = false;
};
