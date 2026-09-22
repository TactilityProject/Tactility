#pragma once

struct Device;

/** Touch-to-exit gesture: a fallback for a keyboard-less terminal start, disabled outright when a
 * keyboard was already present at start so a touch can't close the terminal by accident. */
class TouchInput {
public:
    explicit TouchInput(bool keyboardPresent);
    ~TouchInput();

    /** @return true if the gesture just fired */
    bool touched() const;

private:
    Device* device_ = nullptr;
};
