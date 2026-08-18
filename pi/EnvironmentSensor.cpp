#include "EnvironmentSensor.h"

#include <cmath>
#include <unistd.h>


// ============================================================
// TCA9548A
// ============================================================

TCA9548A::TCA9548A(
    I2CBus& bus,
    uint8_t address
)
    : _bus(bus),
      _address(address)
{
}


bool TCA9548A::begin()
{
    return selectChannel(
        0
    );
}


bool TCA9548A::selectChannel(
    uint8_t channel
)
{
    if (
        channel > 7
    )
    {
        return false;
    }


    uint8_t value =
        static_cast<uint8_t>(
            1U << channel
        );


    return
        _bus.write(
            _address,
            &value,
            1
        );
}


// ============================================================
// Environment sensor
// ============================================================

EnvironmentSensor::EnvironmentSensor(
    I2CBus& bus,
    TCA9548A& mux
)
    : _bus(bus),
      _mux(mux)
{
}


bool EnvironmentSensor::begin()
{
    if (
        !_mux.selectChannel(
            MUX_CHANNEL
        )
    )
    {
        return false;
    }


    /*
     * The sensor ACKs its address.
     */

    uint8_t command[2] = {
        0xF3,
        0x2D
    };


    uint8_t response[3] = {};


    return
        _bus.writeRead(
            SENSOR_ADDRESS,
            command,
            2,
            response,
            3
        );
}


bool EnvironmentSensor::update(
    EnvironmentState& state
)
{
    if (
        !_mux.selectChannel(
            MUX_CHANNEL
        )
    )
    {
        state.valid = false;

        return false;
    }


    // --------------------------------------------------------
    // SHT3x single-shot, high-repeatability measurement.
    // --------------------------------------------------------

    uint8_t command[2] = {
        0x24,
        0x00
    };


    if (
        !_bus.write(
            SENSOR_ADDRESS,
            command,
            2
        )
    )
    {
        state.valid = false;

        return false;
    }


    usleep(
        20000
    );


    uint8_t data[6] = {};


    if (
        !_bus.read(
            SENSOR_ADDRESS,
            data,
            6
        )
    )
    {
        state.valid = false;

        return false;
    }


    // --------------------------------------------------------
    // Verify CRCs.
    // --------------------------------------------------------

    if (
        !checkCrc(
            data[0],
            data[1],
            data[2]
        )
    )
    {
        state.valid = false;

        return false;
    }


    if (
        !checkCrc(
            data[3],
            data[4],
            data[5]
        )
    )
    {
        state.valid = false;

        return false;
    }


    uint16_t rawTemperature =
        (
            static_cast<uint16_t>(
                data[0]
            )
            << 8
        )
        |
        data[1];


    uint16_t rawHumidity =
        (
            static_cast<uint16_t>(
                data[3]
            )
            << 8
        )
        |
        data[4];


    state.temperatureC =
        -45.0f +
        (
            175.0f *
            (
                static_cast<float>(
                    rawTemperature
                ) /
                65535.0f
            )
        );


    state.humidityPercent =
        100.0f *
        (
            static_cast<float>(
                rawHumidity
            ) /
            65535.0f
        );


    state.valid = true;


    return true;
}


// ============================================================
// SHT3x CRC-8
// ============================================================

bool EnvironmentSensor::checkCrc(
    uint8_t msb,
    uint8_t lsb,
    uint8_t crc
)
{
    uint8_t value = 0xFF;

    const uint8_t bytes[2] = {
        msb,
        lsb
    };


    for (uint8_t byte : bytes)
    {
        value ^= byte;


        for (int bit = 0; bit < 8; ++bit)
        {
            if (
                value & 0x80
            )
            {
                value =
                    static_cast<uint8_t>(
                        (
                            value << 1
                        ) ^
                        0x31
                    );
            }
            else
            {
                value =
                    static_cast<uint8_t>(
                        value << 1
                    );
            }
        }
    }


    return value == crc;
}
