PiFlex Config
==============================

This window configures a connected pico-flexray (PiFlex) device: its run mode, its host-defined
injection rules, and its generator (source) schedule. It also shows the device's live status. Open it
from the Connection window by selecting a PicoFlexRay connection and clicking "PiFlex Config".

Everything here talks to the device over USB. Nothing is changed on the device just by opening this
window or connecting: rules, the schedule and the mode are only sent when you click the matching Apply,
Clear, Set or Send button.

Run mode
==============================
The device runs in one of four modes:

* Sniff   - capture only; the device never transmits.
* Bridge  - the device relays between the two FlexRay channels.
* Inject  - the injection rules below are active; matching overrides you send splice into live frames.
* Source  - the generator schedule below is active; the device transmits those slots on its own.

Pick a mode and click "Set mode" to switch the device. The current mode is read from the device when
you connect and shown as "Device mode"; it is never changed automatically on connect - only when you
click Set mode. The Injection rules section is relevant in Inject mode and the Generator schedule
section in Source mode.

Injection rules
==============================
An injection rule tells the firmware which captured frame to modify. Each rule has:

* Slot ID    - the FlexRay slot (0 - 0x7FF).
* Cycle base - the cycle value to match.
* Cycle mask - which cycle bits must match; 0 matches any cycle. A frame matches when
               (frame cycle AND mask) == (cycle base AND mask).
* Win offset - where in the slot payload the replacement window starts.
* Win length - how many bytes of the window actually reach the bus.

Edit the table, then click "Apply rules to device" to send them, or "Clear device rules" to wipe them.
Numbers may be decimal or 0x-hex. Rules are remembered between sessions and re-sent on reconnect only
after you have applied them at least once.

Sending an override (firing an injection)
==============================
Defining a rule does not change any traffic on its own - the rule only says WHAT may be replaced. To
actually inject, you send an override payload that the firmware splices into the matching frame.

1. Put the bytes you want to inject in the rule row's "Override payload (hex)" cell, e.g. `AA BB CC DD`.
   The payload must be an even number of bytes (FlexRay payloads are counted in 2-byte words).
2. Select that rule row and click "Send override".

The override is sent for that row's Slot ID and Cycle base. Only the bytes selected by the rule's window
(Win offset / Win length) actually reach the bus. The override reaches the bus only if the device is in
Inject mode, has a matching rule, and the injector is enabled (un-tick Listen Only on the bus to enable
it). Watch "Override matches" in the status line climb when it hits, or "Override drops" when it misses.

Generator schedule
==============================
In Source mode the device transmits slots from this table on its own schedule. Each source slot has:

* Slot ID   - the FlexRay slot to transmit (0 - 0x7FF).
* FR1 / FR2 - which channel(s) to transmit on.
* Payload   - the slot payload as hex bytes; must be an even number of bytes, 2 - 254.
* Last byte = cycle - when ticked, the device writes the current cycle counter into the final payload byte.

Below the table set the Frame period and Inter-cycle gap (microseconds) and tick "Run generator" to start.
Click "Apply schedule to device" to send it, or "Clear device schedule" to stop and wipe it.

Device status
==============================
The status line polls the device about once a second and shows:

* Injector        - whether overrides are currently enabled (follows the bus Listen Only setting).
* Rules on device - how many injection rules the device currently holds.
* Source slots    - how many generator slots the device currently holds.
* Generator       - whether the source schedule is running.
* Override matches - total overrides that matched a rule and were applied.
* Override drops   - total overrides dropped (no matching rule, injector off, or bad length).
* Source drops     - generator slots the device rejected (for example an odd-length payload).

Use these counters to confirm a rule took and whether your overrides are landing: after "Send override",
"Override matches" should climb on a hit and "Override drops" on a miss.
