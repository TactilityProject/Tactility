#pragma once

// Starts/stops the periodic headphone-jack detection poll (HP_DET pin on io_expander0), which
// toggles the speaker amplifier enable pin accordingly. Called from module.cpp's start()/stop().
void tab5_headphone_detect_start();
void tab5_headphone_detect_stop();
