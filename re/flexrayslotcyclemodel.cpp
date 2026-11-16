#include "flexrayslotcyclemodel.h"

FlexRaySlotCycleModel::FlexRaySlotCycleModel(QObject *parent) :
    QAbstractTableModel(parent)
{
}

QMap<quint8, FrChannelSchedule> FlexRaySlotCycleModel::aggregate(const QVector<CommFrame> &frames)
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
        if (frame.isFlexRayNullFrame())  cell.anyNull = true;
    }

    return result;
}

void FlexRaySlotCycleModel::setSchedule(const FrChannelSchedule &data)
{
    beginResetModel();
    mData = data;
    mSlotOrder.clear();
    mSlotOrder.reserve(mData.slotRows.size());
    for (auto it = mData.slotRows.constBegin(); it != mData.slotRows.constEnd(); ++it)
        mSlotOrder.append(it.key()); //QMap iterates in ascending key (slot ID) order
    endResetModel();
}

void FlexRaySlotCycleModel::clear()
{
    beginResetModel();
    mData = FrChannelSchedule();
    mSlotOrder.clear();
    endResetModel();
}

bool FlexRaySlotCycleModel::slotForRow(int row, quint32 &slotId) const
{
    if (row < 0 || row >= mSlotOrder.size()) return false;
    slotId = mSlotOrder.at(row);
    return true;
}

int FlexRaySlotCycleModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return mSlotOrder.size();
}

int FlexRaySlotCycleModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return FR_NUM_CYCLES;
}

QVariant FlexRaySlotCycleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    quint32 slotId = 0;
    if (!slotForRow(index.row(), slotId)) return QVariant();
    const int cycle = index.column();
    const FrSlotCycleCell *cell = mData.cell(slotId, cycle);
    if (!cell) return QVariant();

    switch (role)
    {
    case Qt::DisplayRole:
        //P1: present vs empty only (no static/changing/error colouring yet - that's P2)
        return cell->present() ? QStringLiteral("●") /* ● */ : QVariant();

    case Qt::TextAlignmentRole:
        return QVariant(Qt::AlignCenter);

    case Qt::ToolTipRole:
    {
        if (!cell->present()) return QVariant();
        const QString slotStr = QStringLiteral("0x%1").arg(slotId, 3, 16, QChar('0')).toUpper();
        QString tip = QStringLiteral("Slot %1  cycle %2\n").arg(slotStr).arg(cycle);
        tip += QStringLiteral("last payload: %1\n")
                   .arg(cell->lastPayload.isEmpty()
                            ? QStringLiteral("(empty)")
                            : QString::fromLatin1(cell->lastPayload.toHex(' ')).toUpper());
        tip += QStringLiteral("count: %1\n").arg(cell->count);
        tip += QStringLiteral("last: %1 us").arg(cell->lastTimeUs);
        if (cell->anyCrcFail) tip += QStringLiteral("\nCRC FAIL");
        if (cell->anyNull)    tip += QStringLiteral("\nnull frame");
        return tip;
    }

    default:
        return QVariant();
    }
}

QVariant FlexRaySlotCycleModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole)
    {
        if (orientation == Qt::Horizontal)
            return section; //cycle number 0..63
        if (orientation == Qt::Vertical)
        {
            if (section < 0 || section >= mSlotOrder.size()) return QVariant();
            return QStringLiteral("0x%1").arg(mSlotOrder.at(section), 3, 16, QChar('0')).toUpper();
        }
    }
    else if (role == Qt::TextAlignmentRole)
    {
        return QVariant(Qt::AlignCenter);
    }
    return QVariant();
}
