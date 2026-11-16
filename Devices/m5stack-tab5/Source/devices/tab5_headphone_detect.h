#pragma once

// Starts/stops the periodic headphone-jack detection poll (HP_DET pin on io_expander0), which
// tells the speaker amplifier (see tab5_speaker_amp.h) whether headphones are plugged in. Called from module.cpp's start()/stop().
void tab5_headphone_detect_start();
void tab5_headphone_detect_stop();
