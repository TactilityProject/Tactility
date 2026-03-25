#pragma once

#include <EspLcdDisplayV2.h>

#include <esp_lcd_mipi_dsi.h>
#include <esp_ldo_regulator.h>

class St7123Display final : public EspLcdDisplayV2 {

    esp_lcd_dsi_bus_handle_t mipiDsiBus = nullptr;
    esp_ldo_channel_handle_t ldoChannel = nullptr;

    bool createMipiDsiBus();

protected:

    bool createIoHandle(esp_lcd_panel_io_handle_t& ioHandle) override;

    esp_lcd_panel_dev_config_t createPanelConfig(std::shared_ptr<EspLcdConfiguration> espLcdConfiguration, gpio_num_t resetPin) override;

    bool createPanelHandle(esp_lcd_panel_io_handle_t ioHandle, const esp_lcd_panel_dev_config_t& panelConfig, esp_lcd_panel_handle_t& panelHandle) override;

    bool useDsiPanel() const override { return true; }

    lvgl_port_display_dsi_cfg_t getLvglPortDisplayDsiConfig(esp_lcd_panel_io_handle_t /*ioHandle*/, esp_lcd_panel_handle_t /*panelHandle*/) override;

public:

    St7123Display(
        const std::shared_ptr<EspLcdConfiguration>& configuration
    ) : EspLcdDisplayV2(configuration) {}

    ~St7123Display() override;

    std::string getName() const override { return "St7123"; }

    std::string getDescription() const override { return "St7123 MIPI-DSI display"; }
};
