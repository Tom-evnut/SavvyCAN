#ifndef LINSCHEDULERUNNER_H
#define LINSCHEDULERUNNER_H

#include <QHash>
#include <QList>
#include "can_structs.h"
#include "dbc/linschedule.h"

/*
 * Runs an LDF schedule table as the LIN master: which frame to send when. It has no timer and sends nothing itself
 * (the LIN Schedule Runner window does both), so it can be tested with a made-up clock.
 *
 * Slot times count from the start of the run, so the schedule doesn't drift when a frame goes out late. If the
 * runner falls more than a whole cycle behind (the computer was busy or asleep), it carries on from now rather than
 * sending everything it missed.
 *
 * Frames are LIN frames with their checksum already worked out (the schedule set's model, classic for the diagnostic
 * IDs), so the connection sends them as they are: a master frame as its header and data, a slave's frame as a header
 * alone.
 */
class LinScheduleRunner
{
public:
    static constexpr double MIN_SLOT_MS = 1.0; //a zero or negative delay in an LDF counts as this

    void setSchedule(const LinSchedule &schedule, CommFrame::LinChecksumModel model); //stops the run
    const LinSchedule &schedule() const { return mSchedule; }
    void setBus(int bus) { mBus = bus; }
    int bus() const { return mBus; }

    //data the master sends for a frame instead of the LDF's, by frame name; kept when the schedule changes
    void setData(const QString &frameName, const QByteArray &data);
    void clearData() { mData.clear(); }
    QByteArray dataFor(const LinScheduleSlot &slot) const;

    void start(qint64 nowUs, quint64 cycles = 0); //cycles 0: until stopped
    void stop() { mRunning = false; }
    bool isRunning() const { return mRunning; }

    QList<CommFrame> due(qint64 nowUs); //the frames whose slots have come, in order
    qint64 nextDueUs() const { return mNextUs; }
    int nextSlot() const { return mIndex; }
    quint64 completedCycles() const { return mCycles; }
    quint64 framesSent() const { return mFramesSent; }

    static CommFrame frameFor(const LinScheduleSlot &slot, const QByteArray &data, CommFrame::LinChecksumModel model,
                              int bus);

private:
    qint64 slotUs(int index) const;
    qint64 cycleUs() const;

    LinSchedule mSchedule;
    CommFrame::LinChecksumModel mModel = CommFrame::LinChecksumEnhanced;
    QHash<QString, QByteArray> mData;
    int mBus = 0;
    bool mRunning = false;
    int mIndex = 0;
    qint64 mNextUs = 0;
    quint64 mCycles = 0;
    quint64 mCycleLimit = 0;
    quint64 mFramesSent = 0;
};

#endif // LINSCHEDULERUNNER_H
