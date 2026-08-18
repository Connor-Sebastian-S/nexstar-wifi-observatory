#pragma once

#include <cstdint>
#include <fstream>
#include <string>


class Recorder
{
public:

    Recorder();

    ~Recorder();


    bool begin(
        const std::string& directory
    );


    void close();


    bool isOpen() const;


    void journal(
        const std::string& message
    );


    void event(
        const std::string& type,
        const std::string& data
    );


    void telescopeSample(
        bool connected,
        bool aligned,
        bool gotoInProgress,
        uint8_t trackingMode,
        double azimuth,
        double altitude,
        double ra,
        double dec
    );


    void gpsSample(
        bool fix,
        double latitude,
        double longitude,
        double altitude,
        uint32_t satellites,
        double hdop
    );


    void environmentSample(
        bool valid,
        double temperature,
        double humidity,
        double dewPoint
    );


private:

    std::ofstream _blackbox;

    std::ofstream _journal;


    static std::string timestamp();

    static uint64_t monotonicMs();


    static std::string csvEscape(
        const std::string& value
    );
};