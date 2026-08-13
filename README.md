# Performance Tracker

A Geode mod for Geometry Dash that records how the game actually performs over
time, and shows the history in a stats panel instead of just a live FPS counter.

* **Geode**: 5.9.0
* **Geometry Dash**: 2.2081

## What it records

Every frame, the mod measures the real frame time with `steady_clock` (not GD's
delta time, which is smoothed and clamped and would hide stutters). Every few
seconds those frames are collapsed into one data point:

| Field | Meaning |
|---|---|
| avg / min / max FPS | over the window |
| 1% low | average frame time of the slowest 1% of frames in the window |
| CPU (game) | this process, % of the whole machine |
| CPU (system) | whole system, % |
| RAM | process working set, MB |
| FPS target | GD's configured target, used for the "under target" stat |
| context | menus, editor, playing, paused |
| level | level ID, plus practice / test mode flags |

A window is closed early whenever you enter or leave a level, so a single data
point never mixes two levels together.

## The stats panel

Button in the bottom row of the main menu. Ranges: **Today / 7 days / 30 days /
All time / Custom**, and four tabs:

* **Overview** - tracked time, average FPS and 1% low, best and worst FPS with
  their timestamps, time spent under 30 / 60 FPS and under your FPS target,
  CPU and RAM averages and peaks, time split between levels and the editor.
* **Graph** - FPS, CPU (game), CPU (system) or RAM over the range, with the
  min/max band behind the average line and hour labels on the axis.
* **Levels** - every level you played in the range, sorted heaviest first
  (lowest average FPS), with its 1% low, its worst dip and time played.
* **Sessions** - one row per game launch, with duration and average FPS.
  Sessions that never reached a clean exit are flagged as probable crashes.

Plus **Export CSV** and **Open folder**.

## Where the data lives

`<Geode save dir>/mods/mifu.performance-tracker/perfdata/`

* `YYYY-MM-DD.bin` - one append-only file per local day, 8 byte header
  (`MPFL` + format version + record size) then 40 byte records, little endian.
  Roughly 29 KB per hour of play at the default 5 second interval.
* `levels.tsv` - level ID to name. Local levels with no server ID get a stable
  negative ID derived from their name so they can still be told apart.
* `sessions.tsv` - one line per launch.
* `exports/` - CSV exports.

## Settings

Sample interval, disk flush interval, whether to track outside of levels,
history retention (auto-delete older files), the optional in-game overlay
(corner, contents, scale) and the main menu button.

## Platform support

FPS tracking works everywhere. CPU and RAM counters are implemented for
**Windows** (`GetProcessTimes`, `GetSystemTimes`, `K32GetProcessMemoryInfo`
resolved dynamically so nothing extra has to be linked) and for
**Android/Linux** (`/proc/self/stat`, `/proc/stat`, `/proc/self/statm`).
On **macOS and iOS** they are reported as unavailable and the UI shows `-`
for them - that code path was not written blind on purpose.

## Known limitation

The 1% low shown for a long range is the average of each window's 1% low, not a
true global percentile: raw per-frame times are not kept on disk. The panel
labels it as such rather than presenting an approximation as an exact figure.

## Building

```
geode build
```

Requires `GEODE_SDK` to point at a 5.9.0 SDK and a C++23 compiler.
