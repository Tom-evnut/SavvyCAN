# SavvyCAN LIN support: current state and rollout plan

Scope: taking LIN from a placeholder to full support in SavvyCAN (Tom-evnut fork): capture, decoding, sending,
log files and reverse-engineering tools. Written against `fork-ci` plus the `flexray-pico` branches, whose FlexRay
work is the template for most of this.

---

## 1. Where LIN stands today

LIN has a frame type and a label, and nothing produces or consumes it.

| Area | State |
|---|---|
| Frame model (`can_structs.h`) | `CommFrame::LINDataFrame` (format `0x3`) exists and round-trips through `setFrameType()` / `frameType()`; it maps to a Qt `DataFrame`. `isDataFrame()` excludes it, so DBC decoding and graphing would skip LIN frames. `isValid()` treats it like classic CAN (any 11-bit ID, up to 8 bytes) |
| Frame list (`canframemodel.cpp`) | Type column shows `L` |
| GVRET (`connections/gvretserial.cpp`) | Reads LIN1/LIN2 enable and baud from `GET_EXT_BUSES` (13) and writes them back with opcode 14, but only logs them: only the SWCAN bus is applied. No opcode carries LIN frames |
| Bus model (`connections/canbus.h`) | CAN only: speed, listen-only, single-wire, CAN FD. No bus type, so a LIN bus can't be represented |
| Other connections | None produce LIN (QtSerialBus, LAWICEL, socketcand, MQTT, CANserver, CANlogserver, pico-flexray) |
| Log files (`framefileio.cpp`, `blfhandler.cpp`, `pcaplite.cpp`) | No format reads or writes LIN. BLF skips non-CAN objects |
| Decoding | No LDF parser. A DBC can't target LIN frames (LIN frames aren't data frames, and DBC messages match on ID and bus only) |
| Sending, playback, scripts | CAN / CAN FD only |
| RE tools, protocol handlers | Nothing LIN-specific (no checksum model handling, no LIN transport layer / diagnostics) |
| Help, FORK-CHANGES | No LIN pages |

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

### D1. Which hardware first

| Option | For | Against |
|---|---|---|
| **A. GVRET firmware (ESP32RET fork, M2RET)** | The GVRET protocol already has LIN1/LIN2 bus settings; the fork already maintains ESP32RET (`Tom-evnut/ESP32RET`) and the GVRET test mocks; LIN is UART-based and cheap to add to an ESP32 with a LIN transceiver | Needs new GVRET opcodes (a protocol proposal upstream, like #1140); needs a board with a LIN transceiver; UART break/error detection on ESP32 is coarse |
| **B. Pico-based LIN sniffer/injector** (like pico-flexray) | PIO gives bit-level timing: break length, sync errors, per-byte timestamps, collision detection; same USB/libusb path as pico-flexray | A new firmware project to maintain |
| C. Commercial adapters (PEAK PLIN-USB, Vector XL, Kvaser LIN) | Robust, master and slave modes, many users already own one | Vendor SDKs, Windows-centric, licensing to check per SDK; one driver per vendor |
| D. Linux `sllin` (LIN over SocketCAN) | Reuses the QtSerialBus SocketCAN path | Out-of-tree driver; LIN details packed into CAN ID flags; Linux only. Check its current state before committing to it |

**Recommendation:** A first. It reuses the most existing code and test bench, and the protocol design work
benefits upstream GVRET users. Keep the connection layer generic so B and C can follow as separate connections.

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

**Coordinate with FlexRay phase 2.** `CommFrame` currently has a FlexRay-only packed field (`flexRayInfo`), and
FlexRay phase 2 plans a proper metadata redesign. Do one design for both: a per-protocol metadata field with
typed accessors (`linChecksum()`, `linErrors()`, ...), rather than a second ad-hoc packed field. That change touches
every file format and the model, so land it once, reviewed, before either protocol builds on it.

### D3. How a LIN bus is represented

- Add a **bus type** (CAN, CAN FD capable, LIN, FlexRay) to `CANBus` / `BusData`, set by the driver, so the
  connection window, filters, the DBC/LDF manager and the sender know what each bus number is.
- LIN bus settings: speed, mode (**monitor** / **master** / **slave**), default checksum model, LIN version
  (1.3 / 2.x / J2602).
- For GVRET: LIN1 and LIN2 become buses after SWCAN (bus 3 and 4 on a device with CAN0, CAN1, SWCAN), and the
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

- **Model:** the D2 frame semantics and metadata (on top of the shared metadata design), `isValid()`,
  `isDataFrame()`, PID parity and both checksum models as helpers with unit tests.
- **Bus type:** D3, with LIN buses shown in the connection window (speed, mode read-only at first).
- **GVRET:** a receive opcode for LIN frames (ID, bus, data, checksum, error flags, device timestamp), applying the
  LIN1/LIN2 settings from `GET_EXT_BUSES`; firmware side in the ESP32RET fork. Old firmware keeps working (the
  opcode is only expected after the device says it supports it, as with `GET_FD`).
- **Frame list:** `L` for LIN, `LN` for a header with no response, `L!` for checksum/parity errors; Frame Details
  shows PID, checksum, checksum model and errors. Overwrite mode keeps header-only IDs (as for FlexRay null slots).
- **Native CSV:** `LIN` in the flags column plus checksum and error flags; old files load unchanged.
- **Done when:** traffic from a real LIN bus (e.g. a seat, mirror or HVAC panel module) shows with correct IDs,
  no-response headers, checksum validity and device timing, and survives a native CSV save/load.

### Phase 2: decoding

- **LDF loader** (D4) in the DBC manager: LIN 2.0-2.2 and J2602 first, LIN 1.3 where cheap (LIN 1.x frame length
  comes from ID bits 4-5). Signals are little-endian, LSB first; logical and physical encodings map onto value
  tables and factor/offset.
- **DBC/LDF bus binding by frame type** (D4 collisions), DBC files usable on LIN buses.
- **Event-triggered frames:** byte 0 is the PID of the unconditional frame carried; decode against that frame.
- **Signal views:** interpretation, graphing, signal viewer and Flow View work on LIN data frames.
- Optional: LDF save, or LDF to DBC export.
- **Done when:** a capture decodes against a real LDF (and against a converted DBC) with no CAN/LIN cross-matches.

### Phase 3: sending

- **Firmware + GVRET:** a transmit opcode for master mode (header, or header + response) and a slave response table
  (respond to a given ID with given data and checksum model). Bus mode settable from SavvyCAN.
- **Send Frames window:** LIN rows (bus, ID, data, checksum model, master/slave-response); scripting gets
  `lin.sendFrame()` / slave response helpers.
- **Schedule table runner:** play an LDF schedule table as master (with the ability to override individual frames'
  data), which is what you need to drive a module on the bench without its car.
- **Playback:** replay a LIN log as master (headers + recorded responses), or as a slave responder only.
- **Done when:** a real LIN slave module can be woken and driven from SavvyCAN using its LDF schedule, and a captured
  session can be replayed to it.

### Phase 4: log file interoperability

- **Vector ASC** LIN lines and **BLF** LIN objects (frames and the main error objects), load first, ASC save after.
- **Wireshark pcap** with the LIN link type.
- Other formats only where a user shows up with files (most CAN-only formats have no LIN representation).
- **Done when:** LIN captures from CANalyzer/CANoe and Wireshark load with IDs, data, checksums and errors intact,
  cross-checked against a reference tool in the test bench.

### Phase 5: reverse-engineering and diagnostics

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
- A mock GVRET device that speaks the new LIN opcodes (in the existing test bench), including old firmware that
  doesn't.
- LDF parser tests against sample LDFs (including event-triggered frames and LIN 1.3 lengths) and a cross-check
  against an independent LDF parser (e.g. Python `ldfparser`).
- Log file cross-checks (ASC, BLF, pcap) against python-can or the vendor tool output, as for CAN FD.
- Hardware: a LIN bench with a real slave module and the chosen interface, before each phase is marked done.

---

## 5. Risks / notes

- **`CommFrame` metadata** is the change everything rides on; do it together with FlexRay phase 2 (D2).
- **Bus type** touches saved settings (`CANBus` stream operators): keep old saved connections loading.
- **GVRET protocol changes** need firmware and an upstream protocol proposal; keep them behind a capability query so
  old devices and old SavvyCAN builds don't misread the stream.
- **LIN is master-driven:** sending as master on a car's bus fights the real master. Monitor stays the default mode,
  and master/slave modes are explicit choices.
- **Upstream:** like the FlexRay work, this is fork-first; split the generic pieces (bus type, frame model, LDF
  loader) so they can be offered upstream independently of any one device.

---

## 6. Suggested sequencing

1. Phase 0 decisions (D1-D4), including the shared CAN/FlexRay/LIN metadata design.
2. Phase 1 (model, bus type, GVRET receive, frame list, native CSV), firmware and SavvyCAN together.
3. Phase 2 (LDF and decoding).
4. Phase 3 (sending) and phase 4 (log files) are independent; run them in parallel once phase 2 lands.
5. Phase 5 accelerators last, on top of real captures.
