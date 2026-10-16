Adding a new connection
========================

![New Connection](./images/NewConnection.png)

At the moment it is possible to use any QT SerialBus compatible device and any GVRET compatible device in any of the supported operating systems. SerialBus supports socketcan on linux, passthrough on Linux and Windows 32 bit, and Vector, PeakCAN, and TinyCAN on supported OS's.

At this time GVRET compatible devices are: EVTVDue, EVTV CANDue (1.3/2/2.1/2.2), 
Teensy 3.1-3.6, Macchina M2, Macchina A0, EVTV ESP32 Due.

You can also use a variety of network based connections to gain access to remote capture hardware.


Connecting To GVRET Devices
==============================

SavvyCAN is able to connect to GVRET compatible devices to capture new traffic. These 
devices will present as serial ports on the connected PC. To connect to a dongle select "Serial Connection." This will bring up a list of serial ports on the machine. Select the proper one and then press "Create New Connection". This will close the window and bring you back to the connection manager window. If connection succeeds the status will show "Connected" for your newly set up device.

You can also connect to some GVRET devices over the network (A0, EVTV ESP32Due). These devices broadcast their address. Once you've selected "Network Connection (GVRET)" you should see a list of IP addresses that appear to have GVRET devices on them. You can also manually enter the proper IP address but if the device did not automatically register itself it is unlikely to work with a manual entry either.


Connecting to QT SerialBus Compatible Devices
=============================================

SavvyCAN can also connect to a wide variety of CAN hardware through the built-in QT
SerialBus drivers. These drivers vary by operating system but support socketcan on LINUX
and Vector tools on both LINUX and Windows. When you select "QT SerialBus Devices" you will
get a list of device types supported. Select a device type and for most devices you should see
the Port list fill out with all registered and valid ports for that driver. Socketcan devices, for
instance, are automatically detected now. Then push "Create New Connection" and you should 
see the new connection in the table on the left of the window. Note that SocketCAN devices 
don't support changing the baud rate within a program. You must do this when you set up 
the connection via console commands. This is outside the scope of this documentation. 
Consult the SocketCAN documentation for details on configuring such devices.

For CAN FD, tick "CAN FD" and pick the data phase rate before creating the connection (both can also be
changed later in the bus settings of the connection window). The CAN speed and data rate are passed to
the driver; drivers that set bit rates themselves (e.g. PEAK) use them, while for SocketCAN they are set
with "ip link" (e.g. `ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on`) and only the CAN FD
setting matters. A virtual SocketCAN interface can carry CAN FD frames too: `ip link set vcan0 mtu 72`.
Received CAN FD frames show "D" in the Type column of the main frame list, "DB" with bit rate switching.

QT also includes a "virtualcan" device type. You can use this to create a bus that will loop back anything you send to it. This is useful for testing without needing to connect any devices or load any log files.

Connecting to Socketcand
========================

This is a LINUX only solution which allows one to connect to a socketcan device that is registered on the local network. You can also set up SSH tunnels or VPN to expand the reach over the internet. It should fill out a list of any available socketcand interfaces. Setting up socketcand is outside the scope of this help file but may your GoogleFu be strong.

Connecting to a Microchip LIN Serial Analyzer (LIN)
===================================================

The Microchip LIN Serial Analyzer (APGDT001) captures LIN traffic over USB. Select "Microchip LIN Serial Analyzer (APGDT001, LIN)": the port list shows the connected analyzers by serial number, and an empty port means the first one found. The analyzer is a plain USB HID device, so it needs no driver; on Linux your user needs access to its hidraw device (a udev rule). This connection is only there if SavvyCAN was built with hidapi.

The connection has one LIN bus, bus 0. In monitor mode (the default) it only listens; in master mode it sends what you send to its bus (see Sending on a LIN bus below). Set the bit rate and mode in the bus settings of the connection window (2400 to 20000 bit/s, 19200 to start with); the debug console says whether the analyzer took the setting.

Received LIN frames have the 6-bit LIN ID as their ID and the response's data bytes as their data. A header nobody answered has no data. SavvyCAN checks each response's checksum, works out whether the ID uses the classic or the enhanced checksum, and checks the ID's parity bits. Timestamps come from the analyzer's clock, in steps of 0.409 ms.

LIN frames show "L" in the Type column of the main frame list, "LN" for a header nobody answered, and "!" is added when the checksum or parity is wrong or the analyzer reported an error. In overwrite mode, IDs that only ever get headers stay in the list. The Frame Details window adds a LIN section for LIN IDs: the protected ID, the checksum model the ID uses, and how many responses, unanswered headers and errors there were. LIN frames are saved in SavvyCAN's native CSV format with "LIN" in the flags column after the data, followed by the checksum received (for example `LIN+CS=DB+CLASSIC`) and any errors. CANalyzer Binary Log (BLF) files and Wireshark pcapng files keep them as LIN frames too (Wireshark decodes the pcapng file's LIN packets, and each bus is an interface named after it); every other format saves them as CAN frames with their ID and data, and SavvyCAN asks before doing so. Wireshark captures with LIN packets (pcap or pcapng, link type LIN) load with "Wireshark SocketCAN and LIN", or when the file type is detected.

LIN captures from files that don't mark their frames as LIN (CSV files from other tools, or saved before SavvyCAN knew LIN) load as CAN frames, which LIN description files don't decode. Use File > Treat Frames as LIN... to turn them into LIN frames: pick the bus, and tick "The last data byte is the LIN checksum" if the log keeps checksums. Frames with a standard ID of 0 to 63 and at most 8 data bytes are converted; the others stay as they are. Save as native CSV afterwards and the file loads as LIN from then on.

The protocol SavvyCAN uses to talk to the analyzer was worked out from Microchip's demo software and hasn't been checked on every analyzer; if something looks wrong, the debug console output helps.

Connecting to a PEAK LIN interface (LIN)
========================================

PEAK's LIN interfaces (PLIN-USB, and the LIN channels of the PCAN-USB Pro FD and PCAN-USB Pro) capture LIN traffic through PEAK's PLIN driver. Install the PLIN driver from peak-system.com: it brings PLinApi.dll, which SavvyCAN uses. Select "PEAK LIN (PLIN-USB, PCAN-USB Pro / Pro FD)": the port list shows each LIN channel as "name, device n, channel m", and an empty port means the first one found. Without the driver the option is greyed out and its tooltip says why. This works on Windows only so far.

The connection has one LIN bus, bus 0, and listens in monitor mode (the default): the channel is set up as a LIN slave that receives every frame ID and never answers. In master mode it sends what you send to its bus (see Sending on a LIN bus below). Set the bit rate in the bus settings (1000 to 20000 bit/s, 19200 to start with). Frames show as described for the Microchip analyzer above, with the checksum checked, the checksum model worked out per ID, and PEAK's error flags (sync, parity, incomplete response, bus shorts) shown as errors. Timestamps come from the interface, in microseconds. Sleep and wake-up events and overruns are counted in the debug console.

Sending on a LIN bus
====================

To send, set the LIN bus to Master in the connection's bus settings (LIN Mode). Then frames sent to that bus, from the Send Frames window, a script or the playback window, go out on the LIN bus, with SavvyCAN as the LIN master:

* a frame with an ID of 0 to 63 and 1 to 8 data bytes is sent as a header and that response, with its checksum;
* a frame with no data, or a remote frame, is sent as a header alone, for a slave to answer; its answer shows in the frame list like any other frame;
* frames with an extended ID, an ID over 63, or CAN FD frames aren't sent, and the debug console says why.

The checksum model is the bus's LIN Checksum setting. Left at "Per ID, as received", each ID is sent with the model it was last seen using on the bus, and with the enhanced (LIN 2.x) checksum if it hasn't been seen yet; diagnostic frames (IDs 0x3C and 0x3D) always use the classic checksum. LIN frames played back from a log keep the checksum they were logged with. The frames sent show in the frame list as the interface reports them, so each appears once. Slave mode (answering headers) isn't there yet.

With the Microchip analyzer, frames sent together (several Send Frames rows on the same trigger, or a script) go out one frame slot apart, so each slave has time to answer: the analyzer itself starts the next header straight away. A header alone gets the slot of an 8-byte response, about 19 ms at 9600 bit/s and 10 ms at 19200, so polling many IDs quickly is limited by that. LIN slaves that have gone to sleep may not answer the first header after a quiet bus: it wakes them up.

Connecting over MQTT
====================

Lastly, it is possible to connect to an MQTT broker to send and receive CAN traffic over the internet. This is much like socketcand but more cross platform and also supports easy broadcasting. For instance, for capture the flag events, it would be possible to connect the device over MQTT and have multiple participants and/or watchers all connected at once. Connection to the MQTT broker is set up in the main SavvyCAN preferences. In this window you merely select the topic name to subscribe to. There is currently no automatic way to list these topics so you will need to know the topic to subscribe to ahead of time. It should be noted that the bidirectional nature of this interface means that everyone is on equal footing. You can create an MQTT interface that others can connect to or you can connect to a topic that is currently being sent to from elsewhere and get the traffic. Additionally, the SavvyCAN source code at GitHub has a python script which can be used to connect a socketcan interface to MQTT. You can use this script on a remote system to connect it to the internet so that you can run SavvyCAN somewhere apart from the device under test.
