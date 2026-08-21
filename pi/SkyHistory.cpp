#include "SkyHistory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>


// ============================================================
// Constructor
// ============================================================

SkyHistory::SkyHistory()
{
}


// ============================================================
// Configuration
// ============================================================

void SkyHistory::setDirectory(
    const std::string& directory
)
{
    _directory =
        directory;
}


// ============================================================
// Bin index
// ============================================================

int SkyHistory::binIndex(
    double azDeg,
    double altDeg
)
{
    double az =
        std::fmod(
            azDeg,
            360.0
        );

    if (
        az < 0.0
    )
    {
        az +=
            360.0;
    }

    int azBin =
        static_cast<int>(
            az / (360.0 / AZ_BINS)
        );

    azBin =
        std::clamp(
            azBin,
            0,
            AZ_BINS - 1
        );

    int altBin =
        static_cast<int>(
            altDeg / (90.0 / ALT_BINS)
        );

    altBin =
        std::clamp(
            altBin,
            0,
            ALT_BINS - 1
        );

    return
        altBin * AZ_BINS +
        azBin;
}


// ============================================================
// Parse "YYYYMMDD_HHMMSS_blackbox.csv" -> epoch
// ============================================================

bool SkyHistory::parseSessionEpochFromFilename(
    const std::string& filename,
    std::time_t& epochOut
)
{
    std::tm parsed{};

    // Recorder writes "<YYYYMMDD_HHMMSS>_blackbox.csv".
    if (
        std::sscanf(
            filename.c_str(),
            "%4d%2d%2d_%2d%2d%2d_blackbox.csv",
            &parsed.tm_year,
            &parsed.tm_mon,
            &parsed.tm_mday,
            &parsed.tm_hour,
            &parsed.tm_min,
            &parsed.tm_sec
        ) != 6
    )
    {
        return false;
    }

    parsed.tm_year -=
        1900;

    parsed.tm_mon -=
        1;

    parsed.tm_isdst =
        -1;

    // Recorder builds this timestamp from localtime_r, so
    // interpret it back as local time (mktime), not UTC.
    epochOut =
        std::mktime(
            &parsed
        );

    return
        epochOut !=
        static_cast<std::time_t>(-1);
}


// ============================================================
// Minimal CSV line splitter (matches Recorder::csvEscape:
// every field is double-quoted, internal quotes doubled).
// ============================================================

bool SkyHistory::parseCsvLine(
    const std::string& line,
    std::vector<std::string>& fieldsOut
)
{
    fieldsOut.clear();

    std::string field;

    bool inQuotes =
        false;

    for (
        std::size_t i = 0;
        i < line.size();
        ++i
    )
    {
        char c =
            line[i];

        if (
            inQuotes
        )
        {
            if (
                c == '"'
            )
            {
                if (
                    i + 1 < line.size() &&
                    line[i + 1] == '"'
                )
                {
                    field +=
                        '"';

                    ++i;
                }
                else
                {
                    inQuotes =
                        false;
                }
            }
            else
            {
                field +=
                    c;
            }
        }
        else
        {
            if (
                c == '"'
            )
            {
                inQuotes =
                    true;
            }
            else if (
                c == ','
            )
            {
                fieldsOut.push_back(
                    field
                );

                field.clear();
            }
            else
            {
                field +=
                    c;
            }
        }
    }

    fieldsOut.push_back(
        field
    );

    return
        !fieldsOut.empty();
}


// ============================================================
// Parse a "telescope" blackbox row
// ============================================================

bool SkyHistory::parseTelescopeLine(
    const std::string& timestampField,
    const std::string& dataField,
    double& azOut,
    double& altOut,
    std::time_t& sampleEpochOut
)
{
    bool haveAz =
        false;

    bool haveAlt =
        false;

    std::size_t start =
        0;

    while (
        start <= dataField.size()
    )
    {
        std::size_t end =
            dataField.find(
                ';',
                start
            );

        if (
            end == std::string::npos
        )
        {
            end =
                dataField.size();
        }

        std::string pair =
            dataField.substr(
                start,
                end - start
            );

        std::size_t eq =
            pair.find(
                '='
            );

        if (
            eq != std::string::npos
        )
        {
            std::string key =
                pair.substr(
                    0,
                    eq
                );

            std::string value =
                pair.substr(
                    eq + 1
                );

            try
            {
                if (
                    key == "az"
                )
                {
                    azOut =
                        std::stod(
                            value
                        );

                    haveAz =
                        true;
                }
                else if (
                    key == "alt"
                )
                {
                    altOut =
                        std::stod(
                            value
                        );

                    haveAlt =
                        true;
                }
            }
            catch (
                ...
            )
            {
                // malformed number -- skip this row
            }
        }

        start =
            end + 1;
    }

    if (
        !haveAz ||
        !haveAlt
    )
    {
        return false;
    }

    std::tm parsed{};

    if (
        std::sscanf(
            timestampField.c_str(),
            "%4d-%2d-%2d %2d:%2d:%2d",
            &parsed.tm_year,
            &parsed.tm_mon,
            &parsed.tm_mday,
            &parsed.tm_hour,
            &parsed.tm_min,
            &parsed.tm_sec
        ) == 6
    )
    {
        parsed.tm_year -=
            1900;

        parsed.tm_mon -=
            1;

        parsed.tm_isdst =
            -1;

        std::time_t epoch =
            std::mktime(
                &parsed
            );

        if (
            epoch !=
            static_cast<std::time_t>(-1)
        )
        {
            sampleEpochOut =
                epoch;
        }
    }

    return true;
}


// ============================================================
// Scan the unread tail of one file
// ============================================================

void SkyHistory::scanFile(
    FileState& file
)
{
    std::string path =
        _directory +
        "/" +
        file.filename;

    std::error_code sizeError;

    uintmax_t currentSize =
        std::filesystem::file_size(
            path,
            sizeError
        );

    if (
        sizeError ||
        currentSize <= file.byteOffset
    )
    {
        return;
    }

    std::ifstream input(
        path,
        std::ios::binary
    );

    if (
        !input
    )
    {
        return;
    }

    input.seekg(
        static_cast<std::streamoff>(
            file.byteOffset
        )
    );

    std::ostringstream buffer;

    buffer
        << input.rdbuf();

    std::string chunk =
        buffer.str();

    std::size_t lastNewline =
        chunk.find_last_of(
            '\n'
        );

    if (
        lastNewline == std::string::npos
    )
    {
        // No complete line arrived yet -- wait for more data.
        return;
    }

    std::string complete =
        chunk.substr(
            0,
            lastNewline + 1
        );

    file.byteOffset +=
        complete.size();

    std::istringstream lines(
        complete
    );

    std::string line;

    while (
        std::getline(
            lines,
            line
        )
    )
    {
        if (
            !line.empty() &&
            line.back() == '\r'
        )
        {
            line.pop_back();
        }

        if (
            line.empty() ||
            line.rfind(
                "timestamp,",
                0
            ) == 0
        )
        {
            // blank line or the header row
            continue;
        }

        std::vector<std::string> fields;

        if (
            !parseCsvLine(
                line,
                fields
            ) ||
            fields.size() < 4
        )
        {
            continue;
        }

        if (
            fields[2] !=
            "telescope"
        )
        {
            continue;
        }

        double az =
            0.0;

        double alt =
            0.0;

        std::time_t sampleEpoch =
            file.lastSampleEpoch;

        if (
            !parseTelescopeLine(
                fields[0],
                fields[3],
                az,
                alt,
                sampleEpoch
            )
        )
        {
            continue;
        }

        file.grid[
            binIndex(
                az,
                alt
            )
        ] +=
            1;

        file.sampleCount +=
            1;

        if (
            file.firstSampleEpoch == 0
        )
        {
            file.firstSampleEpoch =
                sampleEpoch;
        }

        file.lastSampleEpoch =
            sampleEpoch;

        _recentTrail.push_back(
            {
                az,
                alt
            }
        );

        while (
            _recentTrail.size() >
            static_cast<std::size_t>(
                TRAIL_MAX_POINTS
            )
        )
        {
            _recentTrail.pop_front();
        }
    }
}


// ============================================================
// Refresh: discover new files, incrementally scan tracked ones
// ============================================================

void SkyHistory::refresh()
{
    if (
        _directory.empty()
    )
    {
        return;
    }

    std::error_code dirError;

    if (
        !std::filesystem::exists(
            _directory,
            dirError
        ) ||
        dirError
    )
    {
        return;
    }

    for (
        const auto& entry :
        std::filesystem::directory_iterator(
            _directory
        )
    )
    {
        if (
            !entry.is_regular_file()
        )
        {
            continue;
        }

        std::string name =
            entry.path().filename().string();

        if (
            name.size() < 14 ||
            name.find(
                "_blackbox.csv"
            ) == std::string::npos
        )
        {
            continue;
        }

        if (
            _files.find(
                name
            ) != _files.end()
        )
        {
            continue;
        }

        FileState state;

        state.filename =
            name;

        state.grid.assign(
            AZ_BINS * ALT_BINS,
            0
        );

        parseSessionEpochFromFilename(
            name,
            state.sessionStartEpoch
        );

        _files.emplace(
            name,
            std::move(
                state
            )
        );
    }

    // Scan in filename order (== chronological order, given
    // the "YYYYMMDD_HHMMSS_..." naming) so _recentTrail always
    // gets appended to in the right sequence. Iterating the
    // unordered_map directly would scan files in hash order,
    // which has no relationship to time.
    std::vector<std::string> orderedNames;

    orderedNames.reserve(
        _files.size()
    );

    for (
        const auto& entry :
        _files
    )
    {
        orderedNames.push_back(
            entry.first
        );
    }

    std::sort(
        orderedNames.begin(),
        orderedNames.end()
    );

    for (
        const std::string& name :
        orderedNames
    )
    {
        scanFile(
            _files.at(
                name
            )
        );
    }
}


// ============================================================
// Query
// ============================================================

SkyHistoryResult SkyHistory::query(
    int rangeDays
)
{
    refresh();

    SkyHistoryResult result;

    std::time_t cutoff =
        0;

    if (
        rangeDays > 0
    )
    {
        cutoff =
            std::time(
                nullptr
            )
            -
            static_cast<std::time_t>(
                rangeDays
            ) *
            24 * 60 * 60;
    }

    std::vector<int> combined(
        AZ_BINS * ALT_BINS,
        0
    );

    double totalHours =
        0.0;

    int totalSessions =
        0;

    for (
        const auto& entry :
        _files
    )
    {
        const FileState& file =
            entry.second;

        if (
            file.sampleCount == 0
        )
        {
            continue;
        }

        if (
            cutoff > 0 &&
            file.sessionStartEpoch < cutoff
        )
        {
            continue;
        }

        totalSessions +=
            1;

        if (
            file.lastSampleEpoch >
            file.firstSampleEpoch
        )
        {
            totalHours +=
                static_cast<double>(
                    file.lastSampleEpoch -
                    file.firstSampleEpoch
                )
                /
                3600.0;
        }

        for (
            int i = 0;
            i < AZ_BINS * ALT_BINS;
            ++i
        )
        {
            combined[i] +=
                file.grid[i];
        }
    }

    int hottestIndex =
        -1;

    int hottestCount =
        0;

    for (
        int altBin = 0;
        altBin < ALT_BINS;
        ++altBin
    )
    {
        for (
            int azBin = 0;
            azBin < AZ_BINS;
            ++azBin
        )
        {
            int index =
                altBin * AZ_BINS +
                azBin;

            int count =
                combined[index];

            if (
                count <= 0
            )
            {
                continue;
            }

            if (
                count >
                hottestCount
            )
            {
                hottestCount =
                    count;

                hottestIndex =
                    index;
            }

            SkyHistoryCell cell;

            cell.azDeg =
                (
                    azBin +
                    0.5
                )
                *
                (
                    360.0 / AZ_BINS
                );

            cell.altDeg =
                (
                    altBin +
                    0.5
                )
                *
                (
                    90.0 / ALT_BINS
                );

            cell.count =
                count;

            result.cells.push_back(
                cell
            );
        }
    }

    result.stats.totalSessions =
        totalSessions;

    result.stats.totalHours =
        std::round(
            totalHours * 10.0
        )
        /
        10.0;

    if (
        hottestIndex >= 0
    )
    {
        int altBin =
            hottestIndex / AZ_BINS;

        int azBin =
            hottestIndex % AZ_BINS;

        result.stats.hasHottest =
            true;

        result.stats.hottestAzDeg =
            (
                azBin +
                0.5
            )
            *
            (
                360.0 / AZ_BINS
            );

        result.stats.hottestAltDeg =
            (
                altBin +
                0.5
            )
            *
            (
                90.0 / ALT_BINS
            );

        result.stats.hottestCount =
            hottestCount;
    }

    for (
        const SkyHistoryTrailPoint& point :
        _recentTrail
    )
    {
        result.trail.push_back(
            point
        );
    }

    return result;
}
