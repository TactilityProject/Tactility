#include <tactility/drivers/i2c_controller.h>

#define GPIO_API(dev) ((struct i2c_controller_api*)dev->api)

bool i2c_controller_read(struct device* dev, uint8_t address, uint8_t* data, size_t dataSize, TickType_t timeout) {
    return GPIO_API(dev)->read(dev, address, data, dataSize, timeout);
}

bool i2c_controller_write(struct device* dev, uint8_t address, const uint8_t* data, uint16_t dataSize, TickType_t timeout) {
    return GPIO_API(dev)->write(dev, address, data, dataSize, timeout);
}

bool i2c_controller_write_read(struct device* dev, uint8_t address, const uint8_t* write_data, size_t write_data_size, uint8_t* read_data, size_t read_data_size, TickType_t timeout) {
    return GPIO_API(dev)->write_read(dev, address, write_data, write_data_size, read_data, read_data_size, timeout);
}
