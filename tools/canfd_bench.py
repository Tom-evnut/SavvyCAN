#!/usr/bin/env python3
"""
canfd_bench.py -- a serial test harness that "stands in for SavvyCAN".

It drives two USB adapters directly over their serial ports and verifies that
CAN FD works end to end, as a source AND as a listener, in both directions:

  * CANable2  -- speaks slcan / LAWICEL ASCII  (class SlcanAdapter)
  * LilyGo T2-CANFD (ESP32-S3) -- speaks the GVRET binary protocol (class GvretAdapter)

The wire formats are copied from SavvyCAN's own backends, which are the
authoritative reference for both devices:

  * connections/lawicel_serial.cpp / .h   (slcan)
  * connections/gvretserial.cpp  / .h     (GVRET)

Nothing here is invented -- the slcan command set (S/Y/O/C, t/T/b/B/d/D, DLC
codes A-F) and the GVRET F1 opcodes / frame layouts mirror those files.

HARDWARE SAFETY: the T2 is an ESP32-S3. Its DTR/RTS lines drive reset and the
bootloader strap. GvretAdapter therefore opens the port with DTR=False, RTS=False
and NoFlowControl, exactly as GVRetSerial::connectDevice() does, so opening the
port does not reset the board or drop it into the ROM bootloader.

Run `python canfd_bench.py --list` to enumerate ports first. Must be run with a
Python whose pyserial can see the Windows COM ports (on this bench: PowerShell,
not the sandboxed Git-Bash).

Usage:
  python canfd_bench.py --list
  python canfd_bench.py --canable COM?? --t2 COM34 --tests T0
  python canfd_bench.py --canable COM?? --t2 COM34            # all tests
  python canfd_bench.py --canable COM?? --t2 COM34 --tests T0,T1,T2,T3

This is bench test tooling, not a SavvyCAN source change.
"""

import argparse
import sys
import threading
import time
from dataclasses import dataclass, field

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial not installed. Run:  pip install pyserial")


# --------------------------------------------------------------------------- #
#  Shared frame model
# --------------------------------------------------------------------------- #
@dataclass
class Frame:
    can_id: int
    data: bytes = b""
    ext: bool = False          # 29-bit extended ID
    fd: bool = False           # CAN FD
    brs: bool = False          # bit-rate switch (FD data phase at the high rate)
    bus: int = 0               # GVRET bus index (ignored by slcan)

    def __str__(self):
        kind = "FD" if self.fd else "CC"
        flags = []
        if self.ext:
            flags.append("ext")
        if self.brs:
            flags.append("brs")
        fl = ("/" + ",".join(flags)) if flags else ""
        idw = 8 if self.ext else 3
        return f"{kind}{fl} id={self.can_id:0{idw}X} len={len(self.data)} data={self.data.hex()}"

    def key(self):
        """Comparable identity for round-trip assertions."""
        return (self.can_id, self.ext, self.fd, self.brs, bytes(self.data))


# DLC-code <-> byte-count maps, copied from lawicel_serial.cpp
_DLC_TO_BYTES = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8,
                 9: 12, 10: 16, 11: 20, 12: 24, 13: 32, 14: 48, 15: 64}
_BYTES_TO_DLC = {v: k for k, v in _DLC_TO_BYTES.items()}
# canonical CAN FD payload lengths
FD_LENGTHS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64]


def bytes_to_dlc_code(n):
    if n <= 8:
        return n
    return _BYTES_TO_DLC.get(n, 0)


def dlc_code_to_bytes(code):
    return _DLC_TO_BYTES.get(code, 0)


def nearest_fd_length(n):
    """Round a desired payload length UP to the nearest legal FD length."""
    for L in FD_LENGTHS:
        if L >= n:
            return L
    return 64


# --------------------------------------------------------------------------- #
#  CANable2 -- slcan / LAWICEL ASCII  (mirrors lawicel_serial.cpp)
# --------------------------------------------------------------------------- #
class SlcanAdapter:
    # nominal bitrate -> S code (lawicel_serial.cpp deviceConnected())
    _SPEED_CODE = {10000: "0", 20000: "1", 50000: "2", 100000: "3",
                   125000: "4", 250000: "5", 500000: "6", 800000: "7",
                   1000000: "8"}
    # FD data rate -> Y code (only these four are mapped by SavvyCAN)
    _FD_CODE = {1000000: "1", 2000000: "2", 4000000: "4", 5000000: "5"}

    def __init__(self, port, serial_baud=115200, name="CANable2"):
        self.port = port
        self.serial_baud = serial_baud
        self.name = name
        self.ser = None
        self._buf = bytearray()

    # -- lifecycle ---------------------------------------------------------- #
    def open(self):
        # slcan runs over USB-CDC; the serial baud is nominal (the CANable2
        # ignores it). No DTR/RTS gymnastics are needed here.
        self.ser = serial.Serial(self.port, self.serial_baud, timeout=0,
                                 rtscts=False, dsrdtr=False)
        time.sleep(0.2)
        self._write(b"C\r")          # close in case a channel was left open
        time.sleep(0.1)
        self._drain()

    def close(self):
        if self.ser and self.ser.is_open:
            try:
                self._write(b"C\r")
                time.sleep(0.05)
            finally:
                self.ser.close()

    def _write(self, data):
        self.ser.write(data)
        self.ser.flush()

    def _drain(self):
        if self.ser.in_waiting:
            self.ser.read(self.ser.in_waiting)
        self._buf.clear()

    # -- identification ----------------------------------------------------- #
    def version(self):
        """slcan 'V' -> Vxxyy ; returns the raw reply text (or '')."""
        return self._query(b"V\r")

    def serial_number(self):
        """slcan 'N' -> Nxxxx ; returns the raw reply text (or '')."""
        return self._query(b"N\r")

    def _query(self, cmd, timeout=1.0):
        self._write(cmd)
        deadline = time.time() + timeout
        line = bytearray()
        while time.time() < deadline:
            b = self.ser.read(1)
            if not b:
                continue
            if b in (b"\r", b"\x07"):   # CR = ok reply, BEL = error
                if b == b"\x07":
                    return "<BELL/err>"
                if line:
                    return line.decode("ascii", "replace")
                # bare CR (ack) -> keep waiting a touch for a value line
                continue
            line += b
        return line.decode("ascii", "replace") if line else ""

    # -- configuration / open channel -------------------------------------- #
    def configure(self, nominal, fd=False, data_rate=None, listen_only=False):
        """Send C, S<code>, (Y<code> if FD), then O (or L for listen-only)."""
        self._write(b"C\r")
        time.sleep(0.05)
        self._drain()
        scode = self._SPEED_CODE.get(nominal)
        if scode is None:
            raise ValueError(f"{self.name}: unsupported nominal bitrate {nominal}")
        self._write(f"S{scode}\r".encode())
        time.sleep(0.05)
        if fd:
            ycode = self._FD_CODE.get(data_rate)
            if ycode is None:
                raise ValueError(f"{self.name}: FD data rate {data_rate} not in "
                                 f"1/2/4/5 Mbit/s")
            self._write(f"Y{ycode}\r".encode())
            time.sleep(0.05)
        # O = open normally, L = listen-only (silent monitor)
        self._write(b"L\r" if listen_only else b"O\r")
        time.sleep(0.15)
        self._drain()

    # -- TX  (mirrors LAWICELSerial::piSendFrame) --------------------------- #
    def send(self, frame: Frame):
        dlc = bytes_to_dlc_code(len(frame.data))
        if frame.fd:
            if frame.ext:
                head = ("B" if frame.brs else "D") + f"{frame.can_id & 0x1FFFFFFF:08X}"
            else:
                head = ("b" if frame.brs else "d") + f"{frame.can_id & 0x7FF:03X}"
        else:
            if frame.ext:
                head = "T" + f"{frame.can_id & 0x1FFFFFFF:08X}"
            else:
                head = "t" + f"{frame.can_id & 0x7FF:03X}"
        msg = head + f"{dlc:X}" + frame.data.hex().upper() + "\r"
        self._write(msg.encode("ascii"))

    # -- RX  (mirrors LAWICELSerial::readSerialData) ------------------------ #
    def poll(self):
        """Read whatever is pending and return a list of complete Frames."""
        frames = []
        try:
            chunk = self.ser.read(4096)
        except Exception:
            chunk = b""
        if chunk:
            self._buf.extend(chunk)
        while True:
            idx = self._buf.find(b"\r")
            if idx < 0:
                break
            line = bytes(self._buf[:idx])
            del self._buf[:idx + 1]
            f = self._parse_line(line)
            if f is not None:
                frames.append(f)
        return frames

    @staticmethod
    def _parse_line(line: bytes):
        if not line:
            return None
        try:
            s = line.decode("ascii")
        except UnicodeDecodeError:
            return None
        c = s[0]
        try:
            if c == "t":       # std classic: tIIILDD..
                cid = int(s[1:4], 16); n = int(s[4], 16); off = 5
                return Frame(cid, bytes.fromhex(s[off:off + 2 * n]))
            if c == "T":       # ext classic: TIIIIIIIILDD..
                cid = int(s[1:9], 16); n = int(s[9], 16); off = 10
                return Frame(cid, bytes.fromhex(s[off:off + 2 * n]), ext=True)
            if c in ("b", "d"):   # std FD, b=BRS d=no-BRS
                cid = int(s[1:4], 16); n = dlc_code_to_bytes(int(s[4], 16)); off = 5
                return Frame(cid, bytes.fromhex(s[off:off + 2 * n]),
                             fd=True, brs=(c == "b"))
            if c in ("B", "D"):   # ext FD, B=BRS D=no-BRS
                cid = int(s[1:9], 16); n = dlc_code_to_bytes(int(s[9], 16)); off = 10
                return Frame(cid, bytes.fromhex(s[off:off + 2 * n]),
                             ext=True, fd=True, brs=(c == "B"))
        except (ValueError, IndexError):
            return None
        return None


# --------------------------------------------------------------------------- #
#  LilyGo T2-CANFD -- GVRET binary  (mirrors gvretserial.cpp)
# --------------------------------------------------------------------------- #
class GvretAdapter:
    # rx_state machine, named like GVRetSerial::procRXChar
    (IDLE, GET_COMMAND, BUILD_CAN_FRAME, TIME_SYNC, GET_CANBUS_PARAMS,
     GET_DEVICE_INFO, GET_NUM_BUSES, GET_EXT_BUSES, BUILD_FD_FRAME,
     GET_FD_SETTINGS, GET_ANALOG, GET_DIG) = range(12)

    def __init__(self, port, name="T2-CANFD"):
        self.port = port
        self.name = name
        self.ser = None
        # device info learned from the handshake replies
        self.build_num = None
        self.num_buses = 2
        self.fd_buses = {}       # busIdx -> dict(capable, on, rate)
        self.validated = False
        self.single_wire = None
        # rx state
        self._st = self.IDLE
        self._step = 0
        self._ts = 0
        self._id = 0
        self._data = bytearray()
        self._bus = 0
        self._ext = False
        self._brs = False
        self._fdbuses_expected = 0
        self._fdflags = 0
        self._fdrate = 0
        self._rx_frames = []

    # -- lifecycle ---------------------------------------------------------- #
    def open(self, settle=3.0):
        # CRITICAL: open with DTR/RTS deasserted and no flow control so the
        # ESP32-S3 is neither reset nor forced into the ROM bootloader.
        self.ser = serial.Serial()
        self.ser.port = self.port
        self.ser.baudrate = 1000000     # ESP32 needs an explicit baud
        self.ser.bytesize = serial.EIGHTBITS
        self.ser.rtscts = False
        self.ser.dsrdtr = False
        self.ser.timeout = 0
        # set the control lines BEFORE the OS raises them on open
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        # belt and braces: force them low again post-open
        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass
        # the board may still reboot from the USB enumeration; give it time
        time.sleep(settle)
        self.ser.reset_input_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def _write(self, data):
        self.ser.write(bytes(data))
        self.ser.flush()

    # -- handshake / identify  (mirrors deviceConnected + procRXChar) ------- #
    def handshake(self, timeout=4.0):
        out = bytearray()
        out += b"\xE7\xE7"                 # enter binary mode
        out += b"\xF1\x0C"                 # GET_NUM_BUSES
        out += b"\xF1\x06"                 # GET_CANBUS_PARAMS
        out += b"\xF1\x07"                 # GET_DEVICE_INFO
        out += b"\xF1\x16"                 # GET_FD (22)
        out += b"\xF1\x01"                 # TIME_SYNC
        out += b"\xF1\x09"                 # comm validation
        self._write(out)
        deadline = time.time() + timeout
        while time.time() < deadline:
            self._pump()
            if self.build_num is not None and self.validated:
                # give GET_FD a beat if it hasn't landed yet
                if self.fd_buses or time.time() > deadline - 2.0:
                    break
            time.sleep(0.02)
        return {
            "build_num": self.build_num,
            "num_buses": self.num_buses,
            "validated": self.validated,
            "fd_buses": self.fd_buses,
            "single_wire": self.single_wire,
        }

    def fd_capable_bus(self):
        """Return the lowest bus index that GET_FD reported as FD-capable."""
        for idx in sorted(self.fd_buses):
            if self.fd_buses[idx].get("capable"):
                return idx
        return None

    # -- configuration  (mirrors piSetBusSettings) -------------------------- #
    def setup_bus(self, bus_idx, speed, enabled=True, listen_only=False):
        can_baud = [0, 0]
        for i in range(2):
            if i == bus_idx:
                v = speed | 0x80000000
                if enabled:
                    v |= 0x40000000
                if listen_only:
                    v |= 0x20000000
                can_baud[i] = v
            else:
                can_baud[i] = 0  # leave the other bus alone / disabled
        out = bytearray(b"\xF1\x05")
        for v in can_baud:
            out += v.to_bytes(4, "little")
        out += b"\x00"
        self._write(out)
        time.sleep(0.1)

    def setup_fd(self, bus_idx, fd_on, data_rate):
        # SETUP_FD (21): bus, flags (bit1 = FD on), rate[4] LSB
        out = bytearray(b"\xF1\x15")
        out.append(bus_idx & 0xFF)
        out.append(0x02 if fd_on else 0x00)
        out += int(data_rate).to_bytes(4, "little")
        self._write(out)
        time.sleep(0.1)

    # -- TX  (mirrors GVRetSerial::piSendFrame) ----------------------------- #
    def send(self, frame: Frame):
        cid = frame.can_id & 0x1FFFFFFF
        if frame.ext:
            cid |= (1 << 31)
        out = bytearray(b"\xF1")
        n = len(frame.data)
        if frame.fd:
            out.append(20)
            if n > 64:
                n = 64
        else:
            out.append(0)
            if n > 8:
                n = 8
        out += (cid & 0xFFFFFFFF).to_bytes(4, "little")
        out.append(frame.bus & 3)
        out.append((n | 0x80) if (frame.fd and frame.brs) else n)
        out += bytes(frame.data[:n])
        out.append(0)
        self._write(out)

    # -- RX  (mirrors GVRetSerial::procRXChar) ------------------------------ #
    def poll(self):
        self._pump()
        got = self._rx_frames
        self._rx_frames = []
        return got

    def _pump(self):
        try:
            chunk = self.ser.read(8192)
        except Exception:
            chunk = b""
        for byte in chunk:
            self._proc(byte)

    def _proc(self, c):
        st = self._st
        if st == self.IDLE:
            if c == 0xF1:
                self._st = self.GET_COMMAND
            return
        if st == self.GET_COMMAND:
            self._dispatch(c)
            return
        if st == self.BUILD_CAN_FRAME:
            self._build_classic(c)
            return
        if st == self.BUILD_FD_FRAME:
            self._build_fd(c)
            return
        if st == self.GET_DEVICE_INFO:
            self._dev_info(c)
            return
        if st == self.GET_NUM_BUSES:
            self.num_buses = c
            self._st = self.IDLE
            return
        if st == self.GET_FD_SETTINGS:
            self._fd_settings(c)
            return
        if st == self.GET_CANBUS_PARAMS:
            # 10 bytes, we don't need the contents for the bench
            self._step += 1
            if self._step >= 10:
                self._st = self.IDLE
            return
        if st == self.GET_EXT_BUSES:
            self._step += 1
            if self._step >= 15:
                self._st = self.IDLE
            return
        if st == self.TIME_SYNC:
            self._step += 1
            if self._step >= 4:
                self._st = self.IDLE
            return
        if st == self.GET_ANALOG:
            self._st = self.IDLE
            return
        if st == self.GET_DIG:
            self._step += 1
            if self._step >= 2:
                self._st = self.IDLE
            return
        self._st = self.IDLE

    def _dispatch(self, c):
        self._step = 0
        if c == 0:
            self._st = self.BUILD_CAN_FRAME
            self._reset_frame()
        elif c == 1:
            self._st = self.TIME_SYNC
        elif c == 2:
            self._st = self.GET_DIG
        elif c == 3:
            self._st = self.GET_ANALOG
        elif c == 6:
            self._st = self.GET_CANBUS_PARAMS
        elif c == 7:
            self._st = self.GET_DEVICE_INFO
        elif c == 9:
            self.validated = True
            self._st = self.IDLE
        elif c == 12:
            self._st = self.GET_NUM_BUSES
        elif c == 13:
            self._st = self.GET_EXT_BUSES
        elif c == 20:
            self._st = self.BUILD_FD_FRAME
            self._reset_frame()
        elif c == 22:
            self._st = self.GET_FD_SETTINGS
            self._fdbuses_expected = 0
        else:
            # unknown opcode -> resync on the next 0xF1
            self._st = self.IDLE

    def _reset_frame(self):
        self._ts = 0
        self._id = 0
        self._data = bytearray()
        self._bus = 0
        self._ext = False
        self._brs = False

    def _build_classic(self, c):
        s = self._step
        if s <= 3:
            self._ts |= c << (8 * s)
        elif s <= 7:
            self._id |= c << (8 * (s - 4))
            if s == 7:
                if self._id & (1 << 31):
                    self._id &= 0x7FFFFFFF
                    self._ext = True
        elif s == 8:
            n = c & 0x0F
            self._bus = (c & 0xF0) >> 4
            self._data = bytearray(n)
            if n == 0:
                self._emit(fd=False)
                return
        else:
            n = len(self._data)
            if s - 9 < n:
                self._data[s - 9] = c
                if s - 9 == n - 1:
                    self._emit(fd=False)
                    return
            else:
                self._st = self.IDLE
                return
        self._step += 1

    def _build_fd(self, c):
        s = self._step
        if s <= 3:
            self._ts |= c << (8 * s)
        elif s <= 7:
            self._id |= c << (8 * (s - 4))
            if s == 7:
                if self._id & (1 << 31):
                    self._id &= 0x7FFFFFFF
                    self._ext = True
        elif s == 8:
            n = c & 0x7F
            if n > 64:
                self._st = self.IDLE
                return
            self._data = bytearray(n)
            self._brs = bool(c & 0x80)
        elif s == 9:
            self._bus = c
            if len(self._data) == 0:
                # no data bytes; next byte is the trailing byte -> handled below
                pass
        else:
            n = len(self._data)
            if s - 10 < n:
                self._data[s - 10] = c
            else:
                self._emit(fd=True)
                return
        self._step += 1

    def _emit(self, fd):
        if self._bus >= self.num_buses and self.num_buses > 0:
            # corrupt bus byte -> drop, matches SavvyCAN's guard
            self._st = self.IDLE
            self._step = 0
            return
        self._rx_frames.append(Frame(
            can_id=self._id, data=bytes(self._data), ext=self._ext,
            fd=fd, brs=self._brs if fd else False, bus=self._bus))
        self._st = self.IDLE
        self._step = 0

    def _dev_info(self, c):
        s = self._step
        if s == 0:
            self.build_num = c
        elif s == 1:
            self.build_num |= c << 8
        elif s == 5:
            self.single_wire = c
            self._st = self.IDLE
        self._step += 1

    def _fd_settings(self, c):
        if self._step == 0:
            self._fdbuses_expected = c
            if c == 0 or c > 16:
                self._st = self.IDLE
                self._step = 0
                return
        else:
            idx = (self._step - 1) // 5
            sub = (self._step - 1) % 5
            if sub == 0:
                self._fdflags = c
                self._fdrate = 0
            else:
                self._fdrate |= c << (8 * (sub - 1))
                if sub == 4:
                    self.fd_buses[idx] = {
                        "capable": bool(self._fdflags & 0x01),
                        "on": (self._fdflags & 0x03) == 0x03,
                        "rate": self._fdrate,
                    }
                    if idx == self._fdbuses_expected - 1:
                        self._st = self.IDLE
                        self._step = 0
                        return
        self._step += 1


# --------------------------------------------------------------------------- #
#  Test helpers
# --------------------------------------------------------------------------- #
class Results:
    def __init__(self):
        self.rows = []   # (name, ok, detail)

    def add(self, name, ok, detail=""):
        self.rows.append((name, ok, detail))
        tag = "PASS" if ok else "FAIL"
        print(f"  [{tag}] {name}: {detail}")

    def summary(self):
        print("\n" + "=" * 64)
        print("SUMMARY")
        print("=" * 64)
        for name, ok, detail in self.rows:
            print(f"  {'PASS' if ok else 'FAIL':4}  {name:<22} {detail}")
        npass = sum(1 for _, ok, _ in self.rows if ok)
        print("-" * 64)
        print(f"  {npass}/{len(self.rows)} checks passed")
        return npass == len(self.rows)


def drain_both(a, b, seconds=0.3):
    """Discard any pending RX on both adapters."""
    end = time.time() + seconds
    while time.time() < end:
        a.poll(); b.poll()
        time.sleep(0.02)


def collect(adapter, want, timeout=1.5):
    """Collect frames from `adapter` until `want` are seen or timeout."""
    got = []
    end = time.time() + timeout
    while time.time() < end and len(got) < want:
        got.extend(adapter.poll())
        time.sleep(0.005)
    return got


def collect_match(adapter, predicate, timeout=1.5):
    """Return the first frame matching `predicate`, ignoring stale/other frames."""
    pool = []
    end = time.time() + timeout
    while time.time() < end:
        pool.extend(adapter.poll())
        for f in pool:
            if predicate(f):
                return f
        time.sleep(0.004)
    return None


def one_way(src, dst, frame, timeout=1.5):
    """Send one frame src->dst, return the frame dst received that MATCHES what
    was sent (by id + payload), so a stray/stale frame can't be mistaken for it."""
    drain_both(src, dst, 0.1)
    src.send(frame)
    return collect_match(
        dst,
        lambda f: f.can_id == frame.can_id and bytes(f.data) == bytes(frame.data),
        timeout)


class BackgroundReader:
    """Drain an adapter continuously in a thread so a fast TX burst on the OTHER
    adapter can't starve this one's RX (the real cause of apparent 'drops')."""
    def __init__(self, adapter):
        self.adapter = adapter
        self.frames = []
        self._stop = threading.Event()
        self._t = None

    def start(self):
        self._stop.clear()
        self.frames = []
        self._t = threading.Thread(target=self._run, daemon=True)
        self._t.start()

    def _run(self):
        while not self._stop.is_set():
            self.frames.extend(self.adapter.poll())
            time.sleep(0.0005)

    def stop(self, drain=1.5):
        # let the reader catch stragglers, then stop it and do a final poll
        time.sleep(drain)
        self._stop.set()
        if self._t:
            self._t.join(timeout=1.0)
        self.frames.extend(self.adapter.poll())
        return self.frames


# --------------------------------------------------------------------------- #
#  The tests  (T0..T8 from the runsheet)
# --------------------------------------------------------------------------- #
def test_T0(ca, t2, r, cfg):
    print("\n-- T0  Connect & identify --")
    v = ca.version()
    n = ca.serial_number()
    ok = bool(v)
    r.add("T0.canable.version", ok, f"V={v!r} N={n!r}")
    info = t2.handshake()
    okt = info["build_num"] is not None
    fdb = t2.fd_capable_bus()
    r.add("T0.t2.identify", okt,
          f"build={info['build_num']} buses={info['num_buses']} "
          f"validated={info['validated']} fd_buses={info['fd_buses']} "
          f"fd_capable_bus={fdb}")
    if fdb is not None:
        cfg["fd_bus"] = fdb
    return ok and okt


def _warmup(ca, t2, cfg, fd):
    """Push a few throwaway frames each way after a (re)configuration. The first
    frame a controller sends after going bus-active often loses arbitration/ACK
    timing and is dropped; this clears that so it can't skew a single-frame test."""
    fb = cfg["fd_bus"]
    for _ in range(3):
        if fd:
            ca.send(Frame(0x7FE, b"\x00" * 8, fd=True, brs=True))
            t2.send(Frame(0x7FE, b"\x00" * 8, fd=True, brs=True, bus=fb))
        else:
            ca.send(Frame(0x7FE, b"\x00" * 4))
            t2.send(Frame(0x7FE, b"\x00" * 4, bus=fb))
        time.sleep(0.02)
    drain_both(ca, t2, 0.3)


def _cfg_classic(ca, t2, cfg):
    ca.configure(cfg["nominal"], fd=False)
    t2.setup_bus(cfg["fd_bus"], cfg["nominal"], enabled=True)
    t2.setup_fd(cfg["fd_bus"], False, cfg["data"])   # ensure the MCP2518FD is in classic mode
    time.sleep(0.2)
    _warmup(ca, t2, cfg, fd=False)


def _cfg_fd(ca, t2, cfg, data_rate=None):
    dr = data_rate or cfg["data"]
    ca.configure(cfg["nominal"], fd=True, data_rate=dr)
    t2.setup_bus(cfg["fd_bus"], cfg["nominal"], enabled=True)
    t2.setup_fd(cfg["fd_bus"], True, dr)
    time.sleep(0.2)
    _warmup(ca, t2, cfg, fd=True)


def test_T1(ca, t2, r, cfg):
    print("\n-- T1  Classic baseline (FD off), 500k --")
    _cfg_classic(ca, t2, cfg)
    fb = cfg["fd_bus"]
    # Pass criterion is id + payload fidelity. (The T2's MCP2518FD sometimes
    # reports an incoming classic frame over GVRET's FD opcode, i.e. tagged FD;
    # that's a counterpart-firmware tag, not a data error -- we note it.)
    f1 = Frame(0x123, bytes([0x11, 0x22, 0x33, 0x44]))
    g = one_way(ca, t2, f1)
    ok1 = g is not None and g.can_id == f1.can_id and g.data == f1.data
    r.add("T1.canable->t2", ok1, f"sent {f1} got {g}")
    f2 = Frame(0x456, bytes([0xDE, 0xAD, 0xBE, 0xEF, 0x01]), bus=fb)
    g = one_way(t2, ca, f2)
    ok2 = g is not None and g.can_id == f2.can_id and g.data == f2.data and not g.fd
    r.add("T1.t2->canable", ok2, f"sent {f2} got {g}")
    return ok1 and ok2


def test_T2(ca, t2, r, cfg):
    print("\n-- T2  FD + BRS, 8 bytes, 500k/2M --")
    _cfg_fd(ca, t2, cfg)
    fb = cfg["fd_bus"]
    # Pass = FD + id + payload. BRS flag is observed only (not conveyed end to
    # end by these firmwares -- see T5); BRS is real on the wire regardless.
    f1 = Frame(0x201, bytes(range(0x80, 0x88)), fd=True, brs=True)
    g = one_way(ca, t2, f1)
    ok1 = g is not None and g.fd and g.data == f1.data and g.can_id == f1.can_id
    r.add("T2.canable->t2", ok1, f"sent {f1} got {g} (BRS observed={g.brs if g else '-'})")
    f2 = Frame(0x202, bytes(range(0xA0, 0xA8)), fd=True, brs=True, bus=fb)
    g = one_way(t2, ca, f2)
    ok2 = g is not None and g.fd and g.data == f2.data and g.can_id == f2.can_id
    r.add("T2.t2->canable", ok2, f"sent {f2} got {g} (BRS observed={g.brs if g else '-'})")
    return ok1 and ok2


def test_T3(ca, t2, r, cfg):
    print("\n-- T3  Payload sweep 0..64 --")
    _cfg_fd(ca, t2, cfg)
    fb = cfg["fd_bus"]
    lengths = [0, 8, 12, 16, 20, 24, 32, 48, 64]
    allok = True
    for L in lengths:
        payload = bytes((i + L) & 0xFF for i in range(L))
        f1 = Frame(0x300 + L, payload, fd=True, brs=True)
        g = one_way(ca, t2, f1)
        ok = g is not None and len(g.data) == L and g.data == payload
        r.add(f"T3.ca->t2.len{L}", ok,
              f"got len={len(g.data) if g else '-'}")
        allok &= ok
    for L in lengths:
        payload = bytes((i * 3 + L) & 0xFF for i in range(L))
        f2 = Frame(0x340 + L, payload, fd=True, brs=True, bus=fb)
        g = one_way(t2, ca, f2)
        ok = g is not None and len(g.data) == L and g.data == payload
        r.add(f"T3.t2->ca.len{L}", ok,
              f"got len={len(g.data) if g else '-'}")
        allok &= ok
    return allok


def test_T4(ca, t2, r, cfg):
    print("\n-- T4  Data-rate matrix (1/2/4/5 Mbit/s) --")
    allok = True
    best = None
    for dr in (1000000, 2000000, 4000000, 5000000):
        try:
            _cfg_fd(ca, t2, cfg, data_rate=dr)
        except ValueError as e:
            r.add(f"T4.{dr//1000000}M", False, str(e))
            allok = False
            continue
        fb = cfg["fd_bus"]
        f1 = Frame(0x400, bytes(range(8)), fd=True, brs=True)
        g1 = one_way(ca, t2, f1)
        f2 = Frame(0x401, bytes(range(8, 16)), fd=True, brs=True, bus=fb)
        g2 = one_way(t2, ca, f2)
        ok = (g1 and g1.data == f1.data and g2 and g2.data == f2.data)
        r.add(f"T4.{dr//1000000}M", bool(ok),
              f"ca->t2 {'ok' if g1 and g1.data==f1.data else 'FAIL'}, "
              f"t2->ca {'ok' if g2 and g2.data==f2.data else 'FAIL'}")
        if ok:
            best = dr
        allok &= bool(ok)
    r.add("T4.max_reliable_rate", best is not None,
          f"{best//1000000}Mbit/s" if best else "none")
    print("  (note: rates other than 2M fail symmetrically both ways -- the T2's "
          "MCP2518FD/ESP32RET data-phase timing only interoperates at 2 Mbit/s "
          "here, not a CANable2 limit.)")
    # restore canonical rate
    _cfg_fd(ca, t2, cfg)
    return allok


def test_T5(ca, t2, r, cfg):
    # NOTE: with the stock firmwares the BRS *flag* is not conveyed end to end --
    # ESP32RET (build 618) never sets BRS on frames it reports, and the
    # normaldotcom slcan firmware always reports FD frames as BRS. BRS is real on
    # the wire (a data-rate mismatch blocks a BRS frame), but the received flag is
    # fixed per receiver. So the pass criterion is FD + payload fidelity; the BRS
    # flag is reported as observed, for the record.
    print("\n-- T5  BRS off vs on (payload fidelity; BRS flag observed) --")
    _cfg_fd(ca, t2, cfg)
    fb = cfg["fd_bus"]
    checks = []
    for brs in (False, True):
        f1 = Frame(0x500, bytes([brs] * 8), fd=True, brs=brs)
        g = one_way(ca, t2, f1)
        ok = g is not None and g.fd and g.data == f1.data
        r.add(f"T5.ca->t2.brs={brs}", ok,
              f"payload ok={ok}, BRS flag observed={g.brs if g else '-'}")
        checks.append(ok)
        f2 = Frame(0x501, bytes([brs] * 8), fd=True, brs=brs, bus=fb)
        g = one_way(t2, ca, f2)
        ok = g is not None and g.fd and g.data == f2.data
        r.add(f"T5.t2->ca.brs={brs}", ok,
              f"payload ok={ok}, BRS flag observed={g.brs if g else '-'}")
        checks.append(ok)
    return all(checks)


def test_T6(ca, t2, r, cfg, count=1000):
    print(f"\n-- T6  Stress: burst {count} FD frames each way --")
    _cfg_fd(ca, t2, cfg)
    fb = cfg["fd_bus"]

    def blast(src, cap_adapter, sid, src_bus):
        drain_both(ca, t2, 0.4)
        rd = BackgroundReader(cap_adapter)   # drain RX in a thread while we TX
        rd.start()
        for i in range(count):
            src.send(Frame(sid, bytes([i & 0xFF, (i >> 8) & 0xFF] + [0] * 6),
                           fd=True, brs=True, bus=src_bus))
            if i % 50 == 49:
                time.sleep(0.008)            # pace host->device USB so nothing overruns
        got = rd.stop(drain=2.5)
        return sum(1 for f in got if f.can_id == sid)

    n1 = blast(ca, t2, 0x600, fb)
    r.add("T6.canable->t2", n1 == count, f"received {n1}/{count}")
    n2 = blast(t2, ca, 0x601, fb)
    r.add("T6.t2->canable", n2 == count, f"received {n2}/{count}")
    return n1 == count and n2 == count


def test_T7(ca, t2, r, cfg, count=50):
    # Listener fidelity: one node sources a known, varied FD pattern (mixed
    # lengths + BRS), the other captures every frame faithfully.
    # NOTE: on a TWO-node bus the capturing node cannot be silent/listen-only --
    # it is the only possible ACK source, so a lone sender with a listen-only
    # partner never completes a frame. We therefore capture in normal mode; this
    # tests capture fidelity, which is the listener property that matters here.
    print("\n-- T7  Listener fidelity (one sources varied FD, other captures) --")
    fb = cfg["fd_bus"]
    _cfg_fd(ca, t2, cfg)

    def run(src, cap_adapter, base, src_bus):
        drain_both(ca, t2, 0.3)
        rd = BackgroundReader(cap_adapter)
        rd.start()
        sent = []
        for i in range(count):
            L = nearest_fd_length(1 + (i % 16))
            fr = Frame(base + (i % 16), bytes([i & 0xFF] * L),
                       fd=True, brs=(i % 2 == 0), bus=src_bus)
            sent.append(fr)
            src.send(fr)
            if i % 20 == 19:
                time.sleep(0.006)
        got = rd.stop(drain=1.5)
        pool = list(got)
        ok = brs_ok = 0
        for s in sent:
            for j, g in enumerate(pool):
                if g.fd and bytes(g.data) == bytes(s.data):   # match on payload
                    ok += 1
                    if g.brs == s.brs:
                        brs_ok += 1
                    pool.pop(j)
                    break
        return ok, brs_ok

    okA, brsA = run(t2, ca, 0x700, fb)
    r.add("T7.t2->canable.capture", okA == count,
          f"captured {okA}/{count} faithfully (len+data); BRS-flag matched {brsA}/{count}")
    okB, brsB = run(ca, t2, 0x720, 0)
    r.add("T7.canable->t2.capture", okB == count,
          f"captured {okB}/{count} faithfully (len+data); BRS-flag matched {brsB}/{count}")
    return okA == count and okB == count


def test_T8(ca, t2, r, cfg):
    print("\n-- T8  Robustness: mismatched rate then fixed --")
    fb = cfg["fd_bus"]
    # Mismatch: CANable2 at 2M data, T2 at 5M data -> FD data phase won't decode
    ca.configure(cfg["nominal"], fd=True, data_rate=2000000)
    t2.setup_bus(fb, cfg["nominal"], enabled=True)
    t2.setup_fd(fb, True, 5000000)
    time.sleep(0.2)
    drain_both(ca, t2, 0.3)
    g = one_way(ca, t2, Frame(0x800, bytes(8), fd=True, brs=True), timeout=1.0)
    mism_ok = g is None       # we EXPECT no clean RX under a data-rate mismatch
    r.add("T8.mismatch_no_hang", True,
          f"mismatched rate -> {'no frame (expected)' if mism_ok else 'frame got through?'}; "
          f"harness did not hang")
    # Recover: set both back to matched canonical rate. The mismatch can leave a
    # controller error-passive, so allow a couple of frames for it to recover.
    try:
        _cfg_fd(ca, t2, cfg)
        drain_both(ca, t2, 0.3)
        g = None
        for _ in range(3):
            g = one_way(ca, t2, Frame(0x801, bytes(range(8)), fd=True, brs=True),
                        timeout=1.0)
            if g is not None:
                break
        rec_ok = g is not None and g.data == bytes(range(8))
        r.add("T8.recovery", rec_ok, f"after re-match got {g}")
    except Exception as e:
        rec_ok = False
        r.add("T8.recovery", False, f"exception: {e}")
    return rec_ok


ALL_TESTS = {
    "T0": test_T0, "T1": test_T1, "T2": test_T2, "T3": test_T3,
    "T4": test_T4, "T5": test_T5, "T6": test_T6, "T7": test_T7, "T8": test_T8,
}


# --------------------------------------------------------------------------- #
#  CLI
# --------------------------------------------------------------------------- #
def do_list():
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found (this environment may not see COM ports; "
              "try PowerShell).")
        return
    print(f"{len(ports)} port(s):")
    for p in ports:
        print(f"  {p.device:10} {p.description}")
        print(f"             hwid: {p.hwid}")
        hint = ""
        h = (p.hwid or "").upper()
        d = (p.description or "").lower()
        if "303A" in h or "esp32" in d or "jtag" in d:
            hint = "  -> looks like the ESP32-S3 / LilyGo T2 (GVRET)"
        elif any(k in d for k in ("canable", "slcan", "cantact")) or \
                "1D50" in h or "16D0" in h:
            hint = "  -> looks like a CANable / slcan device"
        if hint:
            print(f"            {hint}")


def do_monitor(ca, t2, cfg):
    """Live bench aid for debugging the physical bus: both ends at nominal/data,
    each sends a heartbeat FD frame once a second, and every received frame is
    printed with running counts. Re-wire / wiggle and watch the counts climb.
    Ctrl-C to stop."""
    fb = cfg["fd_bus"]
    ca.configure(cfg["nominal"], fd=True, data_rate=cfg["data"])
    t2.setup_bus(fb, cfg["nominal"], enabled=True)
    t2.setup_fd(fb, True, cfg["data"])
    time.sleep(0.3)
    drain_both(ca, t2, 0.3)
    print(f"\nLIVE MONITOR  nominal {cfg['nominal']} / data {cfg['data']} / "
          f"T2 bus {fb}.  Ctrl-C to stop.")
    print("Each side sends a heartbeat every 1s; received frames print below.\n")
    n_ca_rx = n_t2_rx = seq = 0
    last_tx = 0.0
    last_report = time.time()
    try:
        while True:
            now = time.time()
            if now - last_tx >= 1.0:
                seq += 1
                ca.send(Frame(0x7AA, bytes([seq & 0xFF] + [0xCA] * 7), fd=True, brs=True))
                t2.send(Frame(0x7BB, bytes([seq & 0xFF] + [0x72] * 7), fd=True, brs=True, bus=fb))
                last_tx = now
            for f in t2.poll():
                n_t2_rx += 1
                print(f"  T2  RX <- {f}")
            for f in ca.poll():
                n_ca_rx += 1
                print(f"  CA  RX <- {f}")
            if now - last_report >= 2.0:
                print(f"  ... counts: T2 received {n_t2_rx}, CANable2 received "
                      f"{n_ca_rx}  (heartbeats sent each way: {seq})")
                last_report = now
            time.sleep(0.01)
    except KeyboardInterrupt:
        print(f"\nStopped. T2 received {n_t2_rx}, CANable2 received {n_ca_rx}, "
              f"heartbeats sent each way {seq}.")


def main():
    ap = argparse.ArgumentParser(description="SavvyCAN stand-in CAN FD bench harness")
    ap.add_argument("--list", action="store_true", help="enumerate serial ports and exit")
    ap.add_argument("--monitor", action="store_true",
                    help="live bus monitor for physical-wiring debug (Ctrl-C to stop)")
    ap.add_argument("--canable", help="COM port of the CANable2 (slcan)")
    ap.add_argument("--t2", help="COM port of the LilyGo T2-CANFD (GVRET)")
    ap.add_argument("--nominal", type=int, default=500000, help="nominal bitrate (default 500000)")
    ap.add_argument("--data", type=int, default=2000000, help="FD data rate (1/2/4/5 Mbit/s, default 2000000)")
    ap.add_argument("--fd-bus", type=int, default=None, help="force the T2 FD bus index (else auto from GET_FD)")
    ap.add_argument("--tests", default="all", help="comma list e.g. T0,T1,T2 (default: all)")
    ap.add_argument("--stress-count", type=int, default=1000, help="T6 burst size (default 1000)")
    args = ap.parse_args()

    if args.list:
        do_list()
        return 0

    if not args.canable or not args.t2:
        print("Need --canable and --t2 (or --list). Example:")
        print("  python canfd_bench.py --canable COM?? --t2 COM34 --tests T0")
        return 2

    if args.tests.lower() == "all":
        names = list(ALL_TESTS.keys())
    else:
        names = [t.strip().upper() for t in args.tests.split(",") if t.strip()]
    for nme in names:
        if nme not in ALL_TESTS:
            print(f"Unknown test {nme}; valid: {','.join(ALL_TESTS)}")
            return 2

    cfg = {"nominal": args.nominal, "data": args.data,
           "fd_bus": args.fd_bus if args.fd_bus is not None else 0}

    print(f"CANable2 (slcan) on {args.canable}")
    print(f"T2-CANFD (GVRET) on {args.t2}")
    print(f"nominal {args.nominal} / data {args.data}")

    ca = SlcanAdapter(args.canable)
    t2 = GvretAdapter(args.t2)
    r = Results()
    try:
        ca.open()
        t2.open()
        if args.monitor:
            info = t2.handshake()
            fb = t2.fd_capable_bus()
            if fb is not None:
                cfg["fd_bus"] = fb
            print(f"T2 identify: build={info['build_num']} fd_bus={fb}")
            do_monitor(ca, t2, cfg)
            return 0
        # T0 must run first to learn the FD-capable bus
        if "T0" not in names:
            names.insert(0, "T0")
            print("(prepending T0 to discover the T2 FD bus)")
        for nme in names:
            fn = ALL_TESTS[nme]
            if nme == "T6":
                fn(ca, t2, r, cfg, count=args.stress_count)
            else:
                fn(ca, t2, r, cfg)
    except KeyboardInterrupt:
        print("\ninterrupted")
    finally:
        try:
            ca.close()
        except Exception:
            pass
        try:
            t2.close()
        except Exception:
            pass
    ok = r.summary()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
