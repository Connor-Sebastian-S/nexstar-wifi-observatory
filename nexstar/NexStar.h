#pragma once

#include <Arduino.h>


class NexStar
{
public:

    NexStar();


    // ========================================================
    // Transport
    // ========================================================

    void setSerial(
        HardwareSerial* serial
    );

    bool begin();


    // ========================================================
    // Connection
    // ========================================================

    bool isConnected();


    // ========================================================
    // State
    // ========================================================

    bool isAligned();

    bool isGotoInProgress();

    uint8_t getTrackingMode();


    // ========================================================
    // Position
    // ========================================================

    bool getRaDec(
        double& ra,
        double& dec
    );


    bool getRaDecPrecise(
        double& ra,
        double& dec
    );


    bool getAzAlt(
        double& azimuth,
        double& altitude
    );


    bool getAzAltPrecise(
        double& azimuth,
        double& altitude
    );


    // ========================================================
    // GOTO
    // ========================================================

    bool gotoRaDecPrecise(
        double ra,
        double dec
    );


    bool abortGoto();


    // ========================================================
    // Time / Location
    // ========================================================

    bool setLocation(
        double latitude,
        double longitude
    );


    bool setTime(
        int hour,
        int minute,
        int second,
        int month,
        int day,
        int year,
        int timezoneOffsetHours,
        bool daylightSaving
    );


    // ========================================================
    // Information
    // ========================================================

    bool getDeviceVersion(
        uint8_t& major,
        uint8_t& minor
    );


    bool getDeviceModel(
        char* model,
        size_t modelSize
    );


private:

    HardwareSerial* _serial =
        nullptr;


    bool _connected =
        false;


    // ========================================================
    // Raw protocol
    // ========================================================

    bool query(
        const char* command,
        char* response,
        size_t responseSize,
        uint32_t timeout = 1000
    );


    bool writeCommand(
        const char* command
    );


    bool writeRawCommand(
        const uint8_t* data,
        size_t length
    );


    bool readResponse(
        char* response,
        size_t responseSize,
        uint32_t timeout
    );


    // ========================================================
    // Angle conversion
    // ========================================================

    static double decodeAngle16(
        const char* value
    );


    static double decodeAngle32(
        const char* value
    );


    static void degreesToDMS(
        double degrees,
        int& wholeDegrees,
        int& minutes,
        int& seconds
    );
};