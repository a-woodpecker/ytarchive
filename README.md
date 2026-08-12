# YT Archive

A C++/Qt6 desktop application for archiving video channels. It lists a
channel's uploads in a YouTube-style grid, lets you tick the videos you want,
and downloads them into a per-channel folder tree indexed by a SQLite catalog.
Every saved file is stamped with its **upload** date, not its download date.

## What it does

- Add channels by URL or handle (`@channelname`, `youtube.com/@name`, `/channel/UC…`)
- Loads the full upload list without downloading anything
- Card grid with thumbnails, durations, dates, view counts, and per-video state
- Per-video checkboxes, plus "select all" / "select not archived" / "download everything missing"
- Concurrent downloads with live progress, speed and ETA
- Left navigation panel listing every channel in the catalog
- Search and filter by archive state
- Dark and light themes, switchable without restarting
- Saves sidecar metadata: `.info.json`, thumbnail, description, subtitles, optionally comments

## Requirements

| Dependency | Why |
|---|---|
| Qt 6.2+ (Widgets, Sql, Network) | GUI, SQLite driver, thumbnail fetching |
| CMake 3.19+ and a C++17 compiler | build |
| **yt-dlp** | all YouTube interaction |
| **ffmpeg** | merging separate video/audio streams |

`yt-dlp` and `ffmpeg` are runtime dependencies invoked as subprocesses. If they
aren't on `PATH`, set their full paths in **File → Preferences → Locations**.

```bash
# Debian / Ubuntu
sudo apt install qt6-base-dev libqt6sql6-sqlite qt6-wayland \
                 cmake ninja-build g++ ffmpeg
pipx install yt-dlp        # not the distro package: see the note below

# macOS
brew install qt cmake yt-dlp ffmpeg

# Windows
# Qt via the online installer; yt-dlp.exe and ffmpeg.exe anywhere, then point
# Preferences at them.
```

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ytarchive
```

On macOS you may need `-DCMAKE_PREFIX_PATH=$(brew --prefix qt)`.

## Building on Linux

Verified on a Debian-family system with Qt 6.4.

```bash
sudo apt install qt6-base-dev libqt6sql6-sqlite qt6-wayland \
                 cmake ninja-build g++ ffmpeg
```

What each package is for, because two of them are runtime plugins that fail in
confusing ways when absent:

| Package | Why |
|---|---|
| `qt6-base-dev` | Widgets, Network, headers, CMake config |
| `libqt6sql6-sqlite` | the SQLite driver. Without it the catalog will not open |
| `qt6-wayland` | native Wayland platform plugin. Without it Qt falls back to XWayland |
| `cmake ninja-build g++` | build tools |
| `ffmpeg` | merges separate video and audio streams |

**Do not install `yt-dlp` from apt.** The packaged version lags, and a stale
yt-dlp fails against YouTube in ways that look like bugs in this program:

```bash
sudo apt install pipx && pipx install yt-dlp
# keep it current:  pipx upgrade yt-dlp
```

Then build with the bundled preset:

```bash
cmake --preset unix
cmake --build --preset unix
./build/unix/ytarchive
```

Or without presets:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Wayland notes

On KDE Plasma under Wayland the app runs natively once `qt6-wayland` is
installed. To check which backend is actually in use, or to force one:

```bash
QT_LOGGING_RULES="qt.qpa.*=true" ./build/unix/ytarchive 2>&1 | head
QT_QPA_PLATFORM=wayland ./build/unix/ytarchive     # force native Wayland
QT_QPA_PLATFORM=xcb     ./build/unix/ytarchive     # force XWayland
```

Wayland does not let a client position its own window, so saved window geometry
is applied by the compositor rather than the application. Size is generally
honoured; position may not be. Everything else, including the dock layout, is
restored normally.

## Building on Windows

### 1. Install the toolchain

- **Visual Studio 2026** (Community is fine) with the *Desktop development with
  C++* workload. Visual Studio 2022 works too — see the note on generators below.
- **Qt 6.5+** via the [Qt Online Installer](https://www.qt.io/download-qt-installer).
  Under *Qt > Qt 6.x*, tick **MSVC 2022 64-bit**.

Two things about that Qt selection that look wrong but aren't:

- **Pick the MSVC 2022 kit even on Visual Studio 2026.** Qt does not yet ship a
  prebuilt `msvc2026_64` package. MSVC's toolset (14.5x, shipped as v145 in
  VS 2026) preserves binary compatibility with everything back to VS 2015, so
  the `msvc2022_64` libraries link correctly against a v145 build.
- The Sql and Network modules and the SQLite driver are part of Qt Base, so no
  extra components need selecting.

Note the install path — it looks like `C:\Qt\6.11.1\msvc2022_64`.

### 2. Configure and build

The repository ships a `CMakePresets.json` with the paths already wired up.
**Open it and edit `CMAKE_PREFIX_PATH` under `windows-base` to match your Qt
install path** — that one line is the only thing you need to change.

Then, from the *x64 Native Tools Command Prompt* for your VS version:

```bat
cmake --preset windows-ninja
cmake --build --preset windows-ninja
```

The executable lands in `build\windows-ninja\ytarchive.exe`.

Prefer a `.sln` to debug in the IDE? Use a Visual Studio generator preset
instead — `windows-vs2026` or `windows-vs2022`:

```bat
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026
```

These are *multi-config* generators: the build type is chosen at build time, not
configure time, so the binary lands in `build\windows-vs2026\Release\ytarchive.exe`.

You can also just open the project folder in Visual Studio (*File > Open >
Folder*). VS reads `CMakePresets.json` directly and offers the presets in the
configuration dropdown.

#### If the VS 2026 generator isn't recognised

The `Visual Studio 18 2026` generator name was added in **CMake 4.2**. Visual
Studio 2026 has shipped with older CMake builds bundled, so if
`cmake --preset windows-vs2026` fails with an unknown-generator error, check
what you have:

```bat
cmake --version
cmake --help | findstr 2026
```

Either install CMake 4.2+ from [cmake.org](https://cmake.org/download/) and put
it ahead of the bundled copy on `PATH`, or sidestep the question entirely by
using the `windows-ninja` preset, which doesn't name a VS version at all. Ninja
is the faster option for incremental builds regardless.

#### "Cannot find source file: .../build/windows-vs2026/src/main.cpp"

The path in that message points inside the *build* directory, which is
misleading. When a relative source path isn't present in the source tree, CMake
falls back to looking in the binary directory and reports that second location
in the error. The real meaning is simply: **`src\main.cpp` is not where the
build expects it.**

Almost always this is a layout problem. Check from the project root:

```bat
dir /b
dir /b src
```

You should see `CMakeLists.txt`, `CMakePresets.json`, `README.md`, `resources\`
and `src\` at the top level, and 27 `.h`/`.cpp` files inside `src\`. Common
causes:

- Files downloaded individually and left flat in one folder instead of under
  `src\` and `resources\`.
- A browser appending suffixes — `main (1).cpp`, or `MainWindow.cpp.txt` if the
  file was saved as plain text. Windows hides known extensions by default, so
  turn on *View > File name extensions* in Explorer before checking.
- An extra nesting level, e.g. `youtube-downloader\ytarchive\src\` while
  CMake is being run from `youtube-downloader\`.

The build only fails on the first missing file, so fixing `main.cpp` alone may
just move the error to the next one. Verify all 27 are present in `src\`.

#### "cl.exe is not able to compile a simple test program" (LNK2001 / LNK4272)

If the configure step fails with unresolved externals like `_RTC_InitBase` or
`mainCRTStartup`, scan the log for `LNK4272` warnings. They name the real
problem:

```
...\lib\10.0.26100.0\um\x86\kernel32.lib :
    warning LNK4272: library machine type 'x86' conflicts with target machine type 'x64'
```

The compiler being invoked is the x64 one (`Hostx64\x64\cl.exe`) but the `LIB`
environment variable points at x86 libraries. This happens when the build is
run from the plain **Developer Command Prompt**, which targets x86 by default,
rather than the x64 one.

The Ninja preset sets `"strategy": "external"`, meaning CMake trusts the shell's
environment to already be correct. Two ways to fix it:

- **Use the right prompt.** Start *x64 Native Tools Command Prompt for VS 2026*
  from the Start menu — not *Developer Command Prompt for VS 2026*.
- **Or use a Visual Studio generator preset**, which sets the architecture
  itself and works from any prompt, including a plain `cmd`:

  ```bat
  cmake --preset windows-vs2026
  cmake --build --preset windows-vs2026
  ```

Either way, **delete the failed build directory first**. `CMakeCache.txt`
records the broken compiler detection and will keep replaying it:

```bat
rmdir /s /q build\windows-ninja
```

#### On an ARM64 Windows device

The Visual Studio generators default to the *host* architecture, so on an ARM64
machine a bare configure targets ARM64 and then fails to link the x64 Qt
libraries. The presets pin `x64` explicitly, which is what you want unless you
have built Qt for ARM64 yourself.

### 3. Make it runnable outside the build tree

`windeployqt` copies the Qt DLLs, the platform plugin and the SQLite driver
next to the binary. Without this step the program will not start:

```bat
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe build\windows-ninja\ytarchive.exe
```

### 4. Runtime dependencies

The quickest route is winget:

```bat
winget install -e --id Gyan.FFmpeg
winget install -e --id yt-dlp.yt-dlp
```

Close and reopen your terminal, then confirm both are on `PATH`:

```bat
ffmpeg -version
ffprobe -version
yt-dlp --version
```

If those all respond, leave the paths in **File > Preferences > Locations**
empty — the defaults will find them.

FFmpeg ships no official Windows installer, so winget pulls a community build
(gyan.dev). That package has a known intermittent problem where it fails to add
itself to `PATH`. If `ffmpeg -version` isn't recognised after reopening the
terminal, install it manually instead: download `ffmpeg-release-full.7z` from
<https://www.gyan.dev/ffmpeg/builds/>, extract it to somewhere permanent such as
`C:\ffmpeg`, and either add `C:\ffmpeg\bin` to your `PATH` or point
Preferences directly at `C:\ffmpeg\bin\ffmpeg.exe`.

**Keep `ffprobe.exe` beside `ffmpeg.exe`.** yt-dlp needs both, and it looks for
ffprobe in the same folder as the ffmpeg you gave it. They ship together in
every build, so the only way to break this is to copy just one of them out.

### Using MinGW instead of MSVC

If you picked the MinGW kit in the Qt installer rather than MSVC, use the
compiler that ships with Qt and build with Ninja:

```bat
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\mingw_64
cmake --build build
```

Do not mix kits: a Qt built for MSVC cannot be linked by MinGW or vice versa.

### Windows-specific notes

- **Long paths.** Channel folder + dated filename can exceed the legacy 260
  character limit and downloads will fail with a confusing yt-dlp error. Either
  keep the archive folder shallow (`D:\Archive` rather than a deep path under
  `Documents`), or enable long path support:
  `Computer Configuration > Administrative Templates > System > Filesystem >
  Enable Win32 long paths`.
- **No console window.** The target is built with `WIN32_EXECUTABLE`, so there
  is no terminal behind the GUI. yt-dlp's output is captured in the
  *yt-dlp output* dock instead.
- **Timestamps.** The Windows path uses `SetFileTime`, which sets both the
  created and modified times, so Explorer's *Date created* column agrees with
  the upload date rather than showing when you ran the download.
- **Antivirus.** Real-time scanning of freshly written multi-gigabyte files can
  slow downloads noticeably. Excluding the archive folder is worth doing if
  throughput looks wrong.

## How the archive is laid out

```
<Archive folder>/
├── catalog.db                          SQLite index
├── .cache/thumbnails/                  grid thumbnails
└── Nautical Restoration Log [UCxxx…]/
    ├── .incomplete/                    partial downloads, auto-cleared
    ├── 2024-03-11 [dQw4w9WgXcQ] Steam-Bending the New Frames.mkv
    ├── 2024-03-11 [dQw4w9WgXcQ] Steam-Bending the New Frames.info.json
    ├── 2024-03-11 [dQw4w9WgXcQ] Steam-Bending the New Frames.en.vtt
    └── 2024-03-11 [dQw4w9WgXcQ] Steam-Bending the New Frames.jpg
```

**Media lives on disk, not in the database.** The catalog stores metadata and
paths. Two reasons: multi-gigabyte BLOBs make SQLite slow and fragile to back
up, and an archive whose contents can only be read by one program isn't much of
an archive. Delete `catalog.db` and you still have a browsable, dated video
library; the program rebuilds its index by re-syncing.

## About the timestamps

This is the part with a real subtlety worth knowing about.

A *flat* channel listing — the fast one that fetches hundreds of videos in a few
requests — doesn't carry exact upload dates. The app passes
`--extractor-args youtubetab:approximate_date` to get estimates, and shows those
with a `~` prefix in the grid.

Once a video is actually downloaded, `.info.json` contains the authoritative
`timestamp`/`upload_date`. At that point the app:

1. reads the real date from the sidecar JSON,
2. writes it into the catalog and drops the "approximate" flag,
3. calls `utimes()` / `SetFileTime()` on the media file **and every sidecar
   sharing its basename**, so the whole record carries one consistent date.

`--no-mtime` is passed to yt-dlp deliberately, so it doesn't overwrite this with
the HTTP `Last-Modified` header. The filename template also leads with
`%(upload_date>%Y-%m-%d)s`, so files sort chronologically even in a plain
directory listing.

## Packaging a Windows installer

`packaging\build-installer.ps1` builds, stages the Qt runtime and compiles an
Inno Setup installer in one pass. It needs [Inno Setup 6](https://jrsoftware.org/isdl.php).

```powershell
.\packaging\build-installer.ps1 -QtDir C:\Qt\6.11.1\msvc2022_64

```

The result is `packaging\dist\YTArchive-<version>-setup.exe`. The version
is read from `CMakeLists.txt`, so the installer filename and the binary's own
update check can never disagree.

The installer adds a wizard page after the install-location step asking where
the archive should live, defaulting to `%USERPROFILE%\Videos\Archive`. The
folder is created during the wizard rather than after installing, so a bad path
is caught while it can still be corrected, and it is **left in place on
uninstall** — uninstalling removes the program, never the archive.

`PrivilegesRequired=lowest` means administrator rights are optional; the user
can still choose a machine-wide install.

### How the chosen folder reaches the application

The installer writes `defaults.ini` beside the executable, and `Settings::load()`
reads it only when no user setting exists yet. The registry would have been the
obvious choice and is the wrong one: an elevated installer writing `HKCU` writes
the *administrator's* hive, not that of the person who will run the program.

Paths are stored with forward slashes, because QSettings treats a backslash as
an escape character when reading INI values. Qt accepts forward slashes on
Windows throughout.

## Update checking

The application polls the GitHub Releases API for
[`a-woodpecker/ytarchive`](https://github.com/a-woodpecker/ytarchive). Forks can
repoint it at configure time:

```
cmake -S . -B build -DYTA_GITHUB_REPO=you/yourfork
```

Until the first release is published, a check reports that the repository has no
releases yet. That is the expected response to GitHub's 404, not an error in the
build.

A background check runs at most once every 24 hours, a few seconds after launch;
*Help > Check for updates* forces one. When a newer release exists, a banner
appears offering the installer download, the release notes, and *Skip this
version*. Drafts and pre-releases are ignored.

**Nothing is ever downloaded or installed automatically.** Silently swapping the
binary underneath a program whose job is not losing data is a poor trade, so the
update path always ends at a link the user clicks.

To publish an update: bump `VERSION` in `CMakeLists.txt`, run the packaging
script, and attach the resulting `.exe` to a GitHub release tagged `v<version>`.
The checker prefers an `.exe` asset and falls back to the release page. Version
comparison is numeric and tolerates a leading `v`, so `1.10.0` correctly beats
`1.9.0`.

## Source layout

| File | Role |
|---|---|
| `Database.*` | SQLite schema, upserts that never clobber download state |
| `YtDlp.*` | every yt-dlp argument, in one auditable place |
| `ChannelSync.*` | async channel listing via `QProcess` |
| `DownloadManager.*` | bounded concurrent queue, progress parsing, timestamp stamping |
| `VideoModel.*` | list model + search/state filter proxy |
| `VideoCardDelegate.*` | the painted video card |
| `ThumbnailCache.*` | memory + disk thumbnail cache, deduplicated fetches |
| `UpdateChecker.*` | GitHub Releases polling and version comparison |
| `MainWindow.*` | layout and wiring |
| `packaging/` | Inno Setup script and one-shot build script |
| `Theme.*` | the two colour schemes, and everything the cards paint |
| `resources/style.qss` | dark theme |
| `resources/style-light.qss` | light theme |

## Themes

**File > Preferences > Locations > Theme** offers Dark, Light, and *Match the
system*. Changes apply immediately; no restart.

A theme lives in two places that must be edited together:

- `resources/style.qss` / `resources/style-light.qss` style everything Qt draws.
- `Theme.cpp` holds the colours for everything painted by hand, which is the
  video cards. `VideoCardDelegate` reads them through `Theme::palette()`, so a
  card is never left dark on a light canvas.

Switching themes explicitly repaints the grid. Restyling alone would not, since
the delegate paints outside the stylesheet.

*Match the system* needs Qt 6.5 or newer for `QStyleHints::colorScheme()`. On
older Qt the option is shown but disabled rather than silently doing nothing,
and Dark remains the default.

Two colours are deliberately identical in both themes: the red accent, and the
duration badge on each thumbnail. The badge sits on top of the artwork rather
than the canvas, so a light badge would disappear against pale thumbnails.

## Known limitations

Worth reading before you trust this with an archive you care about.

**Coverage**
- Only the `/videos` tab is listed. Shorts, live streams and premieres live on
  separate tabs and are not enumerated, so a Shorts-heavy channel will look
  nearly empty.
- Playlists and individual video URLs cannot be added — channels only.
- `channels.avatar_path` exists in the schema but is never populated, so the
  navigation panel shows no channel icons.
- Descriptions are usually blank until a video is downloaded; a flat listing
  does not carry them.

**Scale**
- Catalog writes happen on the GUI thread. Syncing a channel with tens of
  thousands of uploads runs two queries per video and will visibly freeze the
  window for the duration.
- The entire video list for the selected channel is held in memory and in the
  model at once. There is no pagination.
- Loaded videos accumulate in memory during a sync; only the parse is streamed.
- The on-disk thumbnail cache grows without bound and is never pruned.

**Preservation gaps**
- No checksums. Nothing detects silent corruption or a truncated file that
  yt-dlp nonetheless exited cleanly on.
- Success is inferred from yt-dlp's exit code and the file existing. The media
  is never probed for playability or duration.
- "Removed from YouTube" is not distinguished from "download failed" — both
  land in the Failed state.
- Re-downloading at a higher quality is not supported. *Forget the downloaded
  copy* clears the catalog entry but leaves the old file, so you get duplicates.
- Absolute paths are stored, so moving the archive folder makes every entry
  report as missing. There is no re-basing tool.

**Dependency on yt-dlp**
- Every listing and download shells out to yt-dlp. If YouTube changes something
  and your yt-dlp is stale, everything fails at once with opaque errors. There
  is no version check or update prompt — update it regularly and independently.
- `--extractor-args youtubetab:approximate_date` is extractor-specific and may
  change or disappear.
- Error text is extracted heuristically from the last `ERROR:` line of stderr.
- Bulk downloading may trigger throttling or bot checks. Beyond yt-dlp's own
  retries there is no backoff, and no configurable delay between videos.

**Timestamps**
- For premieres and streams the recorded time is the publication timestamp,
  which may not be when the content was actually produced.
- When only a date (no time) is available, it is anchored to UTC midnight, so
  local-time display can show the previous day.

**Interface**
- Checkboxes respond only to a mouse click on the box itself; there is no
  keyboard toggle and no shift-click range selection.
- No overall queue progress, only per-video.
- Sorting is fixed at newest-first, and search matches titles only.
- Strings are wrapped in `tr()` but no translations are shipped.

## Notes and behaviour

- **Sync is additive.** Re-syncing a channel adds new videos and refreshes
  titles/view counts, but never forgets a downloaded file. Videos removed from
  YouTube stay in your catalog — which is usually what you want.
- **Missing files are detected** at startup and shown with a "File missing"
  badge rather than silently reappearing as un-downloaded.
- **Resuming works**: `--continue` plus a persistent `.incomplete` directory
  means an interrupted download picks up where it stopped.
- Shorts and live streams live on separate channel tabs; the current sync only
  reads `/videos`. Adding them means listing `/shorts` and `/streams` too.
- Comments are off by default — they can add several minutes per video.
- Cookie options exist for material your own account can see (age-restricted,
  unlisted, memberships) but an anonymous visitor cannot.

## Extending it

Obvious next steps if you want to keep building:

- **Checksums.** Add a `sha256` column and hash on completion — the single most
  useful addition for a preservation archive, since it turns "I have the file"
  into "I can prove the file is intact."
- **Scheduled syncs.** A `QTimer` plus a "watch this channel" flag gives you
  automatic capture of new uploads.
- **Export.** Writing catalog rows to CSV or JSON-LD makes the archive
  ingestible by other tools.
- **Shorts/streams tabs**, per the limitation above.
