// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_panel.h"

#include <algorithm>

/** Rectangle in native panel pixels
 * @note x1 and y1 are exclusive. 
 */
struct Gdep103tc2Region {
    int x0, y0, x1, y1;

    bool empty() const { return x1 <= x0 || y1 <= y0; }

    void merge(const Gdep103tc2Region& other) {
        if (other.empty()) return;
        if (empty()) {
            *this = other;
            return;
        }
        x0 = std::min(x0, other.x0);
        y0 = std::min(y0, other.y0);
        x1 = std::max(x1, other.x1);
        y1 = std::max(y1, other.y1);
    }
};

static constexpr Gdep103tc2Region EMPTY_REGION = { 0, 0, 0, 0 };
static constexpr Gdep103tc2Region FULL_REGION = { 0, 0, PANEL_WIDTH, PANEL_HEIGHT };
