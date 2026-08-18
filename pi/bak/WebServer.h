#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "SkyCatalogue.h"

// ============================================================
// Telescope state
// ============================================================

struct TelescopeWebState
{
    bool connected = false;

    bool waiting = true;

    std::string model =
        "Unknown";

    uint8_t firmwareMajor =
        0;

    uint8_t firmwareMinor =
        0;

    bool aligned =
        false;

    bool gotoInProgress =
        false;

    uint8_t trackingMode =
        255;

    double azimuth =
        0.0;

    double altitude =
        0.0;

    double ra =
        0.0;

    double dec =
        0.0;

    bool positionValid =
        false;

    bool altitudeWarning =
        false;

    std::string altitudeStatus =
        "UNKNOWN";

    uint64_t updatedAt =
        0;
};


// ============================================================
// GPS state
// ============================================================

struct GPSWebState
{
    bool portOpen =
        false;

    bool fix =
        false;

    bool dateTimeValid =
        false;

    double latitude =
        0.0;

    double longitude =
        0.0;

    double altitude =
        0.0;

    uint32_t satellites =
        0;

    double hdop =
        0.0;

    int year =
        0;

    int month =
        0;

    int day =
        0;

    int hour =
        0;

    int minute =
        0;

    int second =
        0;

    uint64_t updatedAt =
        0;
};


// ============================================================
// Environment state
// ============================================================

struct EnvironmentWebState
{
    bool valid =
        false;

    float temperatureC =
        0.0f;

    float humidityPercent =
        0.0f;

    double dewPointC =
        0.0;

    std::string dewRisk =
        "UNKNOWN";

    uint64_t updatedAt =
        0;
};


// ============================================================
// Environment history
// ============================================================

struct EnvironmentHistorySample
{
    uint64_t timestampMs =
        0;

    double temperatureC =
        0.0;

    double humidityPercent =
        0.0;

    double dewPointC =
        0.0;
};


// ============================================================
// GOTO history
// ============================================================

struct GotoHistoryEntry
{
    std::string timestamp;

    double targetRa =
        0.0;

    double targetDec =
        0.0;

    double finalRa =
        0.0;

    double finalDec =
        0.0;

    double errorArcmin =
        0.0;

    uint64_t durationMs =
        0;

    bool wasAligned =
        false;

    bool pointingMeasured =
        false;

    std::string result;
};


// ============================================================
// Slew state
// ============================================================

struct SlewWebState
{
    bool active =
        false;

    double targetRa =
        0.0;

    double targetDec =
        0.0;

    double remainingDegrees =
        0.0;

    uint64_t elapsedMs =
        0;

    std::string status =
        "IDLE";
};


// ============================================================
// Alignment / pointing quality
// ============================================================

struct AlignmentQualityState
{
    bool measured =
        false;

    uint32_t completedPoints =
        0;

    double rmsArcmin =
        0.0;

    double worstArcmin =
        0.0;
};


// ============================================================
// Complete observatory state
// ============================================================

struct ObservatoryWebState
{
    TelescopeWebState telescope;

    GPSWebState gps;

    EnvironmentWebState environment;

    SkyState sky;

    SlewWebState slew;

    AlignmentQualityState alignmentQuality;

    std::vector<GotoHistoryEntry>
        gotoHistory;
};


// ============================================================
// Web server
// ============================================================

class WebServer
{
public:

    WebServer();

    ~WebServer();


    bool begin(
        uint16_t port
    );


    void update(
        const ObservatoryWebState& state
    );


    void addEnvironmentSample(
        double temperatureC,
        double humidityPercent,
        double dewPointC,
        uint64_t timestampMs
    );


    void handleRequests();


    void stop();


    void setAbortGotoHandler(
        std::function<bool()> handler
    );


private:

    int _serverSocket;

    uint16_t _port;

    ObservatoryWebState _state;

    std::function<bool()>
        _abortGotoHandler;


    std::vector<EnvironmentHistorySample>
        _environmentHistory;


    static constexpr uint64_t
        ENVIRONMENT_HISTORY_DURATION_MS =
            24ULL *
            60ULL *
            60ULL *
            1000ULL;


    bool configureSocket();


    void handleClient(
        int clientSocket
    );


    void trimEnvironmentHistory(
        uint64_t nowMs
    );


    std::string buildHtml() const;


    std::string telescopeConnectionName() const;


    std::string trackingModeName() const;


    static std::string htmlEscape(
        const std::string& value
    );


    static std::string formatDuration(
        uint64_t milliseconds
    );
};