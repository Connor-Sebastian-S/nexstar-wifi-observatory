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

#include <cmath>
#include <cstdlib>
#include <ctime>


namespace
{
    // Formats a UTC epoch as a local "HH:MM" string, matching
    // the pattern already used for Moon rise/set elsewhere in
    // this file. Returns an em-dash if epoch is invalid/zero.
    std::string formatLocalTime(
        std::time_t epochUtc,
        bool valid
    )
    {
        if (
            !valid
        )
        {
            return
                "—";
        }

        std::tm localTime{};

        localtime_r(
            &epochUtc,
            &localTime
        );

        char text[32] =
            {};

        std::strftime(
            text,
            sizeof(text),
            "%H:%M",
            &localTime
        );

        return
            std::string(
                text
            );
    }
}


// ============================================================
// Constructor
// ============================================================

WebServer::WebServer()
        : _serverSocket(-1),
          _port(0),
          _state(),
          _abortGotoHandler(),
          _gotoHandler(),
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

void WebServer::setGotoHandler(
    std::function<bool(
        double,
        double
    )> handler
)
{
    _gotoHandler =
        std::move(
            handler
        );
}


void WebServer::setRecorderDirectory(
    const std::string& directory
)
{
    _skyHistory.setDirectory(
        directory
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
    // GOTO endpoint
    // ========================================================

    if (
        std::strncmp(
            request,
            "POST /api/goto?",
            15
        ) == 0
    )
    {
        double ra =
            0.0;

        double dec =
            0.0;


        auto parseQueryDouble =
            [&](const char* key, double& value) -> bool
            {
                std::string needle =
                    std::string(key) +
                    "=";

                const char* start =
                    std::strstr(
                        request,
                        needle.c_str()
                    );

                if (
                    !start
                )
                {
                    return false;
                }


                start +=
                    needle.size();


                char* end =
                    nullptr;


                double parsed =
                    std::strtod(
                        start,
                        &end
                    );


                if (
                    end == start ||
                    !std::isfinite(parsed)
                )
                {
                    return false;
                }


                value =
                    parsed;

                return true;
            };


        bool requestValid =
            parseQueryDouble(
                "ra",
                ra
            )
            &&
            parseQueryDouble(
                "dec",
                dec
            );


        bool success =
            false;


        if (
            requestValid &&
            _gotoHandler
        )
        {
            success =
                _gotoHandler(
                    ra,
                    dec
                );
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
            << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
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
    // Sky history endpoint (heatmap data)
    // ========================================================

    if (
        std::strncmp(
            request,
            "GET /api/sky-history",
            std::strlen(
                "GET /api/sky-history"
            )
        ) == 0
    )
    {
        // Default matches the dome page's default "30d" view.
        int rangeDays =
            30;

        const char* rangeParam =
            std::strstr(
                request,
                "range="
            );

        if (
            rangeParam
        )
        {
            rangeParam +=
                6;

            if (
                std::strncmp(
                    rangeParam,
                    "7d",
                    2
                ) == 0
            )
            {
                rangeDays =
                    7;
            }
            else if (
                std::strncmp(
                    rangeParam,
                    "all",
                    3
                ) == 0
            )
            {
                rangeDays =
                    0;
            }
            else
            {
                rangeDays =
                    30;
            }
        }


        std::string body =
            buildSkyHistoryJson(
                rangeDays
            );


        std::ostringstream output;

        output
            << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: "
            << body.size()
            << "\r\n"
            << "Access-Control-Allow-Origin: *\r\n"
            << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
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
    // Sky map page (3D heatmap dome)
    // ========================================================

    if (
        std::strncmp(
            request,
            "GET /sky-map",
            std::strlen(
                "GET /sky-map"
            )
        ) == 0
    )
    {
        std::string body =
            buildSkyMapHtml();

        std::ostringstream output;

        output
            << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/html; charset=utf-8\r\n"
            << "Cache-Control: no-cache, no-store, must-revalidate\r\n"
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

std::string WebServer::buildSkyHistoryJson(
    int rangeDays
)
{
    SkyHistoryResult result =
        _skyHistory.query(
            rangeDays
        );

    std::ostringstream json;

    json
        << "{"
        << "\"stats\":{"
        << "\"totalSessions\":"
        << result.stats.totalSessions
        << ",\"totalHours\":"
        << std::fixed
        << std::setprecision(1)
        << result.stats.totalHours
        << ",\"hasHottest\":"
        << (
            result.stats.hasHottest
                ? "true"
                : "false"
        )
        << ",\"hottestAz\":"
        << std::setprecision(1)
        << result.stats.hottestAzDeg
        << ",\"hottestAlt\":"
        << result.stats.hottestAltDeg
        << ",\"hottestCount\":"
        << result.stats.hottestCount
        << "},"
        << "\"cells\":[";

    for (
        std::size_t i = 0;
        i < result.cells.size();
        ++i
    )
    {
        const SkyHistoryCell& cell =
            result.cells[i];

        if (
            i > 0
        )
        {
            json
                << ",";
        }

        json
            << "{\"az\":"
            << std::setprecision(1)
            << cell.azDeg
            << ",\"alt\":"
            << cell.altDeg
            << ",\"count\":"
            << cell.count
            << "}";
    }

    json
        << "],"
        << "\"trail\":[";

    for (
        std::size_t i = 0;
        i < result.trail.size();
        ++i
    )
    {
        const SkyHistoryTrailPoint& point =
            result.trail[i];

        if (
            i > 0
        )
        {
            json
                << ",";
        }

        json
            << "{\"az\":"
            << std::setprecision(1)
            << point.azDeg
            << ",\"alt\":"
            << point.altDeg
            << "}";
    }

    json
        << "]"
        << "}";

    return
        json.str();
}


std::string WebServer::buildSkyMapHtml() const
{
    return
        R"SKYMAP_HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sky Dome &mdash; Observing History</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Fraunces:opsz,wght@9..144,500;9..144,600&family=JetBrains+Mono:wght@400;600&display=swap');

  * { box-sizing: border-box; }

  html, body {
    margin: 0;
    padding: 0;
    width: 100%;
    height: 100%;
    background: #05070a;
    color: #e9e4de;
    font-family: system-ui, -apple-system, sans-serif;
    overflow: hidden;
  }

  #mount {
    position: absolute;
    inset: 0;
    cursor: grab;
  }

  .mono { font-family: 'JetBrains Mono', monospace; }
  .display { font-family: 'Fraunces', serif; }

  .overlay {
    position: absolute;
    z-index: 2;
    pointer-events: none;
  }

  #topbar {
    top: 0; left: 0; right: 0;
    padding: 20px 24px;
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
  }

  #eyebrow {
    font-size: 11px;
    letter-spacing: 0.18em;
    color: #8a5346;
    margin-bottom: 6px;
  }

  #title {
    font-size: 26px;
    font-weight: 600;
    color: #f1ece3;
  }

  #stats {
    text-align: right;
    font-size: 12px;
    color: #a8907e;
    line-height: 1.7;
  }

  #stats .hottest { color: #e2664a; }

  #backlink {
    pointer-events: auto;
    font-size: 11px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    color: #6a5850;
    text-decoration: none;
    display: inline-block;
    margin-top: 10px;
  }

  #backlink:hover { color: #e2664a; }

  #legend {
    bottom: 24px; left: 24px;
  }

  #legend-label {
    font-size: 10px;
    color: #8a7a6e;
    margin-bottom: 6px;
  }

  #legend-bar {
    width: 140px;
    height: 8px;
    border-radius: 4px;
    background: linear-gradient(90deg, rgb(27,17,64), rgb(106,27,107), rgb(194,59,74), rgb(255,233,168));
  }

  #legend-scale {
    display: flex;
    justify-content: space-between;
    font-size: 9px;
    color: #8a7a6e;
    margin-top: 4px;
    width: 140px;
  }

  #controls {
    bottom: 24px; right: 24px;
    display: flex;
    gap: 20px;
    align-items: flex-end;
    pointer-events: auto;
  }

  .btn-group { display: flex; gap: 6px; }

  .sdh-btn {
    font-family: 'JetBrains Mono', monospace;
    font-size: 11px;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    background: transparent;
    border: 1px solid #4a2018;
    color: #c98b78;
    padding: 6px 12px;
    border-radius: 3px;
    cursor: pointer;
    transition: border-color 0.15s ease, color 0.15s ease;
  }

  .sdh-btn:hover { border-color: #c2452b; color: #f2b9a2; }

  .sdh-btn.active {
    border-color: #c2452b;
    color: #ffd9c9;
    background: rgba(194,69,43,0.12);
  }

  #hint {
    bottom: 24px; left: 50%;
    transform: translateX(-50%);
    font-size: 10px;
    color: #5c4a40;
    letter-spacing: 0.08em;
  }

  #empty-state {
    position: absolute;
    inset: 0;
    display: none;
    align-items: center;
    justify-content: center;
    flex-direction: column;
    text-align: center;
    z-index: 3;
    pointer-events: none;
  }

  #empty-state.visible { display: flex; }

  #empty-state .display { font-size: 20px; color: #d8cfc4; margin-bottom: 8px; }
  #empty-state .mono { font-size: 12px; color: #7a6a5e; max-width: 320px; line-height: 1.6; }
</style>
</head>
<body>

<div id="mount"></div>

<div id="empty-state">
  <div class="display">No logged pointings yet</div>
  <div class="mono">Once the mount has connected and tracked something, this dome fills in from the session blackbox logs.</div>
</div>

<div class="overlay" id="topbar">
  <div>
    <div id="eyebrow" class="mono">OBSERVING LOG &mdash; SKY DOME</div>
    <div id="title" class="display">Where you've been looking</div>
    <a id="backlink" href="/">&larr; back to dashboard</a>
  </div>
  <div id="stats" class="mono">
    <div id="stats-line">&mdash; sessions &middot; &mdash;h logged</div>
    <div class="hottest" id="stats-hottest"></div>
  </div>
</div>

<div class="overlay" id="legend">
  <div id="legend-label" class="mono">VISITS</div>
  <div id="legend-bar"></div>
  <div id="legend-scale" class="mono"><span>rare</span><span>frequent</span></div>
</div>

<div class="overlay" id="controls">
  <div class="btn-group" id="range-buttons">
    <button class="sdh-btn" data-range="7d">7d</button>
    <button class="sdh-btn active" data-range="30d">30d</button>
    <button class="sdh-btn" data-range="all">all</button>
  </div>
  <div class="btn-group" id="mode-buttons">
    <button class="sdh-btn active" data-mode="heat">heat</button>
    <button class="sdh-btn" data-mode="trail">trail</button>
    <button class="sdh-btn" data-mode="both">both</button>
  </div>
</div>

<div class="overlay mono" id="hint">DRAG TO LOOK AROUND</div>

<script>
(function () {
  "use strict";

  // ----------------------------------------------------------
  // Coordinate helpers (matches SkyHistory's az/alt convention:
  // azimuth clockwise from North, altitude up from horizon)
  // ----------------------------------------------------------

  function azAltToVector(azDeg, altDeg, radius) {
    radius = radius || 1;
    var az = THREE.MathUtils.degToRad(azDeg);
    var theta = THREE.MathUtils.degToRad(90 - altDeg);
    var x = radius * Math.sin(theta) * Math.sin(az);
    var z = -radius * Math.sin(theta) * Math.cos(az);
    var y = radius * Math.cos(theta);
    return new THREE.Vector3(x, y, z);
  }

  var HEAT_STOPS = [
    { t: 0.0, c: [27, 17, 64] },
    { t: 0.35, c: [106, 27, 107] },
    { t: 0.65, c: [194, 59, 74] },
    { t: 1.0, c: [255, 233, 168] }
  ];

  function heatColor(t) {
    t = Math.max(0, Math.min(1, t));
    var a = HEAT_STOPS[0];
    var b = HEAT_STOPS[HEAT_STOPS.length - 1];
    for (var i = 0; i < HEAT_STOPS.length - 1; i++) {
      if (t >= HEAT_STOPS[i].t && t <= HEAT_STOPS[i + 1].t) {
        a = HEAT_STOPS[i];
        b = HEAT_STOPS[i + 1];
        break;
      }
    }
    var span = (b.t - a.t) || 1;
    var lt = (t - a.t) / span;
    var r = (a.c[0] + (b.c[0] - a.c[0]) * lt) / 255;
    var g = (a.c[1] + (b.c[1] - a.c[1]) * lt) / 255;
    var bl = (a.c[2] + (b.c[2] - a.c[2]) * lt) / 255;
    return new THREE.Color(r, g, bl);
  }

  function makeGlowTexture() {
    var size = 128;
    var canvas = document.createElement("canvas");
    canvas.width = size;
    canvas.height = size;
    var ctx = canvas.getContext("2d");
    var gradient = ctx.createRadialGradient(size/2, size/2, 0, size/2, size/2, size/2);
    gradient.addColorStop(0, "rgba(255,255,255,1)");
    gradient.addColorStop(0.35, "rgba(255,255,255,0.7)");
    gradient.addColorStop(1, "rgba(255,255,255,0)");
    ctx.fillStyle = gradient;
    ctx.fillRect(0, 0, size, size);
    var texture = new THREE.Texture(canvas);
    texture.needsUpdate = true;
    return texture;
  }

  function makeLabelSprite(text, color) {
    var canvas = document.createElement("canvas");
    canvas.width = 128;
    canvas.height = 64;
    var ctx = canvas.getContext("2d");
    ctx.font = "600 34px 'JetBrains Mono', monospace";
    ctx.fillStyle = color;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(text, canvas.width / 2, canvas.height / 2);
    var texture = new THREE.Texture(canvas);
    texture.needsUpdate = true;
    var material = new THREE.SpriteMaterial({ map: texture, transparent: true, depthWrite: false });
    var sprite = new THREE.Sprite(material);
    sprite.scale.set(0.22, 0.11, 1);
    return sprite;
  }

  // ----------------------------------------------------------
  // Scene setup
  // ----------------------------------------------------------

  var mount = document.getElementById("mount");

  var scene = new THREE.Scene();
  scene.background = new THREE.Color(0x05070a);

  var camera = new THREE.PerspectiveCamera(72, mount.clientWidth / mount.clientHeight, 0.01, 100);
  camera.position.set(0, 0.02, 0);

  var renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.setSize(mount.clientWidth, mount.clientHeight);
  mount.appendChild(renderer.domElement);

  var ground = new THREE.Mesh(
    new THREE.CircleGeometry(6, 48),
    new THREE.MeshBasicMaterial({ color: 0x0a0c10, side: THREE.DoubleSide, transparent: true, opacity: 0.9 })
  );
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -0.002;
  scene.add(ground);

  var domeShell = new THREE.Mesh(
    new THREE.SphereGeometry(1, 32, 16, 0, Math.PI * 2, 0, Math.PI / 2),
    new THREE.MeshBasicMaterial({ color: 0x2a3346, wireframe: true, transparent: true, opacity: 0.08 })
  );
  scene.add(domeShell);

  var ringMaterialDim = new THREE.LineBasicMaterial({ color: 0x6e2b22, transparent: true, opacity: 0.35 });
  var ringMaterialHorizon = new THREE.LineBasicMaterial({ color: 0xc2452b, transparent: true, opacity: 0.55 });

  [30, 60].forEach(function (altDeg) {
    var pts = [];
    for (var i = 0; i <= 64; i++) pts.push(azAltToVector((i / 64) * 360, altDeg, 1));
    var geo = new THREE.BufferGeometry().setFromPoints(pts);
    scene.add(new THREE.LineLoop(geo, ringMaterialDim));
  });

  (function () {
    var pts = [];
    for (var i = 0; i <= 64; i++) pts.push(azAltToVector((i / 64) * 360, 0.5, 1));
    var geo = new THREE.BufferGeometry().setFromPoints(pts);
    scene.add(new THREE.LineLoop(geo, ringMaterialHorizon));
  })();

  [{ az: 0, label: "N" }, { az: 90, label: "E" }, { az: 180, label: "S" }, { az: 270, label: "W" }]
    .forEach(function (c) {
      var sprite = makeLabelSprite(c.label, "#e2664a");
      sprite.position.copy(azAltToVector(c.az, 4, 1.04));
      scene.add(sprite);
    });

  (function () {
    var starCount = 500;
    var positions = new Float32Array(starCount * 3);
    for (var i = 0; i < starCount; i++) {
      var az = Math.random() * 360;
      var alt = Math.random() * 90;
      var v = azAltToVector(az, alt, 1.4 + Math.random() * 0.4);
      positions[i * 3] = v.x;
      positions[i * 3 + 1] = v.y;
      positions[i * 3 + 2] = v.z;
    }
    var geo = new THREE.BufferGeometry();
    geo.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    var mat = new THREE.PointsMaterial({ color: 0x8a93a8, size: 0.01, transparent: true, opacity: 0.5, sizeAttenuation: true });
    scene.add(new THREE.Points(geo, mat));
  })();

  var glowTexture = makeGlowTexture();

  var heatGeometry = new THREE.BufferGeometry();
  var heatMaterial = new THREE.PointsMaterial({
    size: 0.09, map: glowTexture, transparent: true, depthWrite: false,
    blending: THREE.AdditiveBlending, vertexColors: true, sizeAttenuation: true
  });
  var heatPoints = new THREE.Points(heatGeometry, heatMaterial);
  scene.add(heatPoints);

  var trailMaterial = new THREE.LineBasicMaterial({ color: 0xffce8a, transparent: true, opacity: 0.85 });
  var trailGeometry = new THREE.BufferGeometry();
  var trailLine = new THREE.Line(trailGeometry, trailMaterial);
  scene.add(trailLine);

  var lastPointMarker = new THREE.Sprite(new THREE.SpriteMaterial({
    map: glowTexture, color: 0x9fe8ff, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending
  }));
  lastPointMarker.scale.set(0.14, 0.14, 1);
  lastPointMarker.visible = false;
  scene.add(lastPointMarker);

  var mode = "heat";
  function applyMode() {
    heatPoints.visible = (mode === "heat" || mode === "both");
    trailLine.visible = (mode === "trail" || mode === "both");
    lastPointMarker.visible = trailLine.visible && lastPointMarker.hasPosition;
  }

  // ----------------------------------------------------------
  // Data loading
  // ----------------------------------------------------------

  function renderData(data) {
    var cells = data.cells || [];
    var trail = data.trail || [];
    var stats = data.stats || {};

    document.getElementById("empty-state").classList.toggle(
      "visible", cells.length === 0 && trail.length === 0
    );

    var maxCount = 1;
    cells.forEach(function (c) { maxCount = Math.max(maxCount, c.count); });

    var positions = new Float32Array(cells.length * 3);
    var colors = new Float32Array(cells.length * 3);
    cells.forEach(function (c, i) {
      var v = azAltToVector(c.az, c.alt, 1);
      positions[i * 3] = v.x;
      positions[i * 3 + 1] = v.y;
      positions[i * 3 + 2] = v.z;
      var color = heatColor(c.count / maxCount);
      colors[i * 3] = color.r;
      colors[i * 3 + 1] = color.g;
      colors[i * 3 + 2] = color.b;
    });
    heatGeometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    heatGeometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    heatGeometry.attributes.position.needsUpdate = true;
    heatGeometry.attributes.color.needsUpdate = true;

    var trailPositions = new Float32Array(trail.length * 3);
    trail.forEach(function (p, i) {
      var v = azAltToVector(p.az, p.alt, 1.01);
      trailPositions[i * 3] = v.x;
      trailPositions[i * 3 + 1] = v.y;
      trailPositions[i * 3 + 2] = v.z;
    });
    trailGeometry.setAttribute("position", new THREE.BufferAttribute(trailPositions, 3));
    trailGeometry.attributes.position.needsUpdate = true;

    if (trail.length > 0) {
      var last = trail[trail.length - 1];
      lastPointMarker.position.copy(azAltToVector(last.az, last.alt, 1.02));
      lastPointMarker.hasPosition = true;
    } else {
      lastPointMarker.hasPosition = false;
    }

    document.getElementById("stats-line").textContent =
      (stats.totalSessions || 0) + " sessions \u00b7 " + (stats.totalHours || 0) + "h logged";
    document.getElementById("stats-hottest").textContent =
      stats.hasHottest
        ? "hottest region: " + stats.hottestAlt.toFixed(0) + "\u00b0 alt / " + stats.hottestAz.toFixed(0) + "\u00b0 az"
        : "";

    applyMode();
  }

  function loadRange(range) {
    fetch("/api/sky-history?range=" + encodeURIComponent(range))
      .then(function (r) { return r.json(); })
      .then(renderData)
      .catch(function () {
        document.getElementById("stats-line").textContent = "history unavailable";
      });
  }

  document.querySelectorAll("#range-buttons .sdh-btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll("#range-buttons .sdh-btn").forEach(function (b) { b.classList.remove("active"); });
      btn.classList.add("active");
      loadRange(btn.getAttribute("data-range"));
    });
  });

  document.querySelectorAll("#mode-buttons .sdh-btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      document.querySelectorAll("#mode-buttons .sdh-btn").forEach(function (b) { b.classList.remove("active"); });
      btn.classList.add("active");
      mode = btn.getAttribute("data-mode");
      applyMode();
    });
  });

  loadRange("30d");

  // ----------------------------------------------------------
  // Drag to look around
  // ----------------------------------------------------------

  var yaw = Math.PI * 0.15;
  var pitch = 0.25;
  var dragging = false;
  var lastPointer = { x: 0, y: 0 };
  var lastInteraction = 0;

  renderer.domElement.addEventListener("pointerdown", function (e) {
    dragging = true;
    lastPointer = { x: e.clientX, y: e.clientY };
    lastInteraction = performance.now();
    mount.style.cursor = "grabbing";
  });
  window.addEventListener("pointermove", function (e) {
    lastInteraction = performance.now();
    if (!dragging) return;
    var dx = e.clientX - lastPointer.x;
    var dy = e.clientY - lastPointer.y;
    lastPointer = { x: e.clientX, y: e.clientY };
    yaw -= dx * 0.004;
    pitch = Math.max(-0.15, Math.min(1.4, pitch + dy * 0.004));
  });
  window.addEventListener("pointerup", function () {
    dragging = false;
    mount.style.cursor = "grab";
  });

  var resizeObserver = new ResizeObserver(function () {
    var w = mount.clientWidth, h = mount.clientHeight;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
  });
  resizeObserver.observe(mount);

  function animate() {
    var idle = performance.now() - lastInteraction;
    if (!dragging && idle > 1500) yaw += 0.0009;

    var lookDir = new THREE.Vector3(
      Math.sin(yaw) * Math.cos(pitch),
      Math.sin(pitch),
      -Math.cos(yaw) * Math.cos(pitch)
    );
    camera.lookAt(camera.position.clone().add(lookDir));

    if (lastPointMarker.visible) {
      var pulse = 0.14 + Math.sin(performance.now() * 0.004) * 0.025;
      lastPointMarker.scale.set(pulse, pulse, 1);
    }

    renderer.render(scene, camera);
    requestAnimationFrame(animate);
  }
  animate();
})();
</script>
</body>
</html>
)SKYMAP_HTML";
}


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


        << ".goto-button{"
        << "padding:6px 10px;"
        << "font-size:13px;"
        << "font-weight:bold;"
        << "cursor:pointer;"
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
        << "function gotoTarget(ra,dec,button){"
        << "if(!confirm('GOTO this target?'))return;"
        << "button.disabled=true;"
        << "button.textContent='GOTO...';"
        << "fetch('/api/goto?ra='+encodeURIComponent(ra)"
           "+'&dec='+encodeURIComponent(dec),"
           "{method:'POST'})"
        << ".then(r=>r.json())"
        << ".then(d=>{"
        << "if(d.ok){location.reload();}"
        << "else{"
        << "button.disabled=false;"
        << "button.textContent='GOTO';"
        << "alert('GOTO was rejected. Check the telescope connection or whether a slew is already active.');"
        << "}"
        << "})"
        << ".catch(()=>{"
        << "button.disabled=false;"
        << "button.textContent='GOTO';"
        << "alert('Could not contact TelescopeHub.');"
        << "});"
        << "}"
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
    // What's up tonight?
    // ========================================================

    html
        << "<div class=\"card\">"
        << "<h2>What's Up Tonight?</h2>";


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
        !_state.gps.fix ||
        !_state.gps.dateTimeValid
    )
    {
        html
            << "<div class=\"row\">"
            << "Waiting for valid GPS position and time."
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


        if (
            _state.sky.moon.valid
        )
        {
            html
                << "<h3>Moon</h3>";


            html
                << "<div class=\"row\">"
                << "<span class=\"label\">Phase</span>"
                << "<span class=\"value\">"
                << htmlEscape(
                    _state.sky.moon.phaseName
                )
                << " ("
                << std::fixed
                << std::setprecision(0)
                << _state.sky.moon.illuminationPercent
                << "%)"
                << "</span>"
                << "</div>";


            html
                << "<div class=\"row\">"
                << "<span class=\"label\">Position</span>"
                << "<span class=\"value\">"
                << std::fixed
                << std::setprecision(1)
                << _state.sky.moon.altitudeDeg
                << "&deg; alt / "
                << _state.sky.moon.azimuthDeg
                << "&deg; az"
                << "</span>"
                << "</div>";


            html
                << "<div class=\"row\">"
                << "<span class=\"label\">Moon rise</span>"
                << "<span class=\"value\">";


            if (
                _state.sky.moon.riseValid
            )
            {
                std::time_t rise =
                    _state.sky.moon.riseEpochUtc;

                std::tm localTime{};

                localtime_r(
                    &rise,
                    &localTime
                );

                char text[32] =
                    {};

                std::strftime(
                    text,
                    sizeof(text),
                    "%H:%M",
                    &localTime
                );

                html
                    << text;
            }
            else
            {
                html
                    << "—";
            }


            html
                << "</span>"
                << "</div>";


            html
                << "<div class=\"row\">"
                << "<span class=\"label\">Moon set</span>"
                << "<span class=\"value\">";


            if (
                _state.sky.moon.setValid
            )
            {
                std::time_t set =
                    _state.sky.moon.setEpochUtc;

                std::tm localTime{};

                localtime_r(
                    &set,
                    &localTime
                );

                char text[32] =
                    {};

                std::strftime(
                    text,
                    sizeof(text),
                    "%H:%M",
                    &localTime
                );

                html
                    << text;
            }
            else
            {
                html
                    << "—";
            }


            html
                << "</span>"
                << "</div>";
        }


        if (
            !_state.sky.darkEnough
        )
        {
            std::vector<std::string> upcomingDso;

            std::vector<std::string> visiblePlanets;


            for (
                const SkyTarget& target :
                _state.sky.targets
            )
            {
                if (
                    target.isPlanet
                )
                {
                    visiblePlanets.push_back(
                        target.name
                    );
                }
                else if (
                    upcomingDso.size() < 3
                )
                {
                    upcomingDso.push_back(
                        !target.name.empty()
                            ? target.name
                            : target.designation
                    );
                }
            }


            auto joinNames =
                [](
                    const std::vector<std::string>& names
                ) -> std::string
                {
                    std::string joined;

                    for (
                        std::size_t i = 0;
                        i < names.size();
                        ++i
                    )
                    {
                        if (
                            i > 0 &&
                            i == names.size() - 1
                        )
                        {
                            joined +=
                                names.size() > 2
                                    ? ", and "
                                    : " and ";
                        }
                        else if (
                            i > 0
                        )
                        {
                            joined +=
                                ", ";
                        }

                        joined +=
                            names[i];
                    }

                    return joined;
                };


            html
                << "<div class=\"row\">";


            if (
                !upcomingDso.empty()
            )
            {
                html
                    << "The sky's too bright for deep-sky "
                       "viewing right now &mdash; once it's "
                       "dark, you'll be able to see <strong>"
                    << htmlEscape(
                        joinNames(
                            upcomingDso
                        )
                    )
                    << "</strong>.";
            }
            else
            {
                html
                    << "The sky's too bright for deep-sky "
                       "viewing right now.";
            }


            if (
                !visiblePlanets.empty()
            )
            {
                html
                    << " Right now you can also spot <strong>"
                    << htmlEscape(
                        joinNames(
                            visiblePlanets
                        )
                    )
                    << "</strong>.";
            }


            if (
                _state.sky.moon.aboveHorizon
            )
            {
                html
                    << " The Moon ("
                    << htmlEscape(
                        _state.sky.moon.phaseName
                    )
                    << ") is up too.";
            }


            html
                << " Full list below shows everything that "
                   "qualifies right now, regardless of "
                   "brightness."
                << "</div>";
        }


        if (
            _state.sky.targets.empty()
        )
        {
            html
                << "<div class=\"row\">"
                << "No suitable targets found."
                << "</div>";
        }
        else
        {
            html
                << "<h3>"
                << (
                    _state.sky.darkEnough
                        ? "Recommended targets"
                        : "Everything currently up"
                )
                << "</h3>"
                << "<table>"
                << "<tr>"
                << "<th>Target</th>"
                << "<th>Alt</th>"
                << "<th>Az</th>"
                << "<th>Mag</th>"
                << "<th>Moon</th>"
                << "<th>Best time</th>"
                << "<th></th>"
                << "</tr>";


            for (
                const SkyTarget& target :
                _state.sky.targets
            )
            {
                html
                    << "<tr>"
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
                    << "</td>"
                    << "<td>"
                    << std::fixed
                    << std::setprecision(1)
                    << target.altitudeDeg
                    << "&deg;"
                    << "</td>"
                    << "<td>"
                    << target.azimuthDeg
                    << "&deg;"
                    << "</td>"
                    << "<td>"
                    << target.magnitude
                    << "</td>"
                    << "<td>"
                    << target.moonSeparationDeg
                    << "&deg;"
                    << "</td>"
                    << "<td>";


                if (
                    target.bestAltitudeDeg -
                    target.altitudeDeg
                    <
                    0.5
                )
                {
                    html
                        << "Now";
                }
                else
                {
                    html
                        << formatLocalTime(
                            target.bestTimeEpochUtc,
                            true
                        )
                        << " ("
                        << std::fixed
                        << std::setprecision(0)
                        << target.bestAltitudeDeg
                        << "&deg;)"
                        << std::setprecision(1);
                }


                html
                    << "</td>"
                    << "<td>"
                    << "<button "
                       "class=\"goto-button\" "
                       "onclick=\"gotoTarget("
                    << target.raDeg
                    << ","
                    << target.decDeg
                    << ",this)\">"
                    << "GOTO"
                    << "</button>"
                    << "</td>"
                    << "</tr>";
            }


            html
                << "</table>";
        }
    }


    if (
        !_state.sky.meteorShowers.empty()
    )
    {
        html
            << "<h3>Meteor showers active now</h3>"
            << "<table>"
            << "<tr>"
            << "<th>Shower</th>"
            << "<th>ZHR</th>"
            << "<th>Peak</th>"
            << "<th>Radiant</th>"
            << "<th>Notes</th>"
            << "</tr>";


        for (
            const MeteorShowerStatus& shower :
            _state.sky.meteorShowers
        )
        {
            html
                << "<tr>"
                << "<td><strong>"
                << htmlEscape(
                    shower.name
                )
                << "</strong></td>"
                << "<td>"
                << std::fixed
                << std::setprecision(0)
                << shower.zhr
                << "</td>"
                << "<td>";


            if (
                shower.daysToPeak ==
                0
            )
            {
                html
                    << "Today";
            }
            else if (
                shower.daysToPeak >
                0
            )
            {
                html
                    << "In "
                    << shower.daysToPeak
                    << (
                        shower.daysToPeak == 1
                            ? " day"
                            : " days"
                    );
            }
            else
            {
                html
                    << (
                        -shower.daysToPeak
                    )
                    << (
                        shower.daysToPeak == -1
                            ? " day ago"
                            : " days ago"
                    );
            }


            html
                << "</td>"
                << "<td>";


            if (
                shower.radiantUp
            )
            {
                html
                    << std::setprecision(0)
                    << shower.radiantAltitudeDeg
                    << "&deg; alt / "
                    << shower.radiantAzimuthDeg
                    << "&deg; az";
            }
            else
            {
                html
                    << "below horizon";
            }


            html
                << "</td>"
                << "<td>";


            if (
                !shower.radiantUp
            )
            {
                html
                    << "Radiant not up right now";
            }
            else if (
                shower.moonInterferes
            )
            {
                html
                    << "Moon will wash out fainter meteors";
            }
            else
            {
                html
                    << "Good conditions";
            }


            html
                << "</td>"
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