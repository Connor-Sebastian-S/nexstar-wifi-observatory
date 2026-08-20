# NexStar WiFi Observatory

A Raspberry Pi hub that turns a Celestron NexStar mount into a
WiFi-controllable smart telescope: a web dashboard for planning and
GOTO from your phone, a Stellarium-compatible telescope server for
planetarium software, GPS-derived time/location, environment
sensing, full session logging, and a growing set of astronomy
features layered on top &mdash; recommended targets, planets, meteor
showers, a "best time tonight" planner, and a 3D heatmap of your own
observing history.

It's built for one specific setup (a NexStar hand controller talking
serial over USB, a GPS module, an SHT3x environment sensor behind an
I2C mux), but the pieces are modular enough to adapt to a different
mount or sensor set if you're working from this as a starting point.


## What it's for

A NexStar hand controller alone doesn't know what's actually worth
looking at right now, doesn't log where you've pointed, and can't be
driven from a laptop or phone without a direct serial/USB link. This
project sits between the hand controller and the outside world and
fixes all three:

- It figures out what's above the horizon, bright enough, and far
  enough from the Moon to be worth a look &mdash; and ranks it.
- It logs every session to disk, second by second, so you can see
  where you've actually spent your time.
- It exposes the mount over WiFi, both as a simple web dashboard and
  as a standard Stellarium-protocol telescope server, so any
  planetarium app on the same network can drive it.


## How it fits together

```
                     ┌─────────────────────────────────────┐
                     │            Raspberry Pi              │
                     │                                       │
  NexStar HC ──USB──►│  NexStar.cpp   (hand-controller       │
                     │                 serial protocol)      │
                     │        │                               │
  GPS module ──UART─►│  GPS.cpp        │                      │
                     │        │        │                      │
  SHT3x/I2C mux ────►│  EnvironmentSensor.cpp                 │
                     │        │        │                      │
                     │        ▼        ▼                      │
                     │      main.cpp  (orchestrator loop)      │
                     │        │        │                      │
                     │        │        ├──► SkyCatalogue.cpp   │
                     │        │        │    (targets, planets, │
                     │        │        │     meteor showers,   │
                     │        │        │     best-time-tonight)│
                     │        │        │                      │
                     │        │        ├──► Recorder.cpp       │
                     │        │        │    (blackbox + journal)│
                     │        │        │         │             │
                     │        │        │         ▼             │
                     │        │        │    SkyHistory.cpp      │
                     │        │        │    (heatmap aggregator)│
                     │        │        │                      │
                     │        ▼        ▼                      │
                     │  WebServer.cpp        StellariumServer.cpp
                     │   (dashboard,           (telescope        │
                     │    JSON API,             protocol on      │
                     │    /sky-map)             port 10001)      │
                     └────────┬──────────────────────┬─────────┘
                              │                       │
                        phone / laptop         Stellarium, SkySafari,
                        web browser            or any Stellarium-
                                                protocol client
```

`main.cpp` runs a single-threaded loop that polls the mount, GPS, and
environment sensor on their own intervals, keeps one shared
`ObservatoryState` up to date, and feeds it to both `WebServer` and
`StellariumServer` every tick. Everything is synchronous by design
&mdash; there's no threading to reason about, which matters on a
resource-constrained Pi talking to real hardware over serial and I2C.


## Hardware this expects

| Component | Interface | Notes |
|---|---|---|
| Celestron NexStar hand controller | USB serial (Prolific adapter) | 9600 baud, NexStar HC protocol |
| GPS module | UART (`/dev/serial0`) | 115200 baud, used for time sync and lat/long |
| SHT3x temperature/humidity sensor | I2C via TCA9548A mux (`0x70`) | sensor at `0x44`; feeds dew-point display |

Device paths and addresses are constants at the top of `main.cpp` if
your wiring differs.


## Software layout

| File | Role |
|---|---|
| `main.cpp` | Orchestrator: the poll loop, safety limits, wiring every module together |
| `nexstar/NexStar.cpp` / `.h` | NexStar hand-controller serial protocol (GOTO, tracking, alignment status) |
| `pi/compat/` | Minimal Arduino-style `HardwareSerial`/`Arduino.h` shims, so the NexStar driver runs unmodified on Linux |
| `pi/GPS.cpp` / `.h` | NMEA GPS parsing &mdash; fix status, lat/long, UTC time |
| `pi/I2CBus.cpp`, `EnvironmentSensor.cpp` | I2C mux + SHT3x driver for temperature/humidity |
| `pi/SkyCatalogue.cpp` / `.h` | Everything astronomy: DSO candidate scoring, Sun/Moon/planet positions, meteor shower almanac, best-time-tonight |
| `pi/SkyHistory.cpp` / `.h` | Incrementally aggregates blackbox logs into an alt/az heatmap grid |
| `pi/Recorder.cpp` / `.h` | Per-session blackbox CSV + human-readable journal logging |
| `pi/StellariumServer.cpp` / `.h` | Stellarium telescope-control protocol server (port 10001) |
| `pi/WebServer.cpp` / `.h` | Dashboard, JSON API, and the `/sky-map` 3D heatmap &mdash; hand-rolled HTTP over raw sockets, no framework |
| `catalogue/parse_catalog.py` | One-time: converts Stellarium's binary DSO catalogue into the SQLite database the Pi queries |
| `catalogue/import_names.py` | Attaches common names ("Elephant's Trunk Nebula") to catalogue entries |


## Features

**Recommended targets** &mdash; queries the DSO catalogue for
objects above a minimum altitude, within the magnitude limit, and
far enough from the Moon, then scores and ranks them by magnitude,
altitude, how close to the zenith they are, and Moon separation.
Shown even when it isn't dark yet, so you can see what's coming up
as the sky fades &mdash; the page just relabels itself "Everything
currently up" instead of hiding the list.

**Planets** &mdash; Mercury through Neptune, computed analytically
from Keplerian orbital elements (the same low-precision approach
Paul Schlyter's classic formulas use), good to a few arcminutes.
Sun and Moon were already handled this way; planets slot into the
same scoring pipeline as DSOs.

**Meteor showers** &mdash; a built-in almanac of the dozen reliable
annual showers, checked against today's date, with the radiant's
live alt/az, days to peak, and a note on whether the Moon will wash
out fainter meteors.

**Best time tonight** &mdash; every recommended target also reports
the altitude it'll peak at over roughly the next 12 hours and when,
found by sampling its position every few minutes rather than solving
for transit analytically.

**Sky history heatmap** (`/sky-map`) &mdash; a 3D dome you're
standing inside, textured with a heatmap of everywhere the mount has
actually pointed, built from the blackbox logs. `SkyHistory.cpp`
bins samples into a 5&deg;&times;5&deg; alt/az grid and remembers how
far into each log file it's already read, so a page load only costs
whatever's been logged since the last one &mdash; not a full rescan
of months of history.

**GOTO / abort** from the dashboard, with the mount's current
alt/az, target list, and Moon/Sun state refreshing every few
seconds.


## The catalogue

The DSO catalogue is built once, offline, from Stellarium's own
binary nebula catalogue via `catalogue/parse_catalog.py`, then
loaded into a small SQLite database the Pi queries at runtime.
`import_names.py` adds informal names on top of the raw NGC/IC/M/etc
designations.

One catalogue quirk worth knowing if you regenerate it: Stellarium
stores an *opacity* value (not a real magnitude) in the same field
used for magnitude on dark nebulae (`object_type == 12`, Barnard's
catalogue and similar). Left alone, tiny opacity numbers get read as
implausibly bright magnitudes and crowd out every real target &mdash;
`SkyCatalogue.cpp`'s candidate query explicitly excludes that object
type for exactly this reason.


## Building

```bash
cd ~/telescope-nexstar-wifi/pi
rm -rf build
cmake -S . -B build
cmake --build build
sudo systemctl restart telescopehub
```

Produces the `telescopehub` executable in `pi/build/`. Requires
`libsqlite3-dev` and a C++17 compiler; no other external
dependencies. `main.cpp`'s device paths, ports, and the recorder/
catalogue directories are compile-time constants near the top of the
file if your setup differs from the defaults above.


## Known limitations

- Planet/Sun/Moon positions are low-precision analytic ephemerides
  (arcminute-level) &mdash; fine for pointing a GoTo mount, not for
  narrow-field astrometry.
- Meteor shower radiant coordinates are fixed per shower rather than
  drifting across the active window the way real radiants do.
- "Best time tonight" treats a target's RA/Dec as fixed across the
  12-hour search window; accurate for DSOs, a reasonable
  approximation for planets over one night.
- The sky-map's heatmap reflects logged history only &mdash; there's
  no live "where is the mount pointing right now" marker yet (would
  need a small JSON status endpoint; not implemented).
- `WebServer.cpp` is a hand-rolled single-threaded HTTP server, not
  a general-purpose one &mdash; it's built to serve this dashboard,
  not arbitrary traffic.
