#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "HardwareSerial.h"
#include "WebServer.h"
#include "GPS.h"
#include "I2CBus.h"
#include "EnvironmentSensor.h"
#include "StellariumServer.h"
#include "Recorder.h"
#include "SkyCatalogue.h"

#include "../nexstar/NexStar.h"


// ============================================================
// Configuration
// ============================================================

static constexpr const char* NEXSTAR_DEVICE =
    "/dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller_AMCYb112318-if00-port0";

static constexpr uint32_t NEXSTAR_BAUD =
    9600;

static constexpr const char* GPS_DEVICE =
    "/dev/serial0";

static constexpr uint32_t GPS_BAUD =
    115200;

static constexpr const char* I2C_DEVICE =
    "/dev/i2c-1";

static constexpr uint8_t TCA_ADDRESS =
    0x70;

static constexpr uint16_t WEB_PORT =
    8080;

static constexpr uint16_t STELLARIUM_PORT =
    10001;

static constexpr const char* RECORDER_DIRECTORY =
    "/home/nexstar/telescope-nexstar-wifi/data";

static constexpr const char* CATALOGUE_DATABASE =
    "/home/nexstar/telescope-nexstar-wifi/catalogue/telescopehub_catalogue.db";


// ============================================================
// Safety limits
// ============================================================

static constexpr double MIN_SAFE_ALTITUDE_DEG =
    10.0;

static constexpr double MAX_SAFE_ALTITUDE_DEG =
    85.0;


// ============================================================
// Timing
// ============================================================

static constexpr uint32_t TELESCOPE_POLL_INTERVAL_MS =
    2000;

static constexpr uint32_t TELESCOPE_PROBE_INTERVAL_MS =
    10000;

static constexpr uint32_t TELESCOPE_STARTUP_GRACE_MS =
    5000;

static constexpr uint32_t GPS_LOG_INTERVAL_MS =
    5000;

static constexpr uint32_t SENSOR_POLL_INTERVAL_MS =
    5000;


// ============================================================
// Telescope connection state
// ============================================================

enum class TelescopeConnectionState
{
    NO_USB_DEVICE,
    WAITING_FOR_HC,
    STARTING,
    CONNECTED
};


// ============================================================
// Active GOTO
// ============================================================

struct ActiveGoto
{
    bool active =
        false;

    bool abortRequested =
        false;

    bool sawGotoInProgress =
        false;

    bool alignedAtStart =
        false;

    double targetRa =
        0.0;

    double targetDec =
        0.0;

    uint64_t startedAt =
        0;
};


// ============================================================
// Time
// ============================================================

static uint64_t currentTimeMs()
{
    timespec ts{};

    clock_gettime(
        CLOCK_MONOTONIC,
        &ts
    );

    return
        (
            static_cast<uint64_t>(
                ts.tv_sec
            )
            *
            1000ULL
        )
        +
        (
            static_cast<uint64_t>(
                ts.tv_nsec
            )
            /
            1000000ULL
        );
}


// ============================================================
// Timestamp
// ============================================================

static std::string currentTimestamp()
{
    std::time_t now =
        std::time(
            nullptr
        );

    std::tm localTime{};

    localtime_r(
        &now,
        &localTime
    );

    std::ostringstream output;

    output
        << std::setfill('0')
        << std::setw(2)
        << localTime.tm_hour
        << ":"
        << std::setw(2)
        << localTime.tm_min
        << ":"
        << std::setw(2)
        << localTime.tm_sec;

    return output.str();
}


// ============================================================
// Angular separation
// ============================================================

static double angularSeparationDegrees(
    double ra1,
    double dec1,
    double ra2,
    double dec2
)
{
    constexpr double DEG_TO_RAD =
        3.14159265358979323846 /
        180.0;

    double r1 =
        ra1 *
        DEG_TO_RAD;

    double r2 =
        ra2 *
        DEG_TO_RAD;

    double d1 =
        dec1 *
        DEG_TO_RAD;

    double d2 =
        dec2 *
        DEG_TO_RAD;

    double cosine =
        (
            std::sin(d1) *
            std::sin(d2)
        )
        +
        (
            std::cos(d1) *
            std::cos(d2) *
            std::cos(
                r1 - r2
            )
        );

    cosine =
        std::clamp(
            cosine,
            -1.0,
            1.0
        );

    return
        std::acos(
            cosine
        )
        /
        DEG_TO_RAD;
}


// ============================================================
// Pointing quality
// ============================================================

static void updateAlignmentQuality(
    ObservatoryWebState& state
)
{
    double sumSquares =
        0.0;

    double worst =
        0.0;

    uint32_t count =
        0;

    for (
        const GotoHistoryEntry& entry :
        state.gotoHistory
    )
    {
        if (
            !entry.pointingMeasured
        )
        {
            continue;
        }

        double error =
            entry.errorArcmin;

        sumSquares +=
            error *
            error;

        worst =
            std::max(
                worst,
                error
            );

        ++count;
    }

    if (
        count == 0
    )
    {
        state.alignmentQuality.measured =
            false;

        state.alignmentQuality.completedPoints =
            0;

        state.alignmentQuality.rmsArcmin =
            0.0;

        state.alignmentQuality.worstArcmin =
            0.0;

        return;
    }

    state.alignmentQuality.measured =
        true;

    state.alignmentQuality.completedPoints =
        count;

    state.alignmentQuality.rmsArcmin =
        std::sqrt(
            sumSquares /
            static_cast<double>(
                count
            )
        );

    state.alignmentQuality.worstArcmin =
        worst;
}


// ============================================================
// Altitude safety
// ============================================================

static void updateAltitudeSafety(
    TelescopeWebState& state
)
{
    if (
        !state.positionValid
    )
    {
        state.altitudeWarning =
            false;

        state.altitudeStatus =
            "UNKNOWN";

        return;
    }

    if (
        state.altitude <=
        MIN_SAFE_ALTITUDE_DEG
    )
    {
        state.altitudeWarning =
            true;

        state.altitudeStatus =
            "LOW ALTITUDE";

        return;
    }

    if (
        state.altitude >=
        MAX_SAFE_ALTITUDE_DEG
    )
    {
        state.altitudeWarning =
            true;

        state.altitudeStatus =
            "NEAR ZENITH";

        return;
    }

    state.altitudeWarning =
        false;

    state.altitudeStatus =
        "SAFE";
}


// ============================================================
// Dew point
// ============================================================

static void updateDewPoint(
    EnvironmentWebState& state
)
{
    if (
        !state.valid ||
        state.humidityPercent <= 0.0f ||
        state.humidityPercent > 100.0f
    )
    {
        state.dewPointC =
            0.0;

        state.dewRisk =
            "UNKNOWN";

        return;
    }

    constexpr double A =
        17.62;

    constexpr double B =
        243.12;

    double temperature =
        static_cast<double>(
            state.temperatureC
        );

    double humidity =
        static_cast<double>(
            state.humidityPercent
        );

    double alpha =
        std::log(
            humidity /
            100.0
        )
        +
        (
            A *
            temperature
            /
            (
                B +
                temperature
            )
        );

    state.dewPointC =
        (
            B *
            alpha
        )
        /
        (
            A -
            alpha
        );

    double spread =
        temperature -
        state.dewPointC;

    if (
        spread <=
        2.0
    )
    {
        state.dewRisk =
            "HIGH RISK";
    }
    else if (
        spread <=
        5.0
    )
    {
        state.dewRisk =
            "CAUTION";
    }
    else
    {
        state.dewRisk =
            "SAFE";
    }
}


// ============================================================
// Telescope state
// ============================================================

static bool updateTelescopeState(
    NexStar& telescope,
    TelescopeWebState& state
)
{
    state.connected =
        telescope.isConnected();

    if (
        !state.connected
    )
    {
        state.waiting =
            true;

        state.aligned =
            false;

        state.gotoInProgress =
            false;

        state.trackingMode =
            255;

        state.positionValid =
            false;

        updateAltitudeSafety(
            state
        );

        state.updatedAt =
            currentTimeMs();

        return false;
    }

    state.waiting =
        false;


    uint8_t major =
        0;

    uint8_t minor =
        0;

    if (
        telescope.getDeviceVersion(
            major,
            minor
        )
    )
    {
        state.firmwareMajor =
            major;

        state.firmwareMinor =
            minor;
    }


    char model[64] =
        {};

    if (
        telescope.getDeviceModel(
            model,
            sizeof(model)
        )
    )
    {
        state.model =
            model;
    }


    state.aligned =
        telescope.isAligned();


    state.gotoInProgress =
        telescope.isGotoInProgress();


    state.trackingMode =
        telescope.getTrackingMode();


    double azimuth =
        0.0;

    double altitude =
        0.0;

    double ra =
        0.0;

    double dec =
        0.0;


    bool azAltOK =
        telescope.getAzAltPrecise(
            azimuth,
            altitude
        );


    bool raDecOK =
        telescope.getRaDecPrecise(
            ra,
            dec
        );


    if (
        azAltOK &&
        raDecOK
    )
    {
        state.azimuth =
            azimuth;

        state.altitude =
            altitude;

        state.ra =
            ra;

        state.dec =
            dec;

        state.positionValid =
            true;
    }
    else
    {
        state.positionValid =
            false;
    }


    updateAltitudeSafety(
        state
    );


    state.updatedAt =
        currentTimeMs();


    return true;
}


// ============================================================
// GPS state
// ============================================================

static void updateGpsWebState(
    const GPS& gps,
    GPSWebState& state
)
{
    const GPSState& gpsState =
        gps.state();

    state.portOpen =
        gps.isOpen();

    state.fix =
        gpsState.fix;

    state.dateTimeValid =
        gpsState.dateTimeValid;

    state.latitude =
        gpsState.latitude;

    state.longitude =
        gpsState.longitude;

    state.altitude =
        gpsState.altitude;

    state.satellites =
        gpsState.satellites;

    state.hdop =
        gpsState.hdop;

    state.year =
        gpsState.year;

    state.month =
        gpsState.month;

    state.day =
        gpsState.day;

    state.hour =
        gpsState.hour;

    state.minute =
        gpsState.minute;

    state.second =
        gpsState.second;

    state.updatedAt =
        gpsState.updatedAt;
}


// ============================================================
// Environment state
// ============================================================

static void updateEnvironmentWebState(
    const EnvironmentState& source,
    EnvironmentWebState& state
)
{
    state.valid =
        source.valid;

    state.temperatureC =
        source.temperatureC;

    state.humidityPercent =
        source.humidityPercent;

    state.updatedAt =
        source.updatedAt;

    updateDewPoint(
        state
    );
}


// ============================================================
// GPS UTC → local time
// ============================================================

static bool getLocalTimeFromGPS(
    const GPSState& gps,
    int& hour,
    int& minute,
    int& second,
    int& month,
    int& day,
    int& year,
    int& timezoneOffset,
    bool& daylightSaving
)
{
    if (
        !gps.dateTimeValid
    )
    {
        return false;
    }


    tm utcTm{};

    utcTm.tm_year =
        gps.year -
        1900;

    utcTm.tm_mon =
        gps.month -
        1;

    utcTm.tm_mday =
        gps.day;

    utcTm.tm_hour =
        gps.hour;

    utcTm.tm_min =
        gps.minute;

    utcTm.tm_sec =
        gps.second;


    time_t utcEpoch =
        timegm(
            &utcTm
        );


    if (
        utcEpoch ==
        static_cast<time_t>(
            -1
        )
    )
    {
        return false;
    }


    tm localTm{};


    if (
        localtime_r(
            &utcEpoch,
            &localTm
        ) == nullptr
    )
    {
        return false;
    }


    hour =
        localTm.tm_hour;

    minute =
        localTm.tm_min;

    second =
        localTm.tm_sec;

    month =
        localTm.tm_mon +
        1;

    day =
        localTm.tm_mday;

    year =
        localTm.tm_year +
        1900;

    timezoneOffset =
        static_cast<int>(
            localTm.tm_gmtoff /
            3600
        );

    daylightSaving =
        localTm.tm_isdst >
        0;


    return true;
}


// ============================================================
// GPS → HC
// ============================================================

static bool synchroniseNexStarFromGPS(
    NexStar& telescope,
    const GPSState& gps
)
{
    if (
        !gps.fix ||
        !gps.dateTimeValid
    )
    {
        return false;
    }


    if (
        !telescope.setLocation(
            gps.latitude,
            gps.longitude
        )
    )
    {
        std::printf(
            "NexStar: failed to set GPS location.\n"
        );

        return false;
    }


    int hour =
        0;

    int minute =
        0;

    int second =
        0;

    int month =
        0;

    int day =
        0;

    int year =
        0;

    int timezoneOffset =
        0;

    bool daylightSaving =
        false;


    if (
        !getLocalTimeFromGPS(
            gps,
            hour,
            minute,
            second,
            month,
            day,
            year,
            timezoneOffset,
            daylightSaving
        )
    )
    {
        std::printf(
            "NexStar: failed to convert GPS time.\n"
        );

        return false;
    }


    if (
        !telescope.setTime(
            hour,
            minute,
            second,
            month,
            day,
            year,
            timezoneOffset,
            daylightSaving
        )
    )
    {
        std::printf(
            "NexStar: failed to set GPS time.\n"
        );

        return false;
    }


    std::printf(
        "NexStar: time/location synchronised from GPS.\n"
    );


    std::printf(
        "  Location: %.6f, %.6f\n",
        gps.latitude,
        gps.longitude
    );


    std::printf(
        "  Local time: %04d-%02d-%02d %02d:%02d:%02d\n",
        year,
        month,
        day,
        hour,
        minute,
        second
    );


    std::printf(
        "  Time zone: %+d (%s)\n",
        timezoneOffset,
        daylightSaving
            ? "DST"
            : "standard"
    );


    return true;
}


// ============================================================
// Main
// ============================================================

int main()
{
    setvbuf(
        stdout,
        nullptr,
        _IONBF,
        0
    );

    setvbuf(
        stderr,
        nullptr,
        _IONBF,
        0
    );


    std::printf(
        "\n"
        "========================================\n"
        "             TELESCOPEHUB\n"
        "========================================\n"
    );


    // ========================================================
    // NexStar
    // ========================================================

    HardwareSerial telescopeSerial;

    NexStar telescope;


    telescope.setSerial(
        &telescopeSerial
    );


    TelescopeConnectionState
        telescopeConnection =
            TelescopeConnectionState::
                NO_USB_DEVICE;


    bool telescopeTransportOpen =
        false;


    uint64_t lastTelescopeProbe =
        currentTimeMs();


    uint64_t telescopeStartupGraceUntil =
        0;


    uint64_t lastTelescopePoll =
        0;


    // ========================================================
    // Recorder
    // ========================================================

    Recorder recorder;


    if (
        recorder.begin(
            RECORDER_DIRECTORY
        )
    )
    {
        std::printf(
            "Session recorder started.\n"
        );


        recorder.journal(
            "TelescopeHub started."
        );
    }
    else
    {
        std::fprintf(
            stderr,
            "WARNING: Could not start session recorder.\n"
        );
    }


    // ========================================================
    // Sky catalogue
    // ========================================================

    SkyCatalogue skyCatalogue;


    if (
        skyCatalogue.open(
            CATALOGUE_DATABASE
        )
    )
    {
        std::printf(
            "Sky catalogue opened: %s\n",
            CATALOGUE_DATABASE
        );
    }
    else
    {
        std::fprintf(
            stderr,
            "WARNING: Could not open sky catalogue: %s\n",
            CATALOGUE_DATABASE
        );
    }


    // ========================================================
    // GPS
    // ========================================================

    GPS gps;


    if (
        gps.begin(
            GPS_DEVICE,
            GPS_BAUD
        )
    )
    {
        std::printf(
            "GPS opened on %s at %u baud.\n",
            GPS_DEVICE,
            GPS_BAUD
        );
    }
    else
    {
        std::fprintf(
            stderr,
            "WARNING: Could not open GPS on %s.\n",
            GPS_DEVICE
        );
    }


    // ========================================================
    // I2C
    // ========================================================

    I2CBus i2c;


    if (
        !i2c.begin(
            I2C_DEVICE
        )
    )
    {
        std::fprintf(
            stderr,
            "ERROR: Could not open %s.\n",
            I2C_DEVICE
        );

        return 1;
    }


    std::printf(
        "I2C opened on %s.\n",
        I2C_DEVICE
    );


    TCA9548A mux(
        i2c,
        TCA_ADDRESS
    );


    if (
        mux.begin()
    )
    {
        std::printf(
            "TCA9548A @ 0x%02X OK.\n",
            TCA_ADDRESS
        );
    }
    else
    {
        std::fprintf(
            stderr,
            "WARNING: TCA9548A not responding.\n"
        );
    }


    EnvironmentSensor environment(
        i2c,
        mux
    );


    if (
        environment.begin()
    )
    {
        std::printf(
            "SHT3x @ 0x44 OK.\n"
        );
    }
    else
    {
        std::fprintf(
            stderr,
            "WARNING: SHT3x not responding.\n"
        );
    }


    // ========================================================
    // Web
    // ========================================================

    WebServer webServer;


    if (
        !webServer.begin(
            WEB_PORT
        )
    )
    {
        std::fprintf(
            stderr,
            "ERROR: Could not start HTTP server "
            "on port %u.\n",
            WEB_PORT
        );

        return 1;
    }


    std::printf(
        "Web server listening on port %u.\n",
        WEB_PORT
    );


    // ========================================================
    // Stellarium
    // ========================================================

    StellariumServer stellariumServer;


    if (
        !stellariumServer.begin(
            STELLARIUM_PORT
        )
    )
    {
        std::fprintf(
            stderr,
            "ERROR: Could not start Stellarium server "
            "on port %u.\n",
            STELLARIUM_PORT
        );

        return 1;
    }


    // ========================================================
    // State
    // ========================================================

    EnvironmentState environmentState;

    ObservatoryWebState observatoryState;

    ActiveGoto activeGoto;

    bool gpsSynchronised =
        false;


    // ========================================================
    // Initial telescope state
    // ========================================================

    observatoryState.telescope.connected =
        false;

    observatoryState.telescope.waiting =
        true;

    observatoryState.telescope.model =
        "Waiting for telescope";

    observatoryState.telescope.altitudeStatus =
        "UNKNOWN";


    // ========================================================
    // Initial GPS/environment
    // ========================================================

    updateGpsWebState(
        gps,
        observatoryState.gps
    );


    if (
        environment.update(
            environmentState
        )
    )
    {
        updateEnvironmentWebState(
            environmentState,
            observatoryState.environment
        );


        webServer.addEnvironmentSample(
            observatoryState
                .environment
                .temperatureC,
            observatoryState
                .environment
                .humidityPercent,
            observatoryState
                .environment
                .dewPointC,
            currentTimeMs()
        );
    }


    // ========================================================
    // Web abort callback
    // ========================================================

    webServer.setAbortGotoHandler(
        [&]() -> bool
        {
            if (
                !activeGoto.active
            )
            {
                return false;
            }


            if (
                !telescope.isConnected()
            )
            {
                return false;
            }


            if (
                !telescope.abortGoto()
            )
            {
                return false;
            }


            activeGoto.abortRequested =
                true;


            observatoryState.slew.status =
                "ABORTING";


            recorder.journal(
                "GOTO abort requested from web dashboard."
            );


            recorder.event(
                "goto_abort",
                "source=web"
            );


            return true;
        }
    );


    webServer.update(
        observatoryState
    );


    // ========================================================
    // Timers
    // ========================================================

    uint64_t lastSensorPoll =
        currentTimeMs();


    uint64_t lastGpsLog =
        currentTimeMs();


    // ========================================================
    // Main loop
    // ========================================================

    while (true)
    {
        uint64_t now =
            currentTimeMs();


        // ====================================================
        // GPS
        // ====================================================

        gps.update();


        // ====================================================
        // HTTP
        // ====================================================

        webServer.handleRequests();


        // ====================================================
        // Stellarium
        // ====================================================

        stellariumServer.handle(
            telescope
        );


        // ====================================================
        // New GOTO request
        // ====================================================

        double requestedRa =
            0.0;

        double requestedDec =
            0.0;

        bool gotoAccepted =
            false;


        if (
            stellariumServer.takeGotoRequest(
                requestedRa,
                requestedDec,
                gotoAccepted
            )
        )
        {
            if (
                gotoAccepted
            )
            {
                activeGoto.active =
                    true;

                activeGoto.abortRequested =
                    false;

                activeGoto.sawGotoInProgress =
                    false;

                activeGoto.alignedAtStart =
                    observatoryState
                        .telescope
                        .aligned;

                activeGoto.targetRa =
                    requestedRa;

                activeGoto.targetDec =
                    requestedDec;

                activeGoto.startedAt =
                    currentTimeMs();


                observatoryState.slew.active =
                    true;

                observatoryState.slew.status =
                    "SLEWING";

                observatoryState.slew.targetRa =
                    requestedRa;

                observatoryState.slew.targetDec =
                    requestedDec;

                observatoryState.slew.remainingDegrees =
                    0.0;

                observatoryState.slew.elapsedMs =
                    0;


                std::ostringstream message;

                message
                    << "GOTO accepted: RA "
                    << requestedRa
                    << " Dec "
                    << requestedDec
                    << " (aligned="
                    << (
                        activeGoto.alignedAtStart
                            ? "yes"
                            : "no"
                    )
                    << ")";


                recorder.journal(
                    message.str()
                );


                recorder.event(
                    "goto_start",
                    message.str()
                );
            }
            else
            {
                recorder.journal(
                    "GOTO rejected."
                );


                recorder.event(
                    "goto_rejected",
                    ""
                );
            }
        }


        // ====================================================
        // Telescope transport
        // ====================================================

        if (
            !telescopeTransportOpen
        )
        {
            if (
                now -
                lastTelescopeProbe >=
                TELESCOPE_PROBE_INTERVAL_MS
            )
            {
                lastTelescopeProbe =
                    now;


                std::printf(
                    "Waiting for NexStar USB device...\n"
                );


                if (
                    telescopeSerial.begin(
                        NEXSTAR_DEVICE,
                        NEXSTAR_BAUD
                    )
                )
                {
                    telescopeTransportOpen =
                        true;


                    telescopeConnection =
                        TelescopeConnectionState::
                            WAITING_FOR_HC;


                    std::printf(
                        "NexStar USB serial device opened. "
                        "Waiting for HC.\n"
                    );


                    recorder.journal(
                        "NexStar USB serial device opened."
                    );
                }
            }
        }


        // ====================================================
        // HC state machine
        // ====================================================

        if (
            telescopeTransportOpen
        )
        {
            switch (
                telescopeConnection
            )
            {
                case TelescopeConnectionState::
                    WAITING_FOR_HC:
                {
                    if (
                        now -
                        lastTelescopeProbe >=
                        TELESCOPE_PROBE_INTERVAL_MS
                    )
                    {
                        lastTelescopeProbe =
                            now;


                        std::printf(
                            "Probing NexStar HC...\n"
                        );


                        if (
                            telescope.isConnected()
                        )
                        {
                            telescopeConnection =
                                TelescopeConnectionState::
                                    STARTING;


                            telescopeStartupGraceUntil =
                                now +
                                TELESCOPE_STARTUP_GRACE_MS;


                            observatoryState
                                .telescope
                                .waiting =
                                false;


                            std::printf(
                                "NexStar HC detected. "
                                "Allowing startup to settle...\n"
                            );


                            recorder.journal(
                                "NexStar HC detected."
                            );
                        }
                    }


                    break;
                }


                case TelescopeConnectionState::
                    STARTING:
                {
                    if (
                        now >=
                        telescopeStartupGraceUntil
                    )
                    {
                        telescopeConnection =
                            TelescopeConnectionState::
                                CONNECTED;


                        lastTelescopePoll =
                            0;


                        observatoryState
                            .telescope
                            .waiting =
                            false;


                        recorder.journal(
                            "NexStar HC ready."
                        );


                        std::printf(
                            "NexStar HC ready.\n"
                        );
                    }


                    break;
                }


                case TelescopeConnectionState::
                    CONNECTED:
                {
                    if (
                        now -
                        lastTelescopePoll >=
                        TELESCOPE_POLL_INTERVAL_MS
                    )
                    {
                        lastTelescopePoll =
                            now;


                        bool connected =
                            updateTelescopeState(
                                telescope,
                                observatoryState
                                    .telescope
                            );


                        recorder.telescopeSample(
                            connected,
                            observatoryState
                                .telescope
                                .aligned,
                            observatoryState
                                .telescope
                                .gotoInProgress,
                            observatoryState
                                .telescope
                                .trackingMode,
                            observatoryState
                                .telescope
                                .azimuth,
                            observatoryState
                                .telescope
                                .altitude,
                            observatoryState
                                .telescope
                                .ra,
                            observatoryState
                                .telescope
                                .dec
                        );


                        if (
                            !connected
                        )
                        {
                            telescopeConnection =
                                TelescopeConnectionState::
                                    WAITING_FOR_HC;


                            observatoryState
                                .telescope
                                .connected =
                                false;


                            observatoryState
                                .telescope
                                .waiting =
                                true;


                            observatoryState
                                .telescope
                                .aligned =
                                false;


                            observatoryState
                                .telescope
                                .gotoInProgress =
                                false;


                            observatoryState
                                .telescope
                                .trackingMode =
                                255;


                            observatoryState
                                .telescope
                                .positionValid =
                                false;


                            observatoryState
                                .telescope
                                .altitudeWarning =
                                false;


                            observatoryState
                                .telescope
                                .altitudeStatus =
                                "UNKNOWN";


                            gpsSynchronised =
                                false;


                            lastTelescopeProbe =
                                now;


                            recorder.journal(
                                "NexStar HC connection lost."
                            );


                            std::printf(
                                "NexStar HC connection lost. "
                                "Waiting for HC.\n"
                            );
                        }
                        else
                        {
                            if (
                                observatoryState
                                    .telescope
                                    .positionValid
                            )
                            {
                                stellariumServer
                                    .updatePosition(
                                        observatoryState
                                            .telescope
                                            .ra,
                                        observatoryState
                                            .telescope
                                            .dec,
                                        true,
                                        0
                                    );
                            }
                            else
                            {
                                stellariumServer
                                    .updatePosition(
                                        0.0,
                                        0.0,
                                        false,
                                        0
                                    );
                            }


                            // --------------------------------
                            // GOTO tracking
                            // --------------------------------

                            if (
                                activeGoto.active
                            )
                            {
                                bool currentlyGoto =
                                    observatoryState
                                        .telescope
                                        .gotoInProgress;


                                if (
                                    currentlyGoto
                                )
                                {
                                    activeGoto
                                        .sawGotoInProgress =
                                        true;
                                }


                                observatoryState
                                    .slew
                                    .elapsedMs =
                                    now -
                                    activeGoto.startedAt;


                                if (
                                    observatoryState
                                        .telescope
                                        .positionValid
                                )
                                {
                                    observatoryState
                                        .slew
                                        .remainingDegrees =
                                        angularSeparationDegrees(
                                            observatoryState
                                                .telescope
                                                .ra,
                                            observatoryState
                                                .telescope
                                                .dec,
                                            activeGoto
                                                .targetRa,
                                            activeGoto
                                                .targetDec
                                        );
                                }


                                if (
                                    activeGoto
                                        .sawGotoInProgress
                                    &&
                                    !currentlyGoto
                                )
                                {
                                    GotoHistoryEntry entry;


                                    entry.timestamp =
                                        currentTimestamp();


                                    entry.targetRa =
                                        activeGoto
                                            .targetRa;


                                    entry.targetDec =
                                        activeGoto
                                            .targetDec;


                                    entry.finalRa =
                                        observatoryState
                                            .telescope
                                            .ra;


                                    entry.finalDec =
                                        observatoryState
                                            .telescope
                                            .dec;


                                    entry.durationMs =
                                        now -
                                        activeGoto.startedAt;


                                    entry.wasAligned =
                                        activeGoto
                                            .alignedAtStart;


                                    entry.pointingMeasured =
                                        !activeGoto
                                            .abortRequested
                                        &&
                                        activeGoto
                                            .alignedAtStart
                                        &&
                                        observatoryState
                                            .telescope
                                            .positionValid;


                                    if (
                                        entry.pointingMeasured
                                    )
                                    {
                                        entry.errorArcmin =
                                            angularSeparationDegrees(
                                                entry.targetRa,
                                                entry.targetDec,
                                                entry.finalRa,
                                                entry.finalDec
                                            )
                                            *
                                            60.0;
                                    }
                                    else
                                    {
                                        entry.errorArcmin =
                                            0.0;
                                    }


                                    entry.result =
                                        activeGoto
                                            .abortRequested
                                            ? "ABORTED"
                                            : "COMPLETE";


                                    observatoryState
                                        .gotoHistory
                                        .insert(
                                            observatoryState
                                                .gotoHistory
                                                .begin(),
                                            entry
                                        );


                                    if (
                                        observatoryState
                                            .gotoHistory
                                            .size()
                                        >
                                        10
                                    )
                                    {
                                        observatoryState
                                            .gotoHistory
                                            .pop_back();
                                    }


                                    updateAlignmentQuality(
                                        observatoryState
                                    );


                                    std::ostringstream message;


                                    message
                                        << "GOTO "
                                        << entry.result;


                                    if (
                                        !entry.wasAligned
                                    )
                                    {
                                        message
                                            << " (unaligned)";
                                    }
                                    else if (
                                        entry.pointingMeasured
                                    )
                                    {
                                        message
                                            << ": pointing error "
                                            << entry.errorArcmin
                                            << " arcmin";
                                    }
                                    else
                                    {
                                        message
                                            << ": pointing not measured";
                                    }


                                    message
                                        << ", duration "
                                        << entry.durationMs
                                        << " ms";


                                    recorder.journal(
                                        message.str()
                                    );


                                    recorder.event(
                                        "goto_complete",
                                        message.str()
                                    );


                                    activeGoto.active =
                                        false;


                                    observatoryState
                                        .slew
                                        .active =
                                        false;


                                    if (
                                        activeGoto
                                            .abortRequested
                                    )
                                    {
                                        observatoryState
                                            .slew
                                            .status =
                                            "ABORTED";
                                    }
                                    else
                                    {
                                        observatoryState
                                            .slew
                                            .status =
                                            entry.wasAligned
                                                ? "COMPLETE"
                                                : "COMPLETE — UNALIGNED";
                                    }
                                }
                            }
                        }
                    }


                    break;
                }


                case TelescopeConnectionState::
                    NO_USB_DEVICE:
                {
                    telescopeConnection =
                        TelescopeConnectionState::
                            WAITING_FOR_HC;

                    break;
                }
            }
        }


        // ====================================================
        // GPS → HC synchronisation
        // ====================================================

        if (
            telescopeConnection ==
            TelescopeConnectionState::
                CONNECTED
        )
        {
            const GPSState& gpsState =
                gps.state();


            if (
                gpsState.fix &&
                gpsState.dateTimeValid &&
                !gpsSynchronised &&
                !observatoryState
                    .telescope
                    .aligned
            )
            {
                if (
                    synchroniseNexStarFromGPS(
                        telescope,
                        gpsState
                    )
                )
                {
                    gpsSynchronised =
                        true;


                    recorder.journal(
                        "GPS time/location synchronised to NexStar."
                    );


                    recorder.event(
                        "gps_sync",
                        ""
                    );
                }
            }


            if (
                !gpsState.fix
            )
            {
                gpsSynchronised =
                    false;
            }
        }


        // ====================================================
        // Sensors
        // ====================================================

        if (
            now -
            lastSensorPoll >=
            SENSOR_POLL_INTERVAL_MS
        )
        {
            lastSensorPoll =
                now;


            updateGpsWebState(
                gps,
                observatoryState.gps
            );


            // --------------------------------------------
            // What can I see?
            // --------------------------------------------

            if (
                observatoryState.gps.fix &&
                observatoryState.gps.dateTimeValid
            )
            {
                observatoryState.sky =
                    skyCatalogue.calculate(
                        observatoryState.gps.latitude,
                        observatoryState.gps.longitude,
                        observatoryState.gps.year,
                        observatoryState.gps.month,
                        observatoryState.gps.day,
                        observatoryState.gps.hour,
                        observatoryState.gps.minute,
                        observatoryState.gps.second,
                        20.0,
                        10.0,
                        12
                    );
            }
            else
            {
                observatoryState.sky =
                    SkyState{};
            }


            recorder.gpsSample(
                observatoryState.gps.fix,
                observatoryState.gps.latitude,
                observatoryState.gps.longitude,
                observatoryState.gps.altitude,
                observatoryState.gps.satellites,
                observatoryState.gps.hdop
            );


            if (
                environment.update(
                    environmentState
                )
            )
            {
                updateEnvironmentWebState(
                    environmentState,
                    observatoryState.environment
                );


                // --------------------------------------------
                // Add sample to live graph
                // --------------------------------------------

                webServer.addEnvironmentSample(
                    observatoryState
                        .environment
                        .temperatureC,
                    observatoryState
                        .environment
                        .humidityPercent,
                    observatoryState
                        .environment
                        .dewPointC,
                    now
                );


                // --------------------------------------------
                // Black-box recorder
                // --------------------------------------------

                recorder.environmentSample(
                    observatoryState
                        .environment
                        .valid,
                    observatoryState
                        .environment
                        .temperatureC,
                    observatoryState
                        .environment
                        .humidityPercent,
                    observatoryState
                        .environment
                        .dewPointC
                );
            }
            else
            {
                observatoryState
                    .environment
                    .valid =
                    false;


                updateDewPoint(
                    observatoryState
                        .environment
                );
            }
        }


        // ====================================================
        // Logging
        // ====================================================

        if (
            now -
            lastGpsLog >=
            GPS_LOG_INTERVAL_MS
        )
        {
            lastGpsLog =
                now;


            updateGpsWebState(
                gps,
                observatoryState.gps
            );


            std::printf(
                "\nGPS: %s\n",
                observatoryState.gps.fix
                    ? "FIX"
                    : "NO FIX"
            );


            if (
                observatoryState.gps.fix
            )
            {
                std::printf(
                    "  Latitude:   %.6f\n",
                    observatoryState.gps.latitude
                );


                std::printf(
                    "  Longitude:  %.6f\n",
                    observatoryState.gps.longitude
                );


                std::printf(
                    "  Altitude:   %.1f m\n",
                    observatoryState.gps.altitude
                );


                std::printf(
                    "  Satellites: %u\n",
                    observatoryState.gps.satellites
                );


                std::printf(
                    "  HDOP:       %.1f\n",
                    observatoryState.gps.hdop
                );
            }


            if (
                observatoryState
                    .environment
                    .valid
            )
            {
                std::printf(
                    "Environment: %.2f C / %.2f %% "
                    "(dew point %.2f C, %s)\n",
                    observatoryState
                        .environment
                        .temperatureC,
                    observatoryState
                        .environment
                        .humidityPercent,
                    observatoryState
                        .environment
                        .dewPointC,
                    observatoryState
                        .environment
                        .dewRisk
                        .c_str()
                );
            }
        }


        // ====================================================
        // Publish web state
        // ====================================================

        webServer.update(
            observatoryState
        );


        // ====================================================
        // Small sleep
        // ====================================================

        timespec sleepTime{};


        sleepTime.tv_sec =
            0;

        sleepTime.tv_nsec =
            10000000L;


        nanosleep(
            &sleepTime,
            nullptr
        );
    }


    return 0;
}
