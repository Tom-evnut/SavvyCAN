# Changes in this fork (`fork-ci`)

`fork-ci` follows [collin80/SavvyCAN](https://github.com/collin80/SavvyCAN) `QT6WIP` (synced at `e56f5a3`,
2026-10-29). On 2026-10-28 collin80 merged `fork-ci` itself into `QT6WIP` (`4fb1746`, taking `fork-ci` as of
`6411223`), so upstream now has everything this file lists up to then: the CAN FD series, GitHub repos, log notes,
the OBD-II Poller, FlexRay, the loader and saver fixes and the fork CI workflow. He then removed this file and the
README notice again (`e56f5a3`); they stay on `fork-ci` only.

`fork-ci` only adds what isn't upstream yet:

| Change | Branch | Upstream |
|---|---|---|
| OBD-II Poller after the first car test: polls from its own thread (no more stalls behind a busy GUI); a bitmap answer that comes after discovery's wait is still followed; supported-PID answers in the generated DBC | `obd2-car-fixes` | not offered yet |
| Connections can't be removed while another thread sends on them (lock on the connection list) | `obd2-car-fixes` | not offered yet |
| FlexRay capture, phase 2: the cycle count is a first-class field (Slot and Cycle columns in the frame list) instead of payload byte 0; the stored payload is the true FlexRay payload, while DBC decoding still multiplexes on the cycle | `flexray-phase2-model` | not offered yet |
| Preferences dialog fits small screens: settings in a scroll area, Close button always visible, size capped to the screen | `fix-settings-dialog-size` | not offered yet |
| Every window fits the screen it opens on: size capped and kept on screen each time it is shown (saved sizes from a bigger monitor too); the six windows too tall for a laptop scroll, with their buttons kept visible | `fix-window-sizing` | not offered yet |
| Preferences dialog: F1 help works after the dialog has been closed and reopened, and other key releases are no longer swallowed | `fix-settings-f1-help` | not offered yet |
| This file and the README notice | `fork-ci` only | removed upstream (`e56f5a3`) |

**Builds:** every push to `fork-ci` builds a Linux x64 AppImage and a Windows x64 package. Download them from the
*Fork Build (Linux + Windows)* runs under **Actions**.

## How this file is kept

Every fix or new feature that lands on `fork-ci` gets:

- an entry in the [change log](#change-log), newest first, giving the date, the branch, what changed and why;
- an update to the tables below if it changes what's supported or merged;
- its upstream pull request link in the change log once one is opened.

---

## CAN FD support

Everything here is in upstream `QT6WIP`.

| Interface or file | Receive FD | Send FD | FD setup (data rate, on/off) | Branch |
|---|---|---|---|---|
| GVRET (serial, TCP), e.g. ESP32RET on a LilyGO T-2CAN-FD | ✅ | ✅ opcode 20 | ✅ `GET_FD`/`SETUP_FD` from the bus settings panel, firmware permitting | `fix-gvret-qt6`, `fd-send`, `fd-gvret` |
| QtSerialBus (SocketCAN, PEAK, virtualcan, ...) | ✅ FD/BRS/ESI | ✅ | ✅ FD and data rate in the connection dialog | `fd-serialbus` |
| LAWICEL / slcan | ✅ `d/D/b/B` | ✅ | ✅ `Y1/Y2/Y4/Y5` | `fd-lawicel`, `fd-send` |
| MQTT | ✅ FD/BRS/ESI/remote flags | ✅ | n/a | `fd-network` |
| CANlogserver | ✅ `id##<flags><data>` | n/a | n/a | `fd-network` |
| socketcand | ❌ the socketcand protocol has no CAN FD commands | | | |
| CANserver | ❌ fixed 16-byte Panda records, classic only | | | |
| Native CSV | ✅ | ✅ (save) | | `fd-logs` |
| candump | ✅ `##<flags>` | ✅ | | `fd-logs` |
| Vector ASC | ✅ `CANFD` lines, and `d` lines over 8 bytes (CANalyzer 8.x) | ✅ | | `fd-logs` |
| Vector BLF | ✅ objects 100 and 101 | (no BLF saver) | | `fd-logs` |
| PCAN TRC 2.0/2.1 | ✅ `FD/FB/FE/BI` | (no TRC saver) | | `fd-logs` |
| Wireshark SocketCAN pcap | ✅ | (no pcap saver) | | `fd-logs` |
| DBC files | ✅ `VFrameFormat`, `CANFD_BRS`, signals past byte 8 | ✅ saved back | FD and BRS check boxes in the message editor | `fd-dbc` |
| Send Frames window and scripts | n/a | ✅ FD/BRS columns, `can.sendFDFrame()` | DBC CAN FD messages tick FD/BRS | `fd-send`, `fd-dbc` |
| Playback window | n/a | ✅ as loaded, or **Send all frames as CAN FD** (with or without BRS) | n/a | `fd-playback` |

**Frame list:** the frame type column shows `D` for CAN FD frames, with `B` for BRS and `E` for ESI.

**Frame model:** a frame's type and its FD flag always agree (`fd-core`).

## FlexRay support (`flexray-pico`)

| Area | What works |
|---|---|
| pico-flexray connection | "pico-flexray (FlexRay, USB)" in the new-connection dialog. Receive only: FR1 is bus 0, FR2 is bus 1, 10 Mbit/s, listen-only. Lists boards by serial number (`picoflex...`), reconnects when the board is plugged back in. Null frames are kept (type `FN`), payload CRC errors are marked (`F!`, record format 2 only). With record format 2 firmware, frames carry the board's frame-end timestamps. Needs libusb-1.0 at build time and, on Windows, the WinUSB driver (Zadig) |
| Frame representation | `CommFrame::FLEXRAYDataFrame`: ID = 11-bit slot ID, payload = cycle count byte + up to 254 bytes, never CAN FD. The cycle count byte is what pico-flexray's and openpilot cabana's FlexRay DBC files multiplex on |
| Frame list | Type `F` (`FN` null frame, `F!` CRC error); Slot and Cycle columns show the FlexRay slot ID and cycle count (blank for non-FlexRay); DBC interpretation and graphing include FlexRay data frames, overwrite mode also null-only slots |
| Cycle count | A first-class field (`CommFrame::flexRayCycle()`), not stuffed into the payload; the payload is the true FlexRay payload. DBC decoding uses `flexRayDecodePayload()` to rebuild the `[cycle][payload]` layout, so the pico-flexray / cabana DBCs still multiplex on the cycle. Native CSV keeps the on-disk `[cycle][payload]` data bytes, so older logs load unchanged |
| FlexRay header details | Indicators, header CRC, frame CRC, the device's CRC check and whether the time is the device's are kept with each frame (`CommFrame::setFlexRayHeader()` and friends). Not shown or saved yet, apart from null/CRC error |
| DBC decoding | Signals anywhere in a 255-byte message (was 64) |
| Native CSV | `FR` in the flags column; rows up to 255 bytes; old files load unchanged |
| Frame Info, Flow View, signal editor | Frames over 64 bytes no longer overflow their fixed buffers: per-byte statistics cover the first 8 bytes (Frame Info), the first 64 bytes are shown (Flow View, bit grids) |
| Not done | Stock firmware sends no timestamps (frames get the PC's time per USB transfer); saving the header details to logs; other log formats, a DBC `VFrameFormat` for FlexRay (FlexRay messages over 8 bytes load as CAN FD messages, which only matters for Send Frames and saving) |

## Branches

| Branch | Based on | Contents | Upstream PR |
|---|---|---|---|
| `fix-gvret-qt6` | `QT6WIP` | GVRET crash when saving bus settings (collin80/SavvyCAN#910); zero-length and CAN FD frame parsing (collin80/SavvyCAN#1121) | merged in [#1135](https://github.com/collin80/SavvyCAN/pull/1135) |
| `fd-send` | `fix-gvret-qt6` | Sending CAN FD: GVRET opcode 20, LAWICEL DLC and IDs, Send Frames FD/BRS columns, `can.sendFDFrame()` | merged: [#1135](https://github.com/collin80/SavvyCAN/pull/1135) |
| `fd-core` | `QT6WIP` | One CAN FD representation: frame type and FD flag kept in step; `isDataFrame()` | merged: [#1134](https://github.com/collin80/SavvyCAN/pull/1134) |
| `fd-serialbus` | `fd-core` | QtSerialBus CAN FD | merged: [#1136](https://github.com/collin80/SavvyCAN/pull/1136) |
| `fd-lawicel` | `fd-core` | LAWICEL CAN FD receive fixes and data rate | merged: [#1137](https://github.com/collin80/SavvyCAN/pull/1137) |
| `fd-logs` | `fd-core` | CAN FD in log files | merged: [#1138](https://github.com/collin80/SavvyCAN/pull/1138); `ba3d4ee` (ASC `d` lines, CRTD check) merged: [#1150](https://github.com/collin80/SavvyCAN/pull/1150) |
| `fd-network` | `fd-core` | MQTT and CANlogserver CAN FD | merged: [#1139](https://github.com/collin80/SavvyCAN/pull/1139) |
| `fd-gvret` | `fd-send` + `fd-serialbus` | GVRET `GET_FD`/`SETUP_FD` | merged: [#1140](https://github.com/collin80/SavvyCAN/pull/1140) |
| `fd-dbc` | `fd-send` + `fd-core` | DBC CAN FD messages | merged: [#1141](https://github.com/collin80/SavvyCAN/pull/1141) |
| `fd-playback` | `fd-core` | Playback: send all frames as CAN FD; last frames of a burst no longer dropped | merged: [#1149](https://github.com/collin80/SavvyCAN/pull/1149) |
| `github-phase0-refactor` | `fork-ci` | File dialogs split from loading and saving logs and DBC files; Candump save result | in `QT6WIP` via `4fb1746` |
| `github-phase1-load` | `fork-ci` | GitHub repos: loading logs and DBC files, sign-in, download cache | in `QT6WIP` via `4fb1746` |
| `github-shared-repos` | `fork-ci` | One GitHub repo list for logs and DBC files | in `QT6WIP` via `4fb1746` |
| `github-polish` | `fork-ci` | SavvyCAN GitHub App built in, Choose Repos..., EV-CANlogs preloaded | in `QT6WIP` via `4fb1746` |
| `log-notes` | `fork-ci` | Log notes: format, native CSV block, `.notes.md` file | in `QT6WIP` via `4fb1746` |
| `obd2-car-fixes` | `fork-ci` | OBD-II poller in its own thread, targetted frames, connection list lock; bitmap PIDs in the DBC | not offered yet |
| `obd2-phase4-dbc` | `fork-ci` | OBD-II DBC, Graph Polled PIDs, clock warning; graph and DBC fixes; `TestObd2Dbc` | in `QT6WIP` via `4fb1746` |
| `obd2-phase3-window` | `fork-ci` | OBD-II Poller window, help page, `TestObd2Window` | in `QT6WIP` via `4fb1746` |
| `obd2-phase2-poller` | `fork-ci` | OBD-II poll engine `Obd2Poller`, `TestObd2Poller` | in `QT6WIP` via `4fb1746` |
| `obd2-phase1-pids` | `fork-ci` | OBD-II PID table and decoder, `TestObd2Pids` | in `QT6WIP` via `4fb1746` |
| `obd2-phase0-tests` | `fork-ci` | ISO-TP and UDS unit tests ported to `CommFrame`; UDS payload contract; UDS Scanner NRC | in `QT6WIP` via `4fb1746` |
| `github-install-check` | `fork-ci` | The save window checks the SavvyCAN app is installed on the repo | in `QT6WIP` via `4fb1746` |
| `github-help` | `fork-ci` | Help page for the GitHub windows, F1 and Help buttons | in `QT6WIP` via `4fb1746` |
| `github-phase2-save` | `fork-ci` | Saving logs and DBC files to GitHub: conflicts, new branch and pull request, New Repo... | in `QT6WIP` via `4fb1746` |
| `fix-loader-bounds` | `fork-ci` | Microchip and Wireshark loaders limit the byte count to the data given | in `QT6WIP` via `4fb1746` |
| `fix-pcaplite-bounds` | `fork-ci` | pcaplite skips records over 256 bytes and stops at broken block sizes | in `QT6WIP` via `4fb1746` |
| `log-notes-ui` | `fork-ci` | Log notes dialog on save, templates, Log Notes panel, File > Log Notes... | in `QT6WIP` via `4fb1746` |
| `log-notes-capture` | `fork-ci` | Log notes when capture starts, in continuous logs; markers in the panel and graphs | in `QT6WIP` via `4fb1746` |
| `log-notes-github` | `fork-ci` | Log notes with GitHub: `.notes.md` downloaded with a log and committed with GitHub saves | in `QT6WIP` via `4fb1746` |
| `log-notes-presets` | `fork-ci` | Marker presets: prepared marker texts with a key each, Next and Undo keys, panel buttons | in `QT6WIP` via `4fb1746` |
| `flexray-pico` | `fork-ci` | FlexRay frames, the pico-flexray connection, and FlexRay sending via the firmware injector | in `QT6WIP` via `4fb1746` |
| `flexray-phase1-capture` | `flexray-pico` | Device timestamps, null frames, FlexRay header details (roadmap phase 1) | in `QT6WIP` via `4fb1746` |
| `flexray-phase2-model` | `fork-ci` | FlexRay cycle count a first-class field (not payload byte 0), Slot/Cycle columns, DBC decode via `flexRayDecodePayload()` (roadmap phase 2) | not offered yet |
| `fork-ci` | upstream `QT6WIP` | Upstream plus the changes that aren't merged yet, this file and the README notice | - |

## Other pull requests `fork-ci` carried

Until they were merged upstream (all of them are now in `QT6WIP`), `fork-ci` also carried these open pull requests:
collin80/SavvyCAN#1081, collin80/SavvyCAN#1082, collin80/SavvyCAN#1083, collin80/SavvyCAN#1085,
collin80/SavvyCAN#1086, collin80/SavvyCAN#1087, collin80/SavvyCAN#1093, collin80/SavvyCAN#1094,
collin80/SavvyCAN#1095, collin80/SavvyCAN#1099 and collin80/SavvyCAN#1101.

## Known issues

- **OBD-II Poller not yet tried on a car since `obd2-car-fixes`:** the thread change is tested with fake
  connections only; ThreadSanitizer can't judge it (Qt isn't built for it), and its reports were all at Qt's queued
  call hand-offs.

- **DBC files leak when unloaded** (found by `TestObd2Dbc` under LeakSanitizer; upstream too, not fixed yet):
  `DBCFile`'s destructor frees only its nodes, never its message handler, messages or signals, and
  `DBCSignalHandler::removeAllSignals()` calls `free()` on signals made with `new`.
- **GVRET FD layout:** the `GET_FD`/`SETUP_FD` byte layout proposed in
  [#1140](https://github.com/collin80/SavvyCAN/pull/1140) was merged upstream as proposed. Upstream ESP32RET
  implements it since collin80/ESP32RET#59; T-2CAN-FD board support is collin80/ESP32RET#60.
- **socketcand and CANserver:** classic CAN only (see the CAN FD table).
- **Vehicle Spy:** logs load but can't be saved; the saver was never written (see `fix-log-savers`).
- **pcapng timestamp resolution** (found in `fix-pcaplite-bounds`, upstream too, not fixed yet): `pcaplite.cpp`
  keeps the last file's resolution, so a pcapng file without the option loaded after one with it gets the wrong
  timestamps.
- **GitHub sign-in:** the token is stored scrambled, not encrypted. Private repos need the SavvyCAN GitHub App
  installed on them (Choose Repos... in File > GitHub Account...).

## Testing

Automated tests cover every CAN FD change above:

- QtTest suites built from this code: mock GVRET and LAWICEL devices, Qt `virtualcan`, a real MQTT broker, the
  Send Frames window;
- cross-checks of the log and DBC files with python-can and cantools;
- AddressSanitizer and UndefinedBehaviorSanitizer on every run.

They run in Docker from a separate test bench repository (private for now). The GVRET paths were also tested on
LilyGO T-2CAN-FD hardware.

---

## Change log

### 2026-10-29: every window fits small screens (`fix-window-sizing`)

After the Preferences fix, every window and dialog was opened under `QT_QPA_PLATFORM=offscreen` on 1366x768
(100 % and 125 %), 1920x1080 (100 % and 150 %) and 800x600 screens: 44 in all, counting Log Notes once in each
of its four modes. That covers every `ui/*.ui` window, the code-built ones (OBD-II, log notes, marker presets,
crop, GitHub) and the DBC editors. On 1366x768 at 125 %, 31 of the 44 did not fit:

- **22 started partly off screen:** their default size and position ran past the screen edge (for example
  Connections 960x669 at 100,100). A size saved on a bigger monitor does the same.
- **9 (six windows) could not fit at all,** because their layouts needed more height than the screen has:
  MainWindow (703 px), Frame Info (1014 px, too tall even for 1920x1080), Fuzzing, Search, Log Notes in all four
  modes, and New Graph.

Changes:

- **`windowfit.{h,cpp}` (new):** `SavvyCANApplication::notify` calls `WindowFit::eventDelivered()` for every event.
  Each time a top-level window is shown, the window is capped to the available area of its screen (minus the title
  bar and frame) and moved inside it. This runs after the window's own `showEvent`, where most windows restore their
  saved size and position, and before the window appears. The old check (`Utility::constrainedWindowPos`) only made
  sure the top-left corner was on some screen.
- **Scroll areas:** MainWindow, Frame Info, Fuzzing, Search, Log Notes and New Graph call
  `WindowFit::addScrollArea(this)`. It moves their contents into a scroll area (for MainWindow, its central widget).
  Rows of buttons at the bottom stay below the scroll area, always visible: Search/Prev/Next, Add Graph,
  Start/Stop, and the Log Notes buttons. When it is first shown, each window still opens as big as its contents
  need wherever the screen allows, so nothing changes on a big monitor.
- **Unchanged:** `.ui` files, settings keys, and how the windows look when they fit.
- **Checked:** a harness built from the app's own object files opens every window. All 44 windows now fit at all
  five sizes, and every wrapped window's bottom buttons are inside the window. With every window's saved size set to
  1900x1300 at 400,100 (a 2560x1440 monitor), all 44 still fit; without the cap, 21 to 38 windows don't. The CMake
  build and all 10 unit tests pass (`TestLogNotesUi` now also builds `windowfit.cpp`).

### 2026-10-29: F1 help in the Preferences dialog after reopening (`fix-settings-f1-help`)

- **F1 stopped working:** the dialog is created once and reused, but its F1 key filter was taken off when it closed
  and only put on in the constructor, so F1 opened the help only until the dialog was first closed. The filter now
  goes back on every time the dialog is shown, as in the Connections and Playback windows.
- **Key releases swallowed:** the filter consumed every key release that reached the dialog, not just F1. It now
  consumes only F1 and passes everything else on.
- **Checked** with a small offscreen test. F1 opens `preferences.md` on first show and again after Close, after
  closing the window and after Escape, opening it exactly once each time. A, Escape and Return releases are passed
  on. On `fork-ci` before this fix, the test fails on all the reopen cases and on swallowed keys.

### 2026-10-29: Preferences dialog fits small screens (`fix-settings-dialog-size`)

The Preferences dialog opened at 965x852 whatever the screen, so on a laptop (1366x768, or 1920x1080 at 125–150 %
scaling) its bottom was off screen and the lowest settings could not be reached.

- **Scroll area:** both columns of settings are now in a scroll area. A new Close button and the "some settings need
  a restart" note sit below it, so they are always visible. Close saves the same way closing the window does.
- **Size capped to the screen:** the first time it opens, the dialog sizes itself to show every setting without
  scrolling (965x857 on 1920x1080 at 100 %). It is never bigger than the available area of its screen, minus the
  title bar and frame.
- **Better balance:** the Font and Flowview groups moved from the left column to the right one, so the two columns
  are about the same height.
- **Unchanged:** every setting, widget name and `QSettings` key, and settings are still saved as soon as they change.
- **Checked** with a small harness under `QT_QPA_PLATFORM=offscreen` on 1366x768 (100 % and 125 %), 1920x1080 (100 %
  and 150 %) and 800x600 screens. On each, the dialog and its Close button lie inside the screen's available area,
  all 38 setting widgets can be scrolled fully into view, and Close saves a changed checkbox and an unfinished line
  edit. Builds with qmake and CMake.

### 2026-10-29: synced with upstream `QT6WIP` (`e56f5a3`)

- **`fork-ci` merged upstream:** on 2026-10-28 collin80 merged `fork-ci` (as of `6411223`, the FlexRay merge) into
  `QT6WIP` (`4fb1746`): everything in this file up to then is upstream now, so the summary table shrank to the
  car-test fixes below. He then removed this file and the README notice (`e56f5a3`); `fork-ci` keeps both (the
  README notice is now on top of upstream's new README text).
- **New from upstream:**
  - BLF CAN FD regression fixed (collin80/SavvyCAN#1151) on top of the large-file BLF loader (collin80/SavvyCAN#1116)
    and a fix for graph windows showing another window's plot under OpenGL;
  - filtered continuous logging (collin80/SavvyCAN#1146), a Decimal Value column (collin80/SavvyCAN#1147), new IDs
    enabled in the filter by default (collin80/SavvyCAN#1148);
  - the signal being edited shown as a gradient in the DBC signal editor.
- **Conflicts:** only this file (deleted upstream, kept here).
- **Fork only now:** `obd2-car-fixes` (2026-10-28), not offered upstream yet.
- **Flaky test fixed:** `TestObd2Poller::testRunsInItsOwnThread` (from `obd2-car-fixes`) failed about 1 run in 5 in
  Docker, before and after the merge. `QTRY_COMPARE` looks again only every 50 ms, so the test's reply could arrive
  after the poller's 50 ms discovery wait had run out. The test now gives the poller a 500 ms reply wait: 0 failures
  in 100 runs. The poller itself was right.

### 2026-10-28: OBD-II Poller fixes from the first car test (`obd2-car-fixes`)

From a test on an Astra J (2011) with a T-2CAN-FD: every request was answered in about 24 ms, but the poller often
waited 100 ms to 900 ms before the next one, so it managed about 8 requests/s instead of about 18.

- **The poller runs in a thread of its own.** Received frames reach the GUI thread in 20 ms batches and wait there
  while the GUI is busy (about 2,200 frames/s were coming in), and the poller ran there too. Now its answers come
  straight from the connection's thread as targetted frames (the path scripts already use) for a filter on the reply
  IDs, and it sends from its own thread. The window talks to it through queued calls and a copy of its state.
- **Connection list lock:** `CANConManager` now takes a read/write lock around its list of connections: adding,
  removing and replacing take the write lock, sending, the bus count and targetted frame filters the read lock. So
  a connection can't be removed and deleted while another thread is sending on it. `replace` deletes the old
  connection after letting go of the lock.
- **Timers move with their objects:** the poller's and the ISO-TP handler's timers are now their children, so they
  run in the thread the object is moved to.
- **Discovery follows a late bitmap answer:** in the car test the 0x20 bitmap said 0x40 was supported too, but
  discovery had already finished: the answer came on the bus in 23 ms, the poller saw it only after its 50 ms wait
  ran out. Now a bitmap answer from an ECU that took part in discovery is followed whenever it comes (each bitmap
  asked for once), and discovery goes on.
- **Supported-PID answers in the generated DBC:** PIDs 0x00, 0x20 ... 0xE0 get a raw 32-bit signal each
  (`PID_00_SupportedPids` ...). Before, strict tools such as cantools refused to decode a log containing a discovery.
- **Tests:** `TestObd2Poller` runs a discovery with the poller in a worker thread, answers posted to it by name as
  a connection does, and checks the filter hook and the bus of targetted frames, and replays the car's discovery
  answers (also late); `TestObd2Window` polls through a window whose poller is in its own thread; `TestObd2Dbc`
  decodes a bitmap answer. The new DBC decodes all 1,371 answers of the car log in cantools (strict).
### 2026-10-28: FlexRay cycle count first-class, Slot/Cycle columns (`flexray-phase2-model`)

Phase 2 of the [FlexRay reverse-engineering roadmap](docs/flexray-re-roadmap.md). The FlexRay cycle count is no
longer kept as byte 0 of the payload; it is a first-class `CommFrame` field (`setFlexRayCycle()`/`flexRayCycle()`),
and the stored payload is the true FlexRay payload, so the frame list, bit grids and the reverse-engineering tools
no longer count the cycle byte as data. The frame list gains **Slot** and **Cycle** columns (blank for non-FlexRay
frames). DBC decoding is unchanged for existing DBCs: `CommFrame::flexRayDecodePayload()` rebuilds the
`[cycle][payload]` byte layout the pico-flexray and openpilot cabana DBCs multiplex on, feeding the (now
authoritative) cycle field in as byte 0, so both those DBCs and a standard FlexRay DBC keep decoding. The injector
(`piSendFrame`) now takes the cycle from the field. Native CSV still writes the `[cycle][payload]` data bytes, so
older FlexRay logs load unchanged and new ones are byte-for-byte the same on disk. Branched off `fork-ci`; builds
with and without libusb; the `flexray` and `picoflexrayinject` test suites updated for the new model.

### 2026-10-26: marker presets (`log-notes-presets`)

Phase 5 of the [log notes roadmap](docs/log-notes-roadmap.md), asked for so markers go into running logs more
easily.

- **File > Marker Presets...** (or Presets... in the Log Notes panel): marker texts prepared before the test, in
  order, each with a key picked by pressing it. **From Scenario** adds the notes' Scenario steps with Ctrl+1 to
  Ctrl+9. An optional **Next preset** key adds them in order; **Undo Last Marker** (Ctrl+Shift+M, can be changed or
  cleared) removes the marker added last.
- **A preset's key or button** adds its marker at once, with no dialog, at the newest frame (or the clicked one when
  not capturing). The keys work in every SavvyCAN window. **Ctrl+M** stays as it was: it asks for the text.
- **Keys** SavvyCAN, Qt or the desktop already use are refused, with what uses them (Ctrl+M, Ctrl+O, Ctrl+C, Ctrl+W,
  F1, the DBC editor's F3 to F7, undo/redo, typing keys, AltGr's Ctrl+Alt, ...); keys other programs often use
  (Ctrl+S, Ctrl+Q, F11, ...) are allowed after a warning. The list is in the roadmap, section 8.3.
- **Templates** keep their presets and keys.
- **Tests:** `TestLogNotes` (65) checks 32 keys and the presets; `TestLogNotesUi` (16) the presets window, the
  panel's buttons and templates with presets.

### 2026-10-26: log notes with GitHub logs (`log-notes-github`)

Phase 4 of the [log notes roadmap](docs/log-notes-roadmap.md).

- **Opening a log from GitHub** also downloads its `<log>.notes.md`, so the Log Notes panel shows its notes; a notes
  file cached earlier that GitHub no longer has is dropped. Notes inside a GVRET log already came with it.
- **Saving a log to GitHub** asks for its notes when Save is pressed (Cancel goes back to the GitHub window). They go
  inside a GVRET log, or into `<log>.notes.md`, committed right after the log on the same branch (before any pull
  request). A notes file already there is replaced; saving without notes leaves it and says so.
- **The GitHub window** lists a log with a notes file with its own icon and a "With ..." tooltip, and doesn't list
  the notes file on its own.
- **Code:** "companion files" in `GitHubBrowserDialog` (`setCompanionSuffixes`), `GitHubCache::remove`; a save writer
  returning false with no error now cancels quietly.
- **Tests:** `TestGitHub` adds `testCompanionOpen` and `testCompanionSave` (48 tests).

### 2026-10-26: log notes when capture starts, continuous logs, markers (`log-notes-capture`)

Phase 3 of the [log notes roadmap](docs/log-notes-roadmap.md).

- **Capture starts:** when the first bus connects with no frames, or continuous logging starts, SavvyCAN asks for the
  setup and scenario (Keep Notes or Later), so they're written before the test. Preferences: "Ask for notes when
  capture starts".
- **Continuous logging:** when it stops, the notes (with what was seen) go at the end of the log it wrote. Cancelling
  its file dialog no longer leaves it showing "Cease Continuous Logging" with no file.
- **Markers:** Ctrl+M (File > Add Marker...) or the Add Marker buttons mark the newest frame while capturing, or the
  frame clicked when not capturing. They're listed in the Log Notes panel (double-click to go to the frame), drawn as
  dashed lines labelled at the bottom of graphs (View > Show Log Markers), and saved with the notes.
- **Tests:** `TestLogNotesUi` and `TestLogNotes` cover the new dialogs, the markers list and marker order.
- **Tried** by the user on Windows: the capture-start prompt, markers on a loaded log (kept through save and reload),
  and the marker lines in graphs.

### 2026-10-26: OBD-II answers graphed next to other buses (`obd2-phase4-dbc`)

Phase 4 of the [OBD-II poller roadmap](docs/obd2-roadmap.md).

- **Make DBC...** in the OBD-II Poller writes a DBC file for the answers (a message for each ECU found, every Mode 01
  PID as a signal, three-level multiplexing: service, PID, value) and attaches it to the OBD bus, so the frame view,
  the signal viewer and graphs decode them. The file is kept to decode saved logs again.
- **Graph Polled PIDs** opens a graph window with the ticked PIDs; graphs of the other bus go in the same window.
- **Clock warning** while more than one adapter is open without "Use system clock", since the buses' timestamps
  wouldn't line up.
- **Fixed on the way (upstream too):**
  - Graphs of multiplexed signals took a point from every frame with their ID while capturing (only redrawing from
    captured frames checked the multiplexor), so the line mixed values of other multiplexor values.
  - `DBC_SIGNAL::processAsDouble` refused big-endian signals ending in the last bytes of a frame (a 16-bit Motorola
    signal in bytes 6 and 7, for example), as it checked them like little-endian ones. Graphs then fell back to
    their own scaling, losing value tables and floats.
- **Tests:** `TestObd2Dbc` decodes every field of every PID through the generated DBC and compares with the PID
  table, checks the multiplexing, 29-bit IDs, value tables, a save and reload, and the big-endian bounds;
  `TestObd2Window` gains Make DBC, Graph Polled PIDs and the clock warning. The DBC also decodes the same in
  cantools (strict). All clean under AddressSanitizer and UndefinedBehaviorSanitizer, apart from the DBC loader's
  leaks listed under Known issues.

### 2026-10-26: OBD-II Poller window (`obd2-phase3-window`)

Phase 3 of the [OBD-II poller roadmap](docs/obd2-roadmap.md): the poll engine gets its window.

- **Send Frames > OBD-II Poller:** pick the bus and 11-bit or 29-bit addressing, Discover ECUs, tick the PIDs to
  poll (each with an interval) and Start Polling. The table shows each PID's latest value with its unit, and counts
  answers and timeouts; a line below counts requests, answers, timeouts and refusals. Read VIN reads the VIN from
  one ECU. The requests and answers are captured like other traffic, so a saved log holds them.
- **Poll sets:** Save Poll Set... and Load Poll Set... keep the bus, addressing, rate limit and ticked PIDs in an
  `.obd` (JSON) file.
- **Warnings:** when the bus is listen-only (nothing can be sent), or no bus is open. Closing the window stops
  polling.
- **Help:** `help/obd2.md` (F1 or the Help button), linked from the UDS Scanner page.
- **Tests:** `TestObd2Window` (16 tests) drives the window with a fake clock: the table before and after discovery,
  polling, values, timeouts and refusals, VIN, warnings, poll sets (round trip, files, bad files), closing and
  help. With `SAVVYCAN_SCREENSHOTS` set it saves a picture of the window.

### 2026-10-26: log notes asked for when saving, shown when loading (`log-notes-ui`)

Phase 2 of the [log notes roadmap](docs/log-notes-roadmap.md).

- **Saving a log** (Save Log File, Save Filtered Log File) asks for its notes after the file name is picked: title,
  author, tags, system, setup, scenario, observations, and the buses, DBC files and markers SavvyCAN fills in. Save with
  Notes, Save without Notes, or Cancel. Starts from the loaded log's notes or the last ones saved.
- **Templates** in the dialog fill in the typed fields (e.g. one per test bench).
- **Loading a log** with notes opens the **Log Notes** panel. **File > Log Notes...** edits them and saves them back
  into the log (or its `.notes.md` file); **File > Show Log Notes Panel** shows or hides the panel.
- **Preferences:** "Ask for notes (setup, scenario) when saving logs".
- **GitHub:** a log opened from GitHub shows its notes if they're inside it (native CSV); saving to GitHub doesn't
  ask for notes yet (log notes phase 4).
- **Code:** `LogNotesDialog`, `LogNotesPanel`; `FrameFileIO::pickSaveFile`/`saveFrameFileWithProgress`; `TestLogNotesUi`.
- **Tried** by the user on Windows: open a log, edit its notes, save, reopen.

### 2026-10-25: the save window checks the SavvyCAN app is installed (`github-install-check`)

- **Why:** signed in with GitHub, saving only works on repos the SavvyCAN app is installed on; without it, GitHub
  refused the save (an error 400) and New Repo... failed, with nothing saying why. Found when the user first tried
  saving; installing the app on the account fixed it.
- **Now:** picking a repo in the Save to GitHub window shows a warning with a Choose Repos... link when the app isn't
  installed on it, and Save asks first (Choose Repos..., Save Anyway, Cancel). New Repo... asks when the app isn't
  installed on all the owner's repos. Asked afresh before each save, so a new install counts straight away.
- **How:** `GitHubClient::listInstallations` and `listInstallationRepos` (`GET /user/installations`, and the repos of
  one); only with a GitHub App's sign-in. If GitHub doesn't say, saving goes ahead as before.
- **Help:** `help/github.md` describes the warning. `TestGitHub` (46) covers the warning and the questions.

### 2026-10-25: OBD-II poll engine (`obd2-phase2-poller`)

Phase 2 of the [OBD-II poller roadmap](docs/obd2-roadmap.md); the window that uses it is phase 3.

- **Discovery:** asks all ECUs for PID 0x00, then follows each answering ECU's supported-PID bitmaps with requests
  to that ECU alone.
- **Polling:** a list of PIDs, each for one ECU or for all, with an interval; one request outstanding at a time,
  taken in turn, at most 20 requests a second by default. 11-bit and 29-bit addressing.
- **Timing:** a request times out after 50 ms (P2), or 5 s after a "response pending" reply. A PID an ECU refuses
  as unsupported isn't asked for again.
- **Safety:** only Mode 01 and Mode 09 can be sent; clearing DTCs and every other service is refused in the engine.
- **Tests:** `TestObd2Poller` (19 tests) runs the engine against the real ISO-TP handler with a fake clock:
  discovery with one, several or no ECUs, 29-bit IDs, timeouts, response pending, negative replies, the rate limit,
  intervals, a VIN over three frames with flow control, and the safety list; also with AddressSanitizer and
  UndefinedBehaviorSanitizer.

### 2026-10-25: help for the GitHub windows (`github-help`)

- **New help page** `help/github.md` ("Logs and DBC Files on GitHub"): signing in, and installing the SavvyCAN app on
  the repos SavvyCAN may use (saving fails without it; New Repo... works best with All repositories), loading,
  saving (the save window's fields, conflicts, new branch and pull request), creating repos, and what the error
  messages mean.
- **Opened by** F1 and a new Help button in the GitHub load/save window, the GitHub Account window and the New Repo
  window. The DBC File Manager and main screen help pages link to it.

### 2026-10-25: OBD-II PID table and decoder (`obd2-phase1-pids`)

Phase 1 of the [OBD-II poller roadmap](docs/obd2-roadmap.md); nothing uses it yet (the poll engine is phase 2).

- **Table:** `bus_protocols/obd2_pids.{h,cpp}` lists the SAE J1979 Mode 01 PIDs (engine speed, vehicle speed,
  temperatures, fuel trims, oxygen sensors, control module voltage, odometer and more): name, unit, data bytes and
  each value's bits, scale and offset, with names for states such as fuel system status and fuel type.
- **Decoding:** values from a PID's data bytes; supported-PID bitmaps (0x00, 0x20 ... 0xE0); Mode 01 replies,
  including replies to a request for several PIDs; Mode 09 VIN and ECU name text.
- **Tests:** `TestObd2Pids` checks the table (order, field sizes, names unique and usable in a DBC) and decodes
  worked examples for 27 values, also with AddressSanitizer and UndefinedBehaviorSanitizer.

### 2026-10-25: pcap records over 256 bytes skipped (`fix-pcaplite-bounds`)

`pcaplite.cpp` read each pcap and pcapng record into a 256 byte buffer without checking its length, so any file
with a longer record wrote past the buffer (found in `fix-loader-bounds`; upstream too). Any pcap file can reach the
Wireshark loaders, and other traffic in a capture can be longer than a CAN frame.

- **pcap:** a record over 256 bytes is skipped. A record over 256 KB (libpcap's largest) means the file is broken,
  so the load stops there.
- **pcapng:** an enhanced packet block over 256 bytes is skipped. A block size of 0 made the loader read the same
  block forever; a block size under 12 bytes, or one that would seek past what `fseek` can reach, now stops the load.
  Interface options over 256 bytes were already skipped. The timestamp resolution option is read as the one byte it
  is, so base 2 resolutions now work.
- **Tests:** `TestFileDialogSplit` loads pcap and pcapng files with a 300 byte record (and, for pcapng, a 300 byte
  interface option), then with a 4 GB record or a 0 byte block; also with AddressSanitizer and
  UndefinedBehaviorSanitizer (the old code overflows the buffer).

### 2026-10-25: Short frames in Vector, IXXAT, Cabana and PCAN logs (`fix-short-frame-loaders`)

Found by the `fix-log-savers` round trips; upstream has them too.

- **Vector trace:** loading a frame with no data crashed (a list index out of range): once the line is trimmed it
  has no data column. The loader and the format check now take that as no data, and a line with fewer data bytes
  than its length gets the bytes it has.
- **IXXAT MiniLog:** a frame with no data loaded back with one byte (splitting an empty data column gave one empty
  token).
- **Cabana:** the saver padded every frame to 8 bytes with `00`. It now writes the frame's own bytes, as Cabana does;
  the loader already took the length from the data column.
- **PCAN TRC:** lines of 41 characters or fewer were skipped, which dropped frames with no data (1.1, 1.3, 2.0 and
  2.1). Lines are now checked by their token count only, and 1.1 and 1.3 lines with fewer data bytes than their
  length get the bytes they have, as 2.0 and 2.1 already did.
- **Tests:** `TestFileDialogSplit` round-trips frames of 8 down to 0 bytes in every format, and loads lines laid out
  as Vector, IXXAT, Cabana and PCAN (1.1, 1.3, 2.0) write them, with and without data. The new checks fail on the
  old code.

### 2026-10-25: Microchip and Wireshark loaders limit the byte count (`fix-loader-bounds`)

Both loaders called `payload().resize()` to limit a frame's byte count, but `CommFrame::payload()` returns a copy, so
the limits did nothing (found in `obd2-phase0-tests`; upstream too).

- **Microchip:** a line claiming more bytes than it gives read past the end of the line's fields. The count is now
  limited to 8 and to the bytes on the line, and a negative count gives no bytes. The `;` the saver writes after the
  last byte is no longer counted as a byte.
- **Wireshark:** the byte count came from the record without a check, so a record claiming more bytes than it has
  read past the record. The count is now limited to 8 (the format is classic CAN) and to the bytes in the record.
  A record too short for the CAN header is skipped, and the load reports errors.
- **Tests:** `TestFileDialogSplit` loads malformed Microchip and Wireshark files (counts too large, over 8,
  negative, with no bytes, a cut-off record); also with AddressSanitizer and UndefinedBehaviorSanitizer.

### 2026-10-25: save logs and DBC files to GitHub (`github-phase2-save`)

Phase 2 of the [GitHub repos roadmap](docs/github-repos-roadmap.md).

- **Where:** File > Save Log File to GitHub..., and Save to GitHub in the DBC File Manager (for the DBC picked).
- **The dialog:** the GitHub browser in save mode: repo, branch, file type, a folder (click one in the tree) and a
  file name (click a file to replace it), and an optional commit message. The file is written by SavvyCAN's own
  saver, uploaded as one commit, and then kept in the cache like a loaded file.
- **Saving back:** a log or DBC loaded from GitHub suggests its own place. If the file changed on GitHub since, the
  user picks Overwrite, Save Under a New Name, or Cancel. Replacing a different existing file asks first.
- **Pull requests:** "Save to a new branch and open a pull request" makes the branch, saves there, and opens a pull
  request into the branch picked (the branch goes again if the save fails).
- **New Repo...:** creates a repo, private by default, under the account or one of its organisations, with a README
  so it has a branch. Needs Administration: read and write (added to the SavvyCAN app).
- **Sizes:** over 50 MB asks first; over 100 MB is refused, as GitHub would.
- **DBC files:** after saving, a DBC goes by its GitHub copy (and reloads from it at the next start); when nothing's
  saved, it keeps its old name and unsaved-changes flag (`DBCFile::setFullFilename`).
- **Tests:** `TestGitHub` (43) adds a fake GitHub that keeps repos, branches and file versions, and covers saving,
  conflicts, pull requests and creating repos.

### 2026-10-25: ISO-TP and UDS unit tests working again (`obd2-phase0-tests`)

Phase 0 of the [OBD-II poller roadmap](docs/obd2-roadmap.md):

- **Tests:** `TestIsoTpHandler` and `TestUdsHandler` were written before `CommFrame` replaced `CANFrame` and didn't
  compile. Ported; both pass, also with AddressSanitizer and UndefinedBehaviorSanitizer. `TestUdsHandler` gains a
  send test (an OBD-II Mode 01 request and a UDS ReadDataByIdentifier request, padded single frames).
- **UDS payload:** `tryISOtoUDS` tried to remove the service bytes from a received message's payload, but called
  `remove()` on a copy, so nothing was removed. The decoder, the firmware uploader and the script callback all read
  the payload with the service byte first (upstream adjusted the decoder to that in January), so that stays the rule:
  the no-op calls are gone, the rule is written in `uds_handler.h`, and the tests check it.
- **ISO-TP messages:** `ISOTP_MESSAGE` left `reportedLength`, `lastSequence` and `isMultiframe` uninitialised, so
  copying a message read an undefined bool (found by UndefinedBehaviorSanitizer); they now start at 0 and false.
- **UDS Scanner:** a negative reply showed the meaning of 0x7F (the first payload byte) instead of the reply's
  negative response code; it now uses the code.

### 2026-10-25: notes saved with logs, format and files (`log-notes`)

Phase 1 of the [log notes roadmap](docs/log-notes-roadmap.md): notes on a log's setup, scenario and markers are
saved with the log, so the log needs no separate engineering notes. No UI yet; phase 2 adds the dialog and panel.

- **Format:** Markdown with a `key: value` header (`LogNotes`, `lognotes.{h,cpp}`).
- **Native CSV:** the notes go at the end of the log, each line starting with `# `, commas written as `%2C`. The
  native CSV loader skips those lines. Older builds load such a log only after "salvage what could be loaded?"
  (every frame loads right).
- **Other formats:** a `<log file>.notes.md` file next to the log.
- **Code:** `FrameFileIO::saveLogNotes`/`loadLogNotes`; `TestLogNotes`.

### 2026-10-25: GitHub sign-in for everyone (`github-polish`)

- **SavvyCAN GitHub App built in:** "Sign in with GitHub" uses [github.com/apps/savvycan](https://github.com/apps/savvycan),
  so users register nothing and enter no client ID. Clicking it copies the code and opens GitHub's page; the user
  pastes it and approves.
- **Private repos:** Choose Repos... in the GitHub Account dialog installs the app on the user's (or their
  organisations') repos. A repo GitHub won't show gets a hint in the browser: sign in, or install the app on it.
- **Own app:** "Use my own GitHub App or OAuth App" keeps the client ID field for forks and organisations; the
  private-repos choice only shows for OAuth Apps.
- **Starting repo:** new users' repo list starts with the public `Tom-evnut/EV-CANlogs`.
- **Confirmed** by the user on Windows before this change: signing in with the app and loading logs and DBC files
  from `Tom-evnut/EV-CANlogs`.

### 2026-10-25: CAN-DO, Vehicle Spy and Generic CSV savers (`fix-log-savers`)

Three savers found broken by the phase 0 tests (upstream too).

- **CAN-DO:** saving wrote into an empty `QByteArray`, which Qt 6 doesn't allow (an assertion in debug builds,
  undefined behaviour in release builds). Records are now 12 bytes from the start, and an empty log no longer crashes.
  - The milliseconds were written wrong.
  - The loader and the format check now skip the record a CAN-DO file starts with, instead of failing on it.
  - They no longer read past a short last record or misread bytes of 0x80 and above.
  - A frame stamped just after the minute wraps now gets the right time.
- **Vehicle Spy:** saving reported success without writing anything. The layout of a real Vehicle Spy export isn't
  known well enough to write one, so it's no longer offered for saving (save type indexes after CAN-DO move down by
  one); loading is unchanged.
- **Generic ID/Data CSV:** frames shorter than 8 bytes loaded back with an extra zero byte (the loader counted the
  line end as a byte).
- **Tests:** `TestFileDialogSplit` round-trips CAN-DO and frames of 7, 3, 1 and 0 bytes, and checks the CAN-DO
  details. The short frames found the four other formats listed under [Known issues](#known-issues).

### 2026-10-25: one GitHub repo list for logs and DBC files (`github-shared-repos`)

- **Change:** the repos added in the GitHub browser are one list, shown when loading logs and DBC files alike; most
  repos hold both. Before, each repo was listed for logs, DBC files or both.
- **Removing** a repo now asks first, since it leaves both lists. Repos saved before are all in the one list.

### 2026-10-25: load logs and DBC files from GitHub (`github-phase1-load`)

Phase 1 of the [GitHub repos roadmap](docs/github-repos-roadmap.md):
logs and DBC files kept in GitHub repos load straight into SavvyCAN.

- **Where:** File > Load Log File from GitHub..., and Load from GitHub in the DBC File Manager. File > GitHub
  Account... signs in.
- **Sign-in:** either "Sign in with GitHub" (OAuth device flow: SavvyCAN shows a code to enter on github.com; needs an
  OAuth App's client ID, entered in the GitHub Account dialog) or a pasted personal access token. Public repos work
  without signing in.
- **Browsing:** saved repos (one list for logs and DBC files), branches, the file tree filtered by file type and by
  name.
- **How files load:** the file is downloaded into a cache folder, keeping its name, with a `.origin.json` beside it
  recording the repo, branch, path and blob SHA (which saving back will need). The existing loaders then load it, so
  every log format and autodetect work. DBC files loaded from GitHub reload from the cache at the next start.
- **Limits:** files up to 100 MB (GitHub's API); Git LFS files are recognised but not loaded yet.
- **Fixes on the way:** a DBC file that fails to load no longer shows the previously loaded file a second time (or,
  at startup, gives it the failed file's bus and settings).
- **CI:** the Windows package now includes Qt's `tls` plugins and the Linux AppImage the `tls` plugins, without which
  no HTTPS request works.
- **Tests:** `TestGitHub` runs the API client and the device flow against a fake GitHub on a local port (paging,
  errors, rate limit, cancelled sign-ins, a client deleted mid-request), plus the cache, settings, and both dialogs.

### 2026-10-25: log and DBC loading/saving without the file dialogs (`github-phase0-refactor`)

Phase 0 of the GitHub repos roadmap: groundwork, no visible change apart from the Candump fix.

- **Split:** `FrameFileIO::loadFilters()`/`saveFilters()` with `loadFrameFileAs()`/`saveFrameFileAs()`, and
  `DBCHandler::loadFilters()`/`saveFilters()` with `loadFileAs()`/`saveDBCFileAs()`, load or save by file type
  without a file dialog. The dialogs use them; the DBC window's file types keep their translations.
- **Fix:** saving as Candump/Kayak reported failure even when it worked, so the main window kept the old file name.
- **Tests:** `TestFileDialogSplit` saves and reloads each log type and a DBC file. It found the broken CAN-DO and
  Vehicle Spy savers listed under [Known issues](#known-issues).
### 2026-10-15: FlexRay capture, phase 1: device time, null frames, header details (`flexray-phase1-capture`)

Phase 1 of `docs/flexray-re-roadmap.md` (C1, C3 and C4).

- **Firmware ([Tom-evnut/pico-flexray](https://github.com/Tom-evnut/pico-flexray), branch `savvycan-records`):** a new USB record format 2 that a host asks for
  with vendor request `0xa0`. Every USB mount starts in the original format 1, so other tools (Cabana, the Python
  recorder) are unaffected.
  - Format 2 sets bit 15 of the record length (format 1 lengths are at most 263), then adds a version byte, a flags
    byte (header CRC ok, frame CRC ok) and a `u32` microsecond timestamp taken at the start of the frame-end
    interrupt.
  - It also reports frames whose payload CRC failed (header CRC good), flagged. Format 1 still drops them.
  - Null frames were never dropped by the firmware; SavvyCAN dropped them.
- **Parser:** reads both formats in one stream.
  - **Timestamps:** format 2 frames get the device time, unwrapped (the counter wraps every ~71.6 minutes) and
    mapped onto SavvyCAN's clock. The last frame of each USB transfer is anchored just before the transfer arrived,
    the offset follows the lowest latency seen, and the mapping starts again if the clocks drift more than 1 s apart.
    Format 1 frames are host-stamped as before.
  - **Null frames** are kept. They aren't data frames, so DBC interpretation and graphing skip them.
  - **Header details:** indicators, header CRC, frame CRC and the device's CRC result go into a packed 64-bit
    `CommFrame` field with accessors. It's the stopgap the roadmap asks for until phase 2's `CommFrame` FlexRay
    metadata; the payload is unchanged (cycle count byte + data), so `lateral.dbc` and the injection mapping still
    work.
- **Connection:** asks for format 2 on connecting (stock firmware stalls the request and stays on format 1) and puts
  the board back to format 1 on disconnecting. The debug console says which format the firmware sends.
- **Frame list:** type `FN` for null frames and `F!` for CRC errors; overwrite mode keeps slots that only send null
  frames.
- **Native CSV:** FlexRay flags `FR+NULL` and `FR+CRCERR`. The other header details aren't saved yet (phase 2).
- **Tested** (no board):
  - **Test bench harness:** all 13 suites pass under ASan/UBSan with libusb. `flexray` now has 21 checks, adding
    null frames, header details from both formats, device time mapping (transfer latency, the counter wrap,
    re-anchoring, the system-time clock), both formats mixed and split at every byte, and format 2 resync after
    false starts. `picoflexray` and `picoflexrayinject` still pass.
  - **Windows (MinGW, Qt 6.10.3):** builds with libusb and with `-DUSE_LIBUSB=OFF`.
  - **Firmware:** builds for `pico2` with Pico SDK 2.1.1 and Arm GNU Toolchain 14.2, without warnings in its sources.
- **Needs a real board:** that the format switch and the timestamps work on hardware; timestamp precision under bus
  load; that CRC-failed frames show up on a disturbed bus; that Cabana and the Python recorder still work against
  the new firmware (format 1).

### 2026-10-15: FlexRay capture with pico-flexray (`flexray-pico`)

- **New connection:** pico-flexray, a Raspberry Pi Pico FlexRay capture/man-in-the-middle board
  (https://github.com/dynm/pico-flexray). SavvyCAN reads its USB record stream with libusb in the connection's thread;
  the record parser (`connections/picoflexrayparser.*`) has no USB code, so it's tested with simulated streams.
- **FlexRay frames:** `CommFrame` already had a FlexRay type but nothing used it.
  - FlexRay frames are now valid with up to 255 bytes (cycle count + 254) and an 11-bit slot ID.
  - `setPayload()` no longer marks FlexRay (or any non-CAN) frames as CAN FD. On a CAN frame it now sets the type and
    the flag together, as `fd-core` intends; before it set only the flag.
  - `isDataFrame()` includes FlexRay, so the frame list's DBC interpretation, overwrite mode and graphing see them.
- **DBC:** signal extraction stopped at bit 511 (64 bytes); it now covers 256 bytes.
- **Native CSV:** FlexRay rows have `FR` in the flags column after the data, like CAN FD's `FD`.
- **Fixed (already in SavvyCAN, reachable with CAN FD too):**
  - **Frame Info:** wrote past its 8-byte per-byte arrays for any frame over 8 bytes. The per-byte statistics now
    cover the first 8 bytes; the data length range is reported correctly for frames over 8 bytes.
  - **Flow View:** copied frames into 64-byte buffers without a length check. Its trigger check mixed byte and bit
    offsets, so it read past its buffer for frames over 64 bytes and, for CAN FD, ignored triggers after byte 8.
  - **DBC signal editor:** a message or signal past byte 64 wrote past its 64-byte bit grids.
- **Build:** libusb-1.0 is optional (CMake `USE_LIBUSB`, on by default; pkg-config or `LIBUSB_INCLUDE_DIR`/
  `LIBUSB_LIBRARY`; qmake via pkg-config). Without it the connection is greyed out. `fork-build.yml` installs it
  (apt, vcpkg) and the Windows package includes `libusb-1.0.dll`. Upstream's `build.yml` is unchanged and builds
  without pico-flexray support.
- **Docs:** `help/newconnection.md` covers the connection, Zadig/WinUSB on Windows and a udev rule on Linux.
- **Timestamps:** the board sends none, so frames get the time of their USB transfer, on the same clocks as
  QtSerialBus devices without hardware timestamps (the session's elapsed time, or the system time when chosen).
- **Tested** (no pico-flexray hardware yet):
  - **Two new suites** in the test bench harness, under AddressSanitizer and UndefinedBehaviorSanitizer:
    - `flexray` (17 checks): frame rules; the record parser fed pico-flexray's record format split at every byte,
      with junk and null frames; native CSV round trips; pico-flexray's own `dbc/lateral.dbc` decoding parsed frames
      (multiplexed on the cycle count); the frame list's `F` type, interpretation and overwrite mode; Frame Info and
      Flow View with 255-byte frames (this found the Flow View trigger bug).
    - `picoflexray` (3 checks): the connection with libusb and no board attached starts, reports once that no
      board was found, keeps looking, and stops cleanly.
  - **All existing CAN FD suites** still pass on this branch.
  - **Windows:** builds with MinGW 13.1 and Qt 6.10.3, with libusb 1.0.30, without warnings in the changed files.

### 2026-10-15: BLF CAN FD regression upstream (collin80/SavvyCAN#1151)

- **Merged upstream:** collin80/SavvyCAN#1149 and collin80/SavvyCAN#1150, on 2026-10-14.
- **New regression upstream:** `QT6WIP` 1321d96 has a new streaming BLF parser (collin80/SavvyCAN#1116, large-file
  loading) that dropped the CAN FD handling from collin80/SavvyCAN#1138. CAN FD BLF objects (100 and 101) load as
  classic frames, without BRS or ESI, and remote frames lose their DLC.
- **Fix:** collin80/SavvyCAN#1151 (branch `fix-blf-canfd`).
  - It restores CAN FD handling inside the new parser.
  - It makes upstream's BLF test build on Qt 6 again.
  - It adds CAN FD checks to that test.
- **`fork-ci` not affected:** it predates the new parser. It will be synced with `QT6WIP` once collin80/SavvyCAN#1151
  is merged, so the regression doesn't come in.

### 2026-10-14: remaining changes offered upstream

- **Playback:** `fd-playback` (send all frames as CAN FD; the burst fix) is open as collin80/SavvyCAN#1149.
- **ASC and CRTD:** the newer `fd-logs` fix (`ba3d4ee`: ASC `d` lines, CRTD check crash) is open as
  collin80/SavvyCAN#1150.
- **Once both are merged:** `fork-ci` will differ from upstream only by the fork's CI, README notice and this file.

### 2026-10-14: synced with upstream `QT6WIP`

- **Merged upstream:** collin80 merged the CAN FD series (collin80/SavvyCAN#1134 to collin80/SavvyCAN#1141) on
  2026-10-13, along with the other pull requests `fork-ci` carried.
- **New baseline:** `fork-ci` now follows `QT6WIP` (`3288f1f`) and keeps only `fd-playback`, the newer `fd-logs`
  fix and the fork's CI.
- **Checked:** the automated tests pass on upstream `QT6WIP` itself, apart from the two not-yet-merged `fd-logs`
  fixes. None of the merged changes was lost in collin80's merges with `master`.
- **New from upstream:** `fork-ci` picks up collin80's own work, including:
  - a search window and a UDS firmware uploader;
  - translations;
  - PCAN TRC export (collin80/SavvyCAN#1142);
  - graphing fixes (collin80/SavvyCAN#1143);
  - QtSerialBus on macOS (collin80/SavvyCAN#1145).
- **Fork CI:** `fork-build.yml` has upstream's current Linux and Windows jobs.

### 2026-10-14: ASC files from CANalyzer 8.x; crash in the CRTD check (`fd-logs`)

- **ASC from CANalyzer 8.x:** CANalyzer 8.x (and older `log2asc`) write CAN FD frames as ordinary `d` lines with
  their full length (`d 32 ...`). These now load as CAN FD, without BRS, since the line doesn't record it. Any such
  line used to fail the whole file.
- **CRTD crash (already in SavvyCAN):** the CRTD format check read past the end of an empty token on lines such as
  ASC's `0.000 1 c0 Rx ...`. Auto-detection runs that check after a failed ASC load, so opening such a file could
  crash SavvyCAN.
- **How it was found:** loading a VW ID.4 drive unit log in the automated tests. It now loads all 2,166 frames,
  1,951 of them CAN FD.
- **Upstream:** not yet in collin80/SavvyCAN#1138.

### 2026-10-12: Playback can send everything as CAN FD (`fd-playback`)

- **New option:** "Send all frames as CAN FD" in the Playback window, with "with bit rate switch (BRS)" (on by default).
  - **Why:** for logs that don't record which frames were CAN FD. In candump's screen format, for example, CAN FD
    frames of 8 bytes or less load as classic.
  - **What it changes:** every data frame is sent as CAN FD. Remote frames are sent as they are, since CAN FD has
    none, and the loaded frames aren't changed.
  - **Saved:** both settings are remembered.
- **Fixed (already in SavvyCAN):** with a burst setting over 1, playback dropped up to burst - 1 frames at the end of
  every log. The tick that reached the end returned before sending what it had queued.

### 2026-10-11: CAN FD series offered upstream

- **Pull requests:** the eight `fd-*` branches are open against `collin80/SavvyCAN` `QT6WIP` as collin80/SavvyCAN#1134 to collin80/SavvyCAN#1141,
  listed in [Branches](#branches). collin80/SavvyCAN#1135 includes the GVRET fixes from `fix-gvret-qt6`.
- **Draft:** collin80/SavvyCAN#1140 (GVRET `GET_FD`/`SETUP_FD`) is a draft until collin80 agrees the byte layout.
- **Before opening:** each branch was checked to build on its own.

### 2026-10-11: QtSerialBus timestamp code cleaned up (`fd-serialbus`)

- **Dead code removed:** `framesReceived()` built a timestamp with the static `TimeStamp::fromMicroSeconds()`,
  threw it away, and set the frame's timestamp to zero.
- **Timestamps were already right:** the frame's real timestamp is set later in the same loop. The dead code only
  made it look as if every frame had a zero timestamp (it was listed as a known issue here by mistake).
- **New test:** received frames have non-zero timestamps in arrival order.

### 2026-10-11: fixes found by the automated tests

All 11 bugs below were found on the tests' first run.

**`fd-core`: frame model**

- Asking for CAN FD on a remote or error frame no longer sets the FD flag (or BRS/ESI) without the frame type.

**`fd-lawicel`: LAWICEL**

- The CAN FD data rate (`Y<n>`) is sent when CAN FD is on. It depended on uninitialised members, so it was
  usually missing.

**`fd-serialbus`: QtSerialBus**

- Remote frames are sent as remote frames, not as zero-filled data frames.

**`fd-logs`: log files**

- **ASC:** SavvyCAN's own ASC files no longer lose their first frame.
- **ASC:** remote frame lines no longer fail the whole load.
- **ASC:** channels are read as 1-based, matching the saver, so buses no longer come back one higher.
- **ASC:** remote frames are saved without data bytes.
- **candump:** frames longer than 64 bytes (FD) or 8 bytes (classic) are skipped.
- **candump:** a blank line no longer repeats the previous frame.
- **PCAN TRC and BLF:** remote frames keep their DLC as their length.

**`fd-dbc`: DBC**

- ENUM attributes no longer collect the values of earlier ENUM attributes. This was corrupting `CANFD_BRS` on
  save.

### 2026-10-11: DBC CAN FD messages (`fd-dbc`)

- **Load and save:** `VFrameFormat` (StandardCAN_FD/ExtendedCAN_FD) and `CANFD_BRS` are read and written back.
  Messages longer than 8 bytes count as CAN FD.
- **Message editor:** "CAN FD frame" and "Bit rate switch" check boxes.
- **Send Frames:** ticks FD/BRS for CAN FD messages from the DBC file and sizes the payload.

### 2026-10-11: GVRET CAN FD setup (`fd-gvret`)

- **On connect:** SavvyCAN asks the device for its FD settings (`GET_FD`, 22).
- **Bus settings panel:** FD-capable buses get the FD controls, and changes are sent with `SETUP_FD` (21).
- **Older firmware:** devices that don't answer `GET_FD` are never sent `SETUP_FD`.
- **Bug fixed:** the `GET_FD` reply handler never returned to idle.

### 2026-10-11: network interfaces (`fd-network`)

- **MQTT:** receive honours the remote/FD/error flags; BRS (0x10) and ESI (0x20) are added in both directions.
- **CANlogserver:** reads `id##<flags><data>` CAN FD lines.

### 2026-10-11: log files (`fd-logs`)

CAN FD in six formats:

- **Native CSV:** optional flags column; old files load unchanged.
- **candump:** `##<flags>`.
- **Vector ASC:** `CANFD` lines, read and written.
- **Vector BLF:** objects 100 and 101.
- **PCAN TRC 2.x:** `FD/FB/FE/BI` types.
- **Wireshark SocketCAN pcap.**

### 2026-10-11: LAWICEL (`fd-lawicel`)

- `D` frames no longer set BRS.
- Hex DLC in the timestamp pre-parser.
- CAN FD frame type.
- A warning for unsupported data rates.

### 2026-10-11: QtSerialBus (`fd-serialbus`)

- **Receive:** FD/BRS/ESI are copied on receive.
- **Data rate:** `DataBitRateKey` is set from the data rate.
- **FD stays on:** `piStarted()` no longer turns FD off.
- **Dialog:** the connection dialog shows FD and the data rate.

### 2026-10-11: one CAN FD representation (`fd-core`)

- The frame type and the FD flag are kept in step.
- New `isDataFrame()` check.
- The frame list shows BRS and ESI.

### 2026-10-11: upstream pull requests

- Merged the 11 pull requests listed above into `fork-ci`.
- Fork CI: Linux AppImage and Windows package builds.

### 2026-10-10: sending CAN FD (`fd-send`)

- **GVRET:** opcode 20 transmit.
- **LAWICEL:** hex DLC and ID masking.
- **Send Frames:** FD/BRS columns.
- **Scripts:** `can.sendFDFrame()`.

### 2026-10-10: GVRET fixes (`fix-gvret-qt6`)

- **collin80/SavvyCAN#910:** fixed the crash when saving bus settings under Qt 6.
- **collin80/SavvyCAN#1121:** fixed zero-length and CAN FD frame parsing.
