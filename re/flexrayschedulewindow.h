#ifndef FLEXRAYSCHEDULEWINDOW_H
#define FLEXRAYSCHEDULEWINDOW_H

#include <QDialog>
#include <QMap>
#include "can_structs.h"
#include "re/flexrayslotcyclemodel.h"

namespace Ui {
class FlexRayScheduleWindow;
}

/*
 * FlexRay slot x cycle matrix view (P1 - snapshot grid).
 *
 * Lays the loaded FlexRay traffic out as a grid of slot ID (rows) x cycle 0..63 (columns) so the
 * cycle-multiplexed FlexRay schedule is visible at a glance. P1 is a static snapshot: the grid is
 * (re)built from the current frame list on open and on Refresh - no live incremental update, and no
 * activity/multiplexing colouring (those are P2/P3). The slot-ID column is the table's frozen
 * vertical header, so it stays put while the 64 cycle columns scroll.
 */
class FlexRayScheduleWindow : public QDialog
{
    Q_OBJECT

public:
    explicit FlexRayScheduleWindow(const QVector<CommFrame> *frames, QWidget *parent = nullptr);
    ~FlexRayScheduleWindow();

    void showEvent(QShowEvent *) override;

private slots:
    void rebuild();          //re-aggregate from the current frame list (open / Refresh)
    void channelChanged();   //show the selected channel's grid
    void filtersChanged();   //apply the toolbar filter controls to the grid

private:
    Ui::FlexRayScheduleWindow *ui;
    const QVector<CommFrame> *modelFrames;
    FlexRaySlotCycleModel *gridModel;
    QMap<quint8, FrChannelSchedule> schedules; //all observed channels from the last rebuild

    void showChannel(quint8 bus);
    void buildLegend();      //populate the colour-key legend (theme-aware)
    void updateStatusLabel(quint8 bus);
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void readSettings();
    void writeSettings();
};

#endif // FLEXRAYSCHEDULEWINDOW_H
