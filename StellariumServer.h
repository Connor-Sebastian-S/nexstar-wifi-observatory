#pragma once

#include <cstddef>
#include <cstdint>


class NexStar;


class StellariumServer
{
public:

    StellariumServer();

    ~StellariumServer();


    bool begin(
        uint16_t port
    );


    void updatePosition(
        double ra,
        double dec,
        bool valid,
        uint32_t status = 0
    );


    void handle(
        NexStar& telescope
    );


    void stop();


    bool isConnected() const;


    bool takeGotoRequest(
        double& ra,
        double& dec,
        bool& accepted
    );


private:

    int _serverSocket;

    int _clientSocket;

    uint16_t _port;


    double _ra;

    double _dec;

    bool _positionValid;

    uint32_t _status;


    uint64_t _lastPositionSend;


    bool _gotoRequestPending;

    double _lastGotoRa;

    double _lastGotoDec;

    bool _lastGotoAccepted;


    bool configureSocket();

    bool acceptClient();

    void closeClient();


    void readClient(
        NexStar& telescope
    );


    void sendPosition();


    bool processPacket(
        const uint8_t* packet,
        size_t size,
        NexStar& telescope
    );


    static uint64_t currentTimeMicros();


    static uint16_t readUInt16LE(
        const uint8_t* data
    );


    static uint32_t readUInt32LE(
        const uint8_t* data
    );


    static void writeUInt16LE(
        uint8_t* data,
        uint16_t value
    );


    static void writeUInt32LE(
        uint8_t* data,
        uint32_t value
    );
};