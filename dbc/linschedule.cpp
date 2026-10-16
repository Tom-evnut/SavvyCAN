#include "linschedule.h"
#include "ldfparser.h"
#include <cmath>

qint64 LinSignal::rawValue(const QByteArray &data, const Spec &spec)
{
    quint64 raw = 0;
    for (int bit = 0; bit < spec.size && bit < 64; bit++)
    {
        const int pos = spec.startBit + bit;
        if (pos < 0 || pos / 8 >= data.size()) break;
        if ((quint8(data.at(pos / 8)) >> (pos % 8)) & 1) raw |= quint64(1) << bit;
    }
    if (spec.isSigned && spec.size > 0 && spec.size < 64 && (raw >> (spec.size - 1)) & 1)
        return qint64(raw) - (qint64(1) << spec.size);
    return qint64(raw);
}

bool LinSignal::rawFromText(const QString &text, const Spec &spec, quint64 &raw, QString *why)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
    {
        if (why) *why = "no value";
        return false;
    }
    for (const auto &v : spec.values)
        if (!v.second.isEmpty() && v.second.compare(t, Qt::CaseInsensitive) == 0)
        {
            raw = quint64(v.first);
            return true;
        }

    const int size = qBound(1, spec.size, 64);
    const qint64 minRaw = spec.isSigned ? (size >= 64 ? INT64_MIN : -(qint64(1) << (size - 1))) : 0;
    const quint64 maxRaw = spec.isSigned ? (size >= 64 ? quint64(INT64_MAX) : (quint64(1) << (size - 1)) - 1)
                                         : (size >= 64 ? ~quint64(0) : (quint64(1) << size) - 1);
    bool ok = false;
    if (t.startsWith("0x", Qt::CaseInsensitive))
    {
        const quint64 value = t.mid(2).toULongLong(&ok, 16);
        if (!ok || (size < 64 && value > ((quint64(1) << size) - 1)))
        {
            if (why) *why = QString("%1 isn't a raw value of %2 bits").arg(t).arg(size);
            return false;
        }
        raw = value;
        return true;
    }
    const double physical = t.toDouble(&ok);
    if (!ok || spec.factor == 0)
    {
        if (why) *why = QString("%1 isn't a number or one of the signal's values").arg(t);
        return false;
    }
    const double scaled = std::round((physical - spec.bias) / spec.factor);
    if (scaled < double(minRaw) || scaled > double(maxRaw))
    {
        if (why) *why = QString("%1 is out of the signal's range").arg(t);
        return false;
    }
    const qint64 value = qint64(scaled);
    raw = size >= 64 ? quint64(value) : (quint64(value) & ((quint64(1) << size) - 1));
    return true;
}

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
