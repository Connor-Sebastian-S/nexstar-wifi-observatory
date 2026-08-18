#pragma once

#include "I2CBus.h"


struct EnvironmentState
{
    bool valid = false;

    float temperatureC = 0.0f;

    float humidityPercent = 0.0f;

    uint64_t updatedAt = 0;
};


class TCA9548A
{
public:

    TCA9548A(
        I2CBus& bus,
        uint8_t address
    );


    bool begin();


    bool selectChannel(
        uint8_t channel
    );


private:

    I2CBus& _bus;

    uint8_t _address;
};


class EnvironmentSensor
{
public:

    EnvironmentSensor(
        I2CBus& bus,
        TCA9548A& mux
    );


    bool begin();


    bool update(
        EnvironmentState& state
    );


private:

    I2CBus& _bus;

    TCA9548A& _mux;


    static constexpr uint8_t SENSOR_ADDRESS =
        0x44;

    static constexpr uint8_t MUX_CHANNEL =
        0;


    static bool checkCrc(
        uint8_t msb,
        uint8_t lsb,
        uint8_t crc
    );
};