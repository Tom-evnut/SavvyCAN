# candle_api (LGPLv3)

`candle_api` is the Windows WinUSB driver library that implements the gs_usb /
candleLight protocol, used by SavvyCAN's gs_usb backend (`connections/gs_usb_driver`)
to talk to CANable, CAN Bus Debugger, CANtact and other gs_usb devices.

## Licensing

This library is licensed under the **GNU Lesser General Public License v3** — see
[`LICENSE`](LICENSE). SavvyCAN (GPLv3) links to it dynamically (it is built as a
separate shared library, `candle_api.dll`), which is compatible with the LGPL.

## Source

The full source is vendored here in [`src/`](src/) so the DLL can be built from
source as part of the SavvyCAN build — no prebuilt binary blob is shipped.

Upstream: <https://github.com/BennyEvans/candle_dll>, pinned at commit
[`f9ee85e`](https://github.com/BennyEvans/candle_dll/commit/f9ee85e830be93c1d76a5102a8cb4f79e2719259).

It depends only on the Windows SDK system libraries `SetupApi`, `winusb` and
`Ole32` — no third-party dependencies (no libusb).

## How it is built

On Windows, SavvyCAN's build (both CMake and qmake) compiles `src/*.c` into
`candle_api.dll` as part of the build and links SavvyCAN against it. The resulting
DLL is placed next to `SavvyCAN.exe`. See the top-level `CMakeLists.txt`
(`GS_USB_DRIVER_ENABLED`, `candle_api` target) and `SavvyCAN.pro`.

On Linux the gs_usb backend is not built (gs_usb there is a SocketCAN kernel
feature), so this library is Windows-only.
