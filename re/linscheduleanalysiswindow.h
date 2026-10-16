#ifndef LINSCHEDULEANALYSISWINDOW_H
#define LINSCHEDULEANALYSISWINDOW_H

#include <QDialog>
#include <QVector>
#include "can_structs.h"
#include "linscheduleanalysis.h"

class QLabel;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QTableWidget;

/*
 * RE Tools > LIN Schedule Analysis: the schedule of a LIN bus worked out from the frames in the main list (see
 * linscheduleanalysis.h and help/linscheduleanalysis.md). Frame names come from a loaded LDF, if there is one.
 */
class LinScheduleAnalysisWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LinScheduleAnalysisWindow(const QVector<CommFrame> *frames, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void analyse();
    void copyLdf();

private:
    QString frameName(quint8 id) const;

    const QVector<CommFrame> *mFrames;
    QCheckBox *mAllBuses;
    QSpinBox *mBus;
    QPushButton *mAnalyse;
    QPushButton *mCopy;
    QLabel *mSummary;
    QTableWidget *mIds;
    QTableWidget *mCycle;
    LinScheduleAnalysis::Result mResult;
};

#endif // LINSCHEDULEANALYSISWINDOW_H
