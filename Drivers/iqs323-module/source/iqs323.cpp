// SPDX-License-Identifier: Apache-2.0
#include <drivers/iqs323.h>

#include <tactility/concurrent/event_group.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/touchbar.h>
#include <tactility/log.h>

#include <atomic>
#include <cstring>
#include <new>

#define TAG "IQS323"
#define GET_CONFIG(device) (static_cast<const Iqs323Config*>((device)->config))
#define GET_INTERNAL(device) (static_cast<Iqs323Internal*>(device_get_driver_data(device)))

// Register map and behaviour based on the IQS323 datasheet
// https://www.azoteq.com/product/iqs323/
static constexpr uint8_t REG_PRODUCT_NUMBER = 0x00; // followed by the major and minor version (very nice of them)
static constexpr uint8_t REG_REPORT = 0x10; // just a report of various things
static constexpr uint8_t REG_SENSOR_0 = 0x30;
static constexpr uint8_t REG_SENSOR_1 = 0x40;
static constexpr uint8_t REG_SENSOR_2 = 0x50;
static constexpr uint8_t REG_CHANNEL_0 = 0x60;
static constexpr uint8_t REG_CHANNEL_1 = 0x70;
static constexpr uint8_t REG_CHANNEL_2 = 0x80;
static constexpr uint8_t REG_SLIDER = 0x90;
static constexpr uint8_t REG_GESTURE = 0xA0;
static constexpr uint8_t REG_FILTER = 0xB0;
static constexpr uint8_t REG_SYSTEM_CONTROL = 0xC0;
static constexpr uint8_t REG_TIMING = 0xC1;
static constexpr uint8_t REG_GENERAL = 0xD0;

static constexpr uint16_t PRODUCT_NUMBER_RELEASE_UI = 1106;
static constexpr uint16_t PRODUCT_NUMBER_MOVEMENT_UI = 1462;
static constexpr size_t IDENTITY_WORDS = 3;

static constexpr size_t REPORT_WORDS = 9;
static constexpr size_t REPORT_STATUS = 0;
static constexpr size_t REPORT_GESTURE = 1;
static constexpr size_t REPORT_POSITION = 2;
static constexpr size_t REPORT_FIRST_CHANNEL = 3;
static constexpr size_t WORDS_PER_CHANNEL = 2; // counts, then LTA (long term average)
static constexpr uint8_t CHANNEL_COUNT = 3;

static constexpr size_t SENSOR_WORDS = 10;
static constexpr size_t CHANNEL_WORDS = 4;

static constexpr uint16_t STATUS_POWER_MODE_SHIFT = 14;
static constexpr uint16_t STATUS_POWER_MODE_MASK = 0x03;
static constexpr uint16_t STATUS_CHANNEL_PROX_FIRST_BIT = 8; // prox bit, followed by the touch bit, for each channel
static constexpr uint16_t STATUS_RESET_EVENT = 1 << 7;
static constexpr uint16_t STATUS_ATI_ERROR = 1 << 6;
static constexpr uint16_t STATUS_ATI_ACTIVE = 1 << 5;

static constexpr uint16_t GESTURE_TAP = 1 << 0;
static constexpr uint16_t GESTURE_SWIPE_POSITIVE = 1 << 1;
static constexpr uint16_t GESTURE_SWIPE_NEGATIVE = 1 << 2;
static constexpr uint16_t GESTURE_FLICK_POSITIVE = 1 << 3;
static constexpr uint16_t GESTURE_FLICK_NEGATIVE = 1 << 4;
static constexpr uint16_t GESTURE_HOLD = 1 << 5;

static constexpr uint16_t GESTURE_ENABLE_TAP = 1 << 0;
static constexpr uint16_t GESTURE_ENABLE_SWIPE = 1 << 1;
static constexpr uint16_t GESTURE_ENABLE_FLICK = 1 << 2;
static constexpr uint16_t GESTURE_ENABLE_HOLD = 1 << 3;

static constexpr uint32_t ALWAYS_AVAILABLE_CAPABILITIES =
    TOUCHBAR_CAPABILITY_POSITION |
    TOUCHBAR_CAPABILITY_PROXIMITY |
    TOUCHBAR_CAPABILITY_RAW_COUNTS;

static constexpr size_t MAX_EVENTS_PER_REPORT = CHANNEL_COUNT + 1 + 4;

static constexpr uint16_t SYSTEM_CONTROL_ACK_RESET = 1 << 0;
static constexpr uint16_t SYSTEM_CONTROL_SOFT_RESET = 1 << 1;
static constexpr uint16_t SYSTEM_CONTROL_RE_ATI = 1 << 2;
static constexpr uint16_t SYSTEM_CONTROL_RESEED = 1 << 3;
static constexpr uint16_t SYSTEM_CONTROL_POWER_MODE_SHIFT = 4;
static constexpr uint16_t SYSTEM_CONTROL_EVENT_MODE = 1 << 7;

static constexpr size_t GENERAL_EVENTS_ENABLE_WORD = 3; // low byte
static constexpr uint16_t EVENTS_ENABLE_MASK = 0x00FF;
static constexpr uint16_t EVENT_PROX = 1 << 0;
static constexpr uint16_t EVENT_TOUCH = 1 << 1;
static constexpr uint16_t EVENT_SLIDER = 1 << 2;
static constexpr uint16_t EVENT_ATI_ERROR = 1 << 6;
// The reset event is always reported, so it is not part of the mask
static constexpr uint16_t EVENTS_USED = EVENT_PROX | EVENT_TOUCH | EVENT_SLIDER | EVENT_ATI_ERROR;

static constexpr size_t SLIDER_RESOLUTION_WORD = 3;
static constexpr uint8_t INVALID_RESPONSE_BYTE = 0xEE; // returned for reads outside of a window
static constexpr uint8_t END_COMMUNICATION_COMMAND = 0xFF; // opens a window when sent outside of one
static constexpr uint8_t NONEXISTENT_REGISTER = 0xFE; // reads as 0xEE, unless the chip is locked up (datasheet appendix C)
static constexpr uint8_t POWER_MODE_MAX = IQS323_POWER_MODE_AUTO_NO_ULP;

static constexpr uint32_t WINDOW_TIMEOUT_MS = 100;
static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(50);
static constexpr uint32_t SOFT_RESET_DELAY_MS = 100;
static constexpr uint32_t RE_ATI_START_DELAY_MS = 10;
static constexpr uint32_t ATI_POLL_INTERVAL_MS = 20;
static constexpr uint32_t ATI_TIMEOUT_MS = 3000;
static constexpr uint32_t INIT_RETRY_DELAY_MS = 1000;
static constexpr uint32_t ERROR_BACKOFF_MS = 50;
static constexpr uint32_t INIT_FAILURES_BEFORE_RESET = 3;
static constexpr uint32_t MAX_ATI_RETRIES = 3;
static constexpr uint32_t EVENT_MODE_VERIFY_DELAY_MS = 10;
static constexpr uint32_t HARDWARE_RESET_DELAY_MS = 100; // time to the first RDY window is about 25 ms
static constexpr uint32_t RDY_HIGH_TIMEOUT_MS = 500;

static constexpr uint32_t MCLR_SWITCHOVER_US = 150;
static constexpr uint32_t MCLR_PULSE_US = 500; // the minimum is 250 ns

static constexpr uint32_t THREAD_STACK_SIZE = 4096;
static constexpr uint32_t EVENT_READY = 1 << 0;
static constexpr uint32_t EVENT_STOP = 1 << 1;
static constexpr uint32_t EVENT_WAKE = 1 << 2;

struct Iqs323Internal {
    // Serialises I2C transactions and the use of the RDY pin as output
    Mutex io_mutex;
    // Guards the members below it
    Mutex mutex;
    EventGroupHandle_t events = nullptr;
    GpioDescriptor* ready = nullptr;
    Thread* thread = nullptr;
    // The configuration is loaded and the chip reports events
    std::atomic<bool> initialised = false;
    bool has_report = false;
    std::atomic<uint8_t> power_mode;
    // The chip streams reports instead of reporting events only
    std::atomic<bool> streaming = false;
    // The chip stopped answering properly and only a hardware reset recovers it
    std::atomic<bool> recovery_needed = false;
    uint16_t last_touch_mask = 0;
    uint16_t last_position = 0;
    TouchbarState state = {};
    Iqs323Info info = {};

    explicit Iqs323Internal(uint8_t power_mode) : power_mode(power_mode) {
        mutex_construct(&io_mutex);
        mutex_construct(&mutex);
    }
    ~Iqs323Internal() {
        mutex_destruct(&mutex);
        mutex_destruct(&io_mutex);
    }
};

class ScopedLock {
    Mutex* mutex;
public:
    explicit ScopedLock(Mutex* mutex) : mutex(mutex) { mutex_lock(mutex); }
    ~ScopedLock() { mutex_unlock(mutex); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

// region Communication

static bool is_ready_low(Iqs323Internal* internal) {
    bool high = true;
    return gpio_descriptor_get_level(internal->ready, &high) == ERROR_NONE && !high;
}

// The chip answers only while RDY is low. 
// Outside of such a window, a window is requested by sending the end communication command
// The window closes by itself after the transaction, because every transaction ends with a STOP
static error_t open_window(Device* device, Iqs323Internal* internal) {
    if (is_ready_low(internal)) return ERROR_NONE;

    // The chip does not acknowledge while it is busy (e.g. calibrating): RDY decides whether this worked
    const uint8_t command = END_COMMUNICATION_COMMAND;
    (void)i2c_controller_write(device_get_parent(device), GET_CONFIG(device)->address, &command, 1, I2C_TIMEOUT_TICKS);

    for (uint32_t waited = 0; waited < WINDOW_TIMEOUT_MS; waited++) {
        if (is_ready_low(internal)) return ERROR_NONE;
        delay_millis(1);
    }
    return ERROR_TIMEOUT;
}

static error_t read_words(Device* device, Iqs323Internal* internal, uint8_t reg, uint16_t* words, size_t count) {
    ScopedLock lock(&internal->io_mutex);
    error_t error = open_window(device, internal);
    if (error != ERROR_NONE) return error;

    uint8_t buffer[IQS323_MAX_TRANSFER_WORDS * sizeof(uint16_t)];
    const size_t size = count * sizeof(uint16_t);
    error = i2c_controller_write_read(device_get_parent(device), GET_CONFIG(device)->address, &reg, 1, buffer, size, I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    size_t invalid = 0;
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] == INVALID_RESPONSE_BYTE) invalid++;
    }
    if (invalid == size) return ERROR_RESOURCE;

    for (size_t i = 0; i < count; i++) {
        words[i] = static_cast<uint16_t>(buffer[2 * i] | (buffer[2 * i + 1] << 8));
    }
    return ERROR_NONE;
}

static error_t write_words(Device* device, Iqs323Internal* internal, uint8_t reg, const uint16_t* words, size_t count) {
    ScopedLock lock(&internal->io_mutex);
    error_t error = open_window(device, internal);
    if (error != ERROR_NONE) return error;

    uint8_t buffer[IQS323_MAX_TRANSFER_WORDS * sizeof(uint16_t)];
    for (size_t i = 0; i < count; i++) {
        buffer[2 * i] = static_cast<uint8_t>(words[i] & 0xFF);
        buffer[2 * i + 1] = static_cast<uint8_t>(words[i] >> 8);
    }
    return i2c_controller_write_register(device_get_parent(device), GET_CONFIG(device)->address, reg, buffer, count * sizeof(uint16_t), I2C_TIMEOUT_TICKS);
}

static error_t write_word(Device* device, Iqs323Internal* internal, uint8_t reg, uint16_t word) {
    return write_words(device, internal, reg, &word, 1);
}

static uint16_t system_control_value(const Iqs323Internal* internal, uint16_t flags) {
    return static_cast<uint16_t>(flags | (internal->power_mode.load() << SYSTEM_CONTROL_POWER_MODE_SHIFT));
}

// Reading a register that does not exist returns 0xEE on a healthy chip. Anything else = chip lockup
static error_t check_bus(Device* device, Iqs323Internal* internal) {
    uint8_t response = 0;
    {
        ScopedLock lock(&internal->io_mutex);
        const error_t error = i2c_controller_write_read(device_get_parent(device), GET_CONFIG(device)->address, &NONEXISTENT_REGISTER, 1, &response, 1, I2C_TIMEOUT_TICKS);
        if (error == ERROR_NONE && response == INVALID_RESPONSE_BYTE) return ERROR_NONE;
    }

    LOG_W(TAG, "Chip locked up, resetting it");
    internal->initialised = false;
    internal->recovery_needed = true;
    event_group_set(internal->events, EVENT_WAKE);
    return ERROR_RESOURCE;
}

static uint16_t reporting_flags(const Iqs323Internal* internal) {
    return internal->streaming ? 0 : SYSTEM_CONTROL_EVENT_MODE;
}

static error_t set_event_mode(Device* device, Iqs323Internal* internal, bool enabled) {
    const uint16_t value = system_control_value(internal, enabled ? SYSTEM_CONTROL_EVENT_MODE : 0);
    error_t error = write_word(device, internal, REG_SYSTEM_CONTROL, value);
    if (error != ERROR_NONE) return error;

    // The chip refuses event mode while the reset event is pending, so the bit is checked
    delay_millis(EVENT_MODE_VERIFY_DELAY_MS);
    uint16_t control = 0;
    error = read_words(device, internal, REG_SYSTEM_CONTROL, &control, 1);
    if (error != ERROR_NONE) return error;
    return ((control & SYSTEM_CONTROL_EVENT_MODE) != 0) == enabled ? ERROR_NONE : ERROR_INVALID_STATE;
}

// endregion

// region Report handling

// The chip clears its event flags when they are read, s
static error_t read_report(Device* device, Iqs323Internal* internal, bool* ati_error) {
    uint16_t report[REPORT_WORDS];
    error_t error = read_words(device, internal, REG_REPORT, report, REPORT_WORDS);
    if (error != ERROR_NONE) return error;

    const uint16_t status = report[REPORT_STATUS];
    if ((status & STATUS_RESET_EVENT) != 0) {
        LOG_W(TAG, "Chip reset, configuring it again");
        internal->initialised = false;
        return ERROR_INVALID_STATE;
    }

    const uint16_t gesture_status = report[REPORT_GESTURE];
    TouchbarEvent events[MAX_EVENTS_PER_REPORT];
    size_t event_count = 0;

    {
        ScopedLock lock(&internal->mutex);
        TouchbarState& state = internal->state;
        state.channel_count = CHANNEL_COUNT;
        uint16_t touch_mask = 0;
        for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
            const uint16_t first_bit = STATUS_CHANNEL_PROX_FIRST_BIT + 2 * channel;
            const size_t first_word = REPORT_FIRST_CHANNEL + WORDS_PER_CHANNEL * channel;
            state.channels[channel].proximity = (status & (1 << first_bit)) != 0;
            state.channels[channel].touched = (status & (1 << (first_bit + 1))) != 0;
            state.channels[channel].counts = report[first_word];
            state.channels[channel].reference = report[first_word + 1];
            if (state.channels[channel].touched) touch_mask |= 1 << channel;
        }
        const uint16_t previous_mask = internal->last_touch_mask;
        // The position register only means something while touched, and a gesture can be reported in the cycle of the release
        const uint16_t position = touch_mask != 0 ? report[REPORT_POSITION] : internal->last_position;
        const bool holding = touch_mask != 0 && (gesture_status & GESTURE_HOLD) != 0;

        const auto make_event = [&](TouchbarEventType type, int8_t direction, uint8_t channel, uint16_t event_mask, uint16_t event_position) {
            events[event_count++] = TouchbarEvent {
                .type = type,
                .direction = direction,
                .channel = channel,
                .touch_mask = event_mask,
                .position = event_position
            };
        };

        uint16_t running_mask = previous_mask;
        for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
            const uint16_t bit = 1 << channel;
            if ((touch_mask & bit) != 0 && (previous_mask & bit) == 0) {
                running_mask |= bit;
                make_event(TOUCHBAR_EVENT_TOUCH_DOWN, 0, channel, running_mask, position);
            }
        }
        if (touch_mask != 0 && position != internal->last_position) {
            make_event(TOUCHBAR_EVENT_MOVE, 0, 0, running_mask, position);
        }
        if ((gesture_status & GESTURE_TAP) != 0) {
            make_event(TOUCHBAR_EVENT_TAP, 0, 0, running_mask, position);
        }
        if (holding && !state.holding) {
            make_event(TOUCHBAR_EVENT_HOLD, 0, 0, running_mask, position);
        }
        if ((gesture_status & (GESTURE_SWIPE_POSITIVE | GESTURE_SWIPE_NEGATIVE)) != 0) {
            const int8_t direction = (gesture_status & GESTURE_SWIPE_POSITIVE) != 0 ? 1 : -1;
            make_event(TOUCHBAR_EVENT_SWIPE, direction, 0, running_mask, position);
        }
        if ((gesture_status & (GESTURE_FLICK_POSITIVE | GESTURE_FLICK_NEGATIVE)) != 0) {
            const int8_t direction = (gesture_status & GESTURE_FLICK_POSITIVE) != 0 ? 1 : -1;
            make_event(TOUCHBAR_EVENT_FLICK, direction, 0, running_mask, position);
        }
        for (uint8_t channel = 0; channel < CHANNEL_COUNT; channel++) {
            const uint16_t bit = 1 << channel;
            if ((touch_mask & bit) == 0 && (previous_mask & bit) != 0) {
                running_mask &= ~bit;
                make_event(TOUCHBAR_EVENT_TOUCH_UP, 0, channel, running_mask, position);
            }
        }

        internal->last_position = position;
        state.position = position;
        internal->last_touch_mask = touch_mask;
        state.touched = touch_mask != 0;
        state.holding = holding;
        internal->has_report = true;

        internal->info.current_power_mode = static_cast<Iqs323PowerMode>((status >> STATUS_POWER_MODE_SHIFT) & STATUS_POWER_MODE_MASK);
        internal->info.ati_error = (status & STATUS_ATI_ERROR) != 0;
    }

    *ati_error = (status & STATUS_ATI_ERROR) != 0;
    for (size_t i = 0; i < event_count; i++) {
        touchbar_emit_event(device, events[i]);
    }
    return ERROR_NONE;
}

// endregion

// region Initialisation

static error_t read_identity(Device* device, Iqs323Internal* internal) {
    uint16_t words[IDENTITY_WORDS];
    error_t error = read_words(device, internal, REG_PRODUCT_NUMBER, words, IDENTITY_WORDS);
    if (error != ERROR_NONE) return error;

    if (words[0] != PRODUCT_NUMBER_RELEASE_UI && words[0] != PRODUCT_NUMBER_MOVEMENT_UI) {
        LOG_E(TAG, "Unknown product number %u", static_cast<unsigned>(words[0]));
        return ERROR_RESOURCE;
    }

    ScopedLock lock(&internal->mutex);
    internal->info.product_number = words[0];
    internal->info.major_version = static_cast<uint8_t>(words[1]);
    internal->info.minor_version = static_cast<uint8_t>(words[2]);
    return ERROR_NONE;
}

static error_t write_configuration(Device* device, Iqs323Internal* internal) {
    const auto* config = GET_CONFIG(device);
    static constexpr uint8_t SENSOR_REGS[] = { REG_SENSOR_0, REG_SENSOR_1, REG_SENSOR_2 };
    static constexpr uint8_t CHANNEL_REGS[] = { REG_CHANNEL_0, REG_CHANNEL_1, REG_CHANNEL_2 };

    for (size_t i = 0; i < CHANNEL_COUNT; i++) {
        error_t error = write_words(device, internal, SENSOR_REGS[i], config->sensor_setup + i * SENSOR_WORDS, SENSOR_WORDS);
        if (error != ERROR_NONE) return error;
    }
    for (size_t i = 0; i < CHANNEL_COUNT; i++) {
        error_t error = write_words(device, internal, CHANNEL_REGS[i], config->channel_setup + i * CHANNEL_WORDS, CHANNEL_WORDS);
        if (error != ERROR_NONE) return error;
    }

    uint16_t general[IQS323_GENERAL_SETUP_WORDS];
    memcpy(general, config->general_setup, sizeof(general));
    general[GENERAL_EVENTS_ENABLE_WORD] = static_cast<uint16_t>((general[GENERAL_EVENTS_ENABLE_WORD] & ~EVENTS_ENABLE_MASK) | EVENTS_USED);

    error_t error = write_words(device, internal, REG_SLIDER, config->slider_setup, IQS323_SLIDER_SETUP_WORDS);
    if (error != ERROR_NONE) return error;
    error = write_words(device, internal, REG_GESTURE, config->gesture_setup, IQS323_GESTURE_SETUP_WORDS);
    if (error != ERROR_NONE) return error;
    error = write_words(device, internal, REG_FILTER, config->filter_setup, IQS323_FILTER_SETUP_WORDS);
    if (error != ERROR_NONE) return error;
    error = write_words(device, internal, REG_TIMING, config->timing_setup, IQS323_TIMING_SETUP_WORDS);
    if (error != ERROR_NONE) return error;
    return write_words(device, internal, REG_GENERAL, general, IQS323_GENERAL_SETUP_WORDS);
}

static bool is_stop_requested(Iqs323Internal* internal) {
    return (event_group_get(internal->events) & EVENT_STOP) != 0;
}

// The chip does not answer while it calibrates, so failing reads are expected until it is done (which is fine)
static error_t wait_for_ati(Device* device, Iqs323Internal* internal, bool* ati_error) {
    delay_millis(RE_ATI_START_DELAY_MS);
    for (uint32_t waited = 0; waited < ATI_TIMEOUT_MS && !is_stop_requested(internal); waited += ATI_POLL_INTERVAL_MS) {
        uint16_t status = 0;
        if (read_words(device, internal, REG_REPORT, &status, 1) == ERROR_NONE) {
            if ((status & STATUS_RESET_EVENT) != 0) return ERROR_INVALID_STATE;
            if ((status & STATUS_ATI_ACTIVE) == 0) {
                *ati_error = (status & STATUS_ATI_ERROR) != 0;
                ScopedLock lock(&internal->mutex);
                internal->info.ati_error = *ati_error;
                return ERROR_NONE;
            }
        }
        delay_millis(ATI_POLL_INTERVAL_MS);
    }
    return ERROR_TIMEOUT;
}

static error_t recalibrate(Device* device, Iqs323Internal* internal, bool* ati_error) {
    const error_t error = write_word(device, internal, REG_SYSTEM_CONTROL, system_control_value(internal, SYSTEM_CONTROL_RE_ATI));
    if (error != ERROR_NONE) return error;
    return wait_for_ati(device, internal, ati_error);
}

// A chip that did not reset since the last configuration is reset first bbfore it can use a new configuration
static error_t initialise(Device* device, Iqs323Internal* internal) {
    error_t error = read_identity(device, internal);
    if (error != ERROR_NONE) return error;

    uint16_t status = 0;
    error = read_words(device, internal, REG_REPORT, &status, 1);
    if (error != ERROR_NONE) return error;

    if ((status & STATUS_RESET_EVENT) == 0) {
        error = write_word(device, internal, REG_SYSTEM_CONTROL, SYSTEM_CONTROL_SOFT_RESET);
        if (error != ERROR_NONE) return error;
        delay_millis(SOFT_RESET_DELAY_MS);

        error = read_words(device, internal, REG_REPORT, &status, 1);
        if (error != ERROR_NONE) return error;
        if ((status & STATUS_RESET_EVENT) == 0) return ERROR_INVALID_STATE;
    }

    error = write_configuration(device, internal);
    if (error != ERROR_NONE) return error;

    error = write_word(device, internal, REG_SYSTEM_CONTROL, system_control_value(internal, SYSTEM_CONTROL_ACK_RESET));
    if (error != ERROR_NONE) return error;
    bool ati_error = false;
    error = recalibrate(device, internal, &ati_error);
    if (error != ERROR_NONE) return error;
    if (ati_error) LOG_W(TAG, "Calibration did not reach its target");

    // The state is read once before the reports are limited to events, so that it is valid from the start
    error = read_report(device, internal, &ati_error);
    if (error != ERROR_NONE) return error;

    error = set_event_mode(device, internal, !internal->streaming);
    if (error != ERROR_NONE) return error;

    error = check_bus(device, internal);
    if (error != ERROR_NONE) return error;

    internal->initialised = true;
    LOG_I(TAG, "Ready: product %u v%u.%u", static_cast<unsigned>(internal->info.product_number),
        static_cast<unsigned>(internal->info.major_version), static_cast<unsigned>(internal->info.minor_version));
    return ERROR_NONE;
}

// endregion

// region Reset

static gpio_flags_t ready_input_flags() {
    return GPIO_FLAG_INTERRUPT_TO_OPTIONS(GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_PULL_UP, GPIO_INTERRUPT_LOW_LEVEL);
}

// RDY doubles as the chip's reset line (MCLR)
static error_t pulse_reset_line(Iqs323Internal* internal) {
    ScopedLock lock(&internal->io_mutex);

    for (uint32_t waited = 0; is_ready_low(internal) && waited < RDY_HIGH_TIMEOUT_MS; waited++) {
        delay_millis(1);
    }
    if (is_ready_low(internal)) return ERROR_TIMEOUT;
    delay_micros(MCLR_SWITCHOVER_US);

    gpio_descriptor_disable_interrupt(internal->ready);
    // The level is written both before and after the pin becomes an output, so that it never drives high
    gpio_descriptor_set_level(internal->ready, false);
    error_t error = gpio_descriptor_set_flags(internal->ready, GPIO_FLAG_DIRECTION_OUTPUT);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_level(internal->ready, false);
        delay_micros(MCLR_PULSE_US);
    }
    // The pin goes back to being an input in all cases, the pull-up releases the line
    const error_t restore_error = gpio_descriptor_set_flags(internal->ready, ready_input_flags());
    gpio_descriptor_disable_interrupt(internal->ready);
    event_group_clear(internal->events, EVENT_READY);
    return error != ERROR_NONE ? error : restore_error;
}

// endregion

// region Thread

static void ready_isr(void* context) {
    auto* internal = static_cast<Iqs323Internal*>(context);
    // RDY is armed as a low level interrupt
    gpio_descriptor_disable_interrupt(internal->ready);
    event_group_set(internal->events, EVENT_READY);
}

// A calibration that missed its target is repeated a limited number of times to be safe.
static void handle_ati_error(Device* device, Iqs323Internal* internal, bool ati_error, uint32_t* retries) {
    if (!ati_error) {
        *retries = 0;
        return;
    }
    if (*retries >= MAX_ATI_RETRIES) return;

    (*retries)++;
    LOG_W(TAG, "Calibration did not reach its target, retrying (%lu/%lu)", static_cast<unsigned long>(*retries), static_cast<unsigned long>(MAX_ATI_RETRIES));
    bool still_failing = false;
    const error_t error = recalibrate(device, internal, &still_failing);
    if (error != ERROR_NONE) {
        LOG_W(TAG, "Recalibration failed (%d)", static_cast<int>(error));
    }
}

static int32_t thread_main(void* context) {
    auto* device = static_cast<Device*>(context);
    auto* internal = GET_INTERNAL(device);
    uint32_t failures = 0;
    uint32_t ati_retries = 0;

    while (!is_stop_requested(internal)) {
        if (!internal->initialised) {
            if (internal->recovery_needed) {
                internal->recovery_needed = false;
                if (pulse_reset_line(internal) != ERROR_NONE) {
                    LOG_E(TAG, "Hardware reset failed");
                }
                delay_millis(HARDWARE_RESET_DELAY_MS);
            }
            error_t error = initialise(device, internal);
            if (error == ERROR_NONE) {
                failures = 0;
                ati_retries = 0;
            } else {
                failures++;
                LOG_W(TAG, "Initialisation failed (%d), attempt %lu", static_cast<int>(error), static_cast<unsigned long>(failures));
                // A chip that locked up is only recovered by a reset
                if (failures % INIT_FAILURES_BEFORE_RESET == 0 && pulse_reset_line(internal) != ERROR_NONE) {
                    LOG_E(TAG, "Hardware reset failed");
                }
                event_group_wait(internal->events, EVENT_STOP, false, false, nullptr, pdMS_TO_TICKS(INIT_RETRY_DELAY_MS));
                continue;
            }
        }

        gpio_descriptor_enable_interrupt(internal->ready);
        uint32_t flags = 0;
        event_group_wait(internal->events, EVENT_READY | EVENT_STOP | EVENT_WAKE, false, true, &flags, portMAX_DELAY);
        if ((flags & EVENT_STOP) != 0) break;
        if ((flags & EVENT_READY) == 0) continue;

        bool ati_error = false;
        const error_t error = read_report(device, internal, &ati_error);
        if (error == ERROR_NONE) {
            if (check_bus(device, internal) == ERROR_NONE) {
                handle_ati_error(device, internal, ati_error, &ati_retries);
            }
        } else if (error != ERROR_INVALID_STATE) {
            LOG_W(TAG, "Reading the report failed (%d)", static_cast<int>(error));
            delay_millis(ERROR_BACKOFF_MS);
        }
    }

    gpio_descriptor_disable_interrupt(internal->ready);
    return 0;
}

// endregion

// region Driver lifecycle

static bool is_config_valid(const Iqs323Config* config) {
    return config->power_mode <= POWER_MODE_MAX &&
        config->pin_ready.gpio_controller != nullptr &&
        config->sensor_setup_length == IQS323_SENSOR_SETUP_WORDS &&
        config->channel_setup_length == IQS323_CHANNEL_SETUP_WORDS &&
        config->slider_setup_length == IQS323_SLIDER_SETUP_WORDS &&
        config->gesture_setup_length == IQS323_GESTURE_SETUP_WORDS &&
        config->filter_setup_length == IQS323_FILTER_SETUP_WORDS &&
        config->timing_setup_length == IQS323_TIMING_SETUP_WORDS &&
        config->general_setup_length == IQS323_GENERAL_SETUP_WORDS;
}

static void release_resources(Iqs323Internal* internal) {
    if (internal->ready != nullptr) {
        gpio_descriptor_disable_interrupt(internal->ready);
        gpio_descriptor_remove_callback(internal->ready);
        gpio_descriptor_release(internal->ready);
        internal->ready = nullptr;
    }
    if (internal->events != nullptr) {
        event_group_destruct(&internal->events);
    }
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    if (device_get_type(device_get_parent(device)) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }
    if (!is_config_valid(config)) {
        LOG_E(TAG, "Invalid configuration: check the power mode and the length of the setup arrays");
        return ERROR_INVALID_ARGUMENT;
    }

    auto* internal = new (std::nothrow) Iqs323Internal(config->power_mode);
    if (internal == nullptr) return ERROR_OUT_OF_MEMORY;
    internal->state.position_max = config->slider_setup[SLIDER_RESOLUTION_WORD];
    event_group_construct(&internal->events);

    const auto& pin = config->pin_ready;
    internal->ready = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, ready_input_flags(), GPIO_OWNER_GPIO);
    if (internal->ready == nullptr || gpio_descriptor_add_callback(internal->ready, ready_isr, internal) != ERROR_NONE) {
        LOG_E(TAG, "Failed to set up the RDY pin");
        release_resources(internal);
        delete internal;
        return ERROR_RESOURCE;
    }
    gpio_descriptor_disable_interrupt(internal->ready);

    device_set_driver_data(device, internal);

    internal->thread = thread_alloc_full("iqs323", THREAD_STACK_SIZE, thread_main, device, tskNO_AFFINITY);
    if (internal->thread == nullptr) {
        device_set_driver_data(device, nullptr);
        release_resources(internal);
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }
    thread_set_priority(internal->thread, THREAD_PRIORITY_HIGH);
    if (thread_start(internal->thread) != ERROR_NONE) {
        thread_free(internal->thread);
        device_set_driver_data(device, nullptr);
        release_resources(internal);
        delete internal;
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);

    event_group_set(internal->events, EVENT_STOP);
    thread_join(internal->thread, portMAX_DELAY, pdMS_TO_TICKS(10));
    thread_free(internal->thread);

    device_set_driver_data(device, nullptr);
    release_resources(internal);
    delete internal;
    return ERROR_NONE;
}

// endregion

// region Touchbar API

static error_t read_state(Device* device, TouchbarState* state) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(&internal->mutex);
    if (!internal->has_report) return ERROR_INVALID_STATE;
    *state = internal->state;
    return ERROR_NONE;
}

static bool has_capability(Device* device, uint32_t capability) {
    const uint16_t gesture_enable = GET_CONFIG(device)->gesture_setup[0];
    uint32_t available = ALWAYS_AVAILABLE_CAPABILITIES;
    if ((gesture_enable & GESTURE_ENABLE_TAP) != 0) available |= TOUCHBAR_CAPABILITY_GESTURE_TAP;
    if ((gesture_enable & GESTURE_ENABLE_SWIPE) != 0) available |= TOUCHBAR_CAPABILITY_GESTURE_SWIPE;
    if ((gesture_enable & GESTURE_ENABLE_FLICK) != 0) available |= TOUCHBAR_CAPABILITY_GESTURE_FLICK;
    if ((gesture_enable & GESTURE_ENABLE_HOLD) != 0) available |= TOUCHBAR_CAPABILITY_GESTURE_HOLD;
    return (available & capability) == capability;
}

static const TouchbarApi iqs323_touchbar_api = {
    .capabilities = ALWAYS_AVAILABLE_CAPABILITIES,
    .read_state = read_state,
    .recalibrate = iqs323_recalibrate,
    .has_capability = has_capability
};

// endregion

extern "C" {

error_t iqs323_read_registers(Device* device, uint8_t reg, uint16_t* words, size_t count) {
    if (count == 0 || count > IQS323_MAX_TRANSFER_WORDS) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    const error_t error = read_words(device, internal, reg, words, count);
    if (error != ERROR_NONE) return error;
    return check_bus(device, internal);
}

error_t iqs323_write_registers(Device* device, uint8_t reg, const uint16_t* words, size_t count) {
    if (count == 0 || count > IQS323_MAX_TRANSFER_WORDS) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    const error_t error = write_words(device, internal, reg, words, count);
    if (error != ERROR_NONE) return error;
    return check_bus(device, internal);
}

error_t iqs323_get_info(Device* device, Iqs323Info* info) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(&internal->mutex);
    if (!internal->has_report) return ERROR_INVALID_STATE;
    *info = internal->info;
    return ERROR_NONE;
}

static error_t send_command(Device* device, uint16_t flags) {
    auto* internal = GET_INTERNAL(device);
    if (!internal->initialised) return ERROR_INVALID_STATE;
    const error_t error = write_word(device, internal, REG_SYSTEM_CONTROL, system_control_value(internal, flags | reporting_flags(internal)));
    if (error != ERROR_NONE) return error;
    return check_bus(device, internal);
}

error_t iqs323_set_power_mode(Device* device, Iqs323PowerMode mode) {
    if (mode > POWER_MODE_MAX) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    const uint8_t previous = internal->power_mode.exchange(mode);
    const error_t error = send_command(device, 0);
    if (error != ERROR_NONE) {
        internal->power_mode = previous;
    }
    return error;
}

error_t iqs323_reseed(Device* device) {
    return send_command(device, SYSTEM_CONTROL_RESEED);
}

error_t iqs323_recalibrate(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (!internal->initialised) return ERROR_INVALID_STATE;

    // The chip does not answer while it calibrates (obviously)
    bool ati_error = false;
    error_t error = write_word(device, internal, REG_SYSTEM_CONTROL, system_control_value(internal, SYSTEM_CONTROL_RE_ATI | reporting_flags(internal)));
    if (error != ERROR_NONE) return error;
    error = wait_for_ati(device, internal, &ati_error);
    if (error != ERROR_NONE) return error;
    return check_bus(device, internal);
}

error_t iqs323_set_streaming(Device* device, bool enabled) {
    auto* internal = GET_INTERNAL(device);
    if (!internal->initialised) return ERROR_INVALID_STATE;

    const bool previous = internal->streaming.exchange(enabled);
    error_t error = set_event_mode(device, internal, !enabled);
    if (error != ERROR_NONE) {
        internal->streaming = previous;
        return error;
    }
    return check_bus(device, internal);
}

error_t iqs323_soft_reset(Device* device) {
    auto* internal = GET_INTERNAL(device);
    error_t error = write_word(device, internal, REG_SYSTEM_CONTROL, SYSTEM_CONTROL_SOFT_RESET);
    if (error != ERROR_NONE) return error;

    internal->initialised = false;
    delay_millis(SOFT_RESET_DELAY_MS);
    event_group_set(internal->events, EVENT_WAKE);
    return ERROR_NONE;
}

error_t iqs323_hardware_reset(Device* device) {
    auto* internal = GET_INTERNAL(device);
    error_t error = pulse_reset_line(internal);
    if (error != ERROR_NONE) return error;

    internal->initialised = false;
    delay_millis(HARDWARE_RESET_DELAY_MS);
    event_group_set(internal->events, EVENT_WAKE);
    return ERROR_NONE;
}

Driver iqs323_driver = {
    .name = "iqs323",
    .compatible = (const char*[]) { "azoteq,iqs323", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &iqs323_touchbar_api,
    .device_type = &TOUCHBAR_TYPE,
    .internal = nullptr
};

}
