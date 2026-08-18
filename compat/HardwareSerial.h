#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <termios.h>


class HardwareSerial
{
public:

    HardwareSerial();

    ~HardwareSerial();


    // ========================================================
    // Serial device
    // ========================================================

    bool begin(
        const char* device,
        uint32_t baud
    );

    void end();


    // ========================================================
    // Input
    // ========================================================

    int available();

    int read();


    // ========================================================
    // Output
    // ========================================================

    size_t write(
        const uint8_t* data,
        size_t length
    );

    size_t write(
        uint8_t data
    );

    void flush();


    // ========================================================
    // Arduino-style diagnostic output
    // ========================================================

    void print(
        const char* text
    );

    void print(
        const char* text,
        int value
    );

    void println();

    void println(
        const char* text
    );

    void println(
        int value
    );


    // ========================================================
    // State
    // ========================================================

    bool isOpen() const;


private:

    int _fd;

    std::string _device;

    uint32_t _baud;


    static int baudToTermios(
        uint32_t baud
    );

    void configurePort();
};