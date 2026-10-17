#ifndef LINSCHEDULEWINDOW_H
#define LINSCHEDULEWINDOW_H

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include "linschedulerunner.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class DBCFile;
class DBC_SIGNAL;

/*
 * Send Frames > LIN Schedule Runner: runs a schedule table of a loaded LDF as the LIN master on one bus. The bus's
 * connection has to be in master mode. Frames the master publishes can be given other data while the schedule runs,
 * as hex bytes or signal by signal (from the LDF's signals); see help/linschedulerunner.md.
 */
class LinScheduleWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LinScheduleWindow(QWidget *parent = nullptr);
    ~LinScheduleWindow();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void refreshFiles();
    void fileChanged();
    void scheduleChanged();
    void startStop();
    void sendDue();
    void showStatus();
    void cellChanged(int row, int column);
    void showSignals();
    void signalChanged(int row, int column);

private:
    enum Column { COL_SLOT, COL_FRAME, COL_ID, COL_SENDS, COL_DELAY, COL_DATA, COL_COUNT };
    enum SignalColumn { SIG_NAME, SIG_VALUE, SIG_RAW, SIG_VALUES, SIG_COUNT };

    DBCFile *currentFile() const;
    void fillTable();
    void stopRunning();
    void showData(const QString &frameName); //in every row that sends that frame
    QList<DBC_SIGNAL *> signalsOfRow(int row) const;
    void markSlot(int row);
    qint64 nowUs() const { return mClock.nsecsElapsed() / 1000; }

    QComboBox *mFile;
    QComboBox *mSchedule;
    QSpinBox *mBus;
    QSpinBox *mCycles;
    QPushButton *mStartStop;
    QLabel *mStatus;
    QTableWidget *mTable;
    QLabel *mSignalsLabel;
    QTableWidget *mSignals;
    int mSignalRow = -1; //the schedule row whose signals are shown
    int mMarkedRow = -1; //the row with the next-slot arrow

    LinScheduleRunner mRunner;
    QTimer mSendTimer;
    QTimer mStatusTimer;
    QElapsedTimer mClock;
    bool mFilling = false;
};

#endif // LINSCHEDULEWINDOW_H
