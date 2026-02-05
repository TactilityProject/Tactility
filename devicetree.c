// Default headers
#include <tactility/device.h>
// DTS headers
#include <tactility/bindings/root.h>
#include <tactility/bindings/esp32_gpio.h>
#include <tactility/bindings/esp32_i2c.h>
#include <tactility/bindings/esp32_i2s.h>

static const root_config_dt root_config = {
	"LilyGO T-Deck"
};

static struct Device root = {
	.name = "/",
	.config = &root_config,
	.parent = NULL,
};

static const esp32_i2s_config_dt i2s0_config = {
	I2S_NUM_0,
	7,
	5,
	6,
	GPIO_PIN_NONE,
	GPIO_PIN_NONE
};

static struct Device i2s0 = {
	.name = "i2s0",
	.config = &i2s0_config,
	.parent = &root,
};

static const esp32_i2c_config_dt i2c0_config = {
	I2C_NUM_0,
	400000,
	18,
	8,
	0,
	0
};

static struct Device i2c0 = {
	.name = "i2c0",
	.config = &i2c0_config,
	.parent = &root,
};

static const esp32_i2c_config_dt i2c1_config = {
	I2C_NUM_1,
	400000,
	43,
	44,
	0,
	0
};

static struct Device i2c1 = {
	.name = "i2c1",
	.config = &i2c1_config,
	.parent = &root,
};

static const esp32_gpio_config_dt gpio0_config = {
