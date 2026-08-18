#pragma once

#include <cstddef>
#include <cstdint>


// ============================================================
// Arduino-style integer types
// ============================================================

using uint8_t =
    std::uint8_t;

using uint16_t =
    std::uint16_t;

using uint32_t =
    std::uint32_t;

using int32_t =
    std::int32_t;

using size_t =
    std::size_t;


// ============================================================
// Linux implementation of HardwareSerial
// ============================================================

#include "HardwareSerial.h"


// ============================================================
// Arduino-style timing functions
// ============================================================

uint32_t millis();

void delay(
    uint32_t milliseconds
);


// ============================================================
// Arduino-style global Serial object
// ============================================================

extern HardwareSerial Serial;