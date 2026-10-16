#include "linschedule.h"
#include "ldfparser.h"

double LinSchedule::cycleMs() const
{
    double total = 0;
    for (const LinScheduleSlot &slot : entries) total += slot.delayMs;
    return total;
}

const LinSchedule *LinScheduleSet::find(const QString &name) const
{
    for (const LinSchedule &schedule : schedules)
        if (schedule.name == name) return &schedule;
    return nullptr;
}

void LinScheduleSet::putBits(QByteArray &data, int offset, int size, quint64 value)
{
    for (int bit = 0; bit < size; bit++)
    {
        const int pos = offset + bit;
        if (pos < 0 || pos / 8 >= data.size()) continue;
        const char mask = char(1 << (pos % 8));
        if ((value >> bit) & 1) data[pos / 8] = char(data.at(pos / 8) | mask);
        else data[pos / 8] = char(data.at(pos / 8) & ~mask);
    }
}

QByteArray LinScheduleSet::initialData(const LdfDocument &doc, const LdfFrame &frame)
{
    QByteArray data(qBound(0, frame.length, int(CommFrame::LIN_MAX_PAYLOAD)), char(0xFF));
    for (const LdfFrameSignal &fs : frame.frameSignals)
    {
        const LdfSignal *signal = doc.findSignal(fs.name);
        if (!signal || signal->initValues.isEmpty()) continue;
        if (signal->byteArray)
        {
            for (int i = 0; i < signal->initValues.size() && i * 8 < signal->size; i++)
                putBits(data, fs.offset + i * 8, 8, quint64(signal->initValues.at(i)));
        }
        else putBits(data, fs.offset, signal->size, quint64(signal->initValues.first()));
    }
    return data;
}

LinScheduleSet LinScheduleSet::fromLdf(const LdfDocument &doc, QStringList *warnings)
{
    LinScheduleSet set;
    set.checksumModel = doc.protocolVersion.startsWith("1.") ? CommFrame::LinChecksumClassic
                                                             : CommFrame::LinChecksumEnhanced;

    static const QStringList configCommands = {"AssignNAD", "ConditionalChangeNAD", "DataDump", "SaveConfiguration",
                                               "AssignFrameIdRange", "AssignFrameId", "UnassignFrameId"};
    for (const LdfSchedule &table : doc.schedules)
    {
        LinSchedule schedule;
        schedule.name = table.name;
        for (const LdfScheduleEntry &entry : table.entries)
        {
            LinScheduleSlot slot;
            slot.name = entry.command;
            slot.delayMs = entry.delayMs;
            const LdfFrame *frame = doc.findFrame(entry.command);
            if (entry.command == "FreeFormat")
            {
                //eight data bytes for the master request frame
                slot.kind = LinScheduleSlot::MasterFrame;
                slot.id = CommFrame::LIN_ID_MASTER_REQUEST;
                for (const QString &arg : entry.arguments)
                {
                    bool ok = false;
                    const uint byte = arg.trimmed().toUInt(&ok, 0);
                    slot.data.append(char(ok ? byte : 0xFF));
                }
                slot.data = slot.data.left(CommFrame::LIN_MAX_PAYLOAD);
                slot.data.append(QByteArray(CommFrame::LIN_MAX_PAYLOAD - slot.data.size(), char(0xFF)));
            }
            else if (configCommands.contains(entry.command))
            {
                slot.note = "node configuration commands aren't sent yet";
            }
            else if (frame && (frame->id == CommFrame::LIN_ID_MASTER_REQUEST
                               || (!frame->diagnostic && frame->publisher == doc.master)))
            {
                slot.kind = LinScheduleSlot::MasterFrame;
                slot.id = quint8(frame->id);
                slot.data = initialData(doc, *frame);
            }
            else if (frame)
            {
                slot.kind = LinScheduleSlot::Header;
                slot.id = quint8(frame->id);
            }
            else
            {
                bool found = false;
                for (const LdfEventTriggeredFrame &ef : doc.eventTriggeredFrames)
                {
                    if (ef.name != entry.command) continue;
                    slot.kind = LinScheduleSlot::Header;
                    slot.id = quint8(ef.id);
                    found = true;
                }
                for (const LdfSporadicFrame &sf : doc.sporadicFrames)
                {
                    if (sf.name != entry.command) continue;
                    slot.note = "sporadic frames aren't sent yet";
                    found = true;
                }
                if (!found)
                {
                    slot.note = "not a frame or command of this file";
                    if (warnings)
                        *warnings << QString("schedule %1: %2 isn't a frame of this file, its slot sends nothing")
                                         .arg(table.name, entry.command);
                }
            }
            schedule.entries.append(slot);
        }
        set.schedules.append(schedule);
    }
    return set;
}
