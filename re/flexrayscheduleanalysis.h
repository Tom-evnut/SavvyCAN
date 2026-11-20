#ifndef FLEXRAYSCHEDULEANALYSIS_H
#define FLEXRAYSCHEDULEANALYSIS_H

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>
#include "can_structs.h"

/*
 * Pure, GUI-free FlexRay schedule analysis: the data model and the aggregation / classification /
 * repetition-inference / filtering logic behind the slot x cycle matrix view. Depends only on Qt
 * Core (+ SerialBus for CommFrame), so it can be unit-tested headless without a QApplication, and
 * the GUI (FlexRaySlotCycleModel) builds the presentation on top of it.
 */

//FlexRay communication cycle counts run 0..63
static constexpr int FR_NUM_CYCLES = 64;

//Summary columns appended to the right of the 64 cycle columns (P2: per-slot summary).
static constexpr int FR_COL_COUNT   = FR_NUM_CYCLES;      //total frames across all cycles
static constexpr int FR_COL_REPEAT  = FR_NUM_CYCLES + 1;  //inferred repetition / base cycle
static constexpr int FR_COL_MUX     = FR_NUM_CYCLES + 2;  //cycle-multiplexed flag
static constexpr int FR_NUM_SUMMARY_COLS = 3;
static constexpr int FR_TOTAL_COLS  = FR_NUM_CYCLES + FR_NUM_SUMMARY_COLS;

//FlexRay-legal slot repetitions (base-cycle/repetition pattern): a slot repeats every 2^k cycles.
static constexpr int FR_LEGAL_PERIODS[] = { 1, 2, 4, 8, 16, 32, 64 };

/*
 * One (channel, slotId, cycle) bucket of the FlexRay schedule. Everything the matrix view needs
 * about the frames seen in that bucket, aggregated as frames stream through (no per-frame storage).
 */
struct FrSlotCycleCell {
    quint32 count = 0;              //how many frames landed in this (slot, cycle)
    quint32 nullCount = 0;          //how many of those were FlexRay null frames (no valid payload)
    QByteArray lastPayload;         //payload of the most recent frame (device order, cycle NOT mixed in)
    bool payloadVaries = false;     //seen >1 distinct payload for this (slot, cycle) -> cycle-stable check
    quint64 lastTimeUs = 0;         //timestamp (microseconds) of the most recent frame
    bool anyCrcFail = false;        //any frame here was CRC-checked by the device and failed
    bool anyNull = false;           //any frame here was a FlexRay null frame (no valid payload)

    bool present() const { return count > 0; }
    //a cell carrying at least one real (non-null) data frame
    bool hasData() const { return count > nullCount; }
};

/*
 * Per-cell activity state (P2 colouring):
 *   Empty    - no frame seen
 *   Static   - present, same payload every time, on this (slot, cycle)
 *   Changing - the payload varied over time on this (slot, cycle)
 *   Error    - a CRC-failed frame, or only null frames, landed here
 */
enum class FrCellState { Empty, Static, Changing, Error };

/*
 * A single slot's row across all 64 cycles.
 */
struct FrSlotRow {
    quint32 slotId = 0;
    FrSlotCycleCell cells[FR_NUM_CYCLES];
};

/*
 * Derived per-slot summary (P2): computed purely from a slot's 64 cells, no GUI.
 */
struct FrSlotSummary {
    quint32 slotId = 0;
    quint64 totalCount = 0;        //frames across all cycles
    quint64 nullTotal = 0;         //null frames across all cycles
    int activeCycleCount = 0;      //how many cycles carried any frame
    int baseCycle = -1;            //first cycle the slot appears on (-1 if never)
    int period = 0;                //inferred repetition (1/2/4/.../64); 0 = single/none
    bool cycleMultiplexed = false; //payload differs BETWEEN cycles -> the headline flag
    bool anyChanging = false;      //some (slot,cycle) cell saw >1 payload over time
    bool anyError = false;         //any CRC-failed frame
    bool anyNull = false;          //any null frame

    //a slot with activity but no changing/mux/error/null - a plain static signal
    bool isStatic() const
    {
        return activeCycleCount > 0 && !anyChanging && !cycleMultiplexed && !anyError && !anyNull;
    }
    //a human-readable repetition string, e.g. "every 4 (base 1)", "once (c0)", "-"
    QString repeatText() const;
};

/*
 * Row-level filter settings (P2 filters). A slot row is shown only if it passes all enabled filters.
 */
struct FrFilterSettings {
    bool hideStatic = false;        //drop plain static slots
    bool onlyMultiplexed = false;   //keep only cycle-multiplexed slots
    bool hideNull = false;          //drop slots whose only traffic is null frames
    quint32 minCount = 0;           //drop slots with fewer than this many total frames
};

/*
 * The aggregated schedule for one channel (bus): slots keyed (and therefore sorted) by slot ID,
 * each carrying its 64 cycle cells.
 */
struct FrChannelSchedule {
    //keyed by slotId; QMap keeps them sorted ascending. Named slotRows (not "slots") so Qt's moc,
    //which treats a bare "slots" as the slots-section keyword, doesn't choke where this is included.
    QMap<quint32, FrSlotRow> slotRows;

    //Returns the cell for (slotId, cycle), or nullptr if that slot/cycle was never seen.
    const FrSlotCycleCell *cell(quint32 slotId, int cycle) const
    {
        if (cycle < 0 || cycle >= FR_NUM_CYCLES) return nullptr;
        auto it = slotRows.constFind(slotId);
        if (it == slotRows.constEnd()) return nullptr;
        return &it->cells[cycle];
    }
};

/*
 * The pure analysis entry points. All static, no state, no GUI.
 */
struct FrScheduleAnalysis
{
    //Fold a frame list into a per-channel schedule. Only FlexRay frames are counted; everything else
    //is ignored. Keyed by (getBus(), frameId()=slot, flexRayCycle()).
    static QMap<quint8, FrChannelSchedule> aggregate(const QVector<CommFrame> &frames);

    //Classify a single cell for colouring.
    static FrCellState classifyCell(const FrSlotCycleCell &cell);

    //Summarise a slot across its 64 cells (count, repetition, cycle-multiplex flag, ...).
    static FrSlotSummary summarizeSlot(const FrSlotRow &row);

    //Infer the repetition period from the sorted list of active cycles; writes the base (first) cycle.
    //Returns 0 for <2 active cycles (single/none), else the gcd of the gaps snapped to the legal set.
    static int inferRepetition(const QVector<int> &activeCyclesSorted, int &baseCycleOut);

    //Does a slot pass the current row filters?
    static bool slotPassesFilters(const FrSlotSummary &summary, const FrFilterSettings &filters);
};

#endif // FLEXRAYSCHEDULEANALYSIS_H
