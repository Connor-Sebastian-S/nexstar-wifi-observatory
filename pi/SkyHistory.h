#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>


/*
 * SkyHistory aggregates Recorder's blackbox CSV logs (one file
 * per session, named "<session>_blackbox.csv") into an alt/az
 * grid of "how often has the mount pointed here" counts, for
 * the sky-history heatmap.
 *
 * Design goal: cheap to query on every page load, even after
 * months of logging. Each tracked file remembers how many bytes
 * of it have already been parsed, so a query only re-reads
 * bytes appended since the last call -- it never re-scans
 * history that's already been counted. The one-time cost is the
 * very first query after startup, which has to read everything
 * that exists so far.
 */


struct SkyHistoryCell
{
    double azDeg =
        0.0;

    double altDeg =
        0.0;

    int count =
        0;
};


struct SkyHistoryTrailPoint
{
    double azDeg =
        0.0;

    double altDeg =
        0.0;
};


struct SkyHistoryStats
{
    int totalSessions =
        0;

    double totalHours =
        0.0;

    bool hasHottest =
        false;

    double hottestAzDeg =
        0.0;

    double hottestAltDeg =
        0.0;

    int hottestCount =
        0;
};


struct SkyHistoryResult
{
    std::vector<SkyHistoryCell>
        cells;

    std::vector<SkyHistoryTrailPoint>
        trail;

    SkyHistoryStats stats;
};


class SkyHistory
{
public:

    SkyHistory();


    // Directory containing "*_blackbox.csv" files (same as
    // Recorder's RECORDER_DIRECTORY).
    void setDirectory(
        const std::string& directory
    );


    // Refreshes the cache (only reading newly-appended bytes)
    // and returns the aggregate for the requested window.
    // rangeDays <= 0 means "all time"; otherwise only sessions
    // that started within the last rangeDays days are included.
    SkyHistoryResult query(
        int rangeDays
    );


private:

    struct FileState
    {
        std::string filename;

        std::time_t sessionStartEpoch =
            0;

        std::time_t firstSampleEpoch =
            0;

        std::time_t lastSampleEpoch =
            0;

        uintmax_t byteOffset =
            0;

        long sampleCount =
            0;

        std::vector<int>
            grid;
    };


    static constexpr int AZ_BINS =
        72;

    // 5-degree bins

    static constexpr int ALT_BINS =
        18;

    static constexpr int TRAIL_MAX_POINTS =
        120;


    std::string _directory;

    std::unordered_map<std::string, FileState>
        _files;

    std::deque<SkyHistoryTrailPoint>
        _recentTrail;


    void refresh();


    void scanFile(
        FileState& file
    );


    static bool parseSessionEpochFromFilename(
        const std::string& filename,
        std::time_t& epochOut
    );


    static bool parseCsvLine(
        const std::string& line,
        std::vector<std::string>& fieldsOut
    );


    static bool parseTelescopeLine(
        const std::string& timestampField,
        const std::string& dataField,
        double& azOut,
        double& altOut,
        std::time_t& sampleEpochOut
    );


    static int binIndex(
        double azDeg,
        double altDeg
    );
};
