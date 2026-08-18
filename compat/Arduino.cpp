#include "Arduino.h"

#include <chrono>
#include <thread>

#include "HardwareSerial.h"


// ============================================================
// Program start time
// ============================================================

static const auto programStart =
    std::chrono::steady_clock::now();


// ============================================================
// millis()
// ============================================================

uint32_t millis()
{
    auto now =
        std::chrono::steady_clock::now();


    auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            now - programStart
        );


    return static_cast<uint32_t>(
        elapsed.count()
    );
}


// ============================================================
// delay()
// ============================================================

void delay(
    uint32_t milliseconds
)
{
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            milliseconds
        )
    );
}


// ============================================================
// Arduino-style Serial object
// ============================================================

HardwareSerial Serial;