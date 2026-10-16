#include "linschedulerunner.h"

void LinScheduleRunner::setSchedule(const LinSchedule &schedule, CommFrame::LinChecksumModel model)
{
    mSchedule = schedule;
    mModel = model;
    mRunning = false;
    mIndex = 0;
}

void LinScheduleRunner::setData(const QString &frameName, const QByteArray &data)
{
    mData.insert(frameName, data.left(CommFrame::LIN_MAX_PAYLOAD));
}

QByteArray LinScheduleRunner::dataFor(const LinScheduleSlot &slot) const
{
    return mData.value(slot.name, slot.data);
}

qint64 LinScheduleRunner::slotUs(int index) const
{
    return qint64(qMax(mSchedule.entries.at(index).delayMs, MIN_SLOT_MS) * 1000.0 + 0.5);
}

qint64 LinScheduleRunner::cycleUs() const
{
    qint64 total = 0;
    for (int i = 0; i < mSchedule.entries.size(); i++) total += slotUs(i);
    return total;
}

void LinScheduleRunner::start(qint64 nowUs, quint64 cycles)
{
    mIndex = 0;
    mNextUs = nowUs;
    mCycles = 0;
    mCycleLimit = cycles;
    mFramesSent = 0;
    mRunning = !mSchedule.entries.isEmpty();
}

QList<CommFrame> LinScheduleRunner::due(qint64 nowUs)
{
    QList<CommFrame> frames;
    if (!mRunning) return frames;
    if (nowUs - mNextUs > cycleUs()) mNextUs = nowUs; //a cycle or more behind: carry on from now

    while (mRunning && nowUs >= mNextUs)
    {
        const LinScheduleSlot &slot = mSchedule.entries.at(mIndex);
        if (slot.kind != LinScheduleSlot::Silent)
        {
            frames.append(frameFor(slot, dataFor(slot), mModel, mBus));
            mFramesSent++;
        }
        mNextUs += slotUs(mIndex);
        if (++mIndex >= mSchedule.entries.size())
        {
            mIndex = 0;
            mCycles++;
            if (mCycleLimit && mCycles >= mCycleLimit) mRunning = false;
        }
    }
    return frames;
}

CommFrame LinScheduleRunner::frameFor(const LinScheduleSlot &slot, const QByteArray &data,
                                      CommFrame::LinChecksumModel model, int bus)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(slot.id & 0x3F);
    frame.setBus(bus);
    frame.setReceived(false);
    if (slot.kind == LinScheduleSlot::MasterFrame && !data.isEmpty())
    {
        const bool diagnostic = slot.id == CommFrame::LIN_ID_MASTER_REQUEST || slot.id == CommFrame::LIN_ID_SLAVE_RESPONSE;
        const CommFrame::LinChecksumModel m = diagnostic ? CommFrame::LinChecksumClassic : model;
        const QByteArray bytes = data.left(CommFrame::LIN_MAX_PAYLOAD);
        frame.setPayload(bytes);
        frame.setLinChecksum(CommFrame::linChecksum(bytes, quint8(slot.id & 0x3F), m));
        frame.setLinChecksumModel(m);
    }
    return frame;
}
