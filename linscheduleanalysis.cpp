#include "linscheduleanalysis.h"
#include <QSet>
#include <QtMath>
#include <algorithm>

namespace LinScheduleAnalysis {

namespace {

double median(QList<double> values)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const int n = int(values.size());
    return n % 2 ? values.at(n / 2) : (values.at(n / 2 - 1) + values.at(n / 2)) / 2.0;
}

//the cycle of period p: the first window of p IDs that the next p repeat exactly (or, failing that, the first
//window), turned so that it starts at the lowest ID's first slot - a stable start for the same schedule
QList<quint8> cycleOf(const QList<quint8> &seq, int p)
{
    int start = 0;
    for (int i = 0; i + 2 * p <= seq.size(); i++)
    {
        bool same = true;
        for (int k = 0; k < p && same; k++) same = seq.at(i + k) == seq.at(i + p + k);
        if (same)
        {
            start = i;
            break;
        }
    }
    QList<quint8> cycle = seq.mid(start, p);
    const int lowest = int(std::min_element(cycle.begin(), cycle.end()) - cycle.begin());
    std::rotate(cycle.begin(), cycle.begin() + lowest, cycle.end());
    return cycle;
}

}

int findPeriod(const QList<quint8> &seq, double minMatch)
{
    const int n = int(seq.size());
    for (int p = 1; p * 2 <= n && p <= 256; p++)
    {
        int matches = 0;
        for (int i = 0; i + p < n; i++)
            if (seq.at(i) == seq.at(i + p)) matches++;
        if (double(matches) / (n - p) >= minMatch) return p;
    }
    return 0;
}

Result analyse(const QVector<CommFrame> &frames, int bus)
{
    Result r;
    QList<quint8> seq;
    QList<qint64> times;
    QMap<quint8, IdStats> stats;
    QMap<quint8, QList<double>> periods;
    QMap<quint8, qint64> lastSeen;

    for (const CommFrame &f : frames)
    {
        if (!f.isLin() || (bus >= 0 && f.getBus() != bus)) continue;
        const quint8 id = quint8(f.frameId() & 0x3F);
        const qint64 us = f.timeStamp().microSeconds();
        seq << id;
        times << us;

        IdStats &s = stats[id];
        s.id = id;
        s.headers++;
        if (!f.isLinHeaderOnly())
        {
            s.answered++;
            const int len = int(f.payload().size());
            if (!s.lengths.contains(len)) s.lengths << len;
        }
        if (!f.isReceived()) s.sent++;
        if (f.hasLinError()) s.errors++;
        if (f.linChecksumOk() && f.linChecksumModel() == CommFrame::LinChecksumClassic) s.classic++;
        if (f.linChecksumOk() && f.linChecksumModel() == CommFrame::LinChecksumEnhanced) s.enhanced++;
        if (lastSeen.contains(id)) periods[id] << (us - lastSeen.value(id)) / 1000.0;
        lastSeen[id] = us;
    }

    r.frames = int(seq.size());
    if (r.frames == 0) return r;
    r.durationMs = (times.last() - times.first()) / 1000.0;

    for (auto it = stats.begin(); it != stats.end(); ++it)
    {
        IdStats &s = it.value();
        std::sort(s.lengths.begin(), s.lengths.end());
        const QList<double> &p = periods.value(s.id);
        if (!p.isEmpty())
        {
            double sum = 0, sq = 0;
            s.minPeriodMs = p.first();
            s.maxPeriodMs = p.first();
            for (double v : p)
            {
                sum += v;
                s.minPeriodMs = qMin(s.minPeriodMs, v);
                s.maxPeriodMs = qMax(s.maxPeriodMs, v);
            }
            s.meanPeriodMs = sum / p.size();
            for (double v : p) sq += (v - s.meanPeriodMs) * (v - s.meanPeriodMs);
            s.jitterMs = qSqrt(sq / p.size());
        }
        r.ids << s;
    }

    QList<double> gaps;
    for (int i = 0; i + 1 < times.size(); i++) gaps << (times.at(i + 1) - times.at(i)) / 1000.0;
    r.medianGapMs = median(gaps);

    //the schedule: each slot's delay is the median time from its header to the next, over the steps that
    //follow the cycle
    const int p = findPeriod(seq);
    if (p == 0) return r;
    const QList<quint8> cycle = cycleOf(seq, p);
    QMap<QPair<quint8, quint8>, QList<double>> stepGaps; //(this slot's ID, next slot's ID) -> gaps
    QSet<QPair<quint8, quint8>> steps;
    for (int k = 0; k < p; k++) steps.insert(qMakePair(cycle.at(k), cycle.at((k + 1) % p)));
    int following = 0;
    for (int i = 0; i + 1 < seq.size(); i++)
    {
        const QPair<quint8, quint8> step(seq.at(i), seq.at(i + 1));
        if (!steps.contains(step)) continue;
        following++;
        stepGaps[step] << gaps.at(i);
    }
    r.followRate = gaps.isEmpty() ? 0 : double(following) / gaps.size();
    for (int k = 0; k < p; k++)
    {
        Slot slot;
        slot.id = cycle.at(k);
        slot.delayMs = median(stepGaps.value(qMakePair(cycle.at(k), cycle.at((k + 1) % p))));
        slot.responseRate = stats.value(slot.id).responseRate();
        r.cycle << slot;
        r.cycleMs += slot.delayMs;
    }
    return r;
}

QString Result::ldfSchedule(const QString &tableName, const std::function<QString(quint8)> &frameName) const
{
    QString out = "Schedule_tables {\n  " + tableName + " {\n";
    for (const Slot &slot : cycle)
    {
        QString name = frameName ? frameName(slot.id) : QString();
        if (name.isEmpty()) name = "Frame_0x" + QString("%1").arg(slot.id, 2, 16, QChar('0')).toUpper();
        //LDF delays are whole milliseconds in practice; keep one decimal when the capture says so
        const double ms = qRound(slot.delayMs * 10.0) / 10.0;
        out += QString("    %1 delay %2 ms;\n").arg(name, QString::number(ms));
    }
    out += "  }\n}\n";
    return out;
}

}
