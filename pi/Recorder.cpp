#include "Recorder.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>


// ============================================================
// Constructor
// ============================================================

Recorder::Recorder()
{
}


// ============================================================
// Destructor
// ============================================================

Recorder::~Recorder()
{
    close();
}


// ============================================================
// CSV escaping
// ============================================================

std::string Recorder::csvEscape(
    const std::string& value
)
{
    std::string result;

    result =
        "\"";


    for (
        char c :
        value
    )
    {
        if (
            c == '"'
        )
        {
            result +=
                "\"\"";
        }
        else
        {
            result +=
                c;
        }
    }


    result +=
        "\"";


    return result;
}


// ============================================================
// Begin session
// ============================================================

bool Recorder::begin(
    const std::string& directory
)
{
    try
    {
        std::filesystem::create_directories(
            directory
        );
    }
    catch (...)
    {
        return false;
    }


    std::time_t now =
        std::time(
            nullptr
        );


    std::tm localTime{};


    localtime_r(
        &now,
        &localTime
    );


    std::ostringstream session;


    session
        << std::setfill('0')
        << std::setw(4)
        << localTime.tm_year + 1900
        << std::setw(2)
        << localTime.tm_mon + 1
        << std::setw(2)
        << localTime.tm_mday
        << "_"
        << std::setw(2)
        << localTime.tm_hour
        << std::setw(2)
        << localTime.tm_min
        << std::setw(2)
        << localTime.tm_sec;


    std::string base =
        directory +
        "/" +
        session.str();


    _blackbox.open(
        base +
        "_blackbox.csv",
        std::ios::out |
        std::ios::trunc
    );


    _journal.open(
        base +
        "_journal.txt",
        std::ios::out |
        std::ios::trunc
    );


    if (
        !_blackbox ||
        !_journal
    )
    {
        close();

        return false;
    }


    _blackbox
        << "timestamp,"
        << "monotonic_ms,"
        << "event,"
        << "data\n";


    _blackbox.flush();


    journal(
        "TelescopeHub observing session started."
    );


    return true;
}


// ============================================================
// Close
// ============================================================

void Recorder::close()
{
    if (
        _blackbox.is_open()
    )
    {
        _blackbox.flush();

        _blackbox.close();
    }


    if (
        _journal.is_open()
    )
    {
        _journal.flush();

        _journal.close();
    }
}


// ============================================================
// Is open
// ============================================================

bool Recorder::isOpen() const
{
    return
        _blackbox.is_open() &&
        _journal.is_open();
}


// ============================================================
// Timestamp
// ============================================================

std::string Recorder::timestamp()
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
        << std::setw(4)
        << localTime.tm_year + 1900
        << "-"
        << std::setw(2)
        << localTime.tm_mon + 1
        << "-"
        << std::setw(2)
        << localTime.tm_mday
        << " "
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
// Monotonic milliseconds
// ============================================================

uint64_t Recorder::monotonicMs()
{
    auto now =
        std::chrono::steady_clock::now();


    auto epoch =
        now.time_since_epoch();


    return static_cast<uint64_t>(
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            epoch
        ).count()
    );
}


// ============================================================
// Journal
// ============================================================

void Recorder::journal(
    const std::string& message
)
{
    if (
        !_journal.is_open()
    )
    {
        return;
    }


    _journal
        << "["
        << timestamp()
        << "] "
        << message
        << "\n";


    _journal.flush();
}


// ============================================================
// Generic event
// ============================================================

void Recorder::event(
    const std::string& type,
    const std::string& data
)
{
    if (
        !_blackbox.is_open()
    )
    {
        return;
    }


    _blackbox
        << csvEscape(
            timestamp()
        )
        << ","
        << monotonicMs()
        << ","
        << csvEscape(
            type
        )
        << ","
        << csvEscape(
            data
        )
        << "\n";


    _blackbox.flush();
}


// ============================================================
// Telescope sample
// ============================================================

void Recorder::telescopeSample(
    bool connected,
    bool aligned,
    bool gotoInProgress,
    uint8_t trackingMode,
    double azimuth,
    double altitude,
    double ra,
    double dec
)
{
    std::ostringstream data;


    data
        << "connected="
        << (
            connected
                ? 1
                : 0
        )
        << ";aligned="
        << (
            aligned
                ? 1
                : 0
        )
        << ";goto="
        << (
            gotoInProgress
                ? 1
                : 0
        )
        << ";tracking="
        << static_cast<unsigned>(
            trackingMode
        )
        << ";az="
        << azimuth
        << ";alt="
        << altitude
        << ";ra="
        << ra
        << ";dec="
        << dec;


    event(
        "telescope",
        data.str()
    );
}


// ============================================================
// GPS sample
// ============================================================

void Recorder::gpsSample(
    bool fix,
    double latitude,
    double longitude,
    double altitude,
    uint32_t satellites,
    double hdop
)
{
    std::ostringstream data;


    data
        << "fix="
        << (
            fix
                ? 1
                : 0
        )
        << ";lat="
        << latitude
        << ";lon="
        << longitude
        << ";alt="
        << altitude
        << ";satellites="
        << satellites
        << ";hdop="
        << hdop;


    event(
        "gps",
        data.str()
    );
}


// ============================================================
// Environment sample
// ============================================================

void Recorder::environmentSample(
    bool valid,
    double temperature,
    double humidity,
    double dewPoint
)
{
    std::ostringstream data;


    data
        << "valid="
        << (
            valid
                ? 1
                : 0
        )
        << ";temperature="
        << temperature
        << ";humidity="
        << humidity
        << ";dew_point="
        << dewPoint;


    event(
        "environment",
        data.str()
    );
}
