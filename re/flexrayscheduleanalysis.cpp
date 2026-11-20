#include "flexrayscheduleanalysis.h"

#include <QSet>
#include <cstdlib> // std::abs(long)
#include <numeric> // std::gcd

QMap<quint8, FrChannelSchedule> FrScheduleAnalysis::aggregate(const QVector<CommFrame> &frames)
{
    QMap<quint8, FrChannelSchedule> result;

    for (const CommFrame &frame : frames)
    {
        if (!frame.isFlexRay()) continue; //only FlexRay traffic lands in the matrix

        const int cycle = frame.flexRayCycle();
        if (cycle < 0 || cycle >= FR_NUM_CYCLES) continue; //defensive: cycle is a 6-bit field (0..63)

        const quint8 channel = frame.getBus();
        const quint32 slotId = frame.frameId(); //11-bit FlexRay slot ID

        FrChannelSchedule &sched = result[channel];
        FrSlotRow &row = sched.slotRows[slotId];
        row.slotId = slotId;
        FrSlotCycleCell &cell = row.cells[cycle];

        const QByteArray payload = frame.payload();
        if (cell.count > 0 && payload != cell.lastPayload)
            cell.payloadVaries = true; //a different payload on the same (slot, cycle) => not cycle-stable

        cell.count++;
        cell.lastPayload = payload;
        cell.lastTimeUs = static_cast<quint64>(frame.timeStamp().microSeconds());
        if (frame.hasFlexRayCrcError()) cell.anyCrcFail = true;
        if (frame.isFlexRayNullFrame())
        {
            cell.anyNull = true;
            cell.nullCount++;
        }
    }

    return result;
}

FrCellState FrScheduleAnalysis::classifyCell(const FrSlotCycleCell &cell)
{
    if (!cell.present())                      return FrCellState::Empty;
    if (cell.anyCrcFail || !cell.hasData())   return FrCellState::Error;   //CRC-failed, or only null frames
    if (cell.payloadVaries)                   return FrCellState::Changing;
    return FrCellState::Static;
}

int FrScheduleAnalysis::inferRepetition(const QVector<int> &activeCyclesSorted, int &baseCycleOut)
{
    if (activeCyclesSorted.isEmpty())
    {
        baseCycleOut = -1;
        return 0;
    }
    baseCycleOut = activeCyclesSorted.first();
    if (activeCyclesSorted.size() < 2)
        return 0; //single occurrence - no period to infer

    int g = 0;
    for (int i = 1; i < activeCyclesSorted.size(); ++i)
        g = std::gcd(g, activeCyclesSorted.at(i) - activeCyclesSorted.at(i - 1));
    if (g <= 0) return 0;

    //snap the gcd of the gaps to the nearest FlexRay-legal period (1/2/4/8/16/32/64)
    int best = FR_LEGAL_PERIODS[0];
    long bestDiff = -1;
    for (int p : FR_LEGAL_PERIODS)
    {
        const long d = std::abs(static_cast<long>(p) - static_cast<long>(g));
        if (bestDiff < 0 || d < bestDiff)
        {
            bestDiff = d;
            best = p;
        }
    }
    return best;
}

FrSlotSummary FrScheduleAnalysis::summarizeSlot(const FrSlotRow &row)
{
    FrSlotSummary s;
    s.slotId = row.slotId;

    QVector<int> activeCycles;
    QSet<QByteArray> dataPayloads; //distinct payloads across data-bearing cells -> cross-cycle multiplex

    for (int c = 0; c < FR_NUM_CYCLES; ++c)
    {
        const FrSlotCycleCell &cell = row.cells[c];
        if (!cell.present()) continue;

        activeCycles.append(c);
        s.totalCount += cell.count;
        s.nullTotal  += cell.nullCount;
        if (cell.payloadVaries) s.anyChanging = true;
        if (cell.anyCrcFail)    s.anyError = true;
        if (cell.nullCount > 0) s.anyNull = true;
        if (cell.hasData())     dataPayloads.insert(cell.lastPayload);
    }

    s.activeCycleCount = activeCycles.size();
    //cycle-multiplexed = the (non-null) payload differs between cycles
    s.cycleMultiplexed = dataPayloads.size() > 1;
    s.period = inferRepetition(activeCycles, s.baseCycle); //activeCycles is ascending by construction
    return s;
}

bool FrScheduleAnalysis::slotPassesFilters(const FrSlotSummary &summary, const FrFilterSettings &filters)
{
    if (summary.totalCount < filters.minCount) return false;
    if (filters.hideStatic && summary.isStatic()) return false;
    if (filters.onlyMultiplexed && !summary.cycleMultiplexed) return false;
    //hide slots whose entire traffic is null frames
    if (filters.hideNull && summary.totalCount > 0 && summary.nullTotal == summary.totalCount) return false;
    return true;
}

QString FrSlotSummary::repeatText() const
{
    if (activeCycleCount == 0) return QStringLiteral("-");
    if (activeCycleCount == 1 || period == 0)
        return QStringLiteral("once (c%1)").arg(baseCycle);
    return QStringLiteral("every %1 (base %2)").arg(period).arg(baseCycle);
}
