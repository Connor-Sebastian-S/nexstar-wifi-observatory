#include "WebServer.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>


// ============================================================
// Constructor
// ============================================================

WebServer::WebServer()
    : _serverSocket(-1),
      _port(0),
      _state(),
      _abortGotoHandler(),
      _environmentHistory()
{
}


// ============================================================
// Destructor
// ============================================================

WebServer::~WebServer()
{
    stop();
}


// ============================================================
// Begin
// ============================================================

bool WebServer::begin(
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
            4
        ) < 0
    )
    {
        stop();

        return false;
    }


    return true;
}


// ============================================================
// Configure non-blocking socket
// ============================================================

bool WebServer::configureSocket()
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
// Update
// ============================================================

void WebServer::update(
    const ObservatoryWebState& state
)
{
    _state =
        state;
}


// ============================================================
// Add environment sample
// ============================================================

void WebServer::addEnvironmentSample(
    double temperatureC,
    double humidityPercent,
    double dewPointC,
    uint64_t timestampMs
)
{
    EnvironmentHistorySample sample;


    sample.timestampMs =
        timestampMs;

    sample.temperatureC =
        temperatureC;

    sample.humidityPercent =
        humidityPercent;

    sample.dewPointC =
        dewPointC;


    _environmentHistory.push_back(
        sample
    );


    trimEnvironmentHistory(
        timestampMs
    );
}


// ============================================================
// Trim environment history
// ============================================================

void WebServer::trimEnvironmentHistory(
    uint64_t nowMs
)
{
    const uint64_t cutoff =
        nowMs >
        ENVIRONMENT_HISTORY_DURATION_MS
            ? nowMs -
              ENVIRONMENT_HISTORY_DURATION_MS
            : 0;


    while (
        !_environmentHistory.empty() &&
        _environmentHistory.front().timestampMs
        <
        cutoff
    )
    {
        _environmentHistory.erase(
            _environmentHistory.begin()
        );
    }
}


// ============================================================
// Abort callback
// ============================================================

void WebServer::setAbortGotoHandler(
    std::function<bool()> handler
)
{
    _abortGotoHandler =
        std::move(
            handler
        );
}


// ============================================================
// Handle requests
// ============================================================

void WebServer::handleRequests()
{
    if (
        _serverSocket < 0
    )
    {
        return;
    }


    sockaddr_in clientAddress{};


    socklen_t clientLength =
        sizeof(clientAddress);


    int clientSocket =
        accept(
            _serverSocket,
            reinterpret_cast<
                sockaddr*
            >(
                &clientAddress
            ),
            &clientLength
        );


    if (
        clientSocket < 0
    )
    {
        if (
            errno == EAGAIN ||
            errno == EWOULDBLOCK
        )
        {
            return;
        }


        return;
    }


    handleClient(
        clientSocket
    );
}


// ============================================================
// Handle client
// ============================================================

void WebServer::handleClient(
    int clientSocket
)
{
    char request[4096];


    ssize_t received =
        recv(
            clientSocket,
            request,
            sizeof(request) - 1,
            0
        );


    if (
        received <= 0
    )
    {
        close(
            clientSocket
        );


        return;
    }


    request[
        received
    ] =
        '\0';


    // ========================================================
    // Abort endpoint
    // ========================================================

    if (
        std::strncmp(
            request,
            "POST /api/abort",
            15
        ) == 0
    )
    {
        bool success =
            false;


        if (
            _abortGotoHandler
        )
        {
            success =
                _abortGotoHandler();
        }


        std::string body =
            success
                ? "{\"ok\":true}"
                : "{\"ok\":false}";


        std::ostringstream output;


        output
            << "HTTP/1.1 "
            << (
                success
                    ? "200 OK"
                    : "409 Conflict"
            )
            << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: "
            << body.size()
            << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;


        std::string response =
            output.str();


        send(
            clientSocket,
            response.c_str(),
            response.size(),
            0
        );


        close(
            clientSocket
        );


        return;
    }


    // ========================================================
    // Normal dashboard
    // ========================================================

    std::string body =
        buildHtml();


    std::ostringstream output;


    output
        << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: text/html; charset=utf-8\r\n"
        << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        << "Pragma: no-cache\r\n"
        << "Content-Length: "
        << body.size()
        << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;


    std::string response =
        output.str();


    send(
        clientSocket,
        response.c_str(),
        response.size(),
        0
    );


    close(
        clientSocket
    );
}


// ============================================================
// Connection name
// ============================================================

std::string WebServer::telescopeConnectionName() const
{
    if (
        _state.telescope.connected
    )
    {
        return "CONNECTED";
    }


    if (
        _state.telescope.waiting
    )
    {
        return "WAITING";
    }


    return "DISCONNECTED";
}


// ============================================================
// Tracking name
// ============================================================

std::string WebServer::trackingModeName() const
{
    switch (
        _state.telescope.trackingMode
    )
    {
        case 0:
            return "OFF";

        case 1:
            return "ALT-AZ";

        case 2:
            return "EQ NORTH";

        case 3:
            return "EQ SOUTH";

        default:
            return "UNKNOWN";
    }
}


// ============================================================
// HTML escape
// ============================================================

std::string WebServer::htmlEscape(
    const std::string& value
)
{
    std::string result;


    for (
        char c :
        value
    )
    {
        switch (
            c
        )
        {
            case '&':
                result +=
                    "&amp;";
                break;

            case '<':
                result +=
                    "&lt;";
                break;

            case '>':
                result +=
                    "&gt;";
                break;

            case '"':
                result +=
                    "&quot;";
                break;

            case '\'':
                result +=
                    "&#39;";
                break;

            default:
                result +=
                    c;
                break;
        }
    }


    return result;
}


// ============================================================
// Duration formatter
// ============================================================

std::string WebServer::formatDuration(
    uint64_t milliseconds
)
{
    uint64_t seconds =
        milliseconds /
        1000;


    uint64_t minutes =
        seconds /
        60;


    seconds %=
        60;


    std::ostringstream output;


    if (
        minutes > 0
    )
    {
        output
            << minutes
            << "m ";
    }


    output
        << seconds
        << "s";


    return output.str();
}


// ============================================================
// Build environment chart JavaScript
// ============================================================

static void appendEnvironmentData(
    std::ostringstream& html,
    const std::vector<
        EnvironmentHistorySample
    >& history
)
{
    html
        << "<script>"
        << "const environmentHistory=[";
        

    for (
        size_t i = 0;
        i < history.size();
        ++i
    )
    {
        const auto& sample =
            history[i];


        if (
            i > 0
        )
        {
            html
                << ",";
        }


        html
            << "{"
            << "t:"
            << sample.timestampMs
            << ",temp:"
            << sample.temperatureC
            << ",humidity:"
            << sample.humidityPercent
            << ",dew:"
            << sample.dewPointC
            << "}";
    }


    html
        << "];"
        << "</script>";
}


// ============================================================
// Build HTML
// ============================================================

std::string WebServer::buildHtml() const
{
    std::ostringstream html;


    html
        << "<!DOCTYPE html>"
        << "<html>"
        << "<head>"
        << "<meta charset=\"UTF-8\">"
        << "<meta name=\"viewport\" "
           "content=\"width=device-width\">"
        << "<meta http-equiv=\"refresh\" content=\"5\">"
        << "<title>TelescopeHub</title>"


        << "<style>"


        << "body{"
        << "margin:20px;"
        << "font-family:Arial,Helvetica,sans-serif;"
        << "background:#111;"
        << "color:#eee;"
        << "}"


        << "h1{"
        << "font-size:28px;"
        << "border-bottom:1px solid #666;"
        << "padding-bottom:10px;"
        << "}"


        << "h2{"
        << "margin-top:0;"
        << "}"


        << ".card{"
        << "border:1px solid #555;"
        << "padding:15px;"
        << "margin-bottom:15px;"
        << "}"


        << ".row{"
        << "margin:6px 0;"
        << "}"


        << ".label{"
        << "display:inline-block;"
        << "width:190px;"
        << "color:#aaa;"
        << "}"


        << ".value{"
        << "font-weight:bold;"
        << "}"


        << "button{"
        << "padding:12px 18px;"
        << "font-size:16px;"
        << "font-weight:bold;"
        << "cursor:pointer;"
        << "}"


        << ".abort{"
        << "background:#900;"
        << "color:white;"
        << "border:0;"
        << "}"


        << "table{"
        << "width:100%;"
        << "border-collapse:collapse;"
        << "}"


        << "th,td{"
        << "padding:6px;"
        << "border-bottom:1px solid #444;"
        << "text-align:left;"
        << "}"


        << ".chart-wrap{"
        << "width:100%;"
        << "overflow-x:auto;"
        << "}"


        << "canvas{"
        << "display:block;"
        << "width:100%;"
        << "max-width:100%;"
        << "height:240px;"
        << "background:#0b0b0b;"
        << "border:1px solid #333;"
        << "}"


        << ".chart-note{"
        << "color:#aaa;"
        << "font-size:13px;"
        << "margin-top:6px;"
        << "}"


        << "</style>"


        << "</head>"
        << "<body>";


    // ========================================================
    // Header
    // ========================================================

    html
        << "<h1>TELESCOPEHUB</h1>";


    // ========================================================
    // NexStar
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>NexStar</h2>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Connection</span>"
        << "<span class=\"value\">"
        << telescopeConnectionName()
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Model</span>"
        << "<span class=\"value\">"
        << htmlEscape(
            _state.telescope.model
        )
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Firmware</span>"
        << "<span class=\"value\">"
        << static_cast<unsigned>(
            _state.telescope.firmwareMajor
        )
        << "."
        << static_cast<unsigned>(
            _state.telescope.firmwareMinor
        )
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Alignment</span>"
        << "<span class=\"value\">"
        << (
            _state.telescope.aligned
                ? "YES"
                : "NO"
        )
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Tracking</span>"
        << "<span class=\"value\">"
        << trackingModeName()
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">GOTO</span>"
        << "<span class=\"value\">"
        << (
            _state.telescope.gotoInProgress
                ? "IN PROGRESS"
                : "IDLE"
        )
        << "</span>"
        << "</div>";


    if (
        _state.telescope.positionValid
    )
    {
        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Azimuth</span>"
            << "<span class=\"value\">"
            << std::fixed
            << std::setprecision(3)
            << _state.telescope.azimuth
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Altitude</span>"
            << "<span class=\"value\">"
            << _state.telescope.altitude
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Altitude safety</span>"
            << "<span class=\"value\">"
            << _state.telescope.altitudeStatus
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">RA</span>"
            << "<span class=\"value\">"
            << _state.telescope.ra
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Dec</span>"
            << "<span class=\"value\">"
            << _state.telescope.dec
            << "&deg;"
            << "</span>"
            << "</div>";
    }


    html
        << "</div>";


    // ========================================================
    // Slew
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>Slew</h2>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Status</span>"
        << "<span class=\"value\">"
        << _state.slew.status
        << "</span>"
        << "</div>";


    if (
        _state.slew.active
    )
    {
        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Target RA</span>"
            << "<span class=\"value\">"
            << _state.slew.targetRa
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Target Dec</span>"
            << "<span class=\"value\">"
            << _state.slew.targetDec
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Remaining</span>"
            << "<span class=\"value\">"
            << _state.slew.remainingDegrees
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Elapsed</span>"
            << "<span class=\"value\">"
            << formatDuration(
                _state.slew.elapsedMs
            )
            << "</span>"
            << "</div>";


        html
            << "<p>"
            << "<button "
               "class=\"abort\" "
               "onclick=\"abortGoto()\">"
               "ABORT SLEW"
               "</button>"
            << "</p>";
    }


    html
        << "<script>"
        << "function abortGoto(){"
        << "fetch('/api/abort',{method:'POST'})"
        << ".then(r=>r.json())"
        << ".then(d=>{"
        << "if(d.ok){location.reload();}"
        << "else{alert('Could not abort GOTO.');}"
        << "})"
        << ".catch(()=>alert('Could not contact TelescopeHub.'));"
        << "}"
        << "</script>";


    html
        << "</div>";


    // ========================================================
    // GOTO history
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>GOTO History</h2>";


    if (
        _state.gotoHistory.empty()
    )
    {
        html
            << "<div class=\"row\">"
            << "No GOTOs recorded."
            << "</div>";
    }
    else
    {
        html
            << "<table>"
            << "<tr>"
            << "<th>Time</th>"
            << "<th>Target</th>"
            << "<th>Pointing error</th>"
            << "<th>Duration</th>"
            << "<th>Result</th>"
            << "</tr>";


        for (
            const auto& entry :
            _state.gotoHistory
        )
        {
            std::string result =
                entry.result;


            if (
                !entry.wasAligned &&
                result ==
                "COMPLETE"
            )
            {
                result =
                    "COMPLETE — UNALIGNED";
            }
            else if (
                !entry.pointingMeasured &&
                result ==
                "ABORTED"
            )
            {
                result =
                    "ABORTED — NOT MEASURED";
            }


            html
                << "<tr>"
                << "<td>"
                << htmlEscape(
                    entry.timestamp
                )
                << "</td>"

                << "<td>"
                << std::fixed
                << std::setprecision(3)
                << entry.targetRa
                << "&deg;, "
                << entry.targetDec
                << "&deg;"
                << "</td>"

                << "<td>";


            if (
                entry.pointingMeasured
            )
            {
                html
                    << entry.errorArcmin
                    << "'";
            }
            else
            {
                html
                    << "—";
            }


            html
                << "</td>"

                << "<td>"
                << formatDuration(
                    entry.durationMs
                )
                << "</td>"

                << "<td>"
                << htmlEscape(
                    result
                )
                << "</td>"

                << "</tr>";
        }


        html
            << "</table>";
    }


    html
        << "</div>";


    // ========================================================
    // Pointing quality
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>Alignment / Pointing Quality</h2>";


    if (
        _state.alignmentQuality.measured
    )
    {
        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Measured GOTOs</span>"
            << "<span class=\"value\">"
            << _state.alignmentQuality.completedPoints
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">RMS pointing error</span>"
            << "<span class=\"value\">"
            << _state.alignmentQuality.rmsArcmin
            << " arcmin"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Worst pointing error</span>"
            << "<span class=\"value\">"
            << _state.alignmentQuality.worstArcmin
            << " arcmin"
            << "</span>"
            << "</div>";
    }
    else
    {
        html
            << "<div class=\"row\">"
            << "No valid aligned GOTO measurements yet."
            << "</div>";
    }


    html
        << "</div>";


    // ========================================================
    // GPS
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>GPS</h2>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Serial</span>"
        << "<span class=\"value\">"
        << (
            _state.gps.portOpen
                ? "OPEN"
                : "ERROR"
        )
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Fix</span>"
        << "<span class=\"value\">"
        << (
            _state.gps.fix
                ? "YES"
                : "NO"
        )
        << "</span>"
        << "</div>";


    if (
        _state.gps.fix
    )
    {
        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Latitude</span>"
            << "<span class=\"value\">"
            << _state.gps.latitude
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Longitude</span>"
            << "<span class=\"value\">"
            << _state.gps.longitude
            << "&deg;"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Altitude</span>"
            << "<span class=\"value\">"
            << _state.gps.altitude
            << " m"
            << "</span>"
            << "</div>";
    }


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Satellites</span>"
        << "<span class=\"value\">"
        << _state.gps.satellites
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">HDOP</span>"
        << "<span class=\"value\">"
        << _state.gps.hdop
        << "</span>"
        << "</div>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">UTC</span>"
        << "<span class=\"value\">";


    if (
        _state.gps.dateTimeValid
    )
    {
        html
            << _state.gps.year
            << "-"
            << _state.gps.month
            << "-"
            << _state.gps.day
            << " "
            << std::setfill('0')
            << std::setw(2)
            << _state.gps.hour
            << ":"
            << std::setw(2)
            << _state.gps.minute
            << ":"
            << std::setw(2)
            << _state.gps.second
            << " UTC";
    }
    else
    {
        html
            << "Unavailable";
    }


    html
        << "</span>"
        << "</div>";


    html
        << "</div>";

    // ========================================================
// What can I see?
// ========================================================

html
    << "<div class=\"card\">"
    << "<h2>What Can I See?</h2>";

if (
    !_state.sky.valid
)
{
    html
        << "<div class=\"row\">"
        << "Sky information unavailable."
        << "</div>";
}
else if (
    !_state.gps.fix
)
{
    html
        << "<div class=\"row\">"
        << "Waiting for GPS fix."
        << "</div>";
}
else if (
    !_state.sky.darkEnough
)
{
    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Sun altitude</span>"
        << "<span class=\"value\">"
        << std::fixed
        << std::setprecision(1)
        << _state.sky.sunAltitudeDeg
        << "&deg;"
        << "</span>"
        << "</div>";

    html
        << "<div class=\"row\">"
        << "The sky is currently too bright for "
           "the deep-sky recommendation engine."
        << "</div>";
}
else if (
    _state.sky.targets.empty()
)
{
    html
        << "<div class=\"row\">"
        << "No suitable targets found above the "
           "minimum altitude."
        << "</div>";
}
else
{
    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Sun altitude</span>"
        << "<span class=\"value\">"
        << std::fixed
        << std::setprecision(1)
        << _state.sky.sunAltitudeDeg
        << "&deg;"
        << "</span>"
        << "</div>";

    html
        << "<table>"
        << "<tr>"
        << "<th>Target</th>"
        << "<th>Alt</th>"
        << "<th>Az</th>"
        << "<th>Mag</th>"
        << "</tr>";

    for (
        const SkyTarget& target :
        _state.sky.targets
    )
    {
        html
            << "<tr>";

        html
            << "<td>";

        if (
            !target.name.empty()
        )
        {
            html
                << "<strong>"
                << htmlEscape(
                    target.name
                )
                << "</strong>"
                << "<br>"
                << "<small>"
                << htmlEscape(
                    target.designation
                )
                << "</small>";
        }
        else
        {
            html
                << "<strong>"
                << htmlEscape(
                    target.designation
                )
                << "</strong>";
        }

        html
            << "</td>";

        html
            << "<td>"
            << std::fixed
            << std::setprecision(1)
            << target.altitudeDeg
            << "&deg;"
            << "</td>";

        html
            << "<td>"
            << target.azimuthDeg
            << "&deg;"
            << "</td>";

        html
            << "<td>"
            << target.magnitude
            << "</td>";

        html
            << "</tr>";
    }

    html
        << "</table>";
}

html
    << "</div>";


    // ========================================================
    // Current observatory values
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>Observatory</h2>";


    html
        << "<div class=\"row\">"
        << "<span class=\"label\">Sensor</span>"
        << "<span class=\"value\">"
        << (
            _state.environment.valid
                ? "OK"
                : "ERROR"
        )
        << "</span>"
        << "</div>";


    if (
        _state.environment.valid
    )
    {
        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Temperature</span>"
            << "<span class=\"value\">"
            << _state.environment.temperatureC
            << " &deg;C"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Humidity</span>"
            << "<span class=\"value\">"
            << _state.environment.humidityPercent
            << " %"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Dew point</span>"
            << "<span class=\"value\">"
            << _state.environment.dewPointC
            << " &deg;C"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Dew risk</span>"
            << "<span class=\"value\">"
            << _state.environment.dewRisk
            << "</span>"
            << "</div>";
    }
    else
    {
        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Dew point</span>"
            << "<span class=\"value\">"
            << "UNKNOWN"
            << "</span>"
            << "</div>";


        html
            << "<div class=\"row\">"
            << "<span class=\"label\">Dew risk</span>"
            << "<span class=\"value\">"
            << "UNKNOWN"
            << "</span>"
            << "</div>";
    }


    html
        << "</div>";


    // ========================================================
    // Environment charts
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>Environment History</h2>"
        << "<div class=\"chart-note\">"
        << "Since TelescopeHub started "
        << "(up to the last 24 hours)"
        << "</div>";


    if (
        _environmentHistory.empty()
    )
    {
        html
            << "<div class=\"row\">"
            << "Waiting for environmental samples..."
            << "</div>";
    }
    else
    {
        html
            << "<h3>Temperature / Dew Point</h3>"
            << "<div class=\"chart-wrap\">"
            << "<canvas "
               "id=\"temperatureChart\" "
               "height=\"240\">"
               "</canvas>"
            << "</div>";


        html
            << "<h3>Humidity</h3>"
            << "<div class=\"chart-wrap\">"
            << "<canvas "
               "id=\"humidityChart\" "
               "height=\"240\">"
               "</canvas>"
            << "</div>";


        html
            << "<h3>Dew Point Spread</h3>"
            << "<div class=\"chart-wrap\">"
            << "<canvas "
               "id=\"dewSpreadChart\" "
               "height=\"240\">"
               "</canvas>"
            << "</div>";


        appendEnvironmentData(
            html,
            _environmentHistory
        );


        html
            << R"JS(
<script>

function drawLineChart(
    canvasId,
    points,
    series,
    yLabel,
    unit
)
{
    const canvas =
        document.getElementById(canvasId);

    if (!canvas)
        return;

    const ctx =
        canvas.getContext("2d");

    const rect =
        canvas.getBoundingClientRect();

    const width =
        Math.max(
            700,
            Math.floor(rect.width)
        );

    const height =
        240;

    const dpr =
        window.devicePixelRatio || 1;

    canvas.width =
        width * dpr;

    canvas.height =
        height * dpr;

    ctx.setTransform(
        dpr,
        0,
        0,
        dpr,
        0,
        0
    );

    ctx.clearRect(
        0,
        0,
        width,
        height
    );


    if (
        points.length === 0
    )
    {
        return;
    }


    const left =
        58;

    const right =
        18;

    const top =
        18;

    const bottom =
        34;


    const plotWidth =
        width -
        left -
        right;

    const plotHeight =
        height -
        top -
        bottom;


    let minTime =
        points[0].t;

    let maxTime =
        points[points.length - 1].t;


    if (
        maxTime <= minTime
    )
    {
        maxTime =
            minTime + 1;
    }


    let minValue =
        Infinity;

    let maxValue =
        -Infinity;


    for (
        const point of points
    )
    {
        for (
            const s of series
        )
        {
            const value =
                point[s.key];

            if (
                Number.isFinite(value)
            )
            {
                minValue =
                    Math.min(
                        minValue,
                        value
                    );

                maxValue =
                    Math.max(
                        maxValue,
                        value
                    );
            }
        }
    }


    if (
        !Number.isFinite(minValue) ||
        !Number.isFinite(maxValue)
    )
    {
        return;
    }


    if (
        Math.abs(
            maxValue -
            minValue
        ) < 0.01
    )
    {
        minValue -= 1;
        maxValue += 1;
    }


    const padding =
        Math.max(
            0.5,
            (
                maxValue -
                minValue
            ) * 0.08
        );


    minValue -= padding;
    maxValue += padding;


    function x(value)
    {
        return left +
            (
                (
                    value -
                    minTime
                )
                /
                (
                    maxTime -
                    minTime
                )
            )
            *
            plotWidth;
    }


    function y(value)
    {
        return top +
            (
                1 -
                (
                    (
                        value -
                        minValue
                    )
                    /
                    (
                        maxValue -
                        minValue
                    )
                )
            )
            *
            plotHeight;
    }


    // Background
    ctx.fillStyle =
        "#0b0b0b";

    ctx.fillRect(
        0,
        0,
        width,
        height
    );


    // Grid
    ctx.strokeStyle =
        "#292929";

    ctx.lineWidth =
        1;


    const gridLines =
        5;


    ctx.font =
        "12px Arial";

    ctx.fillStyle =
        "#999";


    for (
        let i = 0;
        i <= gridLines;
        ++i
    )
    {
        const fraction =
            i /
            gridLines;

        const yy =
            top +
            fraction *
            plotHeight;


        ctx.beginPath();

        ctx.moveTo(
            left,
            yy
        );

        ctx.lineTo(
            width - right,
            yy
        );

        ctx.stroke();


        const value =
            maxValue -
            fraction *
            (
                maxValue -
                minValue
            );


        ctx.fillText(
            value.toFixed(1) +
            " " +
            unit,
            4,
            yy + 4
        );
    }


    // X axis
    ctx.strokeStyle =
        "#555";

    ctx.beginPath();

    ctx.moveTo(
        left,
        top +
        plotHeight
    );

    ctx.lineTo(
        width - right,
        top +
        plotHeight
    );

    ctx.stroke();


    // Time labels
    ctx.fillStyle =
        "#999";

    const labels =
        5;


    for (
        let i = 0;
        i <= labels;
        ++i
    )
    {
        const fraction =
            i /
            labels;

        const time =
            minTime +
            fraction *
            (
                maxTime -
                minTime
            );

        const xx =
            left +
            fraction *
            plotWidth;


        const date =
            new Date(
                time
            );


        const text =
            date.toLocaleTimeString(
                [],
                {
                    hour: "2-digit",
                    minute: "2-digit"
                }
            );


        ctx.fillText(
            text,
            xx - 24,
            height - 10
        );
    }


    // Series
    for (
        const s of series
    )
    {
        ctx.strokeStyle =
            s.line;

        ctx.lineWidth =
            2;

        ctx.beginPath();


        let started =
            false;


        for (
            const point of points
        )
        {
            const value =
                point[s.key];


            if (
                !Number.isFinite(value)
            )
            {
                continue;
            }


            const xx =
                x(point.t);

            const yy =
                y(value);


            if (
                !started
            )
            {
                ctx.moveTo(
                    xx,
                    yy
                );

                started =
                    true;
            }
            else
            {
                ctx.lineTo(
                    xx,
                    yy
                );
            }
        }


        ctx.stroke();


        // Last-value marker
        for (
            let i =
                points.length -
                1;
            i >= 0;
            --i
        )
        {
            const point =
                points[i];

            const value =
                point[s.key];


            if (
                Number.isFinite(value)
            )
            {
                ctx.fillStyle =
                    s.line;

                ctx.beginPath();

                ctx.arc(
                    x(point.t),
                    y(value),
                    3,
                    0,
                    Math.PI * 2
                );

                ctx.fill();

                break;
            }
        }
    }


    // Legend
    let legendX =
        left;

    const legendY =
        10;


    ctx.font =
        "12px Arial";


    for (
        const s of series
    )
    {
        ctx.fillStyle =
            s.line;

        ctx.fillRect(
            legendX,
            legendY - 8,
            10,
            10
        );


        ctx.fillStyle =
            "#ccc";

        ctx.fillText(
            s.label,
            legendX + 15,
            legendY + 1
        );


        legendX +=
            100 +
            (
                s.label.length *
                3
            );
    }


    // Y-axis label
    ctx.save();

    ctx.translate(
        12,
        height / 2
    );

    ctx.rotate(
        -Math.PI / 2
    );

    ctx.fillStyle =
        "#aaa";

    ctx.fillText(
        yLabel,
        0,
        0
    );

    ctx.restore();
}


function drawEnvironmentCharts()
{
    drawLineChart(
        "temperatureChart",
        environmentHistory,
        [
            {
                key: "temp",
                label: "Temperature",
                line: "#f0a040"
            },
            {
                key: "dew",
                label: "Dew point",
                line: "#70b7ff"
            }
        ],
        "Temperature",
        "°C"
    );


    drawLineChart(
        "humidityChart",
        environmentHistory,
        [
            {
                key: "humidity",
                label: "Humidity",
                line: "#72d6a2"
            }
        ],
        "Humidity",
        "%"
    );


    const spreadHistory =
        environmentHistory.map(
            point =>
            ({
                t: point.t,
                spread:
                    point.temp -
                    point.dew
            })
        );


    drawLineChart(
        "dewSpreadChart",
        spreadHistory,
        [
            {
                key: "spread",
                label: "Temperature − Dew point",
                line: "#d28cff"
            }
        ],
        "Spread",
        "°C"
    );
}


drawEnvironmentCharts();


window.addEventListener(
    "resize",
    drawEnvironmentCharts
);

</script>
)JS";
    }


    // ========================================================
    // Network
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>Network</h2>"

        << "<div class=\"row\">"
        << "<span class=\"label\">HTTP</span>"
        << "<span class=\"value\">8080</span>"
        << "</div>"

        << "<div class=\"row\">"
        << "<span class=\"label\">Stellarium</span>"
        << "<span class=\"value\">10001</span>"
        << "</div>"

        << "</div>";


    // ========================================================
    // End
    // ========================================================

    html
        << "</body>"
        << "</html>";


    return html.str();
}


// ============================================================
// Stop
// ============================================================

void WebServer::stop()
{
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