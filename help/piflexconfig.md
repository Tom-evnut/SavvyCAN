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

Pick a mode in the "Run mode" box and click "Set mode" to switch the device. The current mode is read
from the device when you connect and shown as "Device mode"; it is never changed automatically on
connect - only when you click Set mode.

To guide you, the Injection rules box (including Send override) is enabled only in Inject mode and the
Generator schedule box only in Source mode; the other is greyed out. In Sniff and Bridge neither config
box applies. Firmware that doesn't support the runtime mode request shows "Device mode: not supported"
and disables the selector, but leaves both config boxes usable so the pre-mode inject/source paths still
work.

Injection rules
==============================
An injection rule tells the firmware which captured frame to modify. Each rule has:

* Slot ID    - the FlexRay slot (0 - 0x7FF).
* Cycle base - the single cycle a sent override splices on. An override carries this one cycle value and
               the firmware matches it exactly, so an override lands ONLY on frames whose cycle equals
               Cycle base (see "Sending an override" below).
* Cycle mask - which cycle bits the rule-match COUNTER uses; 0 counts any cycle. The count uses
               (frame cycle AND mask) == (cycle base AND mask). This governs the "Rule matches" count
               only - it does NOT widen where an override splices.
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
Inject mode, has a matching rule, and the injector is enabled (un-tick Listen Only on the bus to enable it).

The override splices ONLY on the single cycle equal to the rule row's "Cycle base": the override carries
that one cycle value and the firmware matches it exactly (the rule's "Cycle mask" does not apply to
overrides). Known limitation: to override a slot on every cycle you must send one override per cycle
(Cycle base 0 through 63) - there is no any-cycle override on the current firmware.

Confirming a splice landed: watch the CAPTURE - the injected bytes must appear on the wire in the target
slot. The status counters do NOT prove a splice. "Rule matches" counts every forwarded frame that matches
an applied rule whether or not any override is stored, so it climbs on matching traffic even with no override
sent; it cannot tell you a splice was applied. The capture is the only confirmation.

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
* Rule matches    - total forwarded frames that matched an applied rule. This counts RULE hits, NOT overrides
                    applied: it climbs on matching traffic even when no override has been sent, so a climbing
                    number does not mean a splice happened. Confirm a splice from the capture.
* Override drops   - total overrides dropped (no matching rule, injector off, or bad length).
* Source drops     - generator slots the device rejected (for example an odd-length payload).

Use "Rules on device" to confirm a rule took (it should match the number you applied). To confirm an override
actually spliced, look at the CAPTURE - the status counters cannot distinguish an applied splice from an
ordinary rule match.
