#pragma once

enum class Tab5Variant {
    Ili9881c_Gt911,  // Older variant
    St7123,          // Newer variant (default)
};

[[nodiscard]] Tab5Variant detectVariant();
