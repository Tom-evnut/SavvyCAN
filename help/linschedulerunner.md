LIN Schedule Runner
===================

Send Frames > LIN Schedule Runner runs a schedule table from an LDF, with SavvyCAN as the LIN master. That's what a
LIN module on the bench needs to work without its car: the master sends each frame's header in turn, at the times the
schedule gives, and the slaves answer theirs.

Before starting:

* load the LDF in the DBC manager. Only LDFs with schedule tables are offered;
* connect a LIN interface (the Microchip LIN Serial Analyzer or a PEAK LIN interface) and set its bus to Master in the
  Connection window's bus settings (LIN Mode), at the LDF's bit rate.

Then pick the LDF, the schedule table and the bus, and press Start. Cycles sets how many times the table runs; leave it
at "Until stopped" to keep going. The arrow in the Slot column shows the next slot, and the status line counts the
cycles and frames sent. The frames, and the slaves' answers, show in the main frame list as usual.

What each slot sends (the Sends column):

* **Header and data (master):** a frame the master publishes. The data starts as the initial values of the frame's
  signals in the LDF (bits no signal uses are sent as 1). Edit the Data cell to send something else, as 1 to 8 hex
  bytes ("21 00 C0 05" or "2100C005"); the change applies from the next time the frame goes out, in every slot that
  sends that frame, and stays when you switch schedule tables. MasterReq slots send the master request frame's data,
  and FreeFormat slots their eight bytes.
* **Header (a slave answers):** a frame a slave publishes, an event-triggered frame or SlaveResp: the header alone.
* **Nothing:** AssignNAD and the other node configuration commands, and sporadic frames, aren't sent yet. Their slot
  still takes its time, so the rest of the schedule keeps its timing.

The checksum follows the LDF: classic for every frame of a LIN 1.x file, enhanced otherwise, and always classic for
the diagnostic frames (0x3C and 0x3D).

Timing: slots are timed from the start, so a frame that goes out late doesn't push the rest of the schedule back. If
SavvyCAN falls a whole cycle behind (the computer was busy), it carries on from the current slot rather than sending
the missed frames in a burst. The Microchip analyzer needs time for each frame: a header for a slave to answer takes up
to about 19 ms at 9600 bit/s and 10 ms at 19200, so slots shorter than that get spread out.

Changing the schedule table, the LDF or the bus stops the run. So does closing the window.
