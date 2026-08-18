#include "StellariumServer.h"

#include "../nexstar/NexStar.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>


// ============================================================
// Constructor
// ============================================================

StellariumServer::StellariumServer()
    : _serverSocket(-1),
      _clientSocket(-1),
      _port(0),
      _ra(0.0),
      _dec(0.0),
      _positionValid(false),
      _status(0),
      _lastPositionSend(0),
      _gotoRequestPending(false),
      _lastGotoRa(0.0),
      _lastGotoDec(0.0),
      _lastGotoAccepted(false)
{
}


// ============================================================
// Destructor
// ============================================================

StellariumServer::~StellariumServer()
{
    stop();
}


// ============================================================
// Current time
// ============================================================

uint64_t StellariumServer::currentTimeMicros()
{
    timeval tv{};


    gettimeofday(
        &tv,
        nullptr
    );


    return
        (
            static_cast<uint64_t>(
                tv.tv_sec
            )
            *
            1000000ULL
        )
        +
        static_cast<uint64_t>(
            tv.tv_usec
        );
}


// ============================================================
// Begin
// ============================================================

bool StellariumServer::begin(
    uint16_t port
)
{
    stop();


    _port =
        port;


    _serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );


    if (
        _serverSocket < 0
    )
    {
        return false;
    }


    int reuse =
        1;


    setsockopt(
        _serverSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse,
        sizeof(reuse)
    );


    if (
        !configureSocket()
    )
    {
        stop();

        return false;
    }


    sockaddr_in address{};


    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        htonl(
            INADDR_ANY
        );

    address.sin_port =
        htons(
            _port
        );


    if (
        bind(
            _serverSocket,
            reinterpret_cast<
                sockaddr*
            >(
                &address
            ),
            sizeof(address)
        ) < 0
    )
    {
        stop();

        return false;
    }


    if (
        listen(
            _serverSocket,
            1
        ) < 0
    )
    {
        stop();

        return false;
    }


    std::printf(
        "Stellarium server listening on TCP port %u.\n",
        _port
    );


    return true;
}


// ============================================================
// Configure non-blocking socket
// ============================================================

bool StellariumServer::configureSocket()
{
    int flags =
        fcntl(
            _serverSocket,
            F_GETFL,
            0
        );


    if (
        flags < 0
    )
    {
        return false;
    }


    return
        fcntl(
            _serverSocket,
            F_SETFL,
            flags |
            O_NONBLOCK
        ) == 0;
}


// ============================================================
// Accept client
// ============================================================

bool StellariumServer::acceptClient()
{
    sockaddr_in clientAddress{};

    socklen_t length =
        sizeof(clientAddress);


    int socket =
        accept(
            _serverSocket,
            reinterpret_cast<
                sockaddr*
            >(
                &clientAddress
            ),
            &length
        );


    if (
        socket < 0
    )
    {
        if (
            errno == EAGAIN ||
            errno == EWOULDBLOCK
        )
        {
            return false;
        }


        return false;
    }


    closeClient();


    _clientSocket =
        socket;


    int flags =
        fcntl(
            _clientSocket,
            F_GETFL,
            0
        );


    if (
        flags >= 0
    )
    {
        fcntl(
            _clientSocket,
            F_SETFL,
            flags |
            O_NONBLOCK
        );
    }


    int noDelay =
        1;


    setsockopt(
        _clientSocket,
        IPPROTO_TCP,
        TCP_NODELAY,
        &noDelay,
        sizeof(noDelay)
    );


    char addressText[
        INET_ADDRSTRLEN
    ] = {};


    inet_ntop(
        AF_INET,
        &clientAddress.sin_addr,
        addressText,
        sizeof(addressText)
    );


    std::printf(
        "Stellarium connected from %s:%u.\n",
        addressText,
        ntohs(
            clientAddress.sin_port
        )
    );


    _lastPositionSend =
        0;


    return true;
}


// ============================================================
// Close client
// ============================================================

void StellariumServer::closeClient()
{
    if (
        _clientSocket >= 0
    )
    {
        close(
            _clientSocket
        );


        _clientSocket =
            -1;
    }
}


// ============================================================
// Connection
// ============================================================

bool StellariumServer::isConnected() const
{
    return _clientSocket >= 0;
}


// ============================================================
// Update position
// ============================================================

void StellariumServer::updatePosition(
    double ra,
    double dec,
    bool valid,
    uint32_t status
)
{
    _ra =
        ra;

    _dec =
        dec;

    _positionValid =
        valid;

    _status =
        status;
}


// ============================================================
// Take pending GOTO request
// ============================================================

bool StellariumServer::takeGotoRequest(
    double& ra,
    double& dec,
    bool& accepted
)
{
    if (
        !_gotoRequestPending
    )
    {
        return false;
    }


    ra =
        _lastGotoRa;

    dec =
        _lastGotoDec;

    accepted =
        _lastGotoAccepted;


    _gotoRequestPending =
        false;


    return true;
}


// ============================================================
// Little-endian readers
// ============================================================

uint16_t StellariumServer::readUInt16LE(
    const uint8_t* data
)
{
    return
        static_cast<uint16_t>(
            data[0]
            |
            (
                static_cast<uint16_t>(
                    data[1]
                )
                << 8
            )
        );
}


uint32_t StellariumServer::readUInt32LE(
    const uint8_t* data
)
{
    return
        static_cast<uint32_t>(
            data[0]
            |
            (
                static_cast<uint32_t>(
                    data[1]
                )
                << 8
            )
            |
            (
                static_cast<uint32_t>(
                    data[2]
                )
                << 16
            )
            |
            (
                static_cast<uint32_t>(
                    data[3]
                )
                << 24
            )
        );
}


// ============================================================
// Little-endian writers
// ============================================================

void StellariumServer::writeUInt16LE(
    uint8_t* data,
    uint16_t value
)
{
    data[0] =
        static_cast<uint8_t>(
            value &
            0xFF
        );


    data[1] =
        static_cast<uint8_t>(
            (
                value
                >> 8
            )
            &
            0xFF
        );
}


void StellariumServer::writeUInt32LE(
    uint8_t* data,
    uint32_t value
)
{
    data[0] =
        static_cast<uint8_t>(
            value &
            0xFF
        );


    data[1] =
        static_cast<uint8_t>(
            (
                value
                >> 8
            )
            &
            0xFF
        );


    data[2] =
        static_cast<uint8_t>(
            (
                value
                >> 16
            )
            &
            0xFF
        );


    data[3] =
        static_cast<uint8_t>(
            (
                value
                >> 24
            )
            &
            0xFF
        );
}


// ============================================================
// Send position
// ============================================================

void StellariumServer::sendPosition()
{
    if (
        _clientSocket < 0 ||
        !_positionValid
    )
    {
        return;
    }


    uint8_t packet[24] =
        {};


    writeUInt16LE(
        packet,
        24
    );


    writeUInt16LE(
        packet + 2,
        0
    );


    uint64_t timestamp =
        currentTimeMicros();


    for (
        int i = 0;
        i < 8;
        ++i
    )
    {
        packet[
            4 + i
        ] =
            static_cast<uint8_t>(
                timestamp &
                0xFF
            );


        timestamp >>=
            8;
    }


    double normalisedRa =
        _ra;


    while (
        normalisedRa < 0.0
    )
    {
        normalisedRa +=
            360.0;
    }


    while (
        normalisedRa >= 360.0
    )
    {
        normalisedRa -=
            360.0;
    }


    uint64_t raRaw =
        static_cast<uint64_t>(
            std::llround(
                (
                    normalisedRa /
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


    double clampedDec =
        _dec;


    if (
        clampedDec > 180.0
    )
    {
        clampedDec =
            180.0;
    }


    if (
        clampedDec < -180.0
    )
    {
        clampedDec =
            -180.0;
    }


    int32_t decValue =
        static_cast<int32_t>(
            std::llround(
                (
                    clampedDec /
                    360.0
                )
                *
                4294967296.0
            )
        );


    writeUInt32LE(
        packet + 12,
        raValue
    );


    writeUInt32LE(
        packet + 16,
        static_cast<uint32_t>(
            decValue
        )
    );


    writeUInt32LE(
        packet + 20,
        _status
    );


    ssize_t written =
        send(
            _clientSocket,
            packet,
            sizeof(packet),
            MSG_NOSIGNAL
        );


    if (
        written < 0
    )
    {
        if (
            errno != EAGAIN &&
            errno != EWOULDBLOCK
        )
        {
            std::printf(
                "Stellarium client disconnected during write.\n"
            );


            closeClient();
        }


        return;
    }


    _lastPositionSend =
        currentTimeMicros();
}


// ============================================================
// Process packet
// ============================================================

bool StellariumServer::processPacket(
    const uint8_t* packet,
    size_t size,
    NexStar& telescope
)
{
    if (
        packet == nullptr ||
        size < 4
    )
    {
        return false;
    }


    uint16_t packetSize =
        readUInt16LE(
            packet
        );


    uint16_t packetType =
        readUInt16LE(
            packet + 2
        );


    if (
        packetSize != size
    )
    {
        return false;
    }


    /*
     * Stellarium GOTO.
     */

    if (
        packetType == 0 &&
        packetSize == 20
    )
    {
        uint32_t raValue =
            readUInt32LE(
                packet + 12
            );


        uint32_t decRaw =
            readUInt32LE(
                packet + 16
            );


        int32_t decValue =
            static_cast<int32_t>(
                decRaw
            );


        double ra =
            (
                static_cast<double>(
                    raValue
                )
                /
                4294967296.0
            )
            *
            360.0;


        double dec =
            (
                static_cast<double>(
                    decValue
                )
                /
                4294967296.0
            )
            *
            360.0;


        std::printf(
            "Stellarium GOTO request: "
            "RA %.6f deg, Dec %.6f deg\n",
            ra,
            dec
        );


        bool accepted =
            false;


        if (
            telescope.isConnected()
        )
        {
            accepted =
                telescope.gotoRaDecPrecise(
                    ra,
                    dec
                );
        }
        else
        {
            std::printf(
                "Stellarium GOTO ignored: "
                "NexStar HC not ready.\n"
            );
        }


        _lastGotoRa =
            ra;


        _lastGotoDec =
            dec;


        _lastGotoAccepted =
            accepted;


        _gotoRequestPending =
            true;


        if (
            accepted
        )
        {
            std::printf(
                "Stellarium GOTO accepted by NexStar.\n"
            );
        }
        else
        {
            std::printf(
                "Stellarium GOTO rejected by NexStar.\n"
            );
        }


        return true;
    }


    return false;
}


// ============================================================
// Read client
// ============================================================

void StellariumServer::readClient(
    NexStar& telescope
)
{
    if (
        _clientSocket < 0
    )
    {
        return;
    }


    static uint8_t buffer[4096];

    static size_t used =
        0;


    while (true)
    {
        uint8_t incoming[512];


        ssize_t count =
            recv(
                _clientSocket,
                incoming,
                sizeof(incoming),
                0
            );


        if (
            count < 0
        )
        {
            if (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            )
            {
                break;
            }


            closeClient();

            used =
                0;


            return;
        }


        if (
            count == 0
        )
        {
            std::printf(
                "Stellarium client closed connection.\n"
            );


            closeClient();

            used =
                0;


            return;
        }


        size_t incomingSize =
            static_cast<size_t>(
                count
            );


        if (
            used +
            incomingSize >
            sizeof(buffer)
        )
        {
            std::printf(
                "Stellarium input buffer overflow.\n"
            );


            closeClient();

            used =
                0;


            return;
        }


        memcpy(
            buffer + used,
            incoming,
            incomingSize
        );


        used +=
            incomingSize;


        while (
            used >= 2
        )
        {
            uint16_t packetSize =
                readUInt16LE(
                    buffer
                );


            if (
                packetSize < 4 ||
                packetSize >
                sizeof(buffer)
            )
            {
                std::printf(
                    "Bad Stellarium packet size: %u\n",
                    packetSize
                );


                closeClient();

                used =
                    0;


                return;
            }


            if (
                used <
                packetSize
            )
            {
                break;
            }


            processPacket(
                buffer,
                packetSize,
                telescope
            );


            size_t remaining =
                used -
                packetSize;


            if (
                remaining > 0
            )
            {
                memmove(
                    buffer,
                    buffer + packetSize,
                    remaining
                );
            }


            used =
                remaining;
        }
    }
}


// ============================================================
// Handle
// ============================================================

void StellariumServer::handle(
    NexStar& telescope
)
{
    if (
        _serverSocket < 0
    )
    {
        return;
    }


    acceptClient();


    if (
        _clientSocket < 0
    )
    {
        return;
    }


    readClient(
        telescope
    );


    uint64_t now =
        currentTimeMicros();


    if (
        now -
        _lastPositionSend >=
        500000ULL
    )
    {
        sendPosition();
    }
}


// ============================================================
// Stop
// ============================================================

void StellariumServer::stop()
{
    closeClient();


    if (
        _serverSocket >= 0
    )
    {
        close(
            _serverSocket
        );


        _serverSocket =
            -1;
    }
}
