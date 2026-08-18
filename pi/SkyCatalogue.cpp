#include "SkyCatalogue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sqlite3.h>


namespace
{
    constexpr double PI =
        3.1415926535897932384626433832795;

    constexpr double DEG_TO_RAD =
        PI / 180.0;

    constexpr double RAD_TO_DEG =
        180.0 / PI;

    constexpr double MOON_HORIZON_ALTITUDE_DEG =
        -0.57;

    constexpr double MOON_MIN_SEPARATION_DEG =
        20.0;


    double clamp(
        double value,
        double minimum,
        double maximum
    )
    {
        return
            std::max(
                minimum,
                std::min(
                    maximum,
                    value
                )
            );
    }


    double sinDeg(
        double degrees
    )
    {
        return
            std::sin(
                degrees *
                DEG_TO_RAD
            );
    }


    double cosDeg(
        double degrees
    )
    {
        return
            std::cos(
                degrees *
                DEG_TO_RAD
            );
    }


    double asinDeg(
        double value
    )
    {
        return
            std::asin(
                clamp(
                    value,
                    -1.0,
                    1.0
                )
            )
            *
            RAD_TO_DEG;
    }


    double atan2Deg(
        double y,
        double x
    )
    {
        return
            std::atan2(
                y,
                x
            )
            *
            RAD_TO_DEG;
    }


    int daysInMonth(
        int year,
        int month
    )
    {
        static const int days[] =
        {
            31, 28, 31, 30,
            31, 30, 31, 31,
            30, 31, 30, 31
        };

        if (
            month == 2 &&
            (
                (
                    year % 4 == 0 &&
                    year % 100 != 0
                )
                ||
                (
                    year % 400 == 0
                )
            )
        )
        {
            return 29;
        }

        return
            days[
                month - 1
            ];
    }

}


SkyCatalogue::SkyCatalogue()
    : _database(nullptr)
{
}


SkyCatalogue::~SkyCatalogue()
{
    close();
}


bool SkyCatalogue::open(
    const std::string& databasePath
)
{
    close();

    sqlite3* database =
        nullptr;

    int result =
        sqlite3_open_v2(
            databasePath.c_str(),
            &database,
            SQLITE_OPEN_READONLY,
            nullptr
        );

    if (
        result != SQLITE_OK
    )
    {
        if (database)
        {
            std::fprintf(
                stderr,
                "SkyCatalogue: SQLite error: %s\n",
                sqlite3_errmsg(database)
            );

            sqlite3_close(
                database
            );
        }

        return false;
    }

    _database =
        database;

    return true;
}


void SkyCatalogue::close()
{
    if (
        _database
    )
    {
        sqlite3_close(
            static_cast<sqlite3*>(
                _database
            )
        );

        _database =
            nullptr;
    }
}


bool SkyCatalogue::isOpen() const
{
    return
        _database != nullptr;
}


double SkyCatalogue::julianDate(
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second
)
{
    if (
        month <= 2
    )
    {
        --year;
        month += 12;
    }

    int A =
        year / 100;

    int B =
        2 -
        A +
        (
            A / 4
        );

    double dayFraction =
        (
            static_cast<double>(
                hour
            )
            +
            static_cast<double>(
                minute
            ) /
            60.0
            +
            static_cast<double>(
                second
            ) /
            3600.0
        )
        /
        24.0;

    return
        std::floor(
            365.25 *
            (
                year +
                4716
            )
        )
        +
        std::floor(
            30.6001 *
            (
                month +
                1
            )
        )
        +
        static_cast<double>(
            day
        )
        +
        static_cast<double>(
            B
        )
        -
        1524.5
        +
        dayFraction;
}


std::time_t SkyCatalogue::utcEpoch(
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second
)
{
    tm value{};

    value.tm_year =
        year -
        1900;

    value.tm_mon =
        month -
        1;

    value.tm_mday =
        day;

    value.tm_hour =
        hour;

    value.tm_min =
        minute;

    value.tm_sec =
        second;

    return
        timegm(
            &value
        );
}


double SkyCatalogue::greenwichMeanSiderealTime(
    double jd
)
{
    double T =
        (
            jd -
            2451545.0
        )
        /
        36525.0;

    double gmst =
        280.46061837
        +
        360.98564736629 *
        (
            jd -
            2451545.0
        )
        +
        0.000387933 *
        T *
        T
        -
        (
            T *
            T *
            T
            /
            38710000.0
        );

    return
        normaliseDegrees(
            gmst
        );
}


double SkyCatalogue::normaliseDegrees(
    double degrees
)
{
    degrees =
        std::fmod(
            degrees,
            360.0
        );

    if (
        degrees < 0.0
    )
    {
        degrees +=
            360.0;
    }

    return degrees;
}


double SkyCatalogue::normaliseSignedDegrees(
    double degrees
)
{
    degrees =
        normaliseDegrees(
            degrees
        );

    if (
        degrees > 180.0
    )
    {
        degrees -=
            360.0;
    }

    return degrees;
}


void SkyCatalogue::equatorialToHorizontal(
    double raDeg,
    double decDeg,
    double latitudeDeg,
    double longitudeDeg,
    double jd,
    double& altitudeDeg,
    double& azimuthDeg
)
{
    double lstDeg =
        normaliseDegrees(
            greenwichMeanSiderealTime(jd)
            +
            longitudeDeg
        );

    double hourAngleDeg =
        normaliseSignedDegrees(
            lstDeg -
            raDeg
        );

    double latitude =
        latitudeDeg *
        DEG_TO_RAD;

    double declination =
        decDeg *
        DEG_TO_RAD;

    double hourAngle =
        hourAngleDeg *
        DEG_TO_RAD;

    double sinAltitude =
        std::sin(latitude) *
        std::sin(declination)
        +
        std::cos(latitude) *
        std::cos(declination) *
        std::cos(hourAngle);

    sinAltitude =
        clamp(
            sinAltitude,
            -1.0,
            1.0
        );

    double altitude =
        std::asin(
            sinAltitude
        );

    double azimuth =
        std::atan2(
            std::sin(hourAngle),
            (
                std::cos(hourAngle) *
                std::sin(latitude)
                -
                std::tan(declination) *
                std::cos(latitude)
            )
        );

    altitudeDeg =
        altitude *
        RAD_TO_DEG;

    azimuthDeg =
        normaliseDegrees(
            azimuth *
            RAD_TO_DEG
            +
            180.0
        );
}


void SkyCatalogue::calculateSunPosition(
    double jd,
    double& raDeg,
    double& decDeg,
    double* eclipticLongitudeDeg
)
{
    double n =
        jd -
        2451545.0;

    double meanLongitude =
        normaliseDegrees(
            280.460 +
            0.9856474 *
            n
        );

    double meanAnomaly =
        normaliseDegrees(
            357.528 +
            0.9856003 *
            n
        );

    double g =
        meanAnomaly *
        DEG_TO_RAD;

    double eclipticLongitude =
        meanLongitude
        +
        1.915 *
        std::sin(g)
        +
        0.020 *
        std::sin(
            2.0 *
            g
        );

    eclipticLongitude =
        normaliseDegrees(
            eclipticLongitude
        );

    double epsilon =
        (
            23.4393
            -
            0.0000004 *
            n
        )
        *
        DEG_TO_RAD;

    double lambda =
        eclipticLongitude *
        DEG_TO_RAD;

    double ra =
        std::atan2(
            std::cos(epsilon) *
            std::sin(lambda),
            std::cos(lambda)
        );

    double dec =
        std::asin(
            std::sin(epsilon) *
            std::sin(lambda)
        );

    raDeg =
        normaliseDegrees(
            ra *
            RAD_TO_DEG
        );

    decDeg =
        dec *
        RAD_TO_DEG;

    if (
        eclipticLongitudeDeg
    )
    {
        *eclipticLongitudeDeg =
            eclipticLongitude;
    }
}


/*
 * Low-precision geocentric Moon position.
 *
 * This is deliberately a lightweight model suitable for a
 * local observatory dashboard: Moon phase, practical separation
 * from deep-sky targets, and rise/set times. It is not intended
 * to replace a high-precision lunar ephemeris.
 */
void SkyCatalogue::calculateMoonPosition(
    double jd,
    double& raDeg,
    double& decDeg,
    double& eclipticLongitudeDeg,
    double& eclipticLatitudeDeg,
    double& distanceKm
)
{
    double d =
        jd -
        2451543.5;

    double N =
        normaliseDegrees(
            125.1228
            -
            0.0529538083 *
            d
        );

    double inclination =
        5.1454;

    double w =
        normaliseDegrees(
            318.0634
            +
            0.1643573223 *
            d
        );

    double a =
        60.2666;

    double e =
        0.054900;

    double M =
        normaliseDegrees(
            115.3654
            +
            13.0649929509 *
            d
        );


    double E =
        M
        +
        e *
        RAD_TO_DEG *
        sinDeg(M) *
        (
            1.0 +
            e *
            cosDeg(M)
        );


    for (
        int iteration = 0;
        iteration < 6;
        ++iteration
    )
    {
        double correction =
            (
                E
                -
                e *
                RAD_TO_DEG *
                sinDeg(E)
                -
                M
            )
            /
            (
                1.0
                -
                e *
                cosDeg(E)
            );

        E -=
            correction;
    }


    double xv =
        a *
        (
            cosDeg(E) -
            e
        );

    double yv =
        a *
        std::sqrt(
            1.0 -
            e *
            e
        )
        *
        sinDeg(E);

    double v =
        atan2Deg(
            yv,
            xv
        );

    double r =
        std::sqrt(
            xv * xv +
            yv * yv
        );

    double cosN =
        cosDeg(N);

    double sinN =
        sinDeg(N);

    double cosVW =
        cosDeg(
            v + w
        );

    double sinVW =
        sinDeg(
            v + w
        );

    double xh =
        r *
        (
            cosN *
            cosVW
            -
            sinN *
            sinVW *
            cosDeg(inclination)
        );

    double yh =
        r *
        (
            sinN *
            cosVW
            +
            cosN *
            sinVW *
            cosDeg(inclination)
        );

    double zh =
        r *
        sinVW *
        sinDeg(inclination);


    double lonecl =
        atan2Deg(
            yh,
            xh
        );

    double latecl =
        atan2Deg(
            zh,
            std::sqrt(
                xh * xh +
                yh * yh
            )
        );


    double sunMeanLongitude =
        normaliseDegrees(
            280.460 +
            0.9856474 *
            d
        );

    double sunMeanAnomaly =
        normaliseDegrees(
            357.528 +
            0.9856003 *
            d
        );

    double sunEclipticLongitude =
        normaliseDegrees(
            sunMeanLongitude
            +
            1.915 *
            sinDeg(
                sunMeanAnomaly
            )
            +
            0.020 *
            sinDeg(
                2.0 *
                sunMeanAnomaly
            )
        );


    double lunarMeanLongitude =
        normaliseDegrees(
            N +
            w +
            M
        );

    double D =
        normaliseSignedDegrees(
            lunarMeanLongitude -
            sunEclipticLongitude
        );

    double F =
        normaliseSignedDegrees(
            lunarMeanLongitude -
            N
        );


    lonecl +=
        -1.274 *
        sinDeg(
            M -
            2.0 *
            D
        );

    lonecl +=
        0.658 *
        sinDeg(
            2.0 *
            D
        );

    lonecl +=
        -0.186 *
        sinDeg(
            sunMeanAnomaly
        );

    lonecl +=
        -0.059 *
        sinDeg(
            2.0 *
            M -
            2.0 *
            D
        );

    lonecl +=
        -0.057 *
        sinDeg(
            M -
            2.0 *
            D +
            sunMeanAnomaly
        );

    lonecl +=
        0.053 *
        sinDeg(
            M +
            2.0 *
            D
        );

    lonecl +=
        0.046 *
        sinDeg(
            2.0 *
            D -
            sunMeanAnomaly
        );

    lonecl +=
        0.041 *
        sinDeg(
            M -
            sunMeanAnomaly
        );

    lonecl +=
        -0.035 *
        sinDeg(D);

    lonecl +=
        -0.031 *
        sinDeg(
            M +
            sunMeanAnomaly
        );

    lonecl +=
        -0.015 *
        sinDeg(
            2.0 *
            F -
            2.0 *
            D
        );

    lonecl +=
        0.011 *
        sinDeg(
            M -
            4.0 *
            D
        );

    lonecl +=
        -0.009 *
        sinDeg(
            2.0 *
            D -
            M
        );

    lonecl +=
        -0.009 *
        sinDeg(
            2.0 *
            D -
            sunMeanAnomaly
        );

    lonecl +=
        0.008 *
        sinDeg(
            2.0 *
            D +
            sunMeanAnomaly
        );

    lonecl +=
        0.007 *
        sinDeg(
            M +
            sunMeanAnomaly -
            2.0 *
            D
        );

    lonecl +=
        0.006 *
        sinDeg(
            2.0 *
            D +
            M
        );

    lonecl +=
        0.005 *
        sinDeg(
            sunMeanAnomaly -
            M
        );

    lonecl +=
        0.005 *
        sinDeg(
            D +
            sunMeanAnomaly
        );

    lonecl +=
        0.005 *
        sinDeg(
            D +
            M
        );

    lonecl +=
        0.004 *
        sinDeg(
            D -
            sunMeanAnomaly
        );

    lonecl +=
        0.004 *
        sinDeg(
            3.0 *
            M
        );


    latecl +=
        -0.173 *
        sinDeg(
            F -
            2.0 *
            D
        );

    latecl +=
        -0.055 *
        sinDeg(
            M -
            F -
            2.0 *
            D
        );

    latecl +=
        -0.046 *
        sinDeg(
            M +
            F -
            2.0 *
            D
        );

    latecl +=
        0.033 *
        sinDeg(
            F +
            2.0 *
            D
        );

    latecl +=
        0.017 *
        sinDeg(
            2.0 *
            M +
            F
        );


    r +=
        -0.58 *
        cosDeg(
            M -
            2.0 *
            D
        );

    r +=
        -0.46 *
        cosDeg(
            2.0 *
            D
        );


    lonecl =
        normaliseDegrees(
            lonecl
        );


    double epsilon =
        23.4393 *
        DEG_TO_RAD;

    double lambda =
        lonecl *
        DEG_TO_RAD;

    double beta =
        latecl *
        DEG_TO_RAD;

    double ra =
        std::atan2(
            std::sin(lambda) *
            std::cos(epsilon)
            -
            std::tan(beta) *
            std::sin(epsilon),
            std::cos(lambda)
        );

    double dec =
        std::asin(
            std::sin(beta) *
            std::cos(epsilon)
            +
            std::cos(beta) *
            std::sin(epsilon) *
            std::sin(lambda)
        );


    raDeg =
        normaliseDegrees(
            ra *
            RAD_TO_DEG
        );

    decDeg =
        dec *
        RAD_TO_DEG;

    eclipticLongitudeDeg =
        lonecl;

    eclipticLatitudeDeg =
        latecl;

    distanceKm =
        r *
        6378.14;
}


double SkyCatalogue::angularSeparationDegrees(
    double ra1Deg,
    double dec1Deg,
    double ra2Deg,
    double dec2Deg
)
{
    double ra1 =
        ra1Deg *
        DEG_TO_RAD;

    double ra2 =
        ra2Deg *
        DEG_TO_RAD;

    double dec1 =
        dec1Deg *
        DEG_TO_RAD;

    double dec2 =
        dec2Deg *
        DEG_TO_RAD;

    double cosine =
        std::sin(dec1) *
        std::sin(dec2)
        +
        std::cos(dec1) *
        std::cos(dec2) *
        std::cos(
            ra1 -
            ra2
        );

    cosine =
        clamp(
            cosine,
            -1.0,
            1.0
        );

    return
        std::acos(
            cosine
        )
        *
        RAD_TO_DEG;
}


std::string SkyCatalogue::moonPhaseName(
    double phaseAngleDeg
)
{
    phaseAngleDeg =
        normaliseDegrees(
            phaseAngleDeg
        );

    if (
        phaseAngleDeg < 22.5 ||
        phaseAngleDeg >= 337.5
    )
    {
        return "New Moon";
    }

    if (
        phaseAngleDeg < 67.5
    )
    {
        return "Waxing Crescent";
    }

    if (
        phaseAngleDeg < 112.5
    )
    {
        return "First Quarter";
    }

    if (
        phaseAngleDeg < 157.5
    )
    {
        return "Waxing Gibbous";
    }

    if (
        phaseAngleDeg < 202.5
    )
    {
        return "Full Moon";
    }

    if (
        phaseAngleDeg < 247.5
    )
    {
        return "Waning Gibbous";
    }

    if (
        phaseAngleDeg < 292.5
    )
    {
        return "Last Quarter";
    }

    return "Waning Crescent";
}


double SkyCatalogue::moonAltitudeAtEpoch(
    double latitudeDeg,
    double longitudeDeg,
    std::time_t epochUtc
)
{
    tm utcTm{};

    gmtime_r(
        &epochUtc,
        &utcTm
    );

    double jd =
        julianDate(
            utcTm.tm_year + 1900,
            utcTm.tm_mon + 1,
            utcTm.tm_mday,
            utcTm.tm_hour,
            utcTm.tm_min,
            utcTm.tm_sec
        );

    double moonRa =
        0.0;

    double moonDec =
        0.0;

    double moonLon =
        0.0;

    double moonLat =
        0.0;

    double distanceKm =
        0.0;

    calculateMoonPosition(
        jd,
        moonRa,
        moonDec,
        moonLon,
        moonLat,
        distanceKm
    );

    double altitude =
        0.0;

    double azimuth =
        0.0;

    equatorialToHorizontal(
        moonRa,
        moonDec,
        latitudeDeg,
        longitudeDeg,
        jd,
        altitude,
        azimuth
    );

    return altitude;
}


std::time_t SkyCatalogue::findMoonCrossing(
    double latitudeDeg,
    double longitudeDeg,
    std::time_t startEpochUtc,
    std::time_t endEpochUtc,
    bool rising
)
{
    constexpr int STEP_SECONDS =
        600;

    double previousAltitude =
        moonAltitudeAtEpoch(
            latitudeDeg,
            longitudeDeg,
            startEpochUtc
        );

    std::time_t previousTime =
        startEpochUtc;


    for (
        std::time_t currentTime =
            startEpochUtc +
            STEP_SECONDS;

        currentTime <=
            endEpochUtc;

        currentTime +=
            STEP_SECONDS
    )
    {
        double currentAltitude =
            moonAltitudeAtEpoch(
                latitudeDeg,
                longitudeDeg,
                currentTime
            );


        bool crossed =
            rising
                ? (
                    previousAltitude <
                    MOON_HORIZON_ALTITUDE_DEG
                    &&
                    currentAltitude >=
                    MOON_HORIZON_ALTITUDE_DEG
                )
                : (
                    previousAltitude >=
                    MOON_HORIZON_ALTITUDE_DEG
                    &&
                    currentAltitude <
                    MOON_HORIZON_ALTITUDE_DEG
                );


        if (
            crossed
        )
        {
            std::time_t low =
                previousTime;

            std::time_t high =
                currentTime;


            for (
                int iteration = 0;
                iteration < 12;
                ++iteration
            )
            {
                std::time_t middle =
                    low +
                    (
                        high -
                        low
                    )
                    /
                    2;

                double middleAltitude =
                    moonAltitudeAtEpoch(
                        latitudeDeg,
                        longitudeDeg,
                        middle
                    );


                bool middleAbove =
                    middleAltitude >=
                    MOON_HORIZON_ALTITUDE_DEG;


                if (
                    rising
                )
                {
                    if (
                        middleAbove
                    )
                    {
                        high =
                            middle;
                    }
                    else
                    {
                        low =
                            middle;
                    }
                }
                else
                {
                    if (
                        middleAbove
                    )
                    {
                        low =
                            middle;
                    }
                    else
                    {
                        high =
                            middle;
                    }
                }
            }


            return
                low +
                (
                    high -
                    low
                )
                /
                2;
        }


        previousAltitude =
            currentAltitude;

        previousTime =
            currentTime;
    }


    return 0;
}


MoonState SkyCatalogue::calculateMoonState(
    double latitudeDeg,
    double longitudeDeg,
    double jd,
    std::time_t currentEpochUtc,
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second
)
{
    MoonState moon;

    double sunRa =
        0.0;

    double sunDec =
        0.0;

    double sunLongitude =
        0.0;


    calculateSunPosition(
        jd,
        sunRa,
        sunDec,
        &sunLongitude
    );


    double moonRa =
        0.0;

    double moonDec =
        0.0;

    double moonLongitude =
        0.0;

    double moonLatitude =
        0.0;

    double distanceKm =
        0.0;


    calculateMoonPosition(
        jd,
        moonRa,
        moonDec,
        moonLongitude,
        moonLatitude,
        distanceKm
    );


    double altitude =
        0.0;

    double azimuth =
        0.0;


    equatorialToHorizontal(
        moonRa,
        moonDec,
        latitudeDeg,
        longitudeDeg,
        jd,
        altitude,
        azimuth
    );


    double phaseAngle =
        normaliseDegrees(
            moonLongitude -
            sunLongitude
        );


    double illumination =
        (
            1.0 -
            std::cos(
                phaseAngle *
                DEG_TO_RAD
            )
        )
        *
        50.0;


    moon.valid =
        true;

    moon.raDeg =
        moonRa;

    moon.decDeg =
        moonDec;

    moon.altitudeDeg =
        altitude;

    moon.azimuthDeg =
        azimuth;

    moon.illuminationPercent =
        illumination;

    moon.distanceKm =
        distanceKm;

    moon.phaseName =
        moonPhaseName(
            phaseAngle
        );

    moon.aboveHorizon =
        altitude >=
        MOON_HORIZON_ALTITUDE_DEG;


    std::time_t dayStart =
        utcEpoch(
            year,
            month,
            day,
            0,
            0,
            0
        );

    std::time_t scanEnd =
        dayStart +
        48 *
        60 *
        60;


    moon.riseEpochUtc =
        findMoonCrossing(
            latitudeDeg,
            longitudeDeg,
            dayStart,
            scanEnd,
            true
        );

    moon.riseValid =
        moon.riseEpochUtc != 0;


    moon.setEpochUtc =
        findMoonCrossing(
            latitudeDeg,
            longitudeDeg,
            dayStart,
            scanEnd,
            false
        );

    moon.setValid =
        moon.setEpochUtc != 0;


    (void)
        currentEpochUtc;

    (void)
        hour;

    (void)
        minute;

    (void)
        second;


    return moon;
}


SkyState SkyCatalogue::calculate(
    double latitudeDeg,
    double longitudeDeg,
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second,
    double minimumAltitudeDeg,
    double maximumMagnitude,
    int maximumTargets
)
{
    SkyState result;

    if (
        !isOpen()
    )
    {
        return result;
    }


    double jd =
        julianDate(
            year,
            month,
            day,
            hour,
            minute,
            second
        );


    std::time_t currentEpochUtc =
        utcEpoch(
            year,
            month,
            day,
            hour,
            minute,
            second
        );


    // --------------------------------------------------------
    // Sun
    // --------------------------------------------------------

    double sunRa =
        0.0;

    double sunDec =
        0.0;


    calculateSunPosition(
        jd,
        sunRa,
        sunDec
    );


    double sunAltitude =
        0.0;

    double sunAzimuth =
        0.0;


    equatorialToHorizontal(
        sunRa,
        sunDec,
        latitudeDeg,
        longitudeDeg,
        jd,
        sunAltitude,
        sunAzimuth
    );


    result.sunAltitudeDeg =
        sunAltitude;

    result.darkEnough =
        sunAltitude <
        -6.0;


    // --------------------------------------------------------
    // Moon
    // --------------------------------------------------------

    result.moon =
        calculateMoonState(
            latitudeDeg,
            longitudeDeg,
            jd,
            currentEpochUtc,
            year,
            month,
            day,
            hour,
            minute,
            second
        );


    /*
     * Return a valid state even during daylight, so the UI can
     * still show the Moon information.
     */

    result.valid =
        true;


    if (
        !result.darkEnough
    )
    {
        return result;
    }


    // --------------------------------------------------------
    // Candidate query
    // --------------------------------------------------------

    const char* sql =
        R"SQL(
            SELECT
                o.id,
                o.designation,
                o.ra_deg,
                o.dec_deg,
                o.v_mag,

                COALESCE(
                    (
                        SELECT n.name
                        FROM names n
                        WHERE n.object_id = o.id
                        ORDER BY
                            CASE
                                WHEN n.catalogue = 'M'
                                    THEN 0
                                WHEN n.catalogue = 'NGC'
                                    THEN 1
                                WHEN n.catalogue = 'IC'
                                    THEN 2
                                ELSE 3
                            END,
                            n.id
                        LIMIT 1
                    ),
                    ''
                ) AS proper_name

            FROM objects o

            WHERE
                o.v_mag > 0
                AND o.v_mag <= ?

                AND (
                    o.messier > 0
                    OR o.ngc > 0
                    OR o.ic > 0

                    OR EXISTS (
                        SELECT 1
                        FROM names nn
                        WHERE nn.object_id = o.id
                    )
                )
        )SQL";


    sqlite3_stmt* statement =
        nullptr;


    int prepareResult =
        sqlite3_prepare_v2(
            static_cast<sqlite3*>(
                _database
            ),
            sql,
            -1,
            &statement,
            nullptr
        );


    if (
        prepareResult !=
        SQLITE_OK
    )
    {
        std::fprintf(
            stderr,
            "SkyCatalogue: SQL prepare failed: %s\n",
            sqlite3_errmsg(
                static_cast<sqlite3*>(
                    _database
                )
            )
        );

        return result;
    }


    sqlite3_bind_double(
        statement,
        1,
        maximumMagnitude
    );


    while (
        sqlite3_step(
            statement
        ) == SQLITE_ROW
    )
    {
        int objectId =
            sqlite3_column_int(
                statement,
                0
            );


        const char* designationText =
            reinterpret_cast<
                const char*
            >(
                sqlite3_column_text(
                    statement,
                    1
                )
            );


        double ra =
            sqlite3_column_double(
                statement,
                2
            );


        double dec =
            sqlite3_column_double(
                statement,
                3
            );


        double magnitude =
            sqlite3_column_double(
                statement,
                4
            );


        const char* nameText =
            reinterpret_cast<
                const char*
            >(
                sqlite3_column_text(
                    statement,
                    5
                )
            );


        if (
            !designationText
        )
        {
            continue;
        }


        double altitude =
            0.0;

        double azimuth =
            0.0;


        equatorialToHorizontal(
            ra,
            dec,
            latitudeDeg,
            longitudeDeg,
            jd,
            altitude,
            azimuth
        );


        if (
            altitude <
            minimumAltitudeDeg
        )
        {
            continue;
        }


        double moonSeparation =
            angularSeparationDegrees(
                ra,
                dec,
                result.moon.raDeg,
                result.moon.decDeg
            );


        if (
            moonSeparation <
            MOON_MIN_SEPARATION_DEG
        )
        {
            continue;
        }


        // ----------------------------------------------------
        // Score
        // ----------------------------------------------------

        double altitudeScore =
            clamp(
                (
                    altitude -
                    minimumAltitudeDeg
                )
                /
                (
                    90.0 -
                    minimumAltitudeDeg
                ),
                0.0,
                1.0
            );


        double magnitudeScore =
            clamp(
                (
                    maximumMagnitude -
                    magnitude
                )
                /
                maximumMagnitude,
                0.0,
                1.0
            );


        double zenithBonus =
            clamp(
                1.0 -
                (
                    std::abs(
                        altitude -
                        60.0
                    )
                    /
                    60.0
                ),
                0.0,
                1.0
            );


        double moonScore =
            clamp(
                (
                    moonSeparation -
                    MOON_MIN_SEPARATION_DEG
                )
                /
                70.0,
                0.0,
                1.0
            );


        double score =
            (
                magnitudeScore *
                0.45
            )
            +
            (
                altitudeScore *
                0.30
            )
            +
            (
                zenithBonus *
                0.10
            )
            +
            (
                moonScore *
                0.15
            );


        SkyTarget target;


        target.objectId =
            objectId;

        target.designation =
            designationText;

        target.name =
            nameText
                ? nameText
                : "";

        target.raDeg =
            ra;

        target.decDeg =
            dec;

        target.magnitude =
            magnitude;

        target.altitudeDeg =
            altitude;

        target.azimuthDeg =
            azimuth;

        target.moonSeparationDeg =
            moonSeparation;

        target.score =
            score;


        result.targets.push_back(
            target
        );
    }


    sqlite3_finalize(
        statement
    );


    std::sort(
        result.targets.begin(),
        result.targets.end(),
        [](
            const SkyTarget& a,
            const SkyTarget& b
        )
        {
            return
                a.score >
                b.score;
        }
    );


    if (
        static_cast<int>(
            result.targets.size()
        )
        >
        maximumTargets
    )
    {
        result.targets.resize(
            maximumTargets
        );
    }


    return result;
}
