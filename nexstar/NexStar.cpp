#include "NexStar.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>


// ============================================================
// Constructor
// ============================================================

NexStar::NexStar()
{
}


// ============================================================
// Attach serial transport
// ============================================================

void NexStar::setSerial(
    HardwareSerial* serial
)
{
    _serial =
        serial;
}


// ============================================================
// Begin
// ============================================================

bool NexStar::begin()
{
    if (
        _serial == nullptr
    )
    {
        Serial.println(
            "NexStar: no serial transport!"
        );

        return false;
    }


    _connected =
        false;


    return true;
}


// ============================================================
// Write ASCII command
// ============================================================

bool NexStar::writeCommand(
    const char* command
)
{
    if (
        command == nullptr
    )
    {
        return false;
    }


    return writeRawCommand(
        reinterpret_cast<
            const uint8_t*
        >(
            command
        ),
        strlen(
            command
        )
    );
}


// ============================================================
// Write raw command
// ============================================================

bool NexStar::writeRawCommand(
    const uint8_t* data,
    size_t length
)
{
    if (
        _serial == nullptr ||
        data == nullptr ||
        length == 0
    )
    {
        return false;
    }


    /*
     * Clear anything left from a previous transaction.
     */

    while (
        _serial->available()
    )
    {
        _serial->read();
    }


    size_t written =
        _serial->write(
            data,
            length
        );


    _serial->flush();


    if (
        written != length
    )
    {
        return false;
    }


    delay(50);


    return true;
}


// ============================================================
// Read response
// ============================================================

bool NexStar::readResponse(
    char* response,
    size_t responseSize,
    uint32_t timeout
)
{
    if (
        _serial == nullptr ||
        response == nullptr ||
        responseSize < 2
    )
    {
        return false;
    }


    size_t index =
        0;


    response[0] =
        '\0';


    uint32_t start =
        millis();


    while (
        millis() - start <
        timeout
    )
    {
        bool receivedSomething =
            false;


        while (
            _serial->available()
        )
        {
            receivedSomething =
                true;


            int value =
                _serial->read();


            if (
                value < 0
            )
            {
                break;
            }


            uint8_t byte =
                static_cast<uint8_t>(
                    value
                );


            if (
                byte == '#'
            )
            {
                if (
                    index <
                    responseSize - 1
                )
                {
                    response[index++] =
                        static_cast<char>(
                            byte
                        );
                }


                response[index] =
                    '\0';


                return true;
            }


            if (
                index <
                responseSize - 1
            )
            {
                response[index++] =
                    static_cast<char>(
                        byte
                    );
            }
        }


        if (
            receivedSomething
        )
        {
            start =
                millis();
        }


        delay(1);
    }


    response[
        index <
        responseSize
            ? index
            : responseSize - 1
    ] =
        '\0';


    return false;
}


// ============================================================
// Query
// ============================================================

bool NexStar::query(
    const char* command,
    char* response,
    size_t responseSize,
    uint32_t timeout
)
{
    if (
        !writeCommand(
            command
        )
    )
    {
        return false;
    }


    return readResponse(
        response,
        responseSize,
        timeout
    );
}


// ============================================================
// Connection
// ============================================================

bool NexStar::isConnected()
{
    char response[16];


    if (
        !query(
            "Kx",
            response,
            sizeof(response),
            1000
        )
    )
    {
        _connected =
            false;


        return false;
    }


    _connected =
        response[0] == 'x';


    return _connected;
}


// ============================================================
// Alignment
// ============================================================

bool NexStar::isAligned()
{
    char response[16];


    if (
        !query(
            "J",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    return
        static_cast<uint8_t>(
            response[0]
        ) != 0;
}


// ============================================================
// GOTO status
// ============================================================

bool NexStar::isGotoInProgress()
{
    char response[16];


    if (
        !query(
            "L",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    return response[0] == '1';
}


// ============================================================
// Tracking
// ============================================================

uint8_t NexStar::getTrackingMode()
{
    char response[16];


    if (
        !query(
            "t",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return 255;
    }


    return static_cast<uint8_t>(
        response[0]
    );
}


// ============================================================
// Decode 16-bit angle
// ============================================================

double NexStar::decodeAngle16(
    const char* value
)
{
    uint16_t raw =
        static_cast<uint16_t>(
            strtoul(
                value,
                nullptr,
                16
            )
        );


    return
        (
            static_cast<double>(
                raw
            )
            *
            360.0
        )
        /
        65536.0;
}


// ============================================================
// Decode 32-bit angle
// ============================================================

double NexStar::decodeAngle32(
    const char* value
)
{
    uint32_t raw =
        static_cast<uint32_t>(
            strtoul(
                value,
                nullptr,
                16
            )
        );


    return
        (
            static_cast<double>(
                raw
            )
            *
            360.0
        )
        /
        4294967296.0;
}


// ============================================================
// RA / Dec
// ============================================================

bool NexStar::getRaDec(
    double& ra,
    double& dec
)
{
    char response[32];


    if (
        !query(
            "E",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    char* comma =
        strchr(
            response,
            ','
        );


    if (
        comma == nullptr
    )
    {
        return false;
    }


    *comma =
        '\0';


    char* decValue =
        comma + 1;


    char* hash =
        strchr(
            decValue,
            '#'
        );


    if (
        hash != nullptr
    )
    {
        *hash =
            '\0';
    }


    if (
        strlen(response) != 4 ||
        strlen(decValue) != 4
    )
    {
        return false;
    }


    ra =
        decodeAngle16(
            response
        );


    dec =
        decodeAngle16(
            decValue
        );


    return true;
}


// ============================================================
// Precise RA / Dec
// ============================================================

bool NexStar::getRaDecPrecise(
    double& ra,
    double& dec
)
{
    char response[32];


    if (
        !query(
            "e",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    char* comma =
        strchr(
            response,
            ','
        );


    if (
        comma == nullptr
    )
    {
        return false;
    }


    *comma =
        '\0';


    char* decValue =
        comma + 1;


    char* hash =
        strchr(
            decValue,
            '#'
        );


    if (
        hash != nullptr
    )
    {
        *hash =
            '\0';
    }


    if (
        strlen(response) != 8 ||
        strlen(decValue) != 8
    )
    {
        return false;
    }


    ra =
        decodeAngle32(
            response
        );


    uint32_t raw =
        static_cast<uint32_t>(
            strtoul(
                decValue,
                nullptr,
                16
            )
        );


    int32_t signedValue =
        static_cast<int32_t>(
            raw
        );


    dec =
        (
            static_cast<double>(
                signedValue
            )
            *
            360.0
        )
        /
        4294967296.0;


    return true;
}


// ============================================================
// Az / Alt
// ============================================================

bool NexStar::getAzAlt(
    double& azimuth,
    double& altitude
)
{
    char response[32];


    if (
        !query(
            "Z",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    char* comma =
        strchr(
            response,
            ','
        );


    if (
        comma == nullptr
    )
    {
        return false;
    }


    *comma =
        '\0';


    char* altValue =
        comma + 1;


    char* hash =
        strchr(
            altValue,
            '#'
        );


    if (
        hash != nullptr
    )
    {
        *hash =
            '\0';
    }


    if (
        strlen(response) != 4 ||
        strlen(altValue) != 4
    )
    {
        return false;
    }


    azimuth =
        decodeAngle16(
            response
        );


    altitude =
        decodeAngle16(
            altValue
        );


    return true;
}


// ============================================================
// Precise Az / Alt
// ============================================================

bool NexStar::getAzAltPrecise(
    double& azimuth,
    double& altitude
)
{
    char response[32];


    if (
        !query(
            "z",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    char* comma =
        strchr(
            response,
            ','
        );


    if (
        comma == nullptr
    )
    {
        return false;
    }


    *comma =
        '\0';


    char* altValue =
        comma + 1;


    char* hash =
        strchr(
            altValue,
            '#'
        );


    if (
        hash != nullptr
    )
    {
        *hash =
            '\0';
    }


    if (
        strlen(response) != 8 ||
        strlen(altValue) != 8
    )
    {
        return false;
    }


    azimuth =
        decodeAngle32(
            response
        );


    altitude =
        decodeAngle32(
            altValue
        );


    return true;
}


// ============================================================
// Precise RA / Dec GOTO
// ============================================================

bool NexStar::gotoRaDecPrecise(
    double ra,
    double dec
)
{
    if (
        _serial == nullptr
    )
    {
        return false;
    }


    while (
        ra < 0.0
    )
    {
        ra +=
            360.0;
    }


    while (
        ra >= 360.0
    )
    {
        ra -=
            360.0;
    }


    if (
        dec > 180.0
    )
    {
        dec =
            180.0;
    }


    if (
        dec < -180.0
    )
    {
        dec =
            -180.0;
    }


    uint64_t raRaw =
        static_cast<uint64_t>(
            std::llround(
                (
                    ra /
                    360.0
                )
                *
                4294967296.0
            )
        );


    if (
        raRaw >=
        4294967296ULL
    )
    {
        raRaw =
            0;
    }


    uint32_t raValue =
        static_cast<uint32_t>(
            raRaw
        );


    int64_t decRaw =
        static_cast<int64_t>(
            std::llround(
                (
                    dec /
                    360.0
                )
                *
                4294967296.0
            )
        );


    int32_t decValue =
        static_cast<int32_t>(
            decRaw
        );


    char command[64];


    std::snprintf(
        command,
        sizeof(command),
        "r%08lX,%08lX",
        static_cast<unsigned long>(
            raValue
        ),
        static_cast<unsigned long>(
            static_cast<uint32_t>(
                decValue
            )
        )
    );


    std::printf(
        "NexStar GOTO: RA %.6f deg, Dec %.6f deg\n",
        ra,
        dec
    );


    char response[16];


    if (
        !query(
            command,
            response,
            sizeof(response),
            5000
        )
    )
    {
        std::printf(
            "NexStar GOTO: no acknowledgement.\n"
        );

        return false;
    }


    if (
        response[0] != '#'
    )
    {
        std::printf(
            "NexStar GOTO: unexpected response.\n"
        );

        return false;
    }


    std::printf(
        "NexStar GOTO: accepted.\n"
    );


    return true;
}


// ============================================================
// Abort GOTO
// ============================================================

bool NexStar::abortGoto()
{
    char response[8];


    /*
     * Celestron documents M as Cancel GOTO.
     */

    if (
        !query(
            "M",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    return response[0] == '#';
}


// ============================================================
// Convert decimal degrees to DMS
// ============================================================

void NexStar::degreesToDMS(
    double degrees,
    int& wholeDegrees,
    int& minutes,
    int& seconds
)
{
    double absolute =
        std::fabs(
            degrees
        );


    wholeDegrees =
        static_cast<int>(
            absolute
        );


    double minuteValue =
        (
            absolute -
            wholeDegrees
        )
        *
        60.0;


    minutes =
        static_cast<int>(
            minuteValue
        );


    seconds =
        static_cast<int>(
            std::lround(
                (
                    minuteValue -
                    minutes
                )
                *
                60.0
            )
        );


    if (
        seconds >= 60
    )
    {
        seconds =
            0;

        ++minutes;
    }


    if (
        minutes >= 60
    )
    {
        minutes =
            0;

        ++wholeDegrees;
    }
}


// ============================================================
// Set location
// ============================================================

bool NexStar::setLocation(
    double latitude,
    double longitude
)
{
    if (
        latitude > 90.0 ||
        latitude < -90.0 ||
        longitude > 180.0 ||
        longitude < -180.0
    )
    {
        return false;
    }


    int latDegrees = 0;
    int latMinutes = 0;
    int latSeconds = 0;


    int lonDegrees = 0;
    int lonMinutes = 0;
    int lonSeconds = 0;


    degreesToDMS(
        latitude,
        latDegrees,
        latMinutes,
        latSeconds
    );


    degreesToDMS(
        longitude,
        lonDegrees,
        lonMinutes,
        lonSeconds
    );


    uint8_t command[9] = {};


    command[0] =
        'W';


    command[1] =
        static_cast<uint8_t>(
            latDegrees
        );

    command[2] =
        static_cast<uint8_t>(
            latMinutes
        );

    command[3] =
        static_cast<uint8_t>(
            latSeconds
        );

    command[4] =
        latitude < 0.0
            ? 1
            : 0;

    command[5] =
        static_cast<uint8_t>(
            lonDegrees
        );

    command[6] =
        static_cast<uint8_t>(
            lonMinutes
        );

    command[7] =
        static_cast<uint8_t>(
            lonSeconds
        );

    command[8] =
        longitude < 0.0
            ? 1
            : 0;


    if (
        !writeRawCommand(
            command,
            sizeof(command)
        )
    )
    {
        return false;
    }


    char response[8];


    if (
        !readResponse(
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    return response[0] == '#';
}


// ============================================================
// Set time/date
// ============================================================

bool NexStar::setTime(
    int hour,
    int minute,
    int second,
    int month,
    int day,
    int year,
    int timezoneOffsetHours,
    bool daylightSaving
)
{
    if (
        hour < 0 ||
        hour > 23 ||
        minute < 0 ||
        minute > 59 ||
        second < 0 ||
        second > 59 ||
        month < 1 ||
        month > 12 ||
        day < 1 ||
        day > 31 ||
        year < 2000 ||
        year > 2099 ||
        timezoneOffsetHours < -12 ||
        timezoneOffsetHours > 14
    )
    {
        return false;
    }


    uint8_t timezone =
        timezoneOffsetHours < 0
            ? static_cast<uint8_t>(
                256 +
                timezoneOffsetHours
            )
            : static_cast<uint8_t>(
                timezoneOffsetHours
            );


    uint8_t command[9] = {};


    command[0] =
        'H';

    command[1] =
        static_cast<uint8_t>(
            hour
        );

    command[2] =
        static_cast<uint8_t>(
            minute
        );

    command[3] =
        static_cast<uint8_t>(
            second
        );

    command[4] =
        static_cast<uint8_t>(
            month
        );

    command[5] =
        static_cast<uint8_t>(
            day
        );

    command[6] =
        static_cast<uint8_t>(
            year - 2000
        );

    command[7] =
        timezone;

    command[8] =
        daylightSaving
            ? 1
            : 0;


    if (
        !writeRawCommand(
            command,
            sizeof(command)
        )
    )
    {
        return false;
    }


    char response[8];


    if (
        !readResponse(
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    return response[0] == '#';
}


// ============================================================
// Device version
// ============================================================

bool NexStar::getDeviceVersion(
    uint8_t& major,
    uint8_t& minor
)
{
    char response[32];


    if (
        !query(
            "V",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    major =
        static_cast<uint8_t>(
            response[0]
        );


    minor =
        0;


    /*
     * The HC response we have observed is:
     *
     *     0x05 0x23
     *
     * where the second byte is '#'.
     */

    if (
        response[1] != '#'
    )
    {
        minor =
            static_cast<uint8_t>(
                response[1]
            );
    }


    return true;
}


// ============================================================
// Device model
// ============================================================

bool NexStar::getDeviceModel(
    char* model,
    size_t modelSize
)
{
    if (
        model == nullptr ||
        modelSize == 0
    )
    {
        return false;
    }


    char response[32];


    if (
        !query(
            "m",
            response,
            sizeof(response),
            3000
        )
    )
    {
        return false;
    }


    uint8_t deviceModel =
        static_cast<uint8_t>(
            response[0]
        );


    const char* modelName =
        "Unknown";


    switch (
        deviceModel
    )
    {
        case 1:
            modelName =
                "GPS Series";
            break;

        case 3:
            modelName =
                "i-Series";
            break;

        case 4:
            modelName =
                "i-Series SE";
            break;

        case 5:
            modelName =
                "CGE";
            break;

        case 6:
            modelName =
                "Advanced GT";
            break;

        case 7:
            modelName =
                "SLT";
            break;

        case 9:
            modelName =
                "CPC";
            break;

        case 10:
            modelName =
                "GT";
            break;

        case 11:
            modelName =
                "4/5 SE";
            break;

        case 12:
            modelName =
                "6/8 SE";
            break;

        default:
            break;
    }


    strncpy(
        model,
        modelName,
        modelSize - 1
    );


    model[
        modelSize - 1
    ] =
        '\0';


    return true;
}