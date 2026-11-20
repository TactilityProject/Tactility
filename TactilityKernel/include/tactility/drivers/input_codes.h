// SPDX-License-Identifier: Apache-2.0
#pragma once

/**
 * Key codes for ButtonEvent::code.
 * Values follow the Linux input subsystem numbering.
 * Only preprocessor definitions live here, so devicetree files can use them.
 */

#define BUTTON_KEY_NONE   0
#define BUTTON_KEY_ESC    1
#define BUTTON_KEY_ENTER  28
#define BUTTON_KEY_HOME   102
#define BUTTON_KEY_UP     103
#define BUTTON_KEY_LEFT   105
#define BUTTON_KEY_RIGHT  106
#define BUTTON_KEY_DOWN   108
#define BUTTON_KEY_POWER  116
#define BUTTON_KEY_STANDBY 142
#define BUTTON_KEY_SUSPEND 205
#define BUTTON_KEY_MENU   139
#define BUTTON_KEY_BACK   158
#define BUTTON_KEY_PROG1  148
#define BUTTON_KEY_PROG2  149
#define BUTTON_KEY_NEXT   0x197
#define BUTTON_KEY_PREVIOUS 0x19c
