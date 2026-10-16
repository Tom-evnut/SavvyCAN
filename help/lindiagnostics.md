LIN Diagnostics
===============

RE Tools > LIN Diagnostics shows LIN diagnostic messages and sends diagnostic requests as the LIN master: node
configuration (LIN 2.x) and UDS over LIN (ISO 14229 over the LIN transport layer, ISO 17987-2).

On LIN, diagnostics go in two frames: the master sends requests in master request frames (ID 0x3C) and asks for
answers with slave response headers (ID 0x3D), which the addressed slave fills in. Each frame starts with the slave's
node address (NAD); longer messages are split over several frames.

**Watching:** diagnostic frames on any bus show in the list as they come, put back together into messages: the
time, bus, request or response, NAD, the message bytes, and what it means (the service, a positive or negative answer
and why, and what node configuration answers say, such as a slave's supplier and function ID). Decode frame list does
the same for the frames in the main frame list, for a capture.

**Requests** need the bus's LIN connection in master mode (Connection window, bus settings). Pick the bus, the NAD
(7F reaches every slave, 7E is functional) and the request:

* **Read by identifier:** identifier 0 is the product ID (supplier, function, variant), 1 the serial number, 32-63
  are the supplier's own. Supplier 7FFF and function FFFF match any slave.
* **Assign NAD:** gives the slave at the NAD (its initial NAD) a new one, if its supplier and function ID match.
* **Conditional change NAD:** a new NAD for slaves whose identifier's byte, masked and inverted as given, is zero.
* **Save configuration:** the slave keeps its configuration (NAD, frame IDs) through a power cycle.
* **UDS or other request:** any request as hex bytes, the service first, e.g. `22 F1 90` (read data by identifier
  F190), `10 03` (extended session), `3E 00` (tester present).
* **Go to sleep:** the go-to-sleep command (no answer).

After a request SavvyCAN sends slave response headers until the answer is complete, up to "Headers to wait" of them,
one every "Every" milliseconds (at least a frame's slot: about 20 ms at 9600 bit/s). A "response pending" answer
starts the wait again.

**Scans:** Scan NADs asks every NAD from 01 to 7D for its product ID and lists the slaves that answer. Scan data
identifiers sends UDS read data by identifier (22) for each identifier in the range to the NAD above. Stop ends a
scan. A scan of all NADs takes a few seconds per ten NADs at the default settings.
