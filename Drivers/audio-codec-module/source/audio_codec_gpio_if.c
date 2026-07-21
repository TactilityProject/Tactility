// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <tactility/log.h>

#include <stdlib.h>

#define TAG "AudioCodecGpio"

struct GpioAdapterContext {
    audio_codec_gpio_if_t base;
    const struct GpioPinSpec* pins;
    size_t pin_count;
    struct GpioDescriptor** descriptors;
};

static struct GpioAdapterContext* g_context = NULL;

static struct GpioDescriptor* acquire_descriptor(int16_t gpio) {
    if (g_context == NULL || gpio < 0 || (size_t) gpio >= g_context->pin_count) {
        return NULL;
    }

    if (g_context->descriptors[gpio] != NULL) {
        return g_context->descriptors[gpio];
    }

    const struct GpioPinSpec* spec = &g_context->pins[gpio];
    if (spec->gpio_controller == NULL) {
        return NULL;
    }

    struct GpioDescriptor* descriptor = gpio_descriptor_acquire(spec->gpio_controller, spec->pin, GPIO_OWNER_GPIO);
    if (descriptor != NULL) {
        gpio_descriptor_set_flags(descriptor, spec->flags);
        g_context->descriptors[gpio] = descriptor;
    }
    return descriptor;
}

static int gpio_setup(int16_t gpio, audio_gpio_dir_t dir, audio_gpio_mode_t mode) {
    struct GpioDescriptor* descriptor = acquire_descriptor(gpio);
    if (descriptor == NULL) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }

    gpio_flags_t flags = GPIO_FLAG_NONE;
    gpio_descriptor_get_flags(descriptor, &flags);
    flags &= ~(GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_DIRECTION_OUTPUT | GPIO_FLAG_PULL_UP | GPIO_FLAG_PULL_DOWN);

    flags |= (dir == AUDIO_GPIO_DIR_OUT) ? GPIO_FLAG_DIRECTION_OUTPUT : GPIO_FLAG_DIRECTION_INPUT;
    if ((mode & AUDIO_GPIO_MODE_PULL_UP) != 0) {
        flags |= GPIO_FLAG_PULL_UP;
    }
    if ((mode & AUDIO_GPIO_MODE_PULL_DOWN) != 0) {
        flags |= GPIO_FLAG_PULL_DOWN;
    }

    return (gpio_descriptor_set_flags(descriptor, flags) == ERROR_NONE) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int gpio_set(int16_t gpio, bool high) {
    struct GpioDescriptor* descriptor = acquire_descriptor(gpio);
    if (descriptor == NULL) {
        return ESP_CODEC_DEV_NOT_FOUND;
    }
    return (gpio_descriptor_set_level(descriptor, high) == ERROR_NONE) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

// esp_codec_dev's gpio_if_t::get returns plain bool with no error channel, so a failed
// acquire and a legitimate low reading are indistinguishable to the caller; log so the
// failure is at least visible.
static bool gpio_get(int16_t gpio) {
    struct GpioDescriptor* descriptor = acquire_descriptor(gpio);
    if (descriptor == NULL) {
        LOG_E(TAG, "gpio_get: failed to acquire descriptor for pin %d", gpio);
        return false;
    }
    bool high = false;
    gpio_descriptor_get_level(descriptor, &high);
    return high;
}

const audio_codec_gpio_if_t* audio_codec_adapter_new_gpio(const struct GpioPinSpec* pins, size_t pin_count) {
    if (pins == NULL || pin_count == 0) {
        return NULL;
    }

    // esp_codec_dev's gpio_if_t has no per-call user context -- only a single global instance
    // is supported (see g_context usage in acquire_descriptor()/gpio_setup/etc). A second
    // call would silently replace it underneath whatever codec already holds the first
    // pointer, so refuse rather than corrupt that codec's GPIO access.
    if (g_context != NULL) {
        return NULL;
    }

    struct GpioAdapterContext* context = (struct GpioAdapterContext*) calloc(1, sizeof(struct GpioAdapterContext));
    if (context == NULL) {
        return NULL;
    }

    context->descriptors = (struct GpioDescriptor**) calloc(pin_count, sizeof(struct GpioDescriptor*));
    if (context->descriptors == NULL) {
        free(context);
        return NULL;
    }

    context->pins = pins;
    context->pin_count = pin_count;
    context->base.setup = gpio_setup;
    context->base.set = gpio_set;
    context->base.get = gpio_get;

    g_context = context;

    return &context->base;
}

// Note: esp_codec_dev's generic audio_codec_delete_gpio_if() only frees the struct — it doesn't
// know about our descriptor array, so we provide our own cleanup under a non-colliding name.
int audio_codec_adapter_delete_gpio(const audio_codec_gpio_if_t* gpio_if) {
    struct GpioAdapterContext* context = (struct GpioAdapterContext*) gpio_if;
    for (size_t i = 0; i < context->pin_count; i++) {
        if (context->descriptors[i] != NULL) {
            gpio_descriptor_release(context->descriptors[i]);
        }
    }
    free(context->descriptors);

    if (g_context == context) {
        g_context = NULL;
    }
    free(context);
    return ESP_CODEC_DEV_OK;
}
