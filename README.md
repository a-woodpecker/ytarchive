# YT Archive

A C++/Qt6 desktop application for archiving video channels. It lists a channel's
uploads in a card grid, lets you tick the videos you want, and downloads them
into a per-channel folder tree indexed by a SQLite catalog. Every saved file is
stamped with its **upload** date, not its download date.

Downloading is performed by [yt-dlp](https://github.com/yt-dlp/yt-dlp); this
program is the catalog, the interface and the archival discipline around it.

## What it does

- Add channels by URL or handle (`@channelname`, `youtube.com/@name`, `/channel/UC…`)
- Loads the full upload list without downloading anything
- Card grid with thumbnails, durations, dates, view and like counts, and
  per-video state; cards name their channel in the combined view
- Per-video checkboxes, plus *select all*, *select not archived*, *download everything missing*
- Concurrent downloads with live progress, speed and ETA; cancel one or all
- Retry a single failed video, or every failure in the current view at once
- Left navigation panel listing every channel in the catalog
- Search and filter by archive state
- View a downloaded video's description and threaded comments from its context menu
- Dark and light themes, switchable without restarting
- Resizable panels; **View** toggles each one and resets the layout
- Saves sidecar metadata: `.info.json`, thumbnail, description, subtitles, optionally comments

## How the archive is laid out

```
<Archive folder>/
├── catalog.db                          SQLite index
├── .cache/thumbnails/                  grid thumbnails
└── Nautical Restoration Log [UCxxx…]/
    ├── .incomplete/                    partial downloads, resumed on retry
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

## Timestamps

This is the part with a real subtlety worth knowing about.

A *flat* channel listing - the fast one that fetches hundreds of videos in a few
requests - doesn't carry exact upload dates. The app passes
`--extractor-args youtubetab:approximate_date` to get estimates and shows those
with a `~` prefix in the grid.

Once a video is downloaded, its `.info.json` carries the authoritative
`timestamp` / `upload_date`. At that point the app:

1. reads the real date from the sidecar JSON,
2. writes it into the catalog and drops the "approximate" flag,
3. calls `utimes()` / `SetFileTime()` on the media file **and every sidecar
   sharing its basename**, so the whole record carries one consistent date.

`--no-mtime` is passed to yt-dlp deliberately, so it cannot overwrite this with
the HTTP `Last-Modified` header. The filename template leads with
`%(upload_date>%Y-%m-%d)s`, so files sort chronologically even in a plain
directory listing.

## Requirements

| Dependency | Why |
|---|---|
| Qt 6.3+ (Widgets, Sql, Network) | GUI, SQLite driver, thumbnail fetching |
| CMake 3.19+ and a C++17 compiler | build |
| **yt-dlp** | all interaction with the video service |
| **ffmpeg** (with `ffprobe`) | merging separate video and audio streams |
| **A JavaScript runtime** | yt-dlp needs one to sign media URLs |
| **yt-dlp-ejs** | the challenge solver the runtime executes |

yt-dlp and ffmpeg are runtime dependencies invoked as subprocesses. If they
aren't on `PATH`, set their full paths in **File > Preferences > Locations**.

**A JavaScript runtime is required, and it is a separate program from yt-dlp.**
Recent yt-dlp needs one to solve the service's challenges. Without it, channel
listings work normally and every download fails with HTTP 403 - a confusing
split, because the application looks healthy right up to the moment it saves
anything. Only `deno` is used automatically; any other runtime must be named
under **Preferences > Locations > JavaScript runtime**.

```bash
curl -fsSL https://deno.land/install.sh | sh     # recommended, no extra config
sudo apt install nodejs                          # then set the runtime to "node"
```

The application checks for one at startup and reports the path it found in the
Output tab, warning if none is found or if one is present but is not deno and
so would be ignored.

**A runtime that works in a terminal can still be invisible to the
application.** Deno's installer adds `~/.deno/bin` to `PATH` through your
shell's startup file, and a program launched from a desktop menu never reads
those. The same applies to yt-dlp installed with pipx, which lands in
`~/.local/bin`. Every process this application starts therefore runs with
`~/.deno/bin`, `~/.local/bin`, `~/.bun/bin`, `~/.cargo/bin`, `/usr/local/bin`
and their equivalents prepended to `PATH`, so a child yt-dlp can find a runtime
the desktop session did not expose.

**Install yt-dlp with pip or pipx, not from a package manager.** Two reasons.
Packaged versions lag badly, and a stale yt-dlp fails against the service in
ways that look like faults in this program - the single most common cause of
"it stopped working". And packaged builds bundle their own Python, so the
plugins described below cannot reach them.

```bash
pipx install yt-dlp     # Linux, macOS
pip install -U yt-dlp   # Windows
```

## Building on Linux

Verified on Debian-family systems with Qt 6.4.

```bash
sudo apt install qt6-base-dev libqt6sql6-sqlite qt6-wayland \
                 cmake ninja-build g++ ffmpeg
sudo apt install pipx && pipx install yt-dlp
```

Two of those are runtime plugins that fail confusingly when absent:
`libqt6sql6-sqlite` provides the SQLite driver, without which the catalog cannot
open at all; `qt6-wayland` provides the native Wayland platform plugin, without
which Qt silently falls back to XWayland.

The quickest build route is the bundled script, which checks every dependency
before configuring and names the missing package rather than failing partway
through a compile:

```bash
chmod +x packaging/build-linux.sh    # once, after cloning
./packaging/build-linux.sh
```

| Option | Effect |
|---|---|
| `--run` | launch the binary once it builds |
| `--clean` | delete the build tree first |
| `--debug` | debug build with symbols, in `build/linux-debug` |
| `--install-deps` | apt-get everything needed, then build |
| `-j4` | limit parallel jobs |

It also checks the runtime pieces afterwards and warns if yt-dlp came from apt.

Or use CMake directly:

```bash
cmake --preset unix && cmake --build --preset unix
./build/unix/ytarchive
```

### Wayland notes

The app runs natively under Wayland once `qt6-wayland` is installed. To check
which backend is in use, or to force one:

```bash
QT_LOGGING_RULES="qt.qpa.*=true" ./build/unix/ytarchive 2>&1 | head
QT_QPA_PLATFORM=wayland ./build/unix/ytarchive     # force native Wayland
QT_QPA_PLATFORM=xcb     ./build/unix/ytarchive     # force XWayland
```

Wayland does not let a client position its own window, so saved geometry is
applied by the compositor. Size is generally honoured; position may not be.
Dock layout is restored normally.

## Building on macOS

```bash
brew install qt cmake yt-dlp ffmpeg
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build -j
```

Untested; the POSIX timestamp path is shared with Linux.

## Building on Windows

### 1. Install the toolchain

- **Visual Studio 2026** (Community is fine) with the *Desktop development with
  C++* workload. Visual Studio 2022 works too.
- **Qt 6.5+** via the [Qt Online Installer](https://www.qt.io/download-qt-installer).
  Under *Qt > Qt 6.x*, tick **MSVC 2022 64-bit**.

Two things about that Qt selection look wrong but aren't:

- **Pick the MSVC 2022 kit even on Visual Studio 2026.** Qt does not yet ship a
  prebuilt `msvc2026_64` package. MSVC's toolset (v145 in VS 2026) preserves
  binary compatibility back to VS 2015, so the `msvc2022_64` libraries link
  correctly.
- Sql, Network and the SQLite driver are part of Qt Base, so nothing extra needs
  selecting.

Note the install path - it looks like `C:\Qt\6.11.1\msvc2022_64`.

### 2. Configure and build

`CMakePresets.json` has the paths wired up. **Edit `CMAKE_PREFIX_PATH` under
`windows-base` to match your Qt install** - that one line is the only change
needed.

From the *x64 Native Tools Command Prompt*:

```bat
cmake --preset windows-ninja
cmake --build --preset windows-ninja
```

The executable lands in `build\windows-ninja\ytarchive.exe`.

For a `.sln` to debug in the IDE, use `windows-vs2026` or `windows-vs2022`
instead. Those are *multi-config* generators, so the build type is chosen at
build time and the binary lands one level deeper, in
`build\windows-vs2026\Release\`. Visual Studio also reads `CMakePresets.json`
directly via *File > Open > Folder*.

### 3. Deploy the Qt runtime

`windeployqt` copies the Qt DLLs, the platform plugin, the SQLite driver, the
TLS backend and the image format plugins next to the binary. **Without this the
program will not start**, and a partial deployment produces subtler faults - a
missing TLS backend breaks thumbnails while downloads keep working.

```bat
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe build\windows-ninja\ytarchive.exe
```

### 4. Runtime dependencies

```bat
winget install -e --id Gyan.FFmpeg
winget install -e --id Python.Python.3.13
pip install -U yt-dlp
```

Reopen your terminal, then confirm `ffmpeg -version`, `ffprobe -version` and
`yt-dlp --version` all respond. If they do, leave the paths in Preferences empty.

**Install yt-dlp with pip, not winget.** The winget package is a standalone
`yt-dlp.exe` carrying its own bundled Python, and the two plugins this needs -
the challenge solver and the PO token provider - can then never be found:
`pip install` reports success while installing them into a different Python
entirely. Installing yt-dlp with pip puts everything in one place, and every
plugin instruction then works as written.

If you would rather not install Python, winget's yt-dlp is fine for downloads
that need no plugins. For the challenge solver, enable **Preferences >
Downloading > Allow yt-dlp to download its challenge solver**, which needs no
plugin. The PO token provider has no equivalent shortcut: its plugin archive has
to be extracted into `%APPDATA%\yt-dlp\plugins` by hand.

FFmpeg ships no official Windows installer, so winget pulls a community build
(gyan.dev) which intermittently fails to add itself to `PATH`. If it isn't
recognised, download `ffmpeg-release-full.7z` from
<https://www.gyan.dev/ffmpeg/builds/>, extract to somewhere permanent such as
`C:\ffmpeg`, and either add `C:\ffmpeg\bin` to `PATH` or point Preferences at
`C:\ffmpeg\bin\ffmpeg.exe`.

**Keep `ffprobe.exe` beside `ffmpeg.exe`.** yt-dlp needs both and looks for
ffprobe in the same folder as the ffmpeg you gave it.

### Troubleshooting

**`Visual Studio 18 2026` generator not recognised.** That generator name
arrived in **CMake 4.2**, and VS 2026 has shipped with older CMake bundled.
Check with `cmake --version` and `cmake --help | findstr 2026`. Either install
CMake 4.2+ from [cmake.org](https://cmake.org/download/) ahead of the bundled
copy on `PATH`, or use `windows-ninja`, which names no VS version at all and is
faster for incremental builds anyway.

**"Cannot find source file: .../build/windows-vs2026/src/main.cpp".** The path
points inside the *build* directory, which is misleading: when a relative source
path isn't in the source tree, CMake falls back to the binary directory and
reports that. It means `src\main.cpp` isn't where the build expects it. Check
`dir /b` and `dir /b src` - you should see 31 `.h`/`.cpp` files in `src\`.
Usual causes are files left flat instead of under `src\` and `resources\`, a
browser appending suffixes (`main (1).cpp`, or `MainWindow.cpp.txt` - turn on
*View > File name extensions* in Explorer), or an extra nesting level. The build
stops at the first missing file, so verify all of them.

**"cl.exe is not able to compile a simple test program" (LNK2001 / LNK4272).**
Scan the log for `LNK4272` warnings naming `...\um\x86\kernel32.lib` - the
compiler is x64 but `LIB` points at x86 libraries. This happens when building
from the plain **Developer Command Prompt**, which targets x86 by default. Use
the *x64 Native Tools Command Prompt*, or a Visual Studio generator preset,
which sets the architecture itself and works from any prompt. Either way,
`rmdir /s /q build\windows-ninja` first: `CMakeCache.txt` records the broken
detection and replays it.

**ARM64 devices.** Visual Studio generators default to the *host* architecture,
so a bare configure targets ARM64 and fails to link the x64 Qt libraries. The
presets pin `x64`.

### Using MinGW instead of MSVC

```bat
set PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;%PATH%
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\mingw_64
cmake --build build
```

Do not mix kits: a Qt built for MSVC cannot be linked by MinGW or vice versa.

### Windows-specific notes

- **Long paths.** Channel folder plus dated filename can exceed the legacy 260
  character limit, and downloads fail with a confusing yt-dlp error. Keep the
  archive folder shallow (`D:\Archive`, not a deep path under `Documents`), or
  enable *Computer Configuration > Administrative Templates > System >
  Filesystem > Enable Win32 long paths*.
- **No console window.** The target is built with `WIN32_EXECUTABLE`, so
  yt-dlp's output goes to the *Output* dock rather than a terminal.
- **Timestamps.** `SetFileTime` sets both created and modified times, so
  Explorer's *Date created* column agrees with the upload date.
- **Antivirus.** Real-time scanning of freshly written multi-gigabyte files
  slows downloads noticeably; excluding the archive folder is worthwhile.

## Packaging

### Debian package

```bash
chmod +x packaging/build-deb.sh
./packaging/build-deb.sh --install       # omit --install to only build it
```

The `.deb` lands in `packaging/dist/` and installs `/usr/bin/ytarchive` plus a
`.desktop` entry and hicolor icons, so it appears in the application launcher.

Library dependencies are computed with `dpkg-shlibdeps` rather than written by
hand, because Qt's package names differ across distributions. Two are added
manually because they are loaded at runtime and leave no reference in the
binary: `libqt6sql6-sqlite` and `ffmpeg`.

yt-dlp is `Suggests`, not `Recommends`, on purpose - apt installs
recommendations by default, and the packaged yt-dlp is old enough to fail
immediately.

#### Debian and Ubuntu from one package

Ubuntu renamed many library packages with a `t64` suffix during the 64-bit
`time_t` transition; Debian did not, on 64-bit architectures. `dpkg-shlibdeps`
can only name what is installed on the build machine, so each dependency is
rewritten as an alternative covering both spellings:

```
Depends: libqt6core6t64 (>= 6.4.0) | libqt6core6 (>= 6.4.0), ...
```

apt is satisfied by either, so one package installs on both.

**Naming is only half of it.** The binary is compiled against the glibc of the
machine that built it, and that cannot be papered over with alternatives. The
build prints the floor it produced:

```
==> Needs glibc >= 2.34
```

Debian 12 has 2.36, Ubuntu 24.04 has 2.39, Debian 13 has 2.41. A package built
on Debian 12 installs on all three; one built on Ubuntu 24.04 may be refused by
Debian 12 if it picked up a newer symbol. **Build on the oldest system you
intend to support** - which is why the release workflow builds the `.deb`
inside a `debian:12` container rather than on the Ubuntu runner.

The distribution codename stays in the filename and version
(`ytarchive_0.2.2~bookworm_amd64.deb`) as a record of where it was built.
Architecture is likewise whatever you built on, so an arm64 machine needs its
own build.

| Distribution | Qt | Status |
|---|---|---|
| Debian 12 bookworm | 6.4 | works; the reference build target |
| Debian 13 trixie | 6.8 | works, verified |
| Ubuntu 24.04 noble | 6.4 | works, verified |
| Ubuntu 22.04 jammy | 6.2 | **too old**, below the 6.3 minimum |

Check anything else with `apt policy qt6-base-dev | head -2`.

Uninstall with `sudo apt remove ytarchive`. Your archive folder and catalog live
outside the package and are never touched.

### Windows installer

`packaging\build-installer.ps1` builds, stages the Qt runtime and compiles an
Inno Setup installer in one pass. It needs
[Inno Setup 6](https://jrsoftware.org/isdl.php).

```powershell
.\packaging\build-installer.ps1 -QtDir C:\Qt\6.11.1\msvc2022_64
```

The result is `packaging\dist\YTArchive-<version>-setup.exe`. The version is
read from `CMakeLists.txt`, so the installer filename and the binary's update
check cannot disagree.

The installer adds a wizard page asking where the archive should live,
defaulting to `%USERPROFILE%\Videos\Archive`. The folder is created during the
wizard rather than after installing, so a bad path is caught while it can still
be corrected, and it is **left in place on uninstall**.
`PrivilegesRequired=lowest` makes administrator rights optional.

The chosen folder reaches the application through a `defaults.ini` written
beside the executable, which `Settings::load()` reads only when no user setting
exists. The registry would be the obvious choice and is the wrong one: an
elevated installer writing `HKCU` writes the *administrator's* hive, not that of
the person who will run the program. Paths are stored with forward slashes,
because QSettings treats a backslash as an escape character in INI values.

## Update checking

The application polls the GitHub Releases API for
[`a-woodpecker/ytarchive`](https://github.com/a-woodpecker/ytarchive). Forks can
repoint it with `-DYTA_GITHUB_REPO=you/yourfork` at configure time.

A background check runs at most once every 24 hours, a few seconds after launch;
*Help > Check for updates* forces one. When a newer release exists a banner
offers the download, the release notes and *Skip this version*. Drafts and
pre-releases are ignored. Until a first release is published, checks report that
the repository has no releases - the expected response to GitHub's 404.

The download offered is chosen for the machine asking. A release carries builds
for every platform, so the asset is filtered by extension, then by CPU
architecture, then by distribution codename where the name carries one:

| Running on | Offered |
|---|---|
| Windows x86_64 | `YTArchive-0.2.2-setup.exe` |
| Debian 12 amd64 | `ytarchive_0.2.2~bookworm_amd64.deb` |
| Ubuntu 24.04 amd64 | `ytarchive_0.2.2~noble_amd64.deb` |
| Debian arm64 | `ytarchive_0.2.2~bookworm_arm64.deb` |
| Anything with no matching build | the release page |

Each narrowing step is skipped when it would leave nothing, so a release with a
single unlabelled build is still offered rather than withheld.

**Nothing is ever downloaded or installed automatically.** Silently swapping the
binary underneath a program whose job is not losing data is a poor trade, so the
update path always ends at a link you click.

To publish: bump `VERSION` in `CMakeLists.txt`, commit, then push a tag
`v<version>`. The release workflow builds both platforms and attaches the
Windows installer and the `.deb`. It **refuses to run if the tag and
`CMakeLists.txt` disagree**, because a mismatch would tell every user an update
is permanently available, or never mention one at all.

Version comparison is numeric and tolerates a leading `v`, so `1.10.0`
correctly beats `1.9.0`.

Setting `YTA_UPDATE_API_BASE` overrides the API host. It exists for testing
against a mock server, and works for a GitHub Enterprise host too.

## Continuous integration

| Workflow | Trigger | What it does |
|---|---|---|
| `.github/workflows/build.yml` | push, pull request | Builds on Ubuntu and Windows, smoke-tests the binary, builds and installs the `.deb` |
| `.github/workflows/release.yml` | tag `v*` | Verifies the tag, builds the installer and package, publishes a release with both attached |

The Linux smoke test launches the binary with Qt's offscreen platform and
checks it is still running several seconds later - the application is a GUI
with no `--version` flag, so "it starts and stays up" is the meaningful signal.
The release job also installs the `.deb` and removes it again, so a broken
dependency list fails the build rather than a user's machine.

## Themes

**File > Preferences > Locations > Theme** offers Dark, Light and *Match the
system*. Changes apply immediately.

A theme lives in two places that must be edited together: the `.qss` files style
everything Qt draws, while `Theme.cpp` holds the colours for everything painted
by hand - the video cards, and the chevrons used by combo boxes and spin boxes.
Those are drawn at run time into the cache directory and substituted into the
stylesheet, so no image files are committed and their colour always matches the
theme. `VideoCardDelegate` reads those through
`Theme::palette()`, so a card is never left dark on a light canvas. Switching
themes explicitly repaints the grid, because restyling alone would not.

*Match the system* needs Qt 6.5+ for `QStyleHints::colorScheme()`. On older Qt
the option is shown but disabled rather than silently doing nothing.

Two colours are identical in both themes by design: the red accent, and the
duration badge on each thumbnail. That badge sits on the artwork rather than the
canvas, so a light version would vanish against pale thumbnails.

## Source layout

| File | Role |
|---|---|
| `main.cpp` | entry point, settings migration, system proxy |
| `MainWindow.*` | layout and wiring |
| `Database.*` | SQLite schema, upserts that never clobber download state |
| `YtDlp.*` | every yt-dlp argument, in one auditable place |
| `ChannelSync.*` | two-stage streaming channel listing via `QProcess` |
| `DownloadManager.*` | bounded concurrent queue, progress parsing, timestamp stamping |
| `VideoModel.*` | list model plus search/state filter proxy |
| `VideoCardDelegate.*` | the painted video card |
| `VideoGridView.*` | the grid: seam-free scrolling and a sane wheel step |
| `DownloadsPanel.*` | transfer queue view |
| `ThumbnailCache.*` | memory and disk thumbnail cache, deduplicated fetches |
| `UpdateChecker.*` | GitHub Releases polling and version comparison |
| `PreferencesDialog.*` | settings UI |
| `VideoDetailsDialog.*` | description and comment viewer, read from sidecars |
| `Settings.*` | persistence, installer handoff |
| `Theme.*` | both colour schemes, and everything the cards paint |
| `FileTime.*` | cross-platform timestamp stamping |
| `Models.*` | shared structs and formatting helpers |
| `resources/` | stylesheets, icons, Windows resource script, desktop entry |
| `packaging/` | Inno Setup script, build scripts, `.deb` packaging |
| `SetupDialog.*` | checks the external tools and reports what is missing |

## Behaviour worth knowing

- **Sync is additive.** Re-syncing adds new videos and refreshes titles and view
  counts, but never forgets a downloaded file. Videos pulled from the service
  stay in your catalog, which is usually what you want.
- **Missing files are detected** at startup and badged accordingly, rather than
  silently reverting to un-downloaded.
- **Resuming works.** `--continue` plus a persistent `.incomplete` directory
  means an interrupted download picks up where it stopped, including after
  *Cancel all*.
- **Cancelling is not failing.** Cancelled downloads return to "not downloaded"
  rather than being badged as errors.
- **Retrying.** A failed card's context menu reads *Retry download*, with the
  previous error as its tooltip. **Retry failed (N)** in the toolbar appears
  only when the current view holds failures and requeues all of them; it also
  picks up videos badged *File missing*. The stored error is cleared first, so
  a card that fails again shows the new reason rather than the old one.
- Comments are off by default; they add minutes per video. Right-click a
  downloaded video for *View description* or *View comments*; both read the
  sidecar files, so nothing is fetched and a video downloaded without comments
  says so rather than failing silently.
- Cookie options exist for material your own account can see - age-restricted,
  unlisted, memberships - but an anonymous visitor cannot.

## What the cards show

Thumbnail, duration, title, date, and counts. In **All videos** each card also
names its channel; inside a single channel that line is dropped, since it would
repeat on every card, and the cards shrink accordingly.

Like counts appear only for videos that have been downloaded. A flat channel
listing does not carry them - they are read from `.info.json` when a download
completes, along with a refreshed view count. A hidden like count stays absent
rather than showing as zero.

## Panels

The Channels, Downloads and Output panels are dock widgets. Drag the separator
between a panel and the grid to resize it - the separator is deliberately 7px
wide, because Qt uses the stylesheet's `width`/`height` on
`QMainWindow::separator` as the real hit area and a hairline is almost
impossible to grab. It highlights on hover.

**View** toggles each panel and offers *Reset panel layout*, which restores the
arrangement the application ships with. Layout is otherwise remembered between
sessions, so a panel resized once stays that way.

Downloads and Output are tabbed together at the bottom; click a tab to switch.

### Scrolling the grid

One wheel notch moves half a row of cards. Qt's own figure for a pixel-scrolled
icon view works out at roughly one item per scroll line, so with the usual three
lines per notch a single click jumped nearly three rows - a dozen videos at
once. The distance is `wheelScrollLines` from the system multiplied by 45
pixels, so the platform preference still applies. Touchpads are passed through
unchanged, since they already report real pixels.

## Age-restricted and members-only videos

These need an account. **Preferences > Access > Use cookies from browser** names
the browser you are signed in to; yt-dlp reads its cookie database directly, so
nothing is exported and nothing is stored by this application.

Then use **Retry failed** on the videos that were refused. Age and membership
failures are never retried automatically, because retrying cannot help until
this setting changes.

### Browsers yt-dlp knows by name

The dropdown lists every browser yt-dlp accepts: `brave`, `chrome`, `chromium`,
`edge`, `firefox`, `opera`, `safari`, `vivaldi`, `whale`. Anything outside that
list is rejected outright, which is why the field is a list rather than free
text - though it stays editable for the profile forms below.

For a non-default profile, add its name: `firefox:work` or `chrome:Profile 2`.

| Browser | Notes |
|---|---|
| Firefox | Cookies are plain SQLite; the most reliable option on Linux |
| Chrome, Chromium, Brave, Edge, Vivaldi, Opera | Encrypted; on Linux yt-dlp needs access to your keyring, and may prompt |
| Safari | macOS only; grant Full Disk Access to whatever runs yt-dlp |

### LibreWolf, Floorp, Zen and other Firefox forks

These are **not** valid names - yt-dlp rejects anything outside the list above.
Their profiles are Firefox-format though, so pointing yt-dlp at the folder works:

1. In LibreWolf, open `about:profiles`.
2. Copy the **Root Directory** of the profile you are signed in with, typically
   `/home/you/.librewolf/xxxxxxxx.default-default`. A Flatpak install keeps it
   under `~/.var/app/io.gitlab.librewolf-community/.librewolf/` instead.
3. In Preferences > Access, click **Profile folder** and select it. The field
   fills in as `firefox:/home/you/.librewolf/xxxxxxxx.default-default`.

Or type it by hand - the field is editable.

**LibreWolf clears cookies on close by default.** If sign-in never survives,
that is why: check *Settings > Privacy & Security > Cookies and Site Data*, and
either turn the setting off or add an exception for the video service. Sign in
again afterwards, since existing cookies were already discarded.

### Checking it worked

Close the browser first - some hold the cookie database open. Then turn on
**Preferences > Downloading > Verbose yt-dlp output** and retry one
age-restricted video. The Output tab shows the flag being passed:

```
$ /home/you/.local/bin/yt-dlp ... --cookies-from-browser firefox:/home/you/.librewolf/xxxx.default-default ...
```

If the download still fails with the same message, the cookies were read but
did not carry a signed-in session: sign in again in that exact profile, confirm
the video plays there, close the browser, and retry.

### Cookies are sent only when needed

Cookies unlock age-restricted and members-only videos, but they also make the
service offer formats that cannot be downloaded - so a video that works
anonymously starts failing with *Requested format is not available* as soon as
cookies are configured. Applying them to every request trades one problem for
another.

So each video is attempted **without** cookies first. If the failure says an
account was needed, it is retried immediately with them. And if a download that
used cookies comes back with nothing downloadable, it is retried without them.
Each video switches at most once, in either direction, and neither switch counts
against the automatic retry attempts - it is a different attempt, not a repeat
of the same one.

Turn this off under **Preferences > Access > Only send cookies when a video
needs them** if you specifically want every request authenticated.

### Worth knowing

- Cookies are read fresh on every download, so nothing goes stale in this
  program's settings - but the session itself expires after a couple of weeks
  of not using the browser.
- Everything downloaded with cookies is tied to your account. For an archive
  meant to outlive the account, that is worth thinking about.

## If downloads fail with HTTP 403

A 403 means the media URL was refused. It does **not** mean the video is
unavailable - the listing worked, so the service is reachable and the video
exists. The first 403 of a session writes this list to the Output tab.

**1. No JavaScript runtime.** Check the top of the Output tab: the application
reports the runtime it found at startup. If it reports none, that is the cause.
Install deno, or name another runtime in Preferences. Updating yt-dlp does not
help, because the runtime is a separate program.

**Help > Check download support** tests all four external tools and reports
which is missing, with the command to fix it. It runs the same yt-dlp, with the
same environment, that a download would - so its answer is the application's
view, not the shell's. Those can differ.

If the two disagree, turn on **Preferences > Downloading > Verbose yt-dlp
output**. That adds `-v`, stops suppressing warnings, and writes the full
command line to the Output tab, so the binary being run and the providers it
loaded are both visible:

```
$ /home/you/.local/bin/yt-dlp --no-colors --ignore-config -v ...
[debug] [youtube] [pot] PO Token Providers: bgutil:script-deno-1.3.1 (external)
```

A different path there than the one your shell resolves is the whole problem -
a plugin injected into one yt-dlp is invisible to another.

**2. The JavaScript challenge solver is missing.** Distinct from both the
runtime and the token provider: the runtime *executes* the solver, and the
solver itself ships separately. Without it the log reads `n challenge solving
failed` followed by `Only images are available for download`, and the download
ends as *Requested format is not available* even though nothing is wrong with
the video. Install it beside yt-dlp:

```bash
pipx inject yt-dlp yt-dlp-ejs
```

Or enable **Preferences > Downloading > Allow yt-dlp to download its challenge
solver**, which passes `--remote-components ejs:github` and fetches it when
needed. Installing it is preferable: nothing is downloaded mid-archive, and it
works offline.

**3. A PO token is required.** A Proof of Origin token is a separate mechanism
from the JS runtime, and having a runtime does not supply one. Without it,
requests for the affected clients can be refused outright. Run
`yt-dlp -v "<video URL>" 2>&1 | grep -i "pot\|PO Token"`; if it reports
`PO Token Providers: none`, install a provider plugin, or pass a token through
**Preferences > Downloading > Extractor arguments**, for example
`youtube:po_token=web.gvs+YOURTOKEN`. The
[PO Token Guide](https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide) has the
current details, which change often.

Setting a provider up needs two pieces: a plugin for yt-dlp, and a generator
for it to talk to.

**On Windows, check how yt-dlp was installed first.** A build from winget or a
downloaded `yt-dlp.exe` is a standalone executable carrying its own bundled
Python, so `pip install` puts the plugin into a different Python that yt-dlp
never reads - pip reports success and nothing changes. Either install yt-dlp
itself with pip or pipx so both share one Python, or extract the plugin's
release archive into the directory yt-dlp scans, usually
`%APPDATA%\yt-dlp\plugins`. **Help > Check download support** reports the
install type and the exact directory, and adjusts its instructions to match.

```bash
pipx inject yt-dlp bgutil-ytdlp-pot-provider     # the plugin
```

For the generator, **Docker is only the most convenient option, not a
requirement.** The script option runs on demand using the JavaScript runtime
already needed above, with no daemon and no container:

```bash
git clone --single-branch --branch <VERSION> \
  https://github.com/Brainicism/bgutil-ytdlp-pot-provider.git ~/bgutil-ytdlp-pot-provider
cd ~/bgutil-ytdlp-pot-provider/server && deno install --allow-scripts=npm:canvas --frozen
```

Cloned to that default location, nothing further needs configuring. It is
slower than the HTTP server, since each call starts a fresh process, but it
requires no background service. Match the branch to the installed plugin
version - a mismatch fails as though the plugin were absent.

Confirm either way with `yt-dlp -v <url> 2>&1 | grep "PO Token Providers"`,
or with **Help > Check download support**.

**4. yt-dlp is out of date.** `yt-dlp --update-to nightly`. The startup check
reports the version and its age.

**5. The format selector.** Separate video and audio streams are more fragile
than a single pre-combined one, and fail with 403 more readily. Setting quality
to `b` in Preferences trades resolution for reliability, which is worth testing
if only some videos fail.

**6. Rate limiting.** The likeliest cause when failures start *partway through*
a download or only after a queue has been running. Reduce *Simultaneous
downloads* to 1, set a speed limit, and set the pause options under
**Preferences > Downloading**. Pauses cost time on every download, so leave
them off until they are needed.

### Narrowing it down

Run one failing video several ways and see which combination works. Each
attempt downloads for a few seconds - long enough, because a 403 is raised when
the media URL is first requested rather than at the end.

```bash
URL="https://www.youtube.com/watch?v=VIDEOID"

# As the application runs it
yt-dlp -f "bv*+ba/b" --merge-output-format mkv -P /tmp/403test "$URL"

# Without the client that is being forced onto SABR
yt-dlp --extractor-args "youtube:player_client=default,-web_safari" \
       -f "bv*+ba/b" -P /tmp/403test "$URL"

# A single pre-combined stream: lower quality, fewer moving parts
yt-dlp -f b -P /tmp/403test "$URL"
```

If one of these works, put the matching setting into Preferences - the
extractor argument under *Downloading > Extractor arguments*, or `b` as the
quality. If they all fail with 403, client selection is not the cause: check
the runtime, solver and token provider under **Help > Check download support**.

### Pacing options

| Setting | yt-dlp flag | Effect |
|---|---|---|
| Pause between requests | `--sleep-requests` | waits between metadata requests |
| Pause between videos, min/max | `--sleep-interval`, `--max-sleep-interval` | waits a random time between videos |
| Extraction retries | `--extractor-retries` | retries the metadata step |
| Extractor arguments | `--extractor-args` | free-text, for PO tokens and client selection |

All default to off or low.

### Automatic retries

Because a retry re-runs yt-dlp from scratch, it re-extracts the media URLs -
which is usually what a 403 actually needs, rather than a repeat of the same
refused request. Failures that look transient are therefore retried
automatically, three times by default, with a delay that grows on each attempt
(**Preferences > Downloading > Automatic retries**).

Failures the service reports as final - private, removed, members-only,
age-restricted, region-blocked, terminated account - are **never** retried.
Retrying those wastes requests and buries the real reason. A permanent marker
wins even when the message also contains a 403.

**Retry failed (N)** in the toolbar remains for anything that exhausts its
automatic attempts.

### One cause of intermittent 403s

When the service forces SABR streaming on a client, yt-dlp skips that client's
formats and falls back to another - often `android_vr`, whose URLs are refused
even though a PO token was successfully minted for the *first* client. The
verbose log shows this plainly:

```
Retrieved a gvs PO Token for web_safari client
Some web_safari client https formats have been skipped ... forcing SABR streaming
Invoking http downloader on "...&c=ANDROID_VR&..."
ERROR: HTTP Error 403
```

Which client gets forced varies, which is why the failures come and go and why
retrying works. Excluding the fallback client in
**Preferences > Downloading > Extractor arguments** can stabilise it:

```
youtube:player_client=default,-android_vr
```

## If the audio cuts out

Merged files combine a separate video stream and audio stream, so a defect can
come from three places. Decoding to null exercises the whole file without
writing anything:

```bash
ffmpeg -nostdin -v error -i "the-file.mkv" -f null -
```

Silence means the file decodes cleanly. Errors mean it is genuinely damaged.

Audio that stops early can decode without error, so also compare how far it
actually runs against the container length - Matroska rarely stores a per-stream
duration, so the audio has to be decoded rather than read from metadata:

```bash
ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 "the-file.mkv"
ffmpeg -nostdin -v error -stats -i "the-file.mkv" -map 0:a:0 -f null - 2>&1 | tail -1
```

If those two disagree by more than a second, the audio stream is short and the
file should be downloaded again.

A file that passes both but still sounds wrong is a playback problem, not a
download problem. Opus audio in Matroska trips up some players; try mpv or VLC
before suspecting the download.

When re-downloading, delete the affected file **and** the channel's
`.incomplete` folder - a stale partial is what a retry would resume from.
*Forget the downloaded copy* clears the catalog entry but leaves the file, so
remove it yourself.

Livestream VODs sometimes contain genuine gaps in the source. If a re-download
reproduces the same gap at the same timestamp, it is in the original.

## Known limitations

Worth reading before trusting this with an archive you care about.

**Coverage**
- Only the `/videos` tab is listed. Shorts, live streams and premieres live on
  separate tabs and are not enumerated, so a Shorts-heavy channel looks nearly
  empty.
- Playlists and individual video URLs cannot be added - channels only.
- `channels.avatar_path` exists in the schema but is never populated, so the
  navigation panel shows no channel icons.
- Descriptions are usually blank until a video is downloaded.

**Scale**
- Catalog writes happen on the GUI thread. Syncing a channel with tens of
  thousands of uploads runs two queries per video and will visibly freeze the
  window.
- The whole video list for a channel is held in memory and in the model at once;
  there is no pagination, and loaded videos accumulate during a sync.
- The on-disk thumbnail cache grows without bound and is never pruned.

**Preservation gaps**
- No checksums. Nothing detects silent corruption or a truncated file that
  yt-dlp nonetheless exited cleanly on.
- Success is inferred from yt-dlp's exit code and the file existing; the media
  is never probed for playability or duration.
- "Removed from the service" is not distinguished from "download failed" - both
  land in the Failed state.
- Re-downloading at higher quality is unsupported. *Forget the downloaded copy*
  clears the catalog entry but leaves the file, so you get duplicates.
- Absolute paths are stored, so moving the archive folder makes every entry
  report as missing. There is no re-basing tool, and a catalog is not portable
  between machines or operating systems.

**Dependency on yt-dlp**
- Every listing and download shells out to yt-dlp. If the service changes and
  your yt-dlp is stale, everything fails at once. The version and its age are
  reported at startup and a warning is shown past 30 days, but updating is
  still yours to do.
- `--extractor-args youtubetab:approximate_date` is extractor-specific and may
  change or disappear.
- Error text is extracted heuristically from the last `ERROR:` line of stderr.
- Bulk downloading may trigger throttling or bot checks. Beyond yt-dlp's own
  retries there is no backoff and no configurable delay between videos.

**Timestamps**
- For premieres and streams the recorded time is the publication timestamp,
  which may not be when the content was produced.
- When only a date is available it is anchored to UTC midnight, so local-time
  display can show the previous day.

**Interface**
- The comment viewer renders at most 3000 comments in one pass; the filter
  narrows a larger set. Threads are indented one level, so a reply to a reply
  is shown at the same depth as its parent.
- Checkboxes respond only to a click on the box itself; no keyboard toggle, no
  shift-click ranges.
- No overall queue progress, only per-video.
- Sorting is fixed at newest-first, and search matches titles only.
- Strings are wrapped in `tr()` but no translations are shipped.

## Extending it

- **Checksums.** A `sha256` column hashed on completion is the single most
  useful addition for a preservation archive: it turns "I have the file" into
  "I can prove the file is intact."
- **Shorts and streams tabs**, per the coverage limitation above.
- **Scheduled syncs.** A `QTimer` plus a "watch this channel" flag gives
  automatic capture of new uploads.
- **Path re-basing**, so an archive can move between drives or machines.
- **Export.** Writing catalog rows to CSV or JSON-LD makes the archive
  ingestible by other tools.

## Legal note

This tool automates downloads that a person could perform manually. Whether any
particular download is permissible depends on the material, the terms of the
service you retrieve it from, and the copyright law of your jurisdiction.
Archiving your own uploads, public-domain material, or content you are licensed
to keep is a different matter from redistributing someone else's work. That
judgement is the operator's to make.

## AI disclosure

This project was written collaboratively with an AI assistant (Claude, by
Anthropic) over an interactive session. Effectively all of the source, the build
and packaging scripts, and this README were drafted by the model; the
requirements, design decisions, priorities and acceptance testing were the
human author's.

Every feature was exercised on real hardware - Windows 11 and Debian with KDE
Plasma on Wayland - and several defects found that way were fixed in the course
of development, including the file-timestamping glob, the thumbnail decoding
fallback, a window backing-store artefact, and cancellation being misreported as
failure. Some verification was performed by the model against stand-in test
harnesses rather than the live service.

This is disclosed because provenance matters for a tool intended for archival
work, and because AI-written code carries a particular failure mode: it can be
confidently wrong in ways that read as authoritative. Review before relying on
it for anything irreplaceable. The **Known limitations** section above is an
honest account of what this does not do; it is not exhaustive.
