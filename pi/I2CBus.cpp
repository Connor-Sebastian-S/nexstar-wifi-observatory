#include "I2CBus.h"

#include <cerrno>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>


I2CBus::I2CBus()
    : _fd(-1),
      _device()
{
}


I2CBus::~I2CBus()
{
    end();
}


bool I2CBus::begin(
    const char* device
)
{
    end();


    if (
        device == nullptr
    )
    {
        return false;
    }


    _device =
        device;


    _fd =
        open(
            device,
            O_RDWR
        );


    return _fd >= 0;
}


void I2CBus::end()
{
    if (_fd >= 0)
    {
        close(
            _fd
        );

        _fd = -1;
    }
}


bool I2CBus::isOpen() const
{
    return _fd >= 0;
}


bool I2CBus::write(
    uint8_t address,
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
        return false;
    }


    if (
        ioctl(
            _fd,
            I2C_SLAVE,
            address
        ) < 0
    )
    {
        return false;
    }


    ssize_t result =
        ::write(
            _fd,
            data,
            length
        );


    return
        result ==
        static_cast<ssize_t>(
            length
        );
}


bool I2CBus::read(
    uint8_t address,
    uint8_t* data,
    size_t length
)
{
    if (
        _fd < 0 ||
        data == nullptr ||
        length == 0
    )
    {
        return false;
    }


    if (
        ioctl(
            _fd,
            I2C_SLAVE,
            address
        ) < 0
    )
    {
        return false;
    }


    ssize_t result =
        ::read(
            _fd,
            data,
            length
        );


    return
        result ==
        static_cast<ssize_t>(
            length
        );
}


bool I2CBus::writeRead(
    uint8_t address,
    const uint8_t* writeData,
    size_t writeLength,
    uint8_t* readData,
    size_t readLength
)
{
    if (
        _fd < 0 ||
        writeData == nullptr ||
        writeLength == 0 ||
        readData == nullptr ||
        readLength == 0
    )
    {
        return false;
    }


    if (
        ioctl(
            _fd,
            I2C_SLAVE,
            address
        ) < 0
    )
    {
        return false;
    }


    ssize_t written =
        ::write(
            _fd,
            writeData,
            writeLength
        );


    if (
        written !=
        static_cast<ssize_t>(
            writeLength
        )
    )
    {
        return false;
    }


    ssize_t received =
        ::read(
            _fd,
            readData,
            readLength
        );


    return
        received ==
        static_cast<ssize_t>(
            readLength
        );
}
