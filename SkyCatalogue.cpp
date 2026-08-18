#include "SkyCatalogue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sqlite3.h>


namespace
{

constexpr double PI =
    3.1415926535897932384626433832795;

constexpr double DEG_TO_RAD =
    PI / 180.0;

constexpr double RAD_TO_DEG =
    180.0 / PI;


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

}


// ============================================================
// Constructor
// ============================================================

SkyCatalogue::SkyCatalogue()
    : _database(nullptr)
{
}


// ============================================================
// Destructor
// ============================================================

SkyCatalogue::~SkyCatalogue()
{
    close();
}


// ============================================================
// Open
// ============================================================

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


// ============================================================
// Close
// ============================================================

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


// ============================================================
// Is open
// ============================================================

bool SkyCatalogue::isOpen() const
{
    return
        _database != nullptr;
}


// ============================================================
// Julian Date
// ============================================================

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
        (A / 4);

    double dayFraction =
        (
            static_cast<double>(hour)
            +
            static_cast<double>(minute) / 60.0
            +
            static_cast<double>(second) / 3600.0
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
        static_cast<double>(day)
        +
        static_cast<double>(B)
        -
        1524.5
        +
        dayFraction;
}


// ============================================================
// GMST
// ============================================================

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

    return normaliseDegrees(
        gmst
    );
}


// ============================================================
// Normalise 0-360
// ============================================================

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


// ============================================================
// Normalise -180 to +180
// ============================================================

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


// ============================================================
// RA/Dec → Alt/Az
//
// Azimuth measured from North, increasing eastwards.
// ============================================================

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

    /*
     * atan2 expression gives the astronomical
     * azimuth convention, so shift to:
     *
     * 0°   = North
     * 90°  = East
     * 180° = South
     * 270° = West
     */

    double azimuthResult =
        azimuth *
        RAD_TO_DEG
        +
        180.0;

    altitudeDeg =
        altitude *
        RAD_TO_DEG;

    azimuthDeg =
        normaliseDegrees(
            azimuthResult
        );
}


// ============================================================
// Approximate Sun position
//
// Good enough for deciding whether a DSO recommendation
// should be shown. This is not intended as a precision
// solar ephemeris.
// ============================================================

void SkyCatalogue::calculateSunPosition(
    double jd,
    double& raDeg,
    double& decDeg
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
            2.0 * g
        );

    eclipticLongitude =
        normaliseDegrees(
            eclipticLongitude
        );

    double epsilon =
        (
            23.4393 -
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
}


// ============================================================
// Calculate visible targets
// ============================================================

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


    // --------------------------------------------------------
    // Determine Sun altitude
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


    /*
     * Don't recommend DSOs during daylight/civil twilight.
     */

    if (
        !result.darkEnough
    )
    {
        result.valid =
            true;

        return result;
    }


    // --------------------------------------------------------
    // Candidate query
    // --------------------------------------------------------
    //
    // We deliberately constrain this first pass:
    //
    //   V magnitude <= 10
    //
    // and require a Messier/NGC/IC-style identifier or a
    // proper name.
    //
    // This avoids doing calculations on all 1.1 million
    // objects for every dashboard refresh.
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
        prepareResult != SQLITE_OK
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


        // ----------------------------------------------------
        // Horizon filtering
        // ----------------------------------------------------

        if (
            altitude <
            minimumAltitudeDeg
        )
        {
            continue;
        }


        // ----------------------------------------------------
        // Score
        //
        // Brightness matters heavily, but altitude matters too.
        // Objects near the zenith receive a useful bonus.
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


        double score =
            (
                magnitudeScore *
                0.55
            )
            +
            (
                altitudeScore *
                0.30
            )
            +
            (
                zenithBonus *
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

        target.score =
            score;


        result.targets.push_back(
            target
        );
    }


    sqlite3_finalize(
        statement
    );


    // --------------------------------------------------------
    // Sort best targets first
    // --------------------------------------------------------

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


    result.valid =
        true;


    return result;
}