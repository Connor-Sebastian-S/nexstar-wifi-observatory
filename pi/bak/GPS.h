#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <termios.h>


struct GPSState
{
    bool fix = false;
    bool dateTimeValid = false;

    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;

    uint32_t satellites = 0;
    double hdop = 0.0;

    int year = 0;
    int month = 0;
    int day = 0;

    int hour = 0;
    int minute = 0;
    int second = 0;

    uint64_t updatedAt = 0;
};


class GPS
{
public:

    GPS();

    ~GPS();


    bool begin(
        const char* device,
        uint32_t baud
    );


    void update();


    const GPSState& state() const;


    bool isOpen() const;


private:

    int _fd;

    std::string _device;

    uint32_t _baud;

    std::string _line;

    GPSState _state;


    static speed_t baudToTermios(
        uint32_t baud
    );


    bool configurePort();


    void processByte(
        char byte
    );


    void processLine(
        const std::string& line
    );


    bool parseGGA(
        const std::string& line
    );


    bool parseRMC(
        const std::string& line
    );


    static std::vector<std::string> split(
        const std::string& value,
        char delimiter
    );


    static bool parseLatitude(
        const std::string& value,
        char hemisphere,
        double& result
    );


    static bool parseLongitude(
        const std::string& value,
        char hemisphere,
        double& result
    );


    static bool parseTime(
        const std::string& value,
        int& hour,
        int& minute,
        int& second
    );


    static bool parseDate(
        const std::string& value,
        int& day,
        int& month,
        int& year
    );


    static double parseDouble(
        const std::string& value
    );


    static int parseInt(
        const std::string& value
    );
};