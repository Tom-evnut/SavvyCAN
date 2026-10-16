LIN Schedule Analysis
=====================

RE Tools > LIN Schedule Analysis works out a LIN bus's schedule from the LIN frames in the main frame list. Every LIN
frame is a header the master sent (a slave's answer, or nobody's), so the order of their IDs is the master's schedule
table. It's the first thing to look at on an unknown LIN bus, and what you need to drive its slaves with SavvyCAN as
the master.

Pick a bus (or all buses) and press Analyse; it runs when the window opens too. Frames loaded as CAN (from formats
that can't mark LIN frames) need File > Treat Frames as LIN first.

**Frame IDs** lists every ID seen:

* **Headers**: how many times the master sent its header, and **Answered**: how many got a response, as a share. An
  ID answered every time is a slave's regular frame; one rarely answered is an event-triggered frame or a slave that's
  missing or asleep; one never answered may be a frame the master publishes, logged by a device that only sees headers,
  or a slave that isn't there.
* **Period**: the average time between its headers, with the shortest and longest, and the **Jitter** (standard
  deviation). A steady period means a regular schedule slot.
* **Lengths**: the response lengths seen, and **Checksum, errors**: the checksum models seen (classic means LIN 1.x or
  a diagnostic frame), how many frames had errors, and how many SavvyCAN sent itself.

**Schedule table** is the shortest run of IDs that repeats through the capture, allowing for the odd frame slipped in
(a command sent now and then, a diagnostic request). Each slot's delay is the median time from its header to the
next. The line above says how much of the capture follows the cycle: well under 100% means frames come in between
often, or the master switches between schedule tables.

Copy as LDF schedule puts the table on the clipboard as an LDF Schedule_tables block, with the frame names from a
loaded LDF (Frame_0x.. otherwise). Paste it into an LDF to run it with Send Frames > LIN Schedule Runner.
