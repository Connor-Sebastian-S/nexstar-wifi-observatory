#pragma once

#include <cstdint>
#include <string>
#include <vector>


struct SkyTarget
{
    int objectId =
        0;

    std::string designation;

    std::string name;

    double raDeg =
        0.0;

    double decDeg =
        0.0;

    double magnitude =
        99.0;

    double altitudeDeg =
        0.0;

    double azimuthDeg =
        0.0;

    double score =
        0.0;
};


struct SkyState
{
    bool valid =
        false;

    bool darkEnough =
        false;

    double sunAltitudeDeg =
        0.0;

    std::vector<SkyTarget>
        targets;
};


class SkyCatalogue
{
public:

    SkyCatalogue();

    ~SkyCatalogue();


    bool open(
        const std::string& databasePath
    );


    void close();


    bool isOpen() const;


    SkyState calculate(
        double latitudeDeg,
        double longitudeDeg,
        int year,
        int month,
        int day,
        int hour,
        int minute,
        int second,
        double minimumAltitudeDeg = 20.0,
        double maximumMagnitude = 10.0,
        int maximumTargets = 12
    );


private:

    void* _database;


    static double julianDate(
        int year,
        int month,
        int day,
        int hour,
        int minute,
        int second
    );


    static double greenwichMeanSiderealTime(
        double jd
    );


    static void equatorialToHorizontal(
        double raDeg,
        double decDeg,
        double latitudeDeg,
        double longitudeDeg,
        double jd,
        double& altitudeDeg,
        double& azimuthDeg
    );


    static void calculateSunPosition(
        double jd,
        double& raDeg,
        double& decDeg
    );


    static double normaliseDegrees(
        double degrees
    );


    static double normaliseSignedDegrees(
        double degrees
    );
};
