#include "flexrayslotcyclemodel.h"

#include <QGuiApplication>
#include <QPalette>

FlexRaySlotCycleModel::FlexRaySlotCycleModel(QObject *parent) :
    QAbstractTableModel(parent)
{
}

bool FlexRaySlotCycleModel::isDarkTheme()
{
    const QColor win = QGuiApplication::palette().color(QPalette::Window);
    return win.lightness() < 128;
}

QColor FlexRaySlotCycleModel::colorForState(FrCellState state, bool darkTheme)
{
    switch (state)
    {
    case FrCellState::Static:
        return darkTheme ? QColor(0x2E, 0x5E, 0x34) : QColor(0xC8, 0xE6, 0xC9);
    case FrCellState::Changing:
        return darkTheme ? QColor(0x8A, 0x5A, 0x00) : QColor(0xFF, 0xE0, 0xB2);
    case FrCellState::Error:
        return darkTheme ? QColor(0x7A, 0x24, 0x24) : QColor(0xFF, 0xCD, 0xD2);
    case FrCellState::Empty:
    default:
        return QColor(); //invalid -> no brush
    }
}

void FlexRaySlotCycleModel::setSchedule(const FrChannelSchedule &data)
{
    mData = data;
    rebuildRows();
}

void FlexRaySlotCycleModel::setFilters(const FrFilterSettings &filters)
{
    mFilters = filters;
    rebuildRows();
}

void FlexRaySlotCycleModel::rebuildRows()
{
    beginResetModel();
    mSummaries.clear();
    mSlotOrder.clear();
    for (auto it = mData.slotRows.constBegin(); it != mData.slotRows.constEnd(); ++it)
    {
        FrSlotSummary summary = FrScheduleAnalysis::summarizeSlot(it.value());
        mSummaries.insert(it.key(), summary);
        if (FrScheduleAnalysis::slotPassesFilters(summary, mFilters))
            mSlotOrder.append(it.key()); //QMap iterates ascending by slot ID
    }
    endResetModel();
}

void FlexRaySlotCycleModel::clear()
{
    beginResetModel();
    mData = FrChannelSchedule();
    mSummaries.clear();
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
    return FR_TOTAL_COLS;
}

QVariant FlexRaySlotCycleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();

    quint32 slotId = 0;
    if (!slotForRow(index.row(), slotId)) return QVariant();
    const int col = index.column();

    // --- summary columns (right of the 64 cycle columns) ---
    if (col >= FR_NUM_CYCLES)
    {
        const auto sit = mSummaries.constFind(slotId);
        if (sit == mSummaries.constEnd()) return QVariant();
        const FrSlotSummary &sum = sit.value();

        if (role == Qt::TextAlignmentRole)
            return QVariant(Qt::AlignCenter);

        if (role == Qt::DisplayRole)
        {
            switch (col)
            {
            case FR_COL_COUNT:  return QVariant(static_cast<qulonglong>(sum.totalCount));
            case FR_COL_REPEAT: return sum.repeatText();
            case FR_COL_MUX:    return sum.cycleMultiplexed ? QStringLiteral("yes") : QVariant();
            default:            return QVariant();
            }
        }
        if (role == Qt::BackgroundRole && col == FR_COL_MUX && sum.cycleMultiplexed)
            return colorForState(FrCellState::Changing, isDarkTheme());
        return QVariant();
    }

    // --- cycle columns ---
    const int cycle = col;
    const FrSlotCycleCell *cell = mData.cell(slotId, cycle);
    if (!cell) return QVariant();

    const FrCellState state = FrScheduleAnalysis::classifyCell(*cell);

    switch (role)
    {
    case Qt::DisplayRole:
        return cell->present() ? QStringLiteral("●") /* ● */ : QVariant();

    case Qt::TextAlignmentRole:
        return QVariant(Qt::AlignCenter);

    case Qt::BackgroundRole:
    {
        const QColor c = colorForState(state, isDarkTheme());
        return c.isValid() ? QVariant(c) : QVariant();
    }

    case Qt::ToolTipRole:
    {
        if (!cell->present()) return QVariant();
        const QString slotStr = QStringLiteral("0x%1").arg(slotId, 3, 16, QChar('0')).toUpper();
        static const char *stateName[] = { "empty", "static", "changing", "error" };
        QString tip = QStringLiteral("Slot %1  cycle %2  [%3]\n")
                          .arg(slotStr).arg(cycle).arg(stateName[static_cast<int>(state)]);
        tip += QStringLiteral("last payload: %1\n")
                   .arg(cell->lastPayload.isEmpty()
                            ? QStringLiteral("(empty)")
                            : QString::fromLatin1(cell->lastPayload.toHex(' ')).toUpper());
        tip += QStringLiteral("count: %1").arg(cell->count);
        if (cell->nullCount > 0) tip += QStringLiteral("  (null: %1)").arg(cell->nullCount);
        tip += QStringLiteral("\nlast: %1 us").arg(cell->lastTimeUs);
        if (cell->anyCrcFail) tip += QStringLiteral("\nCRC FAIL");
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
        {
            switch (section)
            {
            case FR_COL_COUNT:  return QStringLiteral("Σ Count"); // Σ Count
            case FR_COL_REPEAT: return QStringLiteral("Repeat");
            case FR_COL_MUX:    return QStringLiteral("Mux");
            default:            return section; //cycle number 0..63
            }
        }
        if (orientation == Qt::Vertical)
        {
            if (section < 0 || section >= mSlotOrder.size()) return QVariant();
            const quint32 slotId = mSlotOrder.at(section);
            QString text = QStringLiteral("0x%1").arg(slotId, 3, 16, QChar('0')).toUpper();
            const auto sit = mSummaries.constFind(slotId);
            if (sit != mSummaries.constEnd() && sit.value().cycleMultiplexed)
                text += QStringLiteral(" ⇄"); // ⇄ marks a cycle-multiplexed slot
            return text;
        }
    }
    else if (role == Qt::ToolTipRole && orientation == Qt::Vertical)
    {
        if (section < 0 || section >= mSlotOrder.size()) return QVariant();
        const quint32 slotId = mSlotOrder.at(section);
        const auto sit = mSummaries.constFind(slotId);
        if (sit == mSummaries.constEnd()) return QVariant();
        const FrSlotSummary &sum = sit.value();
        const QString slotStr = QStringLiteral("0x%1").arg(slotId, 3, 16, QChar('0')).toUpper();
        QString tip = QStringLiteral("Slot %1\n").arg(slotStr);
        tip += QStringLiteral("total: %1   active cycles: %2\n").arg(sum.totalCount).arg(sum.activeCycleCount);
        tip += QStringLiteral("repetition: %1\n").arg(sum.repeatText());
        tip += sum.cycleMultiplexed ? QStringLiteral("cycle-multiplexed") : QStringLiteral("not multiplexed");
        return tip;
    }
    else if (role == Qt::TextAlignmentRole && orientation == Qt::Horizontal)
    {
        return QVariant(Qt::AlignCenter);
    }
    return QVariant();
}
