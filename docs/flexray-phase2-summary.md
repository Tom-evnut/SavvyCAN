# FlexRay Phase 2 — summary of changes

Branch: `flexray-phase2-model` (commit `946eace`), merged into `fork-ci`. All test suites
green (flexray 21, picoflexray 3, picoflexrayinject 9); builds pass with and without libusb.

## Goal

Make FlexRay **slot** and **cycle** first-class in SavvyCAN instead of stuffing the cycle
count into payload byte 0 — while keeping every existing capture, DBC, and log loading
byte-for-byte compatible.

## What changed

### 1. Cycle is now a real CommFrame field (`can_structs.h`)
- Cycle stored in the `flexRayInfo` quint64 bitfield (6 bits at `FR_CYCLE_SHIFT = 45`,
  presence flag `FR_HAS_CYCLE = 1<<51`).
- New accessors: `setFlexRayCycle()`, `hasFlexRayCycle()`, `flexRayCycle()`, and
  `flexRayDecodePayload()` (returns `[cycle][payload]` for FlexRay, plain payload otherwise).
- `payload()` now returns the **true** FlexRay payload (0–254 bytes), no cycle byte.
- `isValid()` for FlexRay relaxed to `length <= 254` (empty payload is now valid — needed
  for null frames).
- Fix that mattered: `setFlexRayHeader()` now **preserves** the cycle bits (`FR_HAS_CYCLE`
  + the 6 cycle bits) instead of wiping them. The parser sets the cycle before the header,
  so without this the cycle came back as 0.

### 2. Parser & injector use the field, not byte 0
- `connections/picoflexrayparser.cpp`: reads payload straight from the record and calls
  `setFlexRayCycle(cycle)` + `setPayload(payload)` — no more prepending the cycle byte.
- `connections/picoflexray.cpp` (`overridePayloadFor`): takes the cycle from
  `frame.flexRayCycle()` and the data from `frame.payload()` (was `payload.at(0)` /
  `payload.mid(1)`).

### 3. DBC decoding stays compatible via a rebuild buffer
- `dbc/dbc_classes.cpp`: all signal extraction now reads `flexRayDecodePayload()`, which
  rebuilds `[cycle][payload]`. So the pico-flexray / cabana DBCs (e.g. `dbc/lateral.dbc`)
  that multiplex on the cycle in byte 0 keep decoding **unchanged** — the authoritative
  cycle field now supplies that byte 0.
- `re/graphingwindow.cpp`: DBC-signal graphs use the decode buffer; raw-bit graphs keep the
  true payload (so raw bit tools don't miscount).

### 4. Slot and Cycle columns in the frame list (`canframemodel.*`)
- New **Slot** and **Cycle** columns (Column enum reordered so Data stays last/stretched).
- Slot = frame ID (3-digit hex), Cycle = `flexRayCycle()`; both blank for non-FlexRay.
- Sorting and center alignment wired up for the new columns.

### 5. Native CSV round-trips the fields
- `framefileio.cpp`: on save, FlexRay rows write `[cycle][payload]` on disk (unchanged
  layout); on load (v2), FlexRay splits byte 0 back into the cycle field. **Old logs load
  exactly as before.**

### Docs
- `FORK-CHANGES.md`, `docs/flexray-re-roadmap.md` (Phase 2 marked ✅ done),
  `help/newconnection.md` updated.

## Design in one line

On disk and over USB nothing moved; in memory the cycle is a separate field so RE tools see
clean payload, and DBC decode transparently reconstructs the old `[cycle][payload]` byte
layout so existing DBCs still work.

## Not done yet
- Not verified on real hardware (the Fork Build run produced the Windows package for the
  bench test).
- Phases 3 (RE accelerators: E2E/CRC helper, cycle-aware analysis, slot timeline) and 4
  (playback, interactive injection) remain future work.
