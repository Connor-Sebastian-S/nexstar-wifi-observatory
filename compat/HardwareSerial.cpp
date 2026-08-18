#include "HardwareSerial.h"

#include <cstdio>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>


// ============================================================
// Constructor
// ============================================================

HardwareSerial::HardwareSerial()
    : _fd(-1),
      _device(),
      _baud(0)
{
}


// ============================================================
// Destructor
// ============================================================

HardwareSerial::~HardwareSerial()
{
    end();
}


// ============================================================
// Convert baud rate
// ============================================================

int HardwareSerial::baudToTermios(
    uint32_t baud
)
{
    switch (baud)
    {
        case 1200:
            return static_cast<int>(B1200);

        case 2400:
            return static_cast<int>(B2400);

        case 4800:
            return static_cast<int>(B4800);

        case 9600:
            return static_cast<int>(B9600);

        case 19200:
            return static_cast<int>(B19200);

        case 38400:
            return static_cast<int>(B38400);

        case 57600:
            return static_cast<int>(B57600);

        case 115200:
            return static_cast<int>(B115200);

        default:
            return static_cast<int>(B9600);
    }
}


// ============================================================
// Open serial device
// ============================================================

bool HardwareSerial::begin(
    const char* device,
    uint32_t baud
)
{
    end();


    if (device == nullptr)
    {
        return false;
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


    configurePort();


    return true;
}


// ============================================================
// Configure serial port
// ============================================================

void HardwareSerial::configurePort()
{
    struct termios options;


    if (
        tcgetattr(
            _fd,
            &options
        ) != 0
    )
    {
        return;
    }


    // --------------------------------------------------------
    // Raw mode
    // --------------------------------------------------------

    cfmakeraw(
        &options
    );


    // --------------------------------------------------------
    // Baud
    // --------------------------------------------------------

    speed_t speed =
        static_cast<speed_t>(
            baudToTermios(
                _baud
            )
        );


    cfsetispeed(
        &options,
        speed
    );


    cfsetospeed(
        &options,
        speed
    );


    // --------------------------------------------------------
    // 8-N-1
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // No software flow control
    // --------------------------------------------------------

    options.c_iflag &=
        ~(IXON | IXOFF | IXANY);


    // --------------------------------------------------------
    // Non-blocking input
    // --------------------------------------------------------

    options.c_cc[VMIN] = 0;

    options.c_cc[VTIME] = 0;


    // --------------------------------------------------------
    // Flush stale data
    // --------------------------------------------------------

    tcflush(
        _fd,
        TCIOFLUSH
    );


    // --------------------------------------------------------
    // Apply configuration
    // --------------------------------------------------------

    tcsetattr(
        _fd,
        TCSANOW,
        &options
    );
}


// ============================================================
// Close
// ============================================================

void HardwareSerial::end()
{
    if (_fd >= 0)
    {
        close(
            _fd
        );

        _fd = -1;
    }
}


// ============================================================
// Available bytes
// ============================================================

int HardwareSerial::available()
{
    if (_fd < 0)
    {
        return 0;
    }


    int bytes = 0;


    if (
        ioctl(
            _fd,
            FIONREAD,
            &bytes
        ) != 0
    )
    {
        return 0;
    }


    return bytes;
}


// ============================================================
// Read one byte
// ============================================================

int HardwareSerial::read()
{
    if (_fd < 0)
    {
        return -1;
    }


    uint8_t value = 0;


    ssize_t result =
        ::read(
            _fd,
            &value,
            1
        );


    if (result != 1)
    {
        return -1;
    }


    return static_cast<int>(
        value
    );
}


// ============================================================
// Write buffer
// ============================================================

size_t HardwareSerial::write(
    const uint8_t* data,
    size_t length
)
{
    if (
        _fd < 0 ||
        data == nullptr ||
        length == 0
    )
    {
        return 0;
    }


    ssize_t written =
        ::write(
            _fd,
            data,
            length
        );


    if (written < 0)
    {
        return 0;
    }


    return static_cast<size_t>(
        written
    );
}


// ============================================================
// Write one byte
// ============================================================

size_t HardwareSerial::write(
    uint8_t data
)
{
    return write(
        &data,
        1
    );
}


// ============================================================
// Flush output
// ============================================================

void HardwareSerial::flush()
{
    if (_fd >= 0)
    {
        tcdrain(
            _fd
        );
    }
}


// ============================================================
// Arduino-style diagnostic output
// ============================================================

void HardwareSerial::print(
    const char* text
)
{
    if (text != nullptr)
    {
        std::fputs(
            text,
            stdout
        );
    }

    std::fflush(
        stdout
    );
}


void HardwareSerial::print(
    const char* text,
    int value
)
{
    if (text != nullptr)
    {
        std::fputs(
            text,
            stdout
        );
    }

    std::printf(
        "%d",
        value
    );

    std::fflush(
        stdout
    );
}


void HardwareSerial::println()
{
    std::fputc(
        '\n',
        stdout
    );

    std::fflush(
        stdout
    );
}


void HardwareSerial::println(
    const char* text
)
{
    if (text != nullptr)
    {
        std::fputs(
            text,
            stdout
        );
    }

    std::fputc(
        '\n',
        stdout
    );

    std::fflush(
        stdout
    );
}


void HardwareSerial::println(
    int value
)
{
    std::printf(
        "%d\n",
        value
    );

    std::fflush(
        stdout
    );
}


// ============================================================
// Is open?
// ============================================================

bool HardwareSerial::isOpen() const
{
    return _fd >= 0;
}