#pragma once

// Starts the 24 MHz clock output on GPIO 36 the SC2356 MIPI CSI sensor needs before
// esp_video_init(). Called from module.cpp's start().
void tab5_camera_init();
