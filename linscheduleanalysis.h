#ifndef LINSCHEDULEANALYSIS_H
#define LINSCHEDULEANALYSIS_H

#include <QList>
#include <QMap>
#include <QString>
#include <QVector>
#include <functional>
#include "can_structs.h"

/*
 * Working out a LIN bus's schedule from a capture (RE Tools > LIN Schedule Analysis): every LIN frame is one header
 * the master sent, answered or not, so the order of their IDs is the master's schedule.
 *
 *   - per ID: how often its header comes and how regularly (period and jitter), how often a slave answers it,
 *     the response lengths, checksum model and errors seen, and how many of the frames SavvyCAN sent;
 *   - the schedule table: the shortest run of IDs that repeats through the capture (allowing for the odd frame
 *     slipped in, such as a command or a diagnostic request), each slot's delay (the median time to the next
 *     header), and how much of the capture follows it.
 * The table can be written as an LDF Schedule_tables block, to paste into an LDF and run with the LIN Schedule Runner.
 */
namespace LinScheduleAnalysis {

struct IdStats
{
    quint8 id = 0;
    int headers = 0;            //frames of this ID: the master sent its header every time
    int answered = 0;           //with a response
    int sent = 0;               //sent by SavvyCAN rather than received
    int errors = 0;             //checksum, parity or device-reported errors
    QList<int> lengths;         //response lengths seen, sorted
    int classic = 0, enhanced = 0; //valid checksums of each model
    double meanPeriodMs = 0, minPeriodMs = 0, maxPeriodMs = 0, jitterMs = 0; //between headers; 0 with one header

    double responseRate() const { return headers ? double(answered) / headers : 0; }
};

struct Slot
{
    quint8 id = 0;
    double delayMs = 0;         //median time from this slot's header to the next one's
    double responseRate = 0;    //how often this ID was answered
};

struct Result
{
    int frames = 0;             //LIN frames analysed
    double durationMs = 0;
    QList<IdStats> ids;         //by ID
    QList<Slot> cycle;          //the schedule table; empty if no repeating order was found
    double cycleMs = 0;
    double followRate = 0;      //share of the header-to-header steps that follow the cycle
    double medianGapMs = 0;     //median time between consecutive headers (the slot time, if they're all alike)

    //the cycle as an LDF Schedule_tables block, naming each frame with frameName (its ID if that gives nothing)
    QString ldfSchedule(const QString &tableName, const std::function<QString(quint8)> &frameName = nullptr) const;
};

//the LIN frames of frames on bus (-1: all buses), in the order given (a capture's time order)
Result analyse(const QVector<CommFrame> &frames, int bus = -1);

//the shortest period p (in steps) for which seq[i] == seq[i + p] for at least minMatch of the positions, or 0;
//exposed for the tests
int findPeriod(const QList<quint8> &seq, double minMatch = 0.8);

}

#endif // LINSCHEDULEANALYSIS_H
