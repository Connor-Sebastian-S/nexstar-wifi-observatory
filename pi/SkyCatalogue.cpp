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


    /*
     * Osculating orbital elements at epoch J2000.0 (JD 2451545.0,
     * i.e. d = JD - 2451543.5 in this file's day-number
     * convention), with per-day rates.
     *
     * Source: the same low-precision planetary theory used
     * throughout this file for the Moon (Paul Schlyter,
     * "Keplerian elements for approximate positions of the
     * major planets", stjarnhimlen.se/comp/ppcomp.html).
     * Good to a few arcminutes -- intended for GoTo pointing,
     * not for high-precision astrometry.
     */
    struct PlanetElements
    {
        const char* name;

        double N0, Nd;   // longitude of ascending node (deg, deg/day)
        double i0, id;   // inclination (deg, deg/day)
        double w0, wd;   // argument of perihelion (deg, deg/day)
        double a;        // semi-major axis (AU)
        double e0, ed;   // eccentricity
        double M0, Md;   // mean anomaly (deg, deg/day)
    };

    constexpr PlanetElements PLANETS[] =
    {
        { "Mercury", 48.3313,  3.24587e-5,  7.0047,  5.00e-8,  29.1241,  1.01444e-5, 0.387098, 0.205635,  5.59e-10,  168.6562, 4.0923344368 },
        { "Venus",   76.6799,  2.46590e-5,  3.3946,  2.75e-8,  54.8910,  1.38374e-5, 0.723330, 0.006773, -1.302e-9,   48.0052, 1.6021302244 },
        { "Mars",    49.5574,  2.11081e-5,  1.8497, -1.78e-8, 286.5016,  2.92961e-5, 1.523688, 0.093405,  2.516e-9,   18.6021, 0.5240207766 },
        { "Jupiter", 100.4542, 2.76854e-5,  1.3030, -1.557e-7, 273.8777, 1.64505e-5, 5.20256,  0.048498,  4.469e-9,   19.8950, 0.0830853001 },
        { "Saturn",  113.6634, 2.38980e-5,  2.4886, -1.081e-7, 339.3939, 2.97661e-5, 9.55475,  0.055546, -9.499e-9,  316.9670, 0.0334442282 },
        { "Uranus",  74.0005,  1.3978e-5,   0.7733,  1.9e-8,    96.6612,  3.0565e-5, 19.18171, 0.047318, -1.55e-8,   142.5905, 0.011725806  },
        { "Neptune", 131.7806, 3.0173e-5,   1.7700, -2.55e-7,  272.8461, -6.027e-6,  30.05826, 0.008606,  3.313e-8,  260.2471, 0.005995147  },
    };

    constexpr int PLANET_COUNT =
        static_cast<int>(
            sizeof(PLANETS) / sizeof(PLANETS[0])
        );


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


    /*
     * Solve Kepler's equation E - e*sin(E) = M for the
     * eccentric anomaly E, given M and e in degrees / plain
     * eccentricity. Same iterative approach already used
     * inline in calculateMoonPosition().
     */
    double solveKeplerDeg(
        double meanAnomalyDeg,
        double eccentricity
    )
    {
        double E =
            meanAnomalyDeg
            +
            eccentricity *
            RAD_TO_DEG *
            sinDeg(meanAnomalyDeg) *
            (
                1.0 +
                eccentricity *
                cosDeg(meanAnomalyDeg)
            );

        for (
            int iteration = 0;
            iteration < 8;
            ++iteration
        )
        {
            double correction =
                (
                    E
                    -
                    eccentricity *
                    RAD_TO_DEG *
                    sinDeg(E)
                    -
                    meanAnomalyDeg
                )
                /
                (
                    1.0
                    -
                    eccentricity *
                    cosDeg(E)
                );

            E -=
                correction;
        }

        return E;
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


    /*
     * Non-leap-year day-of-year, used only for the meteor
     * shower almanac's date-range checks. A day or two of
     * leap-year slop doesn't matter for "is this shower
     * roughly active".
     */
    int dayOfYearApprox(
        int month,
        int day
    )
    {
        static const int cumulativeDays[] =
        {
            0, 31, 59, 90, 120, 151,
            181, 212, 243, 273, 304, 334
        };

        return
            cumulativeDays[
                month - 1
            ]
            +
            day;
    }


    constexpr double METEOR_MIN_RADIANT_ALTITUDE_DEG =
        10.0;


    constexpr double TRANSIT_SEARCH_WINDOW_HOURS =
        12.0;


    constexpr double TRANSIT_STEP_MINUTES =
        10.0;


    struct MeteorShowerElements
    {
        const char* name;
        const char* parentBody;

        double zhr;

        int startMonth, startDay;
        int endMonth, endDay;
        int peakMonth, peakDay;

        double radiantRaDeg;
        double radiantDecDeg;
    };

    /*
     * The dozen or so reliably-active annual showers. Radiant
     * coordinates are approximate values near each shower's
     * peak (real radiants drift several degrees over the active
     * window) -- fine for "roughly where to look", not for
     * precise meteor-count science.
     */
    constexpr MeteorShowerElements METEOR_SHOWERS[] =
    {
        { "Quadrantids", "asteroid 2003 EH1", 120.0, 12, 28, 1, 12, 1, 4, 230.1, 48.5 },
        { "Lyrids", "Comet C/1861 G1 (Thatcher)", 18.0, 4, 14, 4, 30, 4, 22, 271.4, 33.6 },
        { "Eta Aquariids", "Comet 1P/Halley", 50.0, 4, 19, 5, 28, 5, 5, 338.0, -1.0 },
        { "Southern delta Aquariids", "Comet 96P/Machholz (probable)", 25.0, 7, 12, 8, 23, 7, 30, 339.0, -16.4 },
        { "Perseids", "Comet 109P/Swift-Tuttle", 100.0, 7, 17, 8, 24, 8, 12, 46.2, 57.4 },
        { "Southern Taurids", "Comet 2P/Encke", 5.0, 9, 10, 11, 20, 10, 10, 32.8, 9.1 },
        { "Draconids", "Comet 21P/Giacobini-Zinner", 10.0, 10, 6, 10, 10, 10, 8, 262.0, 54.0 },
        { "Orionids", "Comet 1P/Halley", 20.0, 10, 2, 11, 7, 10, 21, 95.3, 15.6 },
        { "Northern Taurids", "Comet 2P/Encke", 5.0, 10, 20, 12, 10, 11, 12, 58.1, 22.3 },
        { "Leonids", "Comet 55P/Tempel-Tuttle", 15.0, 11, 6, 11, 30, 11, 17, 152.3, 22.2 },
        { "Geminids", "asteroid 3200 Phaethon", 150.0, 12, 4, 12, 17, 12, 13, 112.3, 32.5 },
        { "Ursids", "Comet 8P/Tuttle", 10.0, 12, 17, 12, 26, 12, 22, 217.4, 75.4 },
    };

    constexpr int METEOR_SHOWER_COUNT =
        static_cast<int>(
            sizeof(METEOR_SHOWERS) / sizeof(METEOR_SHOWERS[0])
        );

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


void SkyCatalogue::sunHeliocentric(
    double jd,
    double& distanceAu,
    double& longitudeDeg
)
{
    /*
     * Earth's own osculating elements, expressed the same way
     * as the PLANETS table (N and i are exactly zero by
     * definition of the ecliptic).
     */

    double d =
        jd -
        2451543.5;

    double w =
        normaliseDegrees(
            282.9404 +
            4.70935e-5 * d
        );

    double a =
        1.000000;

    double e =
        0.016709 -
        1.151e-9 * d;

    double M =
        normaliseDegrees(
            356.0470 +
            0.9856002585 * d
        );

    double E =
        solveKeplerDeg(
            M,
            e
        );

    double xv =
        cosDeg(E) -
        e;

    double yv =
        std::sqrt(
            1.0 -
            e * e
        )
        *
        sinDeg(E);

    double r =
        std::sqrt(
            xv * xv +
            yv * yv
        )
        *
        a;

    double v =
        atan2Deg(
            yv,
            xv
        );

    distanceAu =
        r;

    longitudeDeg =
        normaliseDegrees(
            v + w
        );
}


bool SkyCatalogue::calculatePlanetPosition(
    int planetIndex,
    double jd,
    double& raDeg,
    double& decDeg,
    double& distanceAu,
    double& phaseAngleDeg,
    double& sunDistanceAu,
    std::string& name
)
{
    if (
        planetIndex < 0 ||
        planetIndex >= PLANET_COUNT
    )
    {
        return false;
    }

    const PlanetElements& elements =
        PLANETS[planetIndex];

    name =
        elements.name;

    double d =
        jd -
        2451543.5;

    double N =
        normaliseDegrees(
            elements.N0 +
            elements.Nd * d
        );

    double inclination =
        elements.i0 +
        elements.id * d;

    double w =
        normaliseDegrees(
            elements.w0 +
            elements.wd * d
        );

    double a =
        elements.a;

    double e =
        elements.e0 +
        elements.ed * d;

    double M =
        normaliseDegrees(
            elements.M0 +
            elements.Md * d
        );

    double E =
        solveKeplerDeg(
            M,
            e
        );

    // ------------------------------------------------------
    // Heliocentric ecliptic rectangular coordinates
    // ------------------------------------------------------

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
            e * e
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
            cosN * cosVW
            -
            sinN * sinVW * cosDeg(inclination)
        );

    double yh =
        r *
        (
            sinN * cosVW
            +
            cosN * sinVW * cosDeg(inclination)
        );

    double zh =
        r *
        sinVW *
        sinDeg(inclination);

    // ------------------------------------------------------
    // Geocentric: subtract Earth's heliocentric position
    // ------------------------------------------------------

    double sunDistAu =
        0.0;

    double sunLonDeg =
        0.0;

    sunHeliocentric(
        jd,
        sunDistAu,
        sunLonDeg
    );

    double xs =
        sunDistAu *
        cosDeg(sunLonDeg);

    double ys =
        sunDistAu *
        sinDeg(sunLonDeg);

    double xg =
        xh + xs;

    double yg =
        yh + ys;

    double zg =
        zh;

    double delta =
        std::sqrt(
            xg * xg +
            yg * yg +
            zg * zg
        );

    double lonecl =
        atan2Deg(
            yg,
            xg
        );

    double latecl =
        atan2Deg(
            zg,
            std::sqrt(
                xg * xg +
                yg * yg
            )
        );

    // ------------------------------------------------------
    // Ecliptic -> equatorial
    // ------------------------------------------------------

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
            std::sin(lambda) * std::cos(epsilon)
            -
            std::tan(beta) * std::sin(epsilon),
            std::cos(lambda)
        );

    double dec =
        std::asin(
            std::sin(beta) * std::cos(epsilon)
            +
            std::cos(beta) * std::sin(epsilon) * std::sin(lambda)
        );

    raDeg =
        normaliseDegrees(
            ra * RAD_TO_DEG
        );

    decDeg =
        dec * RAD_TO_DEG;

    distanceAu =
        delta;

    sunDistanceAu =
        r;

    // ------------------------------------------------------
    // Phase angle (Sun-planet-Earth angle), for magnitude
    // ------------------------------------------------------

    double cosPhase =
        (
            r * r +
            delta * delta -
            sunDistAu * sunDistAu
        )
        /
        (
            2.0 * r * delta
        );

    phaseAngleDeg =
        std::acos(
            clamp(
                cosPhase,
                -1.0,
                1.0
            )
        )
        *
        RAD_TO_DEG;

    return true;
}


double SkyCatalogue::planetMagnitude(
    const std::string& name,
    double sunDistanceAu,
    double earthDistanceAu,
    double phaseAngleDeg
)
{
    double base =
        5.0 *
        std::log10(
            sunDistanceAu *
            earthDistanceAu
        );

    double fv =
        phaseAngleDeg;

    if (name == "Mercury")
    {
        return -0.36 + base + 0.027 * fv + 2.2e-13 * std::pow(fv, 6.0);
    }

    if (name == "Venus")
    {
        return -4.34 + base + 0.013 * fv + 4.2e-7 * std::pow(fv, 3.0);
    }

    if (name == "Mars")
    {
        return -1.51 + base + 0.016 * fv;
    }

    if (name == "Jupiter")
    {
        return -9.25 + base + 0.014 * fv;
    }

    if (name == "Saturn")
    {
        // Ring contribution ignored -- a fixed offset is used
        // rather than modelling ring-plane opening angle.
        return -8.88 + base + 0.044 * fv;
    }

    if (name == "Uranus")
    {
        return -7.19 + base + 0.0028 * fv;
    }

    if (name == "Neptune")
    {
        return -6.87 + base;
    }

    return 99.0;
}


int SkyCatalogue::planetCount()
{
    return PLANET_COUNT;
}


void SkyCatalogue::findBestAltitudeWindow(
    double raDeg,
    double decDeg,
    double latitudeDeg,
    double longitudeDeg,
    double startJd,
    std::time_t startEpochUtc,
    double& bestAltitudeDeg,
    std::time_t& bestEpochUtc
)
{
    bestAltitudeDeg =
        -90.0;

    bestEpochUtc =
        startEpochUtc;

    int steps =
        static_cast<int>(
            (
                TRANSIT_SEARCH_WINDOW_HOURS *
                60.0
            )
            /
            TRANSIT_STEP_MINUTES
        );

    for (
        int step = 0;
        step <= steps;
        ++step
    )
    {
        double jd =
            startJd
            +
            (
                step *
                TRANSIT_STEP_MINUTES
            )
            /
            1440.0;

        double altitude =
            0.0;

        double azimuth =
            0.0;

        equatorialToHorizontal(
            raDeg,
            decDeg,
            latitudeDeg,
            longitudeDeg,
            jd,
            altitude,
            azimuth
        );

        if (
            altitude >
            bestAltitudeDeg
        )
        {
            bestAltitudeDeg =
                altitude;

            bestEpochUtc =
                startEpochUtc
                +
                static_cast<std::time_t>(
                    std::llround(
                        step *
                        TRANSIT_STEP_MINUTES *
                        60.0
                    )
                );
        }
    }
}


std::vector<MeteorShowerStatus> SkyCatalogue::calculateMeteorShowers(
    double latitudeDeg,
    double longitudeDeg,
    int month,
    int day,
    double jd,
    const MoonState& moon
)
{
    std::vector<MeteorShowerStatus> result;

    int todayDoy =
        dayOfYearApprox(
            month,
            day
        );


    for (
        int index = 0;
        index < METEOR_SHOWER_COUNT;
        ++index
    )
    {
        const MeteorShowerElements& elements =
            METEOR_SHOWERS[index];

        int startDoy =
            dayOfYearApprox(
                elements.startMonth,
                elements.startDay
            );

        int endDoy =
            dayOfYearApprox(
                elements.endMonth,
                elements.endDay
            );

        int peakDoy =
            dayOfYearApprox(
                elements.peakMonth,
                elements.peakDay
            );


        bool active =
            false;

        if (
            startDoy <=
            endDoy
        )
        {
            active =
                todayDoy >= startDoy &&
                todayDoy <= endDoy;
        }
        else
        {
            // Window spans the year boundary (e.g. Quadrantids,
            // Dec 28 -> Jan 12).
            active =
                todayDoy >= startDoy ||
                todayDoy <= endDoy;
        }

        if (
            !active
        )
        {
            continue;
        }


        // Signed day distance to peak, taking the shortest path
        // around the year so a shower whose window wraps
        // Dec/Jan still reports a sensible small number.
        int daysToPeak =
            peakDoy -
            todayDoy;

        if (
            daysToPeak >
            182
        )
        {
            daysToPeak -=
                365;
        }
        else if (
            daysToPeak <
            -182
        )
        {
            daysToPeak +=
                365;
        }


        double altitude =
            0.0;

        double azimuth =
            0.0;

        equatorialToHorizontal(
            elements.radiantRaDeg,
            elements.radiantDecDeg,
            latitudeDeg,
            longitudeDeg,
            jd,
            altitude,
            azimuth
        );


        MeteorShowerStatus status;

        status.name =
            elements.name;

        status.parentBody =
            elements.parentBody;

        status.zhr =
            elements.zhr;

        status.active =
            true;

        status.daysToPeak =
            daysToPeak;

        status.radiantRaDeg =
            elements.radiantRaDeg;

        status.radiantDecDeg =
            elements.radiantDecDeg;

        status.radiantAltitudeDeg =
            altitude;

        status.radiantAzimuthDeg =
            azimuth;

        status.radiantUp =
            altitude >=
            METEOR_MIN_RADIANT_ALTITUDE_DEG;

        status.moonInterferes =
            moon.aboveHorizon &&
            moon.illuminationPercent >=
                40.0;

        result.push_back(
            status
        );
    }


    std::sort(
        result.begin(),
        result.end(),
        [](
            const MeteorShowerStatus& a,
            const MeteorShowerStatus& b
        )
        {
            return
                std::abs(a.daysToPeak) <
                std::abs(b.daysToPeak);
        }
    );


    return result;
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


    /*
     * Note: we deliberately do NOT return early when
     * !result.darkEnough. The DSO query and planet loop below
     * still run so callers (e.g. the web UI) can show a
     * "here's what will be visible once it's dark" preview,
     * plus which planets/the Moon are up right now, instead of
     * just an empty target list during daylight.
     */


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

                /*
                 * object_type 12 = Stellarium's NebDn (dark
                 * nebula). For these records the "v_mag" column
                 * doesn't hold a real magnitude at all -- per
                 * Stellarium's own Nebula::vMag comment, it
                 * stores an opacity class (roughly 1-6) instead.
                 * Left in, small opacity numbers get treated as
                 * implausibly bright magnitudes and crowd out
                 * every genuine bright target. Dark nebulae also
                 * aren't a good fit for a single-point GoTo
                 * recommendation anyway -- they're seen by
                 * silhouette against a star field, not by
                 * pointing at a bright thing.
                 */
                AND o.object_type != 12

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


        findBestAltitudeWindow(
            ra,
            dec,
            latitudeDeg,
            longitudeDeg,
            jd,
            currentEpochUtc,
            target.bestAltitudeDeg,
            target.bestTimeEpochUtc
        );


        result.targets.push_back(
            target
        );
    }


    sqlite3_finalize(
        statement
    );


    // --------------------------------------------------------
    // Planets
    //
    // These don't live in the catalogue database -- they move,
    // so their positions are computed analytically for the
    // requested time, the same way the Sun and Moon already
    // are above.
    // --------------------------------------------------------

    for (
        int planetIndex = 0;
        planetIndex < planetCount();
        ++planetIndex
    )
    {
        double planetRa =
            0.0;

        double planetDec =
            0.0;

        double planetDistanceAu =
            0.0;

        double phaseAngleDeg =
            0.0;

        double sunDistanceAu =
            0.0;

        std::string planetName;


        bool ok =
            calculatePlanetPosition(
                planetIndex,
                jd,
                planetRa,
                planetDec,
                planetDistanceAu,
                phaseAngleDeg,
                sunDistanceAu,
                planetName
            );


        if (
            !ok
        )
        {
            continue;
        }


        double magnitude =
            planetMagnitude(
                planetName,
                sunDistanceAu,
                planetDistanceAu,
                phaseAngleDeg
            );


        if (
            magnitude >
            maximumMagnitude
        )
        {
            continue;
        }


        double altitude =
            0.0;

        double azimuth =
            0.0;


        equatorialToHorizontal(
            planetRa,
            planetDec,
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
                planetRa,
                planetDec,
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
            -(planetIndex + 1);

        target.designation =
            planetName;

        target.name =
            planetName;

        target.raDeg =
            planetRa;

        target.decDeg =
            planetDec;

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

        target.isPlanet =
            true;


        findBestAltitudeWindow(
            planetRa,
            planetDec,
            latitudeDeg,
            longitudeDeg,
            jd,
            currentEpochUtc,
            target.bestAltitudeDeg,
            target.bestTimeEpochUtc
        );


        result.targets.push_back(
            target
        );
    }


    result.meteorShowers =
        calculateMeteorShowers(
            latitudeDeg,
            longitudeDeg,
            month,
            day,
            jd,
            result.moon
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