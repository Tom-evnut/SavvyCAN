#ifndef FLEXRAYSLOTCYCLEMODEL_H
#define FLEXRAYSLOTCYCLEMODEL_H

#include <QAbstractTableModel>
#include <QColor>
#include <QMap>
#include <QVector>
#include "re/flexrayscheduleanalysis.h"

/*
 * Slot x cycle matrix model for the FlexRay schedule view (the GUI layer).
 *   rows    = observed slot IDs (sorted, filtered), shown as the frozen vertical header
 *   columns = cycles 0..63, then the P2 summary columns (Count / Repeat / Mux)
 * P1: present/empty + tooltip. P2: per-cell state colouring, per-slot summary, cycle-multiplex
 * flag on the row header, and row filters.
 *
 * All the analysis lives in FrScheduleAnalysis (pure, GUI-free, unit-tested); this class only turns
 * it into model roles (text, colour, alignment, tooltips).
 */
class FlexRaySlotCycleModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit FlexRaySlotCycleModel(QObject *parent = nullptr);

    //theme-aware cell background for a state (shared by the grid and the window's legend).
    //Returns an invalid QColor for Empty (no brush).
    static QColor colorForState(FrCellState state, bool darkTheme);
    static bool isDarkTheme();

    //Replace the displayed channel's data (triggers a full model reset).
    void setSchedule(const FrChannelSchedule &data);
    void clear();
    void setFilters(const FrFilterSettings &filters);
    FrFilterSettings filters() const { return mFilters; }

    //row -> slotId (for the current channel, after filtering), or false if out of range
    bool slotForRow(int row, quint32 &slotId) const;

    /* QAbstractTableModel */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    FrChannelSchedule mData;
    QVector<quint32>  mSlotOrder;               //filtered, sorted slot IDs, one per row
    QMap<quint32, FrSlotSummary> mSummaries;    //slotId -> precomputed summary (all slots)
    FrFilterSettings  mFilters;

    void rebuildRows(); //recompute summaries + the filtered row order; emits a model reset
};

#endif // FLEXRAYSLOTCYCLEMODEL_H
