#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>


struct MoonState
{
    bool valid =
        false;

    double raDeg =
        0.0;

    double decDeg =
        0.0;

    double altitudeDeg =
        0.0;

    double azimuthDeg =
        0.0;

    double illuminationPercent =
        0.0;

    double distanceKm =
        0.0;

    std::string phaseName =
        "Unknown";

    bool aboveHorizon =
        false;

    bool riseValid =
        false;

    bool setValid =
        false;

    std::time_t riseEpochUtc =
        0;

    std::time_t setEpochUtc =
        0;
};


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

    double moonSeparationDeg =
        180.0;

    double score =
        0.0;

    /*
     * True for Sun-orbiting bodies (planets) computed
     * analytically, as opposed to fixed deep-sky objects
     * pulled from the catalogue database.
     */
    bool isPlanet =
        false;

    /*
     * "Best time tonight" preview: the highest altitude this
     * target reaches within the next several hours (see
     * TRANSIT_SEARCH_WINDOW_HOURS in SkyCatalogue.cpp), and the
     * UTC epoch at which that happens. For a target that's
     * already past its peak for the night, bestTimeEpochUtc
     * will be at or near "now".
     */
    double bestAltitudeDeg =
        0.0;

    std::time_t bestTimeEpochUtc =
        0;
};


/*
 * One row of the built-in major-meteor-shower almanac, evaluated
 * for the requested date/time/location.
 */
struct MeteorShowerStatus
{
    std::string name;

    std::string parentBody;

    double zhr =
        0.0;

    // True if today's date falls within the shower's active
    // window (which may span a calendar year boundary).
    bool active =
        false;

    // Positive: days remaining until peak. Negative: days since
    // peak already passed (still within the active window).
    int daysToPeak =
        0;

    double radiantRaDeg =
        0.0;

    double radiantDecDeg =
        0.0;

    double radiantAltitudeDeg =
        0.0;

    double radiantAzimuthDeg =
        0.0;

    // True if the radiant is currently above a reasonable
    // observing altitude (see METEOR_MIN_RADIANT_ALTITUDE_DEG).
    bool radiantUp =
        false;

    // True if the Moon is up and bright enough to meaningfully
    // wash out fainter meteors.
    bool moonInterferes =
        false;
};


struct SkyState
{
    bool valid =
        false;

    bool darkEnough =
        false;

    double sunAltitudeDeg =
        0.0;

    MoonState moon;

    std::vector<SkyTarget>
        targets;

    std::vector<MeteorShowerStatus>
        meteorShowers;
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


    static std::time_t utcEpoch(
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
        double& decDeg,
        double* eclipticLongitudeDeg = nullptr
    );


    static void calculateMoonPosition(
        double jd,
        double& raDeg,
        double& decDeg,
        double& eclipticLongitudeDeg,
        double& eclipticLatitudeDeg,
        double& distanceKm
    );


    /*
     * Heliocentric distance (AU) and geocentric ecliptic
     * longitude (deg) of the Sun, solved via Kepler's equation.
     *
     * This is a separate, slightly higher-precision helper than
     * calculateSunPosition(): planet positions need the Sun's
     * true distance and longitude to convert heliocentric
     * planet coordinates into geocentric ones.
     */
    static void sunHeliocentric(
        double jd,
        double& distanceAu,
        double& longitudeDeg
    );


    /*
     * Low-precision geocentric planet position, following the
     * same Keplerian-element approach as calculateMoonPosition().
     * planetIndex selects into the internal PLANETS table
     * (0 = Mercury ... 5 = Neptune, see SkyCatalogue.cpp).
     */
    static bool calculatePlanetPosition(
        int planetIndex,
        double jd,
        double& raDeg,
        double& decDeg,
        double& distanceAu,
        double& phaseAngleDeg,
        double& sunDistanceAu,
        std::string& name
    );


    static double planetMagnitude(
        const std::string& name,
        double sunDistanceAu,
        double earthDistanceAu,
        double phaseAngleDeg
    );


    static int planetCount();


    /*
     * "Best time tonight": searches forward from startJd across
     * TRANSIT_SEARCH_WINDOW_HOURS, sampling altitude every few
     * minutes, and returns the highest altitude reached and the
     * UTC epoch at which it occurs. The target's RA/Dec are
     * treated as fixed over the search window -- fine for DSOs,
     * and close enough for planets over a single night.
     */
    static void findBestAltitudeWindow(
        double raDeg,
        double decDeg,
        double latitudeDeg,
        double longitudeDeg,
        double startJd,
        std::time_t startEpochUtc,
        double& bestAltitudeDeg,
        std::time_t& bestEpochUtc
    );


    /*
     * Evaluates the built-in major-meteor-shower almanac against
     * the given date/location/Moon state.
     */
    static std::vector<MeteorShowerStatus> calculateMeteorShowers(
        double latitudeDeg,
        double longitudeDeg,
        int month,
        int day,
        double jd,
        const MoonState& moon
    );


    static MoonState calculateMoonState(
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
    );


    static double angularSeparationDegrees(
        double ra1Deg,
        double dec1Deg,
        double ra2Deg,
        double dec2Deg
    );


    static std::time_t findMoonCrossing(
        double latitudeDeg,
        double longitudeDeg,
        std::time_t startEpochUtc,
        std::time_t endEpochUtc,
        bool rising
    );


    static double moonAltitudeAtEpoch(
        double latitudeDeg,
        double longitudeDeg,
        std::time_t epochUtc
    );


    static std::string moonPhaseName(
        double phaseAngleDeg
    );


    static double normaliseDegrees(
        double degrees
    );


    static double normaliseSignedDegrees(
        double degrees
    );
};
