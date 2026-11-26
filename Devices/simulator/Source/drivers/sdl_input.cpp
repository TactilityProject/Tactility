// SPDX-License-Identifier: Apache-2.0
#include "sdl_input.h"
#include "sdl_display.h"

#include <tactility/Memory.hpp>
#include <tactility/check.h>
#include <tactility/drivers/keyboard.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace {

constexpr size_t KEY_QUEUE_CAPACITY = 32;

std::mutex state_mutex;

// The position in window coordinates, and the size of the window then
SdlPointerState pointer_state = { 0, 0, false };
int window_width = 0;
int window_height = 0;

struct KeyQueue {
    KeyboardKeyData events[KEY_QUEUE_CAPACITY] = {};
    size_t head = 0;
    size_t count = 0;
};

// Allocated on first use and never freed. Guarded by state_mutex.
KeyQueue& key_queue() {
    static KeyQueue* instance = tt::newObject<KeyQueue>();
    check(instance != nullptr);
    return *instance;
}

bool text_input_started = false;

// A two-finger swipe on a trackpad arrives as wheel events. It is turned into a touch drag that starts where the pointer is,
// so it scrolls and swipes the same way as a finger does on a device. The drag ends when the wheel events stop.
constexpr uint32_t WHEEL_SWIPE_END_MS = 150;
constexpr float WHEEL_SWIPE_PIXELS_PER_UNIT = 12.0f;
bool wheel_swipe_active = false;
// The press is seen before the first move, as a press that starts at its moved position isn't a drag
bool wheel_swipe_started_now = false;
int32_t wheel_swipe_origin_x = 0;
int32_t wheel_swipe_origin_y = 0;
float wheel_swipe_offset_x = 0.0f;
float wheel_swipe_offset_y = 0.0f;
uint32_t wheel_swipe_last_ms = 0;

struct HeldControlKey {
    // The press, so that its release reports the same modifiers. The key is 0 when the slot is free.
    KeyboardKeyData data = {};
    uint8_t ref_count = 0;
};

constexpr size_t MAX_HELD_CONTROL_KEYS = 11;
HeldControlKey held_control_keys[MAX_HELD_CONTROL_KEYS] = {};

struct HeldTextKey {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    KeyboardKeyData data = {};
};

constexpr size_t MAX_HELD_TEXT_KEYS = 11;
HeldTextKey held_text_keys[MAX_HELD_TEXT_KEYS] = {};

// The key down that the next text input belongs to
struct TextKeyDown {
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    uint8_t modifiers = 0;
};

TextKeyDown last_text_keydown;

KeyboardKeyData last_reported = {};

KeyboardKeyData make_key_data(uint32_t key, uint8_t modifiers) {
    KeyboardKeyData data = {};
    data.key = key;
    data.modifiers = modifiers;
    return data;
}

// The modifiers of an SDL event, which are the ones from when the key was pressed rather than the current ones
uint8_t to_modifiers(uint16_t sdl_modifiers) {
    return (sdl_modifiers & KMOD_SHIFT) != 0 ? KEYBOARD_MODIFIER_SHIFT : 0;
}

bool mark_control_key_held(const KeyboardKeyData& data) {
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.data.key == data.key) {
            slot.ref_count++;
            return false;
        }
    }
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.ref_count == 0) {
            slot.data = data;
            slot.ref_count = 1;
            return true;
        }
    }
    return true; // unreachable
}

// Replaces the data with the one of the press, when there was one
bool clear_control_key_held(KeyboardKeyData& data) {
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.data.key == data.key) {
            data = slot.data;
            slot.ref_count--;
            if (slot.ref_count == 0) {
                slot.data = {};
                return true;
            }
            return false;
        }
    }
    return true;
}

const HeldControlKey* find_any_held_control_key() {
    for (const HeldControlKey& slot : held_control_keys) {
        if (slot.data.key != 0) {
            return &slot;
        }
    }
    return nullptr;
}

void push_key(KeyboardKeyData data, bool pressed) {
    if (data.key == 0) {
        return;
    }
    data.pressed = pressed;
    KeyQueue& queue = key_queue();
    if (queue.count >= KEY_QUEUE_CAPACITY) {
        size_t evict_offset = 0;
        for (size_t i = 0; i < queue.count; i++) {
            if (queue.events[(queue.head + i) % KEY_QUEUE_CAPACITY].pressed) {
                evict_offset = i;
                break;
            }
        }
        for (size_t i = evict_offset; i + 1 < queue.count; i++) {
            queue.events[(queue.head + i) % KEY_QUEUE_CAPACITY] = queue.events[(queue.head + i + 1) % KEY_QUEUE_CAPACITY];
        }
        queue.count--;
    }
    queue.events[(queue.head + queue.count) % KEY_QUEUE_CAPACITY] = data;
    queue.count++;
    last_reported = data;

    if (!pressed) {
        const HeldControlKey* other_held_key = find_any_held_control_key();
        if (other_held_key != nullptr) {
            last_reported = other_held_key->data;
            last_reported.pressed = true;
        }
    }
}

// @return the data of the held key, which is the one of the first press while it types the same character, or null without a free slot
const KeyboardKeyData* hold_text_key(SDL_Scancode scancode, const KeyboardKeyData& data) {
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return nullptr;
    }
    for (HeldTextKey& slot : held_text_keys) {
        if (slot.scancode == scancode) {
            if (slot.data.key != data.key) {
                push_key(slot.data, false);
                slot.data = data;
            }
            return &slot.data;
        }
    }
    for (HeldTextKey& slot : held_text_keys) {
        if (slot.scancode == SDL_SCANCODE_UNKNOWN) {
            slot.scancode = scancode;
            slot.data = data;
            return &slot.data;
        }
    }
    return nullptr;
}

void release_text_key(SDL_Scancode scancode) {
    for (HeldTextKey& slot : held_text_keys) {
        if (slot.scancode == scancode) {
            const KeyboardKeyData data = slot.data;
            slot = HeldTextKey {};
            push_key(data, false);
            return;
        }
    }
}

// The OS does not deliver SDL_KEYUP for keys that were down when the window lost keyboard
// focus, so nothing else would ever release them.
void release_all_held_keys() {
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.data.key != 0) {
            const KeyboardKeyData data = slot.data;
            slot = HeldControlKey {};
            push_key(data, false);
        }
    }
    for (HeldTextKey& slot : held_text_keys) {
        if (slot.scancode != SDL_SCANCODE_UNKNOWN) {
            const KeyboardKeyData data = slot.data;
            slot = HeldTextKey {};
            push_key(data, false);
        }
    }
    last_text_keydown = {};
}

void set_pointer_position(int32_t window_x, int32_t window_y) {
    pointer_state.x = window_x;
    pointer_state.y = window_y;
    SDL_Renderer* renderer = sdl_display_get_renderer();
    SDL_Window* window = renderer != nullptr ? SDL_RenderGetWindow(renderer) : nullptr;
    if (window != nullptr) {
        SDL_GetWindowSize(window, &window_width, &window_height);
    }
}

void map_to_view(int32_t* x, int32_t* y) {
    int view_width, view_height;
    if (window_width <= 0 || window_height <= 0 || !sdl_display_get_view_size(&view_width, &view_height) || view_width <= 0 || view_height <= 0) {
        return;
    }
    const float scale = std::min(static_cast<float>(window_width) / view_width, static_cast<float>(window_height) / view_height);
    const float offset_x = (window_width - view_width * scale) / 2.0f;
    const float offset_y = (window_height - view_height * scale) / 2.0f;
    *x = std::clamp(static_cast<int32_t>((*x - offset_x) / scale), 0, view_width - 1);
    *y = std::clamp(static_cast<int32_t>((*y - offset_y) / scale), 0, view_height - 1);
    sdl_display_view_to_native(x, y);
}

uint32_t keycode_to_key(SDL_Keycode sdl_key) {
    switch (sdl_key) {
        case SDLK_RIGHT: return CODEPOINT_ARROW_RIGHT;
        case SDLK_LEFT: return CODEPOINT_ARROW_LEFT;
        case SDLK_UP: return CODEPOINT_ARROW_UP;
        case SDLK_DOWN: return CODEPOINT_ARROW_DOWN;
        case SDLK_ESCAPE: return CODEPOINT_ESCAPE;
        case SDLK_BACKSPACE: return CODEPOINT_BACKSPACE;
        case SDLK_DELETE: return CODEPOINT_DELETE;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return CODEPOINT_ENTER;
        case SDLK_TAB: return CODEPOINT_TAB;
        case SDLK_HOME: return CODEPOINT_HOME;
        case SDLK_END: return CODEPOINT_END;
        default: return 0;
    }
}

}

void sdl_input_pump() {
    bool quit_requested = false;

    {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (!text_input_started) {
            SDL_StartTextInput();
            text_input_started = true;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_MOUSEMOTION: {
                    // Read like a press does: event.motion.x/y can differ from the live position, e.g. on a scaled display,
                    // which made a drag jump sideways right after the press
                    int live_x, live_y;
                    SDL_GetMouseState(&live_x, &live_y);
                    set_pointer_position(live_x, live_y);
                    break;
                }
                case SDL_MOUSEWHEEL: {
                    // A pressed mouse button is a drag of its own
                    if (pointer_state.pressed && !wheel_swipe_active) {
                        break;
                    }
                    float delta_x = static_cast<float>(event.wheel.x);
                    float delta_y = static_cast<float>(event.wheel.y);
#if SDL_VERSION_ATLEAST(2, 0, 18)
                    delta_x = event.wheel.preciseX;
                    delta_y = event.wheel.preciseY;
#endif
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                        delta_x = -delta_x;
                        delta_y = -delta_y;
                    }
                    if (!wheel_swipe_active) {
                        int live_x, live_y;
                        SDL_GetMouseState(&live_x, &live_y);
                        set_pointer_position(live_x, live_y);
                        wheel_swipe_origin_x = live_x;
                        wheel_swipe_origin_y = live_y;
                        wheel_swipe_offset_x = 0.0f;
                        wheel_swipe_offset_y = 0.0f;
                        pointer_state.pressed = true;
                        wheel_swipe_active = true;
                        wheel_swipe_started_now = true;
                    }
                    wheel_swipe_offset_x += delta_x * WHEEL_SWIPE_PIXELS_PER_UNIT;
                    wheel_swipe_offset_y += delta_y * WHEEL_SWIPE_PIXELS_PER_UNIT;
                    wheel_swipe_last_ms = SDL_GetTicks();
                    break;
                }
                case SDL_MOUSEBUTTONDOWN:
                    wheel_swipe_active = false;
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        // event.button.x/y can be stale immediately after a window resize (an
                        // SDL/X11 event-queue quirk)
                        int live_x, live_y;
                        SDL_GetMouseState(&live_x, &live_y);
                        set_pointer_position(live_x, live_y);
                        pointer_state.pressed = true;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        pointer_state.pressed = false;
                    }
                    break;
                case SDL_KEYDOWN: {
                    uint32_t key = keycode_to_key(event.key.keysym.sym);
                    if (key != 0) {
                        last_text_keydown = {};
                        // event.key.repeat is true for the auto-repeat events the OS sends
                        // while a key is held
                        if (event.key.repeat) {
                            break;
                        }
                        const KeyboardKeyData data = make_key_data(key, to_modifiers(event.key.keysym.mod));
                        if (mark_control_key_held(data)) {
                            push_key(data, true);
                        }
                    } else {
                        last_text_keydown = { event.key.keysym.scancode, to_modifiers(event.key.keysym.mod) };
                    }
                    break;
                }
                case SDL_KEYUP: {
                    uint32_t key = keycode_to_key(event.key.keysym.sym);
                    if (key != 0) {
                        KeyboardKeyData data = make_key_data(key, to_modifiers(event.key.keysym.mod));
                        if (clear_control_key_held(data)) {
                            push_key(data, false);
                        }
                    } else {
                        release_text_key(event.key.keysym.scancode);
                    }
                    break;
                }
                case SDL_TEXTINPUT:
                    // ASCII only
                    if (static_cast<uint8_t>(event.text.text[0]) < 0x80) {
                        const KeyboardKeyData data = make_key_data(static_cast<uint8_t>(event.text.text[0]), last_text_keydown.modifiers);
                        const KeyboardKeyData* held = hold_text_key(last_text_keydown.scancode, data);
                        if (held != nullptr) {
                            // Held until the physical key comes up, like a control key
                            push_key(*held, true);
                        } else {
                            // No KEYDOWN to pair with or no free slot: fall back to an
                            // instantaneous tap
                            push_key(data, true);
                            push_key(data, false);
                        }
                    }
                    break;
                case SDL_WINDOWEVENT:
                    // Resizing doesn't change what LVGL last rendered, only how large it should
                    // appear so re-present the existing frame at the new scale immediately
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        window_width = event.window.data1;
                        window_height = event.window.data2;
                        sdl_display_present_now();
                    } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        release_all_held_keys();
                    }
                    break;
                case SDL_QUIT:
                    quit_requested = true;
                    break;
                default:
                    break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (wheel_swipe_active) {
            if (wheel_swipe_started_now) {
                wheel_swipe_started_now = false;
            } else {
                set_pointer_position(wheel_swipe_origin_x + static_cast<int32_t>(wheel_swipe_offset_x), wheel_swipe_origin_y + static_cast<int32_t>(wheel_swipe_offset_y));
            }
            if (SDL_GetTicks() - wheel_swipe_last_ms > WHEEL_SWIPE_END_MS) {
                pointer_state.pressed = false;
                wheel_swipe_active = false;
            }
        }
    }

    if (quit_requested) {
        exit(0);
    }
}

void sdl_input_get_pointer_state(SdlPointerState* out_state) {
    std::lock_guard<std::mutex> lock(state_mutex);
    *out_state = pointer_state;
    map_to_view(&out_state->x, &out_state->y);
}

bool sdl_input_pop_key(KeyboardKeyData* out_data) {
    std::lock_guard<std::mutex> lock(state_mutex);
    KeyQueue& queue = key_queue();
    if (queue.count == 0) {
        return false;
    }
    *out_data = queue.events[queue.head];
    queue.head = (queue.head + 1) % KEY_QUEUE_CAPACITY;
    queue.count--;
    return true;
}

bool sdl_input_has_queued_key() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return key_queue().count > 0;
}

bool sdl_input_get_held_key(KeyboardKeyData* out_data) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!last_reported.pressed || last_reported.key == 0) {
        return false;
    }
    for (const HeldControlKey& slot : held_control_keys) {
        if (slot.data.key == last_reported.key) {
            *out_data = last_reported;
            return true;
        }
    }
    return false;
}
