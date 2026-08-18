cmake_minimum_required(VERSION 3.16)

project(telescopehub)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


find_package(Threads REQUIRED)


add_executable(
    telescopehub

    main.cpp

    WebServer.cpp
    StellariumServer.cpp
    Recorder.cpp

    GPS.cpp
    I2CBus.cpp
    EnvironmentSensor.cpp

    compat/Arduino.cpp
    compat/HardwareSerial.cpp

    ../nexstar/NexStar.cpp
)


target_include_directories(
    telescopehub
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}/compat
    ${CMAKE_CURRENT_SOURCE_DIR}/../nexstar
)


target_link_libraries(
    telescopehub

    PRIVATE
    Threads::Threads
)