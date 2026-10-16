#ifndef LDFTODBC_H
#define LDFTODBC_H

#include <QStringList>
#include "ldfparser.h"

class DBCFile;

/*
 * Turns an LDF into SavvyCAN's DBC classes, so interpretation, graphing, the signal views and the DBC editor work on
 * LIN as they do on CAN:
 *   - nodes: the master and the slaves (node attributes go in the slave's comment);
 *   - messages: each frame and diagnostic frame (ID 0-63, its length, its publisher as sender), and each
 *     event-triggered frame: its byte 0 (the protected ID of the frame it carries) becomes a multiplexor, and the
 *     signals of each frame it can carry are multiplexed on that frame's protected ID;
 *   - signals: little-endian, unsigned, at the frame's bit offset; the first physical range of the signal's
 *     encoding gives factor, offset, unit and range, its logical values the value list; the subscriber the receiver;
 *   - file attributes: BusType "LIN" (so the file only matches LIN frames, and stays LIN when saved as a DBC),
 *     LIN_protocol_version and LIN_speed.
 * Sporadic frames carry existing frames, so they add nothing. Schedule tables aren't kept yet (phase 3, sending).
 */
namespace LdfToDbc {

void fill(const LdfDocument &doc, DBCFile *file, QStringList *warnings = nullptr);

}

#endif // LDFTODBC_H
