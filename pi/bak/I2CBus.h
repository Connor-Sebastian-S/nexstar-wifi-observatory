#pragma once

#include <cstddef>
#include <cstdint>
#include <string>


class I2CBus
{
public:

    I2CBus();

    ~I2CBus();


    bool begin(
        const char* device
    );


    void end();


    bool isOpen() const;


    bool write(
        uint8_t address,
        const uint8_t* data,
        size_t length
    );


    bool read(
        uint8_t address,
        uint8_t* data,
        size_t length
    );


    bool writeRead(
        uint8_t address,
        const uint8_t* writeData,
        size_t writeLength,
        uint8_t* readData,
        size_t readLength
    );


private:

    int _fd;

    std::string _device;
};