#ifndef LINSCHEDULE_H
#define LINSCHEDULE_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include "can_structs.h"

class LdfDocument;
struct LdfFrame;

/*
 * An LDF's schedule tables, as kept with the loaded file (DBCFile::linSchedules) for the LIN schedule runner
 * (LinScheduleRunner). Each slot already says what the master sends in it:
 *   - a frame the master publishes (and MasterReq, and FreeFormat): the header and the data, which starts as the
 *     LDF's initial signal values (unused bits recessive, 1) and can be changed while the schedule runs;
 *   - a frame a slave publishes, an event-triggered frame, SlaveResp: the header alone, for the slaves to answer;
 *   - node configuration commands (AssignNAD and the others) and sporadic frames aren't sent yet: their slot passes
 *     with nothing sent, so the timing of the rest stays as the LDF says.
 */
struct LinScheduleSlot
{
    enum Kind {
        MasterFrame,    //header and data
        Header,         //header alone
        Silent,         //nothing sent (see note)
    };
    Kind kind = Silent;
    QString name;       //the frame, or the command as written in the LDF
    quint8 id = 0;
    QByteArray data;    //MasterFrame: the data to send unless it's changed
    double delayMs = 0; //from this slot to the next
    QString note;       //Silent: why nothing is sent
};

struct LinSchedule
{
    QString name;
    QList<LinScheduleSlot> entries;

    double cycleMs() const;
};

struct LinScheduleSet
{
    //the checksum of the unconditional frames: classic for LIN 1.x files, enhanced otherwise (diagnostic frames are
    //always classic)
    CommFrame::LinChecksumModel checksumModel = CommFrame::LinChecksumEnhanced;
    QList<LinSchedule> schedules;

    bool isEmpty() const { return schedules.isEmpty(); }
    const LinSchedule *find(const QString &name) const;

    static LinScheduleSet fromLdf(const LdfDocument &doc, QStringList *warnings = nullptr);
    //a frame's data from its signals' initial values, unused bits 1
    static QByteArray initialData(const LdfDocument &doc, const LdfFrame &frame);
    //writes the low size bits of value at bit offset (LIN: little-endian, least significant bit first)
    static void putBits(QByteArray &data, int offset, int size, quint64 value);
};

/*
 * Setting a signal in a LIN frame's data from what someone types: the LIN Schedule Runner's signal editor. LIN signals
 * are little-endian, least significant bit first, at a bit offset.
 */
namespace LinSignal {

struct Spec
{
    int startBit = 0;
    int size = 8;
    double factor = 1;
    double bias = 0;
    bool isSigned = false;
    QList<QPair<qint64, QString>> values; //value table: raw value, name
};

//the raw value at startBit (size bits; sign-extended if isSigned)
qint64 rawValue(const QByteArray &data, const Spec &spec);

/*
 * The raw value for text: a value table name ("Unlock"), a raw value in hex ("0xBD00"), or a physical value
 * ("12.5": raw = (value - bias) / factor, rounded). False (with why) if it's none of those or doesn't fit in the
 * signal; a signed value comes back as its two's complement in size bits.
 */
bool rawFromText(const QString &text, const Spec &spec, quint64 &raw, QString *why = nullptr);

}

#endif // LINSCHEDULE_H
