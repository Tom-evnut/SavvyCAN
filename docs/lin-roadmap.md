# SavvyCAN LIN support: current state and rollout plan

Scope: taking LIN from a placeholder to full support in SavvyCAN (Tom-evnut fork): capture, decoding, sending,
log files and reverse-engineering tools. Written against `fork-ci` plus the `flexray-pico` branches, whose FlexRay
work is the template for most of this. The first LIN interface is a Microchip APGDT001 (LIN Serial Analyzer),
tested against a real LIN slave module.

---

## 1. Where LIN stands (2026-10-16)

Phases 1 to 4 are largely done and the APGDT001 side is confirmed on hardware (BMW E90 HVAC flap motors at
9600 bit/s: monitoring, header-only polling and frames with data, as master). Everything is merged in `fork-ci-lin`
(fork-ci plus every LIN branch), which CI builds for Linux and Windows on every push.

| Area | State |
|---|---|
| Frame model | `CommFrame` LIN frames: 6-bit ID, response data, packed `linInfo` (checksum, model, received PID, errors, publisher, device time); PID parity and both checksums (`lin-phase1-model`) |
| Bus model | Bus type CAN/LIN/FlexRay, LIN mode (monitor/master/slave) and checksum model, saved settings still load (`lin-phase1-model`) |
| APGDT001 | Monitor and master, confirmed on the device; frames sent one frame slot apart (`lin-phase1-model`, `lin-send`) |
| PEAK LIN | Monitor and master through PLinApi.dll loaded at run time, Windows only; not tried on a device (`lin-peak`, `lin-send`) |
| Display | `L` / `LN` / `!` in the frame list, a LIN section in Frame Details; overwrite mode keeps header-only IDs |
| Decoding | LDF loader into the DBC classes (LIN 1.3 to 2.2A, J2602, event-triggered frames), files matched on frame type, ID-only lookups (graphs, signal viewer, filter labels, Frame Details, file comparator) LIN-aware (`lin-phase2-ldf`, `lin-id-lookup`) |
| Sending | As master from Send Frames, scripts and playback; LDF schedule tables kept and run by Send Frames > LIN Schedule Runner (`lin-send`, `lin-schedule`) |
| Log files | Native CSV flags; LIN text logs; Treat Frames as LIN for flag-less captures; BLF save and load (frames, checksum and send errors, receive errors; sleep/wake-up counted); Wireshark pcap/pcapng load and pcapng save; a warning before saving LIN frames to a format that can't mark them (`lin-phase1-model`, `lin-phase2-ldf`, `lin-blf`, `lin-blf-events`, `lin-pcap`, `lin-save-warning`) |
| RE tools | RE Tools > LIN Schedule Analysis: per-ID period, response rate, checksum models, and the schedule table reconstructed from a capture (`lin-schedule-analysis`) |
| Not done | Slave mode, node configuration commands in schedules, LIN rows in Send Frames and `lin.sendFrame()`, LIN transport layer / diagnostics, GVRET LIN, PEAK on Linux, ASC LIN |

**Branches** (each merged into `fork-ci-lin`; none is upstream yet):

| Branch | Contains | What |
|---|---|---|
| `lin-roadmap` | `fork-ci` | This document |
| `lin-phase1-model` | `fork-ci` | Frame model, bus type, APGDT001 connection, display, native CSV |
| `lin-phase2-ldf` | `lin-phase1-model` | LDF loader, matching on frame type, LIN text logs, Treat Frames as LIN |
| `lin-peak` | `lin-phase1-model` | PEAK LIN connection |
| `blf-save` | `fork-ci` | BLF saving for CAN and CAN FD, BLF loader fixes (can go upstream on its own) |
| `lin-blf` | `lin-peak`, `blf-save` | LIN in BLF files |
| `lin-send` | `lin-blf` | Sending as master (APGDT001, PEAK), send spacing |
| `lin-schedule` | `lin-send` | LDF schedule tables and the LIN Schedule Runner |
| `lin-id-lookup` | `lin-phase2-ldf` | LIN-aware lookups where only an ID is known |
| `lin-save-warning` | `lin-blf` | Asks before saving LIN frames to a format that can't mark them |
| `lin-pcap` | `lin-save-warning` | Wireshark LIN load, pcapng save, pcaplite fixes |
| `lin-blf-events` | `lin-blf` | BLF LIN receive errors, sleep and wake-up events |
| `lin-schedule-analysis` | `lin-phase2-ldf` | RE Tools > LIN Schedule Analysis |

### LIN basics the design has to carry

- A frame is a **header** sent by the master (break, sync `0x55`, protected ID) and an optional **response** of
  1-8 data bytes plus a checksum, sent by whichever node publishes that ID (master or a slave).
- **Protected ID (PID)** = 6-bit frame ID (0-63) + two parity bits. IDs 0-59 carry signals; `0x3C` is the master
  request and `0x3D` the slave response (diagnostics); 62-63 are reserved.
- **Checksum:** *classic* (data bytes only; LIN 1.x, and always for `0x3C`/`0x3D`) or *enhanced* (PID + data;
  LIN 2.x). Which one an ID uses is part of what you reverse-engineer.
- A header with **no response** is normal (unconditional frame whose slave is absent, sporadic or event-triggered
  slot with nothing to send) and is itself information.
- Typical speeds: 19200, 10417 and 9600 bit/s; up to 20 kbit/s.
- Errors worth keeping: parity error, checksum error, no response, incomplete response, sync/framing errors, and
  collisions on event-triggered frames.

---

## 2. Design decisions to make first (phase 0)

These shape everything after and are cheap to settle before code lands.

### D1. Which hardware first (decided: APGDT001)

| Option | For | Against |
|---|---|---|
| **APGDT001 (Microchip LIN Serial Analyzer)** | Already on the bench; USB; Microchip lists host (master), slave and non-intrusive snooper roles, so it covers monitoring and both sending modes | Its PC protocol isn't publicly documented (mostly worked out below from Microchip's demo); coarse timestamps (0.409 ms); a new connection type, not a reuse of GVRET |
| A. GVRET firmware (ESP32RET fork, M2RET) | The GVRET protocol already has LIN1/LIN2 bus settings; the fork already maintains ESP32RET (`Tom-evnut/ESP32RET`) and the GVRET test mocks; LIN is UART-based and cheap to add to an ESP32 with a LIN transceiver | Needs new GVRET opcodes (a protocol proposal upstream, like #1140); needs a board with a LIN transceiver; UART break/error detection on ESP32 is coarse |
| B. Pico-based LIN sniffer/injector (like pico-flexray) | PIO gives bit-level timing: break length, sync errors, per-byte timestamps, collision detection; same USB/libusb path as pico-flexray | A new firmware project to maintain |
| C. Commercial adapters (PEAK PLIN-USB, Vector XL, Kvaser LIN) | Robust, master and slave modes, many users already own one | Vendor SDKs, Windows-centric, licensing to check per SDK; one driver per vendor |
| D. Linux `sllin` (LIN over SocketCAN) | Reuses the QtSerialBus SocketCAN path | Out-of-tree driver; LIN details packed into CAN ID flags; Linux only. Check its current state before committing to it |

**Decision:** the APGDT001 first, because it's the adapter on the bench; **PEAK LIN second** (not on the bench yet,
see below). Keep the connection layer generic so GVRET (A), a Pico sniffer (B) and other vendor adapters (C) can
follow as separate connections.

**PEAK LIN interfaces (second connection).** PEAK's LIN hardware: PLIN-USB (one LIN channel), PCAN-USB Pro FD (two
CAN FD + two LIN) and the discontinued PCAN-USB Pro (two CAN + two LIN). From PEAK's pages, to check against
their documentation when the work starts:

- **Windows:** PLIN-API (`PLinApi.dll` + header), installed with PEAK's driver. Load the DLL at run time, as Qt's
  PEAK CAN plugin does with `PCANBasic.dll`, so SavvyCAN builds and runs without it and the connection only appears
  when it's installed. Check the API's licence for shipping its header in the repository.
- **Linux:** PEAK's LIN driver (1.3.0 covers all three devices) exposes a character-device interface rather than
  PLIN-API, so the Linux side is a second, smaller backend behind the same connection.
- **What it gives over the APGDT001:** a hardware client per channel that can be monitor, master or slave; frame
  entries per ID (direction, length, checksum type) and schedule tables kept in the device; device timestamps and
  per-frame error flags from the driver. That covers phase 3's master and slave modes and schedule runner in the
  device instead of in SavvyCAN.
- **CAN on the Pro FD / Pro** keeps going through QtSerialBus (`peakcan`); the LIN channels are a separate
  connection with buses of type LIN (D3).
- **Testing without hardware:** a fake PLIN backend in the unit tests (the connection talks to an interface, not
  the DLL directly); bench testing once a device is available.

**How the APGDT001 talks to a PC.** Worked out from Microchip's *LIN Library API Demo* (Java; library jars read as
bytecode, not run), then checked on the device on 2026-10-16 (see *Confirmed on the device* below).

- **Transport: USB HID**, no vendor driver. The library opens the HID device whose product ID is `0x0A04` (it
  doesn't check the vendor ID; Microchip's is `0x04D8`) and exchanges 64-byte reports, report ID 0 (65 bytes with
  the ID on Windows). Qt has no HID module, so the connection needs **hidapi**, an optional build dependency like
  libusb is for pico-flexray. Works on Windows, Linux and macOS.
- **Firmware family: PICkit Serial Analyzer.** The library calls the device "PKSA"/"PICkitS"; commands, the
  configuration block and the tagged receive stream follow that tool's design.
- **Output reports** (byte 0 = report ID, then a command):
  - `01 00` cold reset, `01 01` warm reset, `01 02` status request, `01 07` clear comms, `0B AB` special status
    request, `03 01 20` reset the event timer.
  - `02 …` writes the configuration block (about 24 bytes; written between a cold and a warm reset, then read back
    with a status request and compared). Bytes 22-23 of the block set the **bit rate**: a 16-bit divisor
    `round(20 MHz / 4 / baud) - 1`, low byte first. The library's default block gives 10 kbit/s; the demo then sets
    19200.
  - `03 <n+6> 84 <n+2> <PID> <data…> <checksum> 1F 77 00` sends a frame **as master** (header + response with
    `n` data bytes; the first length counts the script bytes from `84` to `77`, the second the PID, data and
    checksum): a small script for the PICkit Serial engine. The host computes the PID and the checksum, so
    either checksum model works, and a wrong checksum can be sent on purpose.
  - Slave responses (the device answering headers as a **slave**) live in a device buffer the library calls
    CBUF3: cleared with `0A`, loaded with `05 <count> …` ("slave profiles").
  - Listen / transmit / display-all are host-side parser modes in the library, not device commands.
- **Input reports** are a run of packets, each starting with a type byte: `80` ends the report; `81`, `82`, `83`,
  `84`, `88` are fixed-size status packets (3, 25, 21, 7 and 2 bytes); `85`, `86`, `87` are `<type> <len> <bytes>`
  and carry the receive stream.
- **Receive stream** (tags; `0x1n` tags are the PICkit Serial common ones, `0x8n` the LIN ones):
  - `83`: break seen, a new frame starts (and ends any frame still being built);
  - `10 b`: one byte from the bus: the first is the protected ID, then the data bytes and the checksum;
  - `13 lo hi`: event timer, in ticks of **0.409 ms**; `14`: timer rollover (+26.8 s);
  - `85 lo hi`: bit rate measured by the device; `82 x`: status error; `1C 77`: script finished;
  - `11 n …`: a block of bytes; `12`, `15`, `1A`, `80`, `81`, `86`, `87` carry bytes the library skips; `84`, `88`,
    `16`-`19`, `1B` stand alone.
- **What that means for SavvyCAN:**
  - Frames aren't delimited by the device. A frame ends at the next break, after 10 bytes (PID + 8 data + checksum)
    or after a bit-rate-dependent timeout; the last byte is the checksum. A PID with nothing after it is a header
    with no response.
  - The device doesn't check checksums or parity. `CommFrame::verifyLinChecksum()` and the PID parity check do it,
    and find out each ID's checksum model.
  - Timestamps come from the device but are coarse: 0.409 ms per tick (about 2 bit times at 19200), one per frame.
    Good enough for schedules and periods, not for bit timing.
  - Errors the library reports per frame: bus timeout (1), timer error (3), status error (4), event marker error
    (5), next header received before the frame finished (6). They map onto `CommFrame`'s LIN error flags
    (no response / incomplete response / sync) once checked on the device.
- **Confirmed on the device** (2026-10-16, an APGDT001 driving BMW E90 HVAC flap motors, logged with a raw HID
  probe and then with SavvyCAN itself):
  - it enumerates as vendor `0x04D8`, product `0x0A04`, with 65-byte input and output reports;
  - cold reset, configuration block, warm reset, status request: the status reply echoes all 24 configuration
    bytes in its `82` packet, at 9600 and 19200 bit/s;
  - the bus stream comes in `86` packets; each report's packets end at `80`, and the rest of the report is left
    over from earlier ones (the parser must stop at `80`);
  - the event timer ticks every 0.409 ms; the cold reset can deliver `14` rollovers buffered since the last session;
  - the header-only transmit script (`03 05 84 01 <PID> 1F 77`) sends a header and the slaves answer it; frames
    with data go out too;
  - in the stream, the script status (`1C 77`) comes before the device's own PID byte;
  - **the device starts each transmit script at once**, even while a slave is still answering: headers sent back to
    back went out 4 ms apart at 9600 bit/s and nobody answered. SavvyCAN spaces them one frame slot apart;
  - slaves that have been quiet go to sleep and don't answer the first header (it wakes them).
- **Still open:** the rest of the configuration block (role, autobaud); the slave profile layout (CBUF3) for slave
  mode; what `85` and `87` packets carry; how the library's per-frame errors show in the stream.
- **Licence:** Microchip's demo and library may only be used with Microchip products. SavvyCAN's connection must be
  written from the protocol as described here, not ported from Microchip's code, and none of their files go into
  the repository (they stay in the git-ignored `test/lin-data/apgdt001/`).

### D2. How a LIN frame lives in `CommFrame`

- **Frame ID** = the 6-bit unprotected ID (0-63), never extended. The PID is derived (parity is computed, and a
  received PID with bad parity is flagged, not stored as a different ID).
- **Payload** = response data bytes only (0-8). Zero bytes = header with no response.
- **Metadata** (needs a home, like FlexRay's header details): received checksum byte, checksum model
  (classic / enhanced / unknown), checksum OK, error flags (parity, checksum, no response, incomplete, framing,
  collision), who published the response if the device knows (master / slave), and device timestamp.
- **Validity:** `isValid()` for LIN: ID <= 63, payload <= 8, not extended. `setPayload()` must never turn a LIN frame
  into CAN FD.
- **`isDataFrame()`:** true for LIN frames that have a response, so DBC/LDF decoding, graphing and the analysis
  tools pick them up.

**Decided: same pattern as FlexRay, independent of it.** On `flexray-phase1-capture`, FlexRay header details live in
one packed `quint64 flexRayInfo` field with typed accessors. LIN gets its own `linInfo` field built the same way
(flag bits, shifts, `setLin...()` / `lin...()` accessors), and LIN branches from and merges into `fork-ci` without
needing the FlexRay branches. When both are in `fork-ci` the two fields sit side by side; FlexRay phase 2's metadata
redesign can fold them into one per-protocol field later, behind the same accessors.

### D3. How a LIN bus is represented

- Add a **bus type** (CAN, CAN FD capable, LIN, FlexRay) to `CANBus` / `BusData`, set by the driver, so the
  connection window, filters, the DBC/LDF manager and the sender know what each bus number is.
- LIN bus settings: speed, mode (**monitor** / **master** / **slave**), default checksum model, LIN version
  (1.3 / 2.x / J2602).
- The APGDT001 is one LIN bus (bus 0 of its connection), with speed and role set from SavvyCAN if its command set
  allows.
- Later, for GVRET: LIN1 and LIN2 become buses after SWCAN (bus 3 and 4 on a device with CAN0, CAN1, SWCAN), and the
  baud/enable values already read from `GET_EXT_BUSES` are applied to them.

### D4. Decoding: LDF and DBC

- **LDF** is the native LIN description (nodes, frames, signals, encodings, schedule tables, diagnostic frames,
  event-triggered and sporadic frames). Parse it into the existing DBC classes (`DBCFile`, `DBC_MESSAGE`,
  `DBC_SIGNAL`) so interpretation, graphing, signal viewer and the editor work without a parallel decoding stack.
  Keep LIN-only parts (schedule tables, node attributes, encoding types) in side structures for the sender and
  RE tools.
- **DBC for LIN** (people often convert LDF to DBC) works once messages can be bound to LIN frames.
- **ID collisions:** LIN IDs 0-63 overlap CAN IDs. Message lookup must match frame type as well as ID and bus, or a
  CAN DBC on "all buses" will decode LIN frames and vice versa.

---

## 3. Rollout plan (phased)

Each phase is one branch (or a short series) with help and `FORK-CHANGES.md` updated as it lands, as for FlexRay.

### Phase 1: receive and display (monitor mode)

**Status: done** except GVRET; the APGDT001 is confirmed on the device, PEAK hasn't been tried on one.

- **Model:** the D2 frame semantics and metadata (on top of the shared metadata design), `isValid()`,
  `isDataFrame()`, PID parity and both checksum models as helpers with unit tests.
- **Bus type:** D3, with LIN buses shown in the connection window (speed, mode read-only at first).
- **APGDT001 connection:** "Microchip LIN Serial Analyzer (APGDT001)" in the new-connection dialog, listening only:
  hidapi (optional at build time, like libusb), reset and configuration block with the bit rate, then the
  receive stream parsed into `CommFrame`s (D1): PID parity and checksum checked with the step 1 helpers, header-only
  frames kept, the library's per-frame errors mapped to LIN error flags. The 0.409 ms event timer, unwrapped with its
  rollover markers, mapped onto SavvyCAN's clock as the pico-flexray parser does. A parser kept separate from the
  USB code, as `picoflexrayparser` is, so the mock and the tests can feed it bytes. *Done on `lin-phase1-model`
  (monitor mode, not yet tried on a device): `connections/apgdt001parser.*` (receive stream),
  `apgdt001session.*` (commands, configuration check, quiet-bus flush) and `apgdt001.*` (hidapi), with unit tests
  for the first two. The LIN bus type and settings (D3) are done too. Confirmed on the device 2026-10-16.*
- **PEAK LIN connection (after the APGDT001, separate branch):** "PEAK LIN (PLIN-USB, PCAN-USB Pro / Pro FD)",
  monitor mode first: PLIN-API loaded at run time on Windows, PEAK's LIN driver on Linux, one LIN bus per channel,
  device timestamps and error flags mapped onto `CommFrame`'s LIN details (D1).
- **GVRET (later, separate branch):** a receive opcode for LIN frames, applying the LIN1/LIN2 settings from
  `GET_EXT_BUSES`; firmware side in the ESP32RET fork. Old firmware keeps working (the opcode is only expected after
  the device says it supports it, as with `GET_FD`).
- **Frame list:** `L` for LIN, `LN` for a header with no response, `L!` for checksum/parity errors; Frame Details
  shows PID, checksum, checksum model and errors. Overwrite mode keeps header-only IDs (as for FlexRay null slots).
- **Native CSV:** `LIN` in the flags column plus checksum and error flags; old files load unchanged.
- **Done when:** traffic between the APGDT001 and the bench slave module (with Microchip's GUI as master, or another
  master) shows in SavvyCAN with correct IDs, no-response headers, checksum validity and timing, and survives a
  native CSV save/load.

### Phase 2: decoding

**Status: done** except LDF save/export (optional) and the BCD/ASCII encodings (shown as raw values). Checked against python `ldfparser` on real LDFs, and real captures decode.

- **LDF loader** (D4) in the DBC manager: LIN 2.0-2.2 and J2602 first, LIN 1.3 where cheap (LIN 1.x frame length
  comes from ID bits 4-5). Signals are little-endian, LSB first; logical and physical encodings map onto value
  tables and factor/offset.
- **DBC/LDF bus binding by frame type** (D4 collisions), DBC files usable on LIN buses.
- **Event-triggered frames:** byte 0 is the PID of the unconditional frame carried; decode against that frame.
- **Signal views:** interpretation, graphing, signal viewer and Flow View work on LIN data frames.
- Optional: LDF save, or LDF to DBC export.
- **Done when:** a capture decodes against a real LDF (and against a converted DBC) with no CAN/LIN cross-matches.

### Phase 3: sending

**Status: master mode done** on the APGDT001 (confirmed on the device) and PEAK (untested), from Send Frames, scripts and playback, and LDF schedule tables run by the LIN Schedule Runner (`lin-schedule`). **Not done:** slave mode (APGDT001 slave profiles, PEAK publisher frame entries), node configuration commands (AssignNAD and the others) and sporadic frames in schedules, LIN rows in Send Frames, `lin.sendFrame()`, PEAK schedules in the device, playback as a slave responder only, GVRET.

- **APGDT001:** master mode (header, or header + response) and slave mode (respond to a given ID with given data and
  checksum model), using its host and slave roles. Bus mode settable from SavvyCAN.
- **PEAK LIN:** master and slave through the device's own frame table and schedules (its hardware client), so a
  schedule from an LDF can be loaded into the device and run there.
- **GVRET (later):** matching transmit opcode and slave response table in the ESP32RET fork.
- **Send Frames window:** LIN rows (bus, ID, data, checksum model, master/slave-response); scripting gets
  `lin.sendFrame()` / slave response helpers.
- **Schedule table runner:** play an LDF schedule table as master (with the ability to override individual frames'
  data), which is what you need to drive a module on the bench without its car.
- **Playback:** replay a LIN log as master (headers + recorded responses), or as a slave responder only.
- **Done when:** a real LIN slave module can be woken and driven from SavvyCAN using its LDF schedule, and a captured
  session can be replayed to it.

### Phase 4: log file interoperability

**Status: done for BLF and Wireshark** (load and save; checked with python-can and tshark 4.4), plus native CSV and LIN text logs. **ASC LIN is blocked** until there's a sample file or Vector's format document.

- **Vector ASC** LIN lines and **BLF** LIN objects (frames and the main error objects), load first, ASC save after.
- **Wireshark pcap** with the LIN link type.
- Other formats only where a user shows up with files (most CAN-only formats have no LIN representation).
- **Done when:** LIN captures from CANalyzer/CANoe and Wireshark load with IDs, data, checksums and errors intact,
  cross-checked against a reference tool in the test bench.

### Phase 5: reverse-engineering and diagnostics

**Status: started.** Checksum model per ID (Frame Details) and schedule reconstruction (RE Tools > LIN Schedule Analysis, `lin-schedule-analysis`) are done. **Not done:** the LIN transport layer and diagnostics, UDS over LIN, and LIN in the sniffer, bit-flip, range state and fuzzing tools.

- **Checksum model detection:** per ID, report classic vs enhanced from captured responses (a quick hint about
  LIN version and node generation).
- **Schedule reconstruction:** per-ID period and order, response rate, which headers go unanswered (absent or
  sporadic slaves, event-triggered slots); a timeline view shared with FlexRay roadmap phase 3's slot view if that
  lands first. Needs phase 1 device timestamps.
- **LIN transport layer and diagnostics:** a `bus_protocols` handler for the `0x3C`/`0x3D` transport layer (NAD,
  PCI, segmented messages), node configuration services (read by identifier, assign NAD, assign frame ID range),
  and UDS over LIN in the UDS scanner.
- **Analysis tools:** sniffer, bit-flip, range state and fuzzing on LIN data frames (fuzzing slave responses via the
  phase 3 slave table).
- **Done when:** a module's schedule, checksum models and diagnostic identity can be worked out in-app from a
  capture plus active probing.

---

## 4. Testing

- Unit tests for PID parity and both checksums against known vectors, and for `isValid()` / `isDataFrame()` on LIN.
- A mock APGDT001 in the test bench (Zombie-CANFD-Testbench `tests/savvycan`), built from the USB capture, so the
  connection is tested without hardware. Later, a mock GVRET device with the LIN opcodes, including old firmware
  that doesn't have them.
- Real LDF files and captures live in `test/lin-data/` (`ldf/`, `captures/`), which a local `.gitignore` keeps out of
  git; tests that need them skip when the folder is empty.
- LDF parser tests against sample LDFs (including event-triggered frames and LIN 1.3 lengths) and a cross-check
  against an independent LDF parser (e.g. Python `ldfparser`).
- Log file cross-checks (ASC, BLF, pcap) against python-can or the vendor tool output, as for CAN FD.
- Hardware: the APGDT001 and the bench slave module, before each phase is marked done.
- **Done so far:** unit tests for every piece above (in `unit_tests/`, run under AddressSanitizer with leak
  detection); BLF CAN/CAN FD checked both ways with python-can 4.6; BLF and pcapng LIN checked with tshark 4.4.19;
  LDF layouts identical to python `ldfparser`; the APGDT001 on the bench (monitor, polling, data frames).

---

## 5. Risks / notes

- **`CommFrame` metadata** is the change everything rides on. `linInfo` follows `flexRayInfo`'s pattern (D2), so
  merging the two branches only conflicts textually in `can_structs.h`.
- **APGDT001 protocol:** worked out from Microchip's demo library, not from a specification; monitoring and master
  mode are now confirmed on the device (D1), slave mode isn't. It's plain HID, so no closed driver or DLL is needed
  on any platform.
- **APGDT001 limits:** 0.409 ms timestamps; frames delimited by the host (next break, length or timeout), so a
  response cut short by a timeout can't always be told from a shorter frame.
- **Microchip's licence:** their demo and library are used only to learn the protocol; nothing of theirs is copied
  into SavvyCAN or committed.
- **Bus type** touches saved settings (`CANBus` stream operators): keep old saved connections loading.
- **GVRET protocol changes** need firmware and an upstream protocol proposal; keep them behind a capability query so
  old devices and old SavvyCAN builds don't misread the stream.
- **LIN is master-driven:** sending as master on a car's bus fights the real master. Monitor stays the default mode,
  and master/slave modes are explicit choices.
- **Upstream:** like the FlexRay work, this is fork-first; split the generic pieces (bus type, frame model, LDF
  loader) so they can be offered upstream independently of any one device.

---

## 6. Next steps

1. Bench: run the LIN Schedule Runner against the BMW flap motors (`test/lin-data/ldf/BMW_HVAC_Flap_Motors.ldf`):
   polling, then Set_Position and Move_To_Position. That's phase 3's "done when".
2. LIN rows in Send Frames (checksum model per row, one-off frames) and editing master frame data by signal in the
   schedule runner.
3. Slave mode: APGDT001 slave profiles (needs the CBUF3 layout, from a USB capture of Microchip's GUI), PEAK
   publisher frame entries.
4. Node configuration commands in schedules (AssignNAD, ConditionalChangeNAD, ...), then the LIN transport layer
   and diagnostics (phase 5).
5. PEAK on a device, then PEAK on Linux; GVRET LIN with the ESP32RET firmware; ASC LIN when there's a sample.
6. Offer the generic pieces upstream: `blf-save` first, then the frame model, bus type and LDF loader.
