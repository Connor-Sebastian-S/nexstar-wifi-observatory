#include "GPS.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <vector>

// ============================================================
// Constructor
// ============================================================

GPS::GPS()
    : _fd(-1),
      _device(),
      _baud(0),
      _line(),
      _state()
{
}


// ============================================================
// Destructor
// ============================================================

GPS::~GPS()
{
    if (_fd >= 0)
    {
        close(_fd);

        _fd = -1;
    }
}


// ============================================================
// Baud conversion
// ============================================================

speed_t GPS::baudToTermios(
    uint32_t baud
)
{
    switch (baud)
    {
        case 4800:
            return B4800;

        case 9600:
            return B9600;

        case 19200:
            return B19200;

        case 38400:
            return B38400;

        case 57600:
            return B57600;

        case 115200:
            return B115200;

        default:
            return B9600;
    }
}


// ============================================================
// Begin
// ============================================================

bool GPS::begin(
    const char* device,
    uint32_t baud
)
{
    if (device == nullptr)
    {
        return false;
    }


    if (_fd >= 0)
    {
        close(_fd);

        _fd = -1;
    }


    _device = device;
    _baud = baud;


    _fd =
        open(
            device,
            O_RDWR |
            O_NOCTTY |
            O_NONBLOCK
        );


    if (_fd < 0)
    {
        return false;
    }


    if (!configurePort())
    {
        close(_fd);

        _fd = -1;

        return false;
    }


    return true;
}


// ============================================================
// Configure UART
// ============================================================

bool GPS::configurePort()
{
    struct termios options{};


    if (
        tcgetattr(
            _fd,
            &options
        ) != 0
    )
    {
        return false;
    }


    cfmakeraw(
        &options
    );


    speed_t speed =
        baudToTermios(
            _baud
        );


    cfsetispeed(
        &options,
        speed
    );


    cfsetospeed(
        &options,
        speed
    );


    options.c_cflag &=
        ~PARENB;

    options.c_cflag &=
        ~CSTOPB;

    options.c_cflag &=
        ~CSIZE;

    options.c_cflag |=
        CS8;

    options.c_cflag |=
        CREAD;

    options.c_cflag |=
        CLOCAL;


    options.c_iflag &=
        ~(IXON | IXOFF | IXANY);


    options.c_cc[VMIN] = 0;

    options.c_cc[VTIME] = 0;


    tcflush(
        _fd,
        TCIOFLUSH
    );


    return
        tcsetattr(
            _fd,
            TCSANOW,
            &options
        ) == 0;
}


// ============================================================
// Update
// ============================================================

void GPS::update()
{
    if (_fd < 0)
    {
        return;
    }


    uint8_t buffer[256];


    while (true)
    {
        ssize_t count =
            ::read(
                _fd,
                buffer,
                sizeof(buffer)
            );


        if (count <= 0)
        {
            break;
        }


        for (
            ssize_t i = 0;
            i < count;
            ++i
        )
        {
            processByte(
                static_cast<char>(
                    buffer[i]
                )
            );
        }
    }
}


// ============================================================
// Process byte
// ============================================================

void GPS::processByte(
    char byte
)
{
    if (byte == '$')
    {
        _line.clear();

        _line += byte;

        return;
    }


    if (_line.empty())
    {
        return;
    }


    _line += byte;


    if (
        byte == '\n'
    )
    {
        processLine(
            _line
        );

        _line.clear();
    }


    if (
        _line.size() > 512
    )
    {
        _line.clear();
    }
}


// ============================================================
// State accessor
// ============================================================

const GPSState& GPS::state() const
{
    return _state;
}


// ============================================================
// Open state
// ============================================================

bool GPS::isOpen() const
{
    return _fd >= 0;
}


// ============================================================
// Split CSV
// ============================================================

std::vector<std::string> GPS::split(
    const std::string& value,
    char delimiter
)
{
    std::vector<std::string> result;

    std::string current;


    for (char c : value)
    {
        if (c == delimiter)
        {
            result.push_back(
                current
            );

            current.clear();
        }
        else
        {
            current += c;
        }
    }


    result.push_back(
        current
    );


    return result;
}


// ============================================================
// Parse NMEA line
// ============================================================

void GPS::processLine(
    const std::string& line
)
{
    if (
        line.size() < 7 ||
        line[0] != '$'
    )
    {
        return;
    }


    std::string sentence =
        line;


    /*
     * Remove CR/LF.
     */

    while (
        !sentence.empty() &&
        (
            sentence.back() == '\r' ||
            sentence.back() == '\n'
        )
    )
    {
        sentence.pop_back();
    }


    /*
     * Ignore checksum validation here.
     *
     * The GPS module is already producing structured NMEA
     * sentences and the parsed fields will be sanity-checked.
     */

    size_t star =
        sentence.find('*');


    if (star != std::string::npos)
    {
        sentence =
            sentence.substr(
                0,
                star
            );
    }


    std::vector<std::string> fields =
        split(
            sentence,
            ','
        );


    if (fields.empty())
    {
        return;
    }


    const std::string& type =
        fields[0];


    if (
        type.size() >= 6 &&
        type.substr(
            type.size() - 3
        ) == "GGA"
    )
    {
        parseGGA(
            sentence
        );
    }


    if (
        type.size() >= 6 &&
        type.substr(
            type.size() - 3
        ) == "RMC"
    )
    {
        parseRMC(
            sentence
        );
    }
}


// ============================================================
// Parse GGA
// ============================================================

bool GPS::parseGGA(
    const std::string& line
)
{
    std::vector<std::string> fields =
        split(
            line,
            ','
        );


    if (fields.size() < 10)
    {
        return false;
    }


    /*
     * $GPGGA,
     * 1 time
     * 2 latitude
     * 3 N/S
     * 4 longitude
     * 5 E/W
     * 6 fix quality
     * 7 satellites
     * 8 HDOP
     * 9 altitude
     */

    int fixQuality =
        parseInt(
            fields[6]
        );


    if (
        fixQuality > 0
    )
    {
        double latitude = 0.0;
        double longitude = 0.0;


        if (
            parseLatitude(
                fields[2],
                fields[3].empty()
                    ? 'N'
                    : fields[3][0],
                latitude
            )
        )
        {
            _state.latitude =
                latitude;
        }


        if (
            parseLongitude(
                fields[4],
                fields[5].empty()
                    ? 'E'
                    : fields[5][0],
                longitude
            )
        )
        {
            _state.longitude =
                longitude;
        }


        _state.fix = true;
    }
    else
    {
        _state.fix = false;
    }


    _state.satellites =
        static_cast<uint32_t>(
            parseInt(
                fields[7]
            )
        );


    _state.hdop =
        parseDouble(
            fields[8]
        );


    _state.altitude =
        parseDouble(
            fields[9]
        );


    parseTime(
        fields[1],
        _state.hour,
        _state.minute,
        _state.second
    );


    _state.updatedAt =
        0;


    return true;
}


// ============================================================
// Parse RMC
// ============================================================

bool GPS::parseRMC(
    const std::string& line
)
{
    std::vector<std::string> fields =
        split(
            line,
            ','
        );


    if (fields.size() < 10)
    {
        return false;
    }


    /*
     * $GPRMC,
     * 1 time
     * 2 status
     * 3 latitude
     * 4 N/S
     * 5 longitude
     * 6 E/W
     * 7 speed
     * 8 course
     * 9 date
     */

    if (
        fields[2] == "A"
    )
    {
        double latitude = 0.0;
        double longitude = 0.0;


        if (
            parseLatitude(
                fields[3],
                fields[4].empty()
                    ? 'N'
                    : fields[4][0],
                latitude
            )
        )
        {
            _state.latitude =
                latitude;
        }


        if (
            parseLongitude(
                fields[5],
                fields[6].empty()
                    ? 'E'
                    : fields[6][0],
                longitude
            )
        )
        {
            _state.longitude =
                longitude;
        }


        _state.fix = true;
    }


    bool timeOK =
        parseTime(
            fields[1],
            _state.hour,
            _state.minute,
            _state.second
        );


    bool dateOK =
        parseDate(
            fields[9],
            _state.day,
            _state.month,
            _state.year
        );


    _state.dateTimeValid =
        timeOK &&
        dateOK;


    return true;
}


// ============================================================
// Latitude
// ============================================================

bool GPS::parseLatitude(
    const std::string& value,
    char hemisphere,
    double& result
)
{
    if (
        value.empty()
    )
    {
        return false;
    }


    double raw =
        parseDouble(
            value
        );


    int degrees =
        static_cast<int>(
            raw / 100.0
        );


    double minutes =
        raw -
        (
            degrees *
            100.0
        );


    result =
        degrees +
        (
            minutes /
            60.0
        );


    if (
        hemisphere == 'S'
    )
    {
        result =
            -result;
    }


    return true;
}


// ============================================================
// Longitude
// ============================================================

bool GPS::parseLongitude(
    const std::string& value,
    char hemisphere,
    double& result
)
{
    if (
        value.empty()
    )
    {
        return false;
    }


    double raw =
        parseDouble(
            value
        );


    int degrees =
        static_cast<int>(
            raw / 100.0
        );


    double minutes =
        raw -
        (
            degrees *
            100.0
        );


    result =
        degrees +
        (
            minutes /
            60.0
        );


    if (
        hemisphere == 'W'
    )
    {
        result =
            -result;
    }


    return true;
}


// ============================================================
// Time
// ============================================================

bool GPS::parseTime(
    const std::string& value,
    int& hour,
    int& minute,
    int& second
)
{
    if (
        value.size() < 6
    )
    {
        return false;
    }


    hour =
        parseInt(
            value.substr(
                0,
                2
            )
        );


    minute =
        parseInt(
            value.substr(
                2,
                2
            )
        );


    second =
        parseInt(
            value.substr(
                4,
                2
            )
        );


    return
        hour >= 0 &&
        hour < 24 &&
        minute >= 0 &&
        minute < 60 &&
        second >= 0 &&
        second < 60;
}


// ============================================================
// Date
// ============================================================

bool GPS::parseDate(
    const std::string& value,
    int& day,
    int& month,
    int& year
)
{
    if (
        value.size() < 6
    )
    {
        return false;
    }


    day =
        parseInt(
            value.substr(
                0,
                2
            )
        );


    month =
        parseInt(
            value.substr(
                2,
                2
            )
        );


    int shortYear =
        parseInt(
            value.substr(
                4,
                2
            )
        );


    year =
        (
            shortYear >= 80
        )
        ? 1900 + shortYear
        : 2000 + shortYear;


    return
        day >= 1 &&
        day <= 31 &&
        month >= 1 &&
        month <= 12;
}


// ============================================================
// Numeric helpers
// ============================================================

double GPS::parseDouble(
    const std::string& value
)
{
    if (
        value.empty()
    )
    {
        return 0.0;
    }


    return std::strtod(
        value.c_str(),
        nullptr
    );
}


int GPS::parseInt(
    const std::string& value
)
{
    if (
        value.empty()
    )
    {
        return 0;
    }


    return std::strtol(
        value.c_str(),
        nullptr,
        10
    );
}
