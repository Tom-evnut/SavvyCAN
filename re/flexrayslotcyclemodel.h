#ifndef FLEXRAYSLOTCYCLEMODEL_H
#define FLEXRAYSLOTCYCLEMODEL_H

#include <QAbstractTableModel>
#include <QByteArray>
#include <QMap>
#include <QVector>
#include "can_structs.h"

//FlexRay communication cycle counts run 0..63
static constexpr int FR_NUM_CYCLES = 64;

/*
 * One (channel, slotId, cycle) bucket of the FlexRay schedule. Everything the matrix view needs
 * about the frames seen in that bucket, aggregated as frames stream through (no per-frame storage).
 */
struct FrSlotCycleCell {
    quint32 count = 0;              //how many frames landed in this (slot, cycle)
    QByteArray lastPayload;         //payload of the most recent frame (device order, cycle NOT mixed in)
    bool payloadVaries = false;     //seen >1 distinct payload for this (slot, cycle) -> cycle-stable check
    quint64 lastTimeUs = 0;         //timestamp (microseconds) of the most recent frame
    bool anyCrcFail = false;        //any frame here was CRC-checked by the device and failed
    bool anyNull = false;           //any frame here was a FlexRay null frame (no valid payload)

    bool present() const { return count > 0; }
};

/*
 * A single slot's row across all 64 cycles.
 */
struct FrSlotRow {
    quint32 slotId = 0;
    FrSlotCycleCell cells[FR_NUM_CYCLES];
};

/*
 * The aggregated schedule for one channel (bus): slots keyed (and therefore sorted) by slot ID,
 * each carrying its 64 cycle cells.
 */
struct FrChannelSchedule {
    //keyed by slotId; QMap keeps them sorted ascending. Named slotRows (not "slots") so Qt's moc,
    //which treats a bare "slots" as the slots-section keyword, doesn't choke on this struct.
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
 * Slot x cycle matrix model for the FlexRay schedule view (P1 - snapshot grid).
 *   rows    = observed slot IDs (sorted), shown as the frozen vertical header
 *   columns = cycles 0..63
 * The cell shows present/empty; the tooltip carries last payload, count and last timestamp.
 *
 * aggregate() is a pure, GUI-free function so it can be unit-tested against a synthetic frame list.
 */
class FlexRaySlotCycleModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FlexRaySlotCycleModel(QObject *parent = nullptr);

    //Pure aggregator: fold a frame list into a per-channel schedule. Only FlexRay frames are counted;
    //everything else is ignored. Keyed by (getBus(), frameId()=slot, flexRayCycle()).
    static QMap<quint8, FrChannelSchedule> aggregate(const QVector<CommFrame> &frames);

    //Replace the displayed channel's data (triggers a full model reset).
    void setSchedule(const FrChannelSchedule &data);
    void clear();

    //row -> slotId (for the current channel), or false if out of range
    bool slotForRow(int row, quint32 &slotId) const;

    /* QAbstractTableModel */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    FrChannelSchedule mData;
    QVector<quint32>  mSlotOrder; //sorted slot IDs, one per row
};

#endif // FLEXRAYSLOTCYCLEMODEL_H
