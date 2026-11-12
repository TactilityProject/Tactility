// SPDX-License-Identifier: Apache-2.0
#include <development/remote.h>
#include <development/remote_access.h>
#include <development/remote_input.h>

#include <app/event.h>

#include <tactility/Memory.hpp>
#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/drivers/display.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <mbedtls/ssl.h>

#include <atomic>
#include <cstring>

#ifdef ESP_PLATFORM
#include <miniz.h>
#endif

namespace {

constexpr auto* TAG = "DevelopmentRemote";

// Bounds the wait for socket input, which doesn't signal the event group
constexpr uint32_t POLL_INTERVAL_MS = 20;
constexpr size_t RECEIVE_BUFFER_SIZE = DEVELOPMENT_FRAME_HEADER_SIZE + DEVELOPMENT_FRAME_PAYLOAD_MAX;
constexpr uint32_t SEND_TIMEOUT_MS = 5000;

constexpr size_t RECT_HEADER_SIZE = 9;
// Raw pixels per rectangle frame. A compressed payload is never larger than the raw one
constexpr size_t STRIP_RAW_MAX = DEVELOPMENT_REMOTE_FRAME_PAYLOAD_MAX - RECT_HEADER_SIZE;
constexpr size_t TRANSMIT_SIZE = DEVELOPMENT_FRAME_HEADER_SIZE + DEVELOPMENT_REMOTE_FRAME_PAYLOAD_MAX;
constexpr uint8_t RECT_FLAG_DEFLATE = 1 << 0;

constexpr MemoryPolicy MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

// How often the orientation of the UI is read, as it can change at any time
constexpr uint32_t ORIENTATION_POLL_INTERVAL_MS = 500;

const DevelopmentRemoteUiHandlers* uiHandlers = nullptr;

/**
 * The display state that the display hook shares with the session task.
 * It is never freed because a hook callback can still run after the hook was removed.
 * Everything below `mutex` is guarded by it. `pixels` is null while no session is active.
 */
struct Shared {
    Mutex mutex {};
    Shared() { mutex_construct(&mutex); }

    Device* display = nullptr;
    uint8_t* pixels = nullptr;
    // The resolution that the driver reports, which is the one of the image at its configured swap state
    uint16_t nativeWidth = 0;
    uint16_t nativeHeight = 0;
    bool baselineSwapXy = false;
    // Of the shadow image and of the pixels that are sent, which for a monochrome display are 8 bit grayscale
    uint8_t bytesPerPixel = 0;
    DisplayColorFormat colorFormat = DISPLAY_COLOR_FORMAT_RGB565;
    // The display draws 1 bit pixels, which are unpacked into the shadow image
    bool monochrome = false;

    // The orientation of the UI that the UI handlers report, see DEVELOPMENT_REMOTE_ORIENTATION_*
    uint8_t orientation = 0;
    // The size of the image that draw calls address. Swapped with the native size by swap_xy
    uint16_t width = 0;
    uint16_t height = 0;
    bool swapXy = false;
    bool mirrorX = false;
    bool mirrorY = false;
    bool invert = false;
    bool on = true;
    bool stateChanged = false;

    // The last display state that was set through the display API, which the getters of a driver don't report.
    // Tracked also without a session, so that a session knows the state of a display that was set up earlier
    // The display that the tracked values are about. Values of another display are dropped
    Device* trackedDevice = nullptr;
    bool swapKnown = false;
    bool trackedSwapXy = false;
    bool mirrorKnown = false;
    bool trackedMirrorX = false;
    bool trackedMirrorY = false;
    bool invertKnown = false;
    bool trackedInvert = false;
    bool onKnown = false;
    bool trackedOn = true;

    /** Set while `pixels` is valid. Lets draw calls skip the mutex when there is no session */
    std::atomic<bool> active { false };

    bool dirty = false;
    int32_t dirtyX0 = 0;
    int32_t dirtyY0 = 0;
    int32_t dirtyX1 = 0;
    int32_t dirtyY1 = 0;
};

/** Allocated on first use and never freed, in external RAM when there is any */
Shared& shared() {
    static Shared* instance = tt::newObject<Shared>();
    check(instance != nullptr);
    return *instance;
}

uint8_t bytesPerPixelOf(DisplayColorFormat format) {
    switch (format) {
        case DISPLAY_COLOR_FORMAT_BGR565:
        case DISPLAY_COLOR_FORMAT_BGR565_SWAPPED:
        case DISPLAY_COLOR_FORMAT_RGB565:
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
            return 2;
        case DISPLAY_COLOR_FORMAT_RGB888:
            return 3;
        case DISPLAY_COLOR_FORMAT_GRAYSCALE8:
        case DISPLAY_COLOR_FORMAT_MONOCHROME:
            return 1;
        default:
            return 0;
    }
}

/** Call with the mutex held */
void trackDevice(Shared& s, Device* device) {
    if (s.trackedDevice != device) {
        s.trackedDevice = device;
        s.swapKnown = false;
        s.mirrorKnown = false;
        s.invertKnown = false;
        s.onKnown = false;
    }
}

void markDirty(Shared& s, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (!s.dirty) {
        s.dirty = true;
        s.dirtyX0 = x0;
        s.dirtyY0 = y0;
        s.dirtyX1 = x1;
        s.dirtyY1 = y1;
        return;
    }
    s.dirtyX0 = x0 < s.dirtyX0 ? x0 : s.dirtyX0;
    s.dirtyY0 = y0 < s.dirtyY0 ? y0 : s.dirtyY0;
    s.dirtyX1 = x1 > s.dirtyX1 ? x1 : s.dirtyX1;
    s.dirtyY1 = y1 > s.dirtyY1 ? y1 : s.dirtyY1;
}

// region Display hook

void onDrawBitmap(Device* device, int32_t xStart, int32_t yStart, int32_t xEnd, int32_t yEnd, const void* colorData) {
    Shared& s = shared();
    if (!s.active.load()) {
        return;
    }
    mutex_lock(&s.mutex);
    if (s.pixels != nullptr && device == s.display && xEnd > xStart && yEnd > yStart) {
        const size_t bpp = s.bytesPerPixel;
        const size_t sourceStride = s.monochrome ? static_cast<size_t>(xEnd - xStart + 7) / 8 : static_cast<size_t>(xEnd - xStart) * bpp;
        const int32_t x0 = xStart < 0 ? 0 : xStart;
        const int32_t y0 = yStart < 0 ? 0 : yStart;
        const int32_t x1 = xEnd > s.width ? s.width : xEnd;
        const int32_t y1 = yEnd > s.height ? s.height : yEnd;
        if (x1 > x0 && y1 > y0) {
            const auto* source = static_cast<const uint8_t*>(colorData);
            const size_t rowBytes = static_cast<size_t>(x1 - x0) * bpp;
            for (int32_t y = y0; y < y1; y++) {
                uint8_t* target = s.pixels + (static_cast<size_t>(y) * s.width + x0) * bpp;
                const uint8_t* row = source + static_cast<size_t>(y - yStart) * sourceStride;
                if (s.monochrome) {
                    // Rows are packed MSB first, and a set bit is white
                    for (int32_t x = x0; x < x1; x++) {
                        const int32_t column = x - xStart;
                        *target++ = (row[column / 8] & (0x80 >> (column % 8))) != 0 ? 0xFF : 0x00;
                    }
                } else {
                    memcpy(target, row + static_cast<size_t>(x0 - xStart) * bpp, rowBytes);
                }
            }
            markDirty(s, x0, y0, x1, y1);
        }
    }
    mutex_unlock(&s.mutex);
}

void onClear(Device* device) {
    Shared& s = shared();
    if (!s.active.load()) {
        return;
    }
    mutex_lock(&s.mutex);
    if (s.pixels != nullptr && device == s.display) {
        // A cleared monochrome panel is white
        memset(s.pixels, s.monochrome ? 0xFF : 0, static_cast<size_t>(s.width) * s.height * s.bytesPerPixel);
        markDirty(s, 0, 0, s.width, s.height);
    }
    mutex_unlock(&s.mutex);
}

void onSwapXy(Device* device, bool swapAxes) {
    Shared& s = shared();
    mutex_lock(&s.mutex);
    trackDevice(s, device);
    s.swapKnown = true;
    s.trackedSwapXy = swapAxes;
    if (s.pixels != nullptr && device == s.display && swapAxes != s.swapXy) {
        s.swapXy = swapAxes;
        // Only a change of the configured swap state transposes the image
        const bool transposed = swapAxes != s.baselineSwapXy;
        s.width = transposed ? s.nativeHeight : s.nativeWidth;
        s.height = transposed ? s.nativeWidth : s.nativeHeight;
        // The content is addressed differently now, so the old image is meaningless
        memset(s.pixels, s.monochrome ? 0xFF : 0, static_cast<size_t>(s.nativeWidth) * s.nativeHeight * s.bytesPerPixel);
        s.dirty = false;
        s.stateChanged = true;
    }
    mutex_unlock(&s.mutex);
}

void onMirror(Device* device, bool xAxis, bool yAxis) {
    Shared& s = shared();
    mutex_lock(&s.mutex);
    trackDevice(s, device);
    s.mirrorKnown = true;
    s.trackedMirrorX = xAxis;
    s.trackedMirrorY = yAxis;
    if (s.pixels != nullptr && device == s.display && (xAxis != s.mirrorX || yAxis != s.mirrorY)) {
        s.mirrorX = xAxis;
        s.mirrorY = yAxis;
        s.stateChanged = true;
    }
    mutex_unlock(&s.mutex);
}

void onInvertColor(Device* device, bool invert) {
    Shared& s = shared();
    mutex_lock(&s.mutex);
    trackDevice(s, device);
    s.invertKnown = true;
    s.trackedInvert = invert;
    if (s.pixels != nullptr && device == s.display && invert != s.invert) {
        s.invert = invert;
        s.stateChanged = true;
    }
    mutex_unlock(&s.mutex);
}

void onDispOnOff(Device* device, bool on) {
    Shared& s = shared();
    mutex_lock(&s.mutex);
    trackDevice(s, device);
    s.onKnown = true;
    s.trackedOn = on;
    if (s.pixels != nullptr && device == s.display && on != s.on) {
        s.on = on;
        s.stateChanged = true;
    }
    mutex_unlock(&s.mutex);
}

const DisplayHook DISPLAY_HOOK = {
    .on_draw_bitmap = onDrawBitmap,
    .on_clear = onClear,
    .on_refresh = nullptr,
    .on_mirror = onMirror,
    .on_swap_xy = onSwapXy,
    .on_set_gap = nullptr,
    .on_invert_color = onInvertColor,
    .on_disp_on_off = onDispOnOff,
};

// endregion

// region Sending

struct Sender {
    DevelopmentSession* session = nullptr;
    uint8_t* transmit = nullptr;
    uint8_t* strip = nullptr;
    /** Null when compression is unavailable */
    uint8_t* compressed = nullptr;
#ifdef ESP_PLATFORM
    tdefl_compressor* compressor = nullptr;
#endif
};

void writeU16(uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value >> 8);
    out[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t readU16(const uint8_t* in) {
    return static_cast<uint16_t>((in[0] << 8) | in[1]);
}

bool sendFrame(Sender& sender, DevelopmentFrameType type, size_t length) {
    development_frame_write_header(type, static_cast<uint16_t>(length), sender.transmit);
    return development_session_write(sender.session, sender.transmit, DEVELOPMENT_FRAME_HEADER_SIZE + length, SEND_TIMEOUT_MS);
}

/** Layout shared by the info and state frames */
constexpr size_t STATE_SIZE = 11;

size_t writeState(uint8_t* out, const Shared& s) {
    writeU16(out, s.width);
    writeU16(out + 2, s.height);
    out[4] = static_cast<uint8_t>(s.colorFormat);
    out[5] = s.swapXy;
    out[6] = s.mirrorX;
    out[7] = s.mirrorY;
    out[8] = s.invert;
    out[9] = s.on;
    out[10] = s.orientation;
    return STATE_SIZE;
}

/** @return the compressed size in sender.compressed, or 0 when the data doesn't get smaller or compression is unavailable */
size_t compress(Sender& sender, const uint8_t* data, size_t size) {
#ifdef ESP_PLATFORM
    if (sender.compressor == nullptr || sender.compressed == nullptr) {
        return 0;
    }
    const int flags = TDEFL_WRITE_ZLIB_HEADER | TDEFL_GREEDY_PARSING_FLAG | TDEFL_NONDETERMINISTIC_PARSING_FLAG | 16;
    if (tdefl_init(sender.compressor, nullptr, nullptr, flags) != TDEFL_STATUS_OKAY) {
        return 0;
    }
    size_t inputSize = size;
    // An output buffer that is too small makes the status something other than TDEFL_STATUS_DONE
    size_t outputSize = size;
    const tdefl_status status = tdefl_compress(sender.compressor, data, &inputSize, sender.compressed, &outputSize, TDEFL_FINISH);
    return status == TDEFL_STATUS_DONE && outputSize < size ? outputSize : 0;
#else
    (void) sender;
    (void) data;
    (void) size;
    return 0;
#endif
}

/** Sends [x0, x1) x [y0, y1) of the shadow image as frames, which are each small enough for a frame */
bool sendRect(Sender& sender, Shared& s, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    const size_t bpp = s.bytesPerPixel;
    const size_t rowBytes = static_cast<size_t>(x1 - x0) * bpp;
    const int32_t maxRows = static_cast<int32_t>(STRIP_RAW_MAX / rowBytes);
    if (maxRows < 1) {
        return false;
    }
    for (int32_t y = y0; y < y1; y += maxRows) {
        const int32_t rows = y + maxRows > y1 ? y1 - y : maxRows;
        const size_t rawSize = rowBytes * rows;

        // The shadow is copied out so that the draw calls don't wait for compression or the network
        mutex_lock(&s.mutex);
        const bool valid = s.pixels != nullptr && x1 <= s.width && y + rows <= s.height;
        if (valid) {
            for (int32_t row = 0; row < rows; row++) {
                memcpy(sender.strip + row * rowBytes, s.pixels + (static_cast<size_t>(y + row) * s.width + x0) * bpp, rowBytes);
            }
        }
        mutex_unlock(&s.mutex);
        if (!valid) {
            // The size changed in between, so a new full redraw is on its way
            return true;
        }

        const size_t compressedSize = compress(sender, sender.strip, rawSize);
        const uint8_t* source = compressedSize > 0 ? sender.compressed : sender.strip;
        const size_t dataSize = compressedSize > 0 ? compressedSize : rawSize;

        uint8_t* payload = sender.transmit + DEVELOPMENT_FRAME_HEADER_SIZE;
        writeU16(payload, static_cast<uint16_t>(x0));
        writeU16(payload + 2, static_cast<uint16_t>(y));
        writeU16(payload + 4, static_cast<uint16_t>(x1 - x0));
        writeU16(payload + 6, static_cast<uint16_t>(rows));
        payload[8] = compressedSize > 0 ? RECT_FLAG_DEFLATE : 0;
        memcpy(payload + RECT_HEADER_SIZE, source, dataSize);
        if (!sendFrame(sender, DEVELOPMENT_FRAME_REMOTE_RECT, RECT_HEADER_SIZE + dataSize)) {
            return false;
        }
    }
    return true;
}

uint8_t readOrientation() {
    if (uiHandlers != nullptr && uiHandlers->get_orientation != nullptr) {
        return uiHandlers->get_orientation() & (DEVELOPMENT_REMOTE_ORIENTATION_ROTATION_MASK | DEVELOPMENT_REMOTE_ORIENTATION_SOFTWARE);
    }
    return 0;
}

/** Reads the orientation of the UI now and then, which makes the next update report it when it changed */
void pollOrientation(Shared& s, TickType_t& lastPoll) {
    const TickType_t now = xTaskGetTickCount();
    if (now - lastPoll < pdMS_TO_TICKS(ORIENTATION_POLL_INTERVAL_MS)) {
        return;
    }
    lastPoll = now;
    // The handler takes the UI lock, which a draw call holds while it takes the mutex of the shared state
    const uint8_t orientation = readOrientation();
    mutex_lock(&s.mutex);
    if (s.pixels != nullptr && orientation != s.orientation) {
        s.orientation = orientation;
        s.stateChanged = true;
    }
    mutex_unlock(&s.mutex);
}

/** @return false when the connection must close */
bool sendUpdates(Sender& sender, Shared& s) {
    mutex_lock(&s.mutex);
    const bool stateChanged = s.stateChanged;
    s.stateChanged = false;
    uint8_t state[STATE_SIZE];
    if (stateChanged) {
        writeState(state, s);
    }
    const bool dirty = s.dirty;
    const int32_t x0 = s.dirtyX0;
    const int32_t y0 = s.dirtyY0;
    const int32_t x1 = s.dirtyX1;
    const int32_t y1 = s.dirtyY1;
    s.dirty = false;
    mutex_unlock(&s.mutex);

    if (stateChanged) {
        memcpy(sender.transmit + DEVELOPMENT_FRAME_HEADER_SIZE, state, sizeof(state));
        if (!sendFrame(sender, DEVELOPMENT_FRAME_REMOTE_STATE, sizeof(state))) {
            return false;
        }
    }
    return !dirty || sendRect(sender, s, x0, y0, x1, y1);
}

// endregion

void sendFail(DevelopmentSession* session, const char* message) {
    development_session_send_frame(session, DEVELOPMENT_FRAME_FAIL, message, static_cast<uint16_t>(strlen(message)));
}

// region Input

/** @return false when the connection must close */
bool handleFrame(DevelopmentSession* session, RemoteInput* input, const DevelopmentFrame& frame) {
    switch (frame.type) {
        case DEVELOPMENT_FRAME_INPUT_ENABLE:
            if (frame.length != 1) {
                return false;
            }
            if (remote_input_set_enabled(input, frame.payload[0])) {
                return development_session_send_frame(session, DEVELOPMENT_FRAME_OK, nullptr, 0);
            }
            sendFail(session, "failed to create input device");
            return true;
        case DEVELOPMENT_FRAME_INPUT_KEY: {
            if (frame.length != 5) {
                return false;
            }
            const uint32_t key = (static_cast<uint32_t>(frame.payload[0]) << 24) | (static_cast<uint32_t>(frame.payload[1]) << 16) |
                (static_cast<uint32_t>(frame.payload[2]) << 8) | frame.payload[3];
            const uint8_t flags = frame.payload[4];
            remote_input_key(input, key, (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0);
            return true;
        }
        case DEVELOPMENT_FRAME_INPUT_TOUCH: {
            if (frame.length < 1) {
                return false;
            }
            const uint8_t count = frame.payload[0];
            if (count > REMOTE_INPUT_MAX_TOUCH_POINTS || frame.length != 1 + count * 4) {
                return false;
            }
            RemoteTouchPoint points[REMOTE_INPUT_MAX_TOUCH_POINTS];
            for (uint8_t i = 0; i < count; i++) {
                points[i] = { readU16(frame.payload + 1 + i * 4), readU16(frame.payload + 3 + i * 4) };
            }
            remote_input_touch(input, points, count);
            return true;
        }
        default:
            return false;
    }
}

/** @return false when the connection must close */
bool processFrames(DevelopmentSession* session, RemoteInput* input, uint8_t* receive, size_t& receiveLength) {
    size_t offset = 0;
    bool connected = true;
    while (connected) {
        DevelopmentFrame frame {};
        size_t consumed = 0;
        const auto result = development_frame_parse(receive + offset, receiveLength - offset, &frame, &consumed);
        if (result == DevelopmentFrameParseResult::Incomplete) {
            break;
        }
        connected = result == DevelopmentFrameParseResult::Ok && handleFrame(session, input, frame);
        offset += consumed;
    }
    memmove(receive, receive + offset, receiveLength - offset);
    receiveLength -= offset;
    return connected;
}

// endregion

void releaseShadow(Shared& s) {
    s.active.store(false);
    mutex_lock(&s.mutex);
    uint8_t* pixels = s.pixels;
    s.pixels = nullptr;
    s.display = nullptr;
    s.dirty = false;
    mutex_unlock(&s.mutex);
    memory_free(pixels);
}

} // namespace

extern "C" void development_remote_start(void) {
    // Allocates the shared state now, so that the display hook never has to
    shared();
    display_set_hook(&DISPLAY_HOOK);
}

extern "C" void development_remote_stop(void) {
    display_set_hook(nullptr);
}

extern "C" void development_remote_set_ui_handlers(const DevelopmentRemoteUiHandlers* handlers) {
    uiHandlers = handlers;
}

void development_remote_run(DevelopmentSession* session, TaskEventGroup* eventGroup, AppEventSubscription* subscription, bool* closeRequested) {
#ifdef ESP_PLATFORM
    if (memory_external_total() == 0) {
        sendFail(session, "remote access needs external RAM");
        return;
    }
#endif

    Shared& s = shared();
    Device* display = nullptr;
    if (device_get_first_active_by_type(&DISPLAY_TYPE, &display) != ERROR_NONE) {
        sendFail(session, "no display");
        return;
    }

    const DisplayColorFormat displayFormat = display_get_color_format(display);
    const bool monochrome = displayFormat == DISPLAY_COLOR_FORMAT_MONOCHROME;
    // The viewer gets monochrome images as grayscale
    const DisplayColorFormat format = monochrome ? DISPLAY_COLOR_FORMAT_GRAYSCALE8 : displayFormat;
    const uint8_t bpp = bytesPerPixelOf(format);
    const uint16_t nativeWidth = display_get_resolution_x(display);
    const uint16_t nativeHeight = display_get_resolution_y(display);
    if (bpp == 0) {
        device_put(display);
        sendFail(session, "unsupported display format");
        return;
    }

    auto* pixels = static_cast<uint8_t*>(memory_calloc_with_policy(1, static_cast<size_t>(nativeWidth) * nativeHeight * bpp, &MEMORY_POLICY));
    auto* receive = static_cast<uint8_t*>(memory_alloc_with_policy(RECEIVE_BUFFER_SIZE, &MEMORY_POLICY));
    Sender sender;
    sender.session = session;
    sender.transmit = static_cast<uint8_t*>(memory_alloc_with_policy(TRANSMIT_SIZE, &MEMORY_POLICY));
    sender.strip = static_cast<uint8_t*>(memory_alloc_with_policy(STRIP_RAW_MAX, &MEMORY_POLICY));
#ifdef ESP_PLATFORM
    sender.compressor = static_cast<tdefl_compressor*>(memory_alloc_with_policy(sizeof(tdefl_compressor), &MEMORY_POLICY));
    sender.compressed = static_cast<uint8_t*>(memory_alloc_with_policy(STRIP_RAW_MAX, &MEMORY_POLICY));
    if (sender.compressor == nullptr || sender.compressed == nullptr) {
        LOG_W(TAG, "Compression is unavailable");
        memory_free(sender.compressor);
        memory_free(sender.compressed);
        sender.compressor = nullptr;
        sender.compressed = nullptr;
    }
#endif
    RemoteInput* input = remote_input_alloc();

    if (pixels == nullptr || receive == nullptr || sender.transmit == nullptr || sender.strip == nullptr || input == nullptr) {
        LOG_E(TAG, "Out of memory");
        sendFail(session, "out of memory");
    } else {
        const uint8_t initialOrientation = readOrientation();
        mutex_lock(&s.mutex);
        s.orientation = initialOrientation;
        trackDevice(s, display);
        // A driver reports its configured state, not the one that was set later
        if (monochrome) {
            memset(pixels, 0xFF, static_cast<size_t>(nativeWidth) * nativeHeight * bpp);
        }
        // The getters of a display without the capability are not implemented
        const bool canSwap = display_has_capability(display, DISPLAY_CAPABILITY_CAP_SWAP_XY);
        const bool canMirror = display_has_capability(display, DISPLAY_CAPABILITY_CAP_MIRROR);
        const bool baselineSwap = canSwap && display_get_swap_xy(display);
        const bool swap = s.swapKnown ? s.trackedSwapXy : baselineSwap;
        const bool transposed = swap != baselineSwap;
        s.display = display;
        s.pixels = pixels;
        s.nativeWidth = nativeWidth;
        s.nativeHeight = nativeHeight;
        s.bytesPerPixel = bpp;
        s.colorFormat = format;
        s.monochrome = monochrome;
        s.swapXy = swap;
        s.baselineSwapXy = baselineSwap;
        s.width = transposed ? nativeHeight : nativeWidth;
        s.height = transposed ? nativeWidth : nativeHeight;
        s.mirrorX = s.mirrorKnown ? s.trackedMirrorX : (canMirror && display_get_mirror_x(display));
        s.mirrorY = s.mirrorKnown ? s.trackedMirrorY : (canMirror && display_get_mirror_y(display));
        s.invert = s.invertKnown && s.trackedInvert;
        s.on = !s.onKnown || s.trackedOn;
        s.stateChanged = false;
        s.dirty = false;
        s.active.store(true);
        uint8_t info[STATE_SIZE];
        writeState(info, s);
        mutex_unlock(&s.mutex);
        pixels = nullptr;

        memcpy(sender.transmit + DEVELOPMENT_FRAME_HEADER_SIZE, info, sizeof(info));
        bool connected = sendFrame(sender, DEVELOPMENT_FRAME_REMOTE_INFO, sizeof(info));
        if (connected) {
            if (uiHandlers != nullptr && uiHandlers->redraw != nullptr) {
                uiHandlers->redraw();
            }
            LOG_I(TAG, "Streaming %ux%u", static_cast<unsigned>(s.width), static_cast<unsigned>(s.height));
        }

        size_t receiveLength = 0;
        TickType_t lastOrientationPoll = xTaskGetTickCount();
        while (connected && !*closeRequested) {
            const int result = mbedtls_ssl_read(&session->ssl, receive + receiveLength, RECEIVE_BUFFER_SIZE - receiveLength);
            if (result > 0) {
                receiveLength += static_cast<size_t>(result);
                connected = processFrames(session, input, receive, receiveLength);
            } else if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
                task_event_group_wait_any(eventGroup, nullptr, pdMS_TO_TICKS(POLL_INTERVAL_MS));
            } else {
                connected = false;
            }
            pollOrientation(s, lastOrientationPoll);
            connected = connected && sendUpdates(sender, s);
            AppEvent event {};
            while (app_event_poll(subscription, &event) == ERROR_NONE) {
                if (event.type == APP_EVENT_CLOSE) {
                    *closeRequested = true;
                }
            }
        }
    }

    if (input != nullptr) {
        remote_input_free(input);
    }
    releaseShadow(s);
    // Only freed when the shadow was never handed over
    memory_free(pixels);
    memory_free(receive);
    memory_free(sender.transmit);
    memory_free(sender.strip);
    memory_free(sender.compressed);
#ifdef ESP_PLATFORM
    memory_free(sender.compressor);
#endif
    device_put(display);
    LOG_I(TAG, "Remote session ended");
}
