#ifndef LINDIAGNOSTICSWINDOW_H
#define LINDIAGNOSTICSWINDOW_H

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include "can_structs.h"
#include "bus_protocols/lintp.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

/*
 * RE Tools > LIN Diagnostics: LIN diagnostic messages (master request 0x3C, slave response 0x3D) decoded from the bus
 * and the frame list, and node configuration and UDS requests sent as the LIN master, one at a time or as a scan of
 * NADs or data identifiers. See bus_protocols/lintp.h and help/lindiagnostics.md.
 */
class LinDiagnosticsWindow : public QDialog
{
    Q_OBJECT

public:
    explicit LinDiagnosticsWindow(const QVector<CommFrame> *frames, QWidget *parent = nullptr);
    ~LinDiagnosticsWindow();

public slots:
    void gotTargettedFrame(CommFrame frame); //diagnostic frames from the connections, as they come

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void serviceChanged();
    void send();
    void scanNads();
    void scanDids();
    void stopRequests();
    void decodeFrameList();
    void tick();

private:
    enum Service { ReadById, AssignNad, ConditionalChangeNad, SaveConfiguration, UdsRequest, GoToSleep };

    bool parseHex(const QLineEdit *edit, quint32 maxValue, quint32 &value, const QString &what);
    void startRequests(const QList<LinTp::Requester::Request> &requests, const QString &what);
    void addMessage(const LinTp::Message &message);
    void addNote(const QString &text);
    qint64 nowUs() const { return mClock.nsecsElapsed() / 1000; }
    void listen(bool on);

    const QVector<CommFrame> *mFrames;
    QSpinBox *mBus;
    QLineEdit *mNad;
    QComboBox *mService;
    QSpinBox *mIdentifier;
    QLineEdit *mSupplier;
    QLineEdit *mFunction;
    QLineEdit *mNewNad;
    QLineEdit *mData;
    QPushButton *mSend;
    QLineEdit *mDidFrom;
    QLineEdit *mDidTo;
    QSpinBox *mPollMs;
    QSpinBox *mPolls;
    QPushButton *mStop;
    QTableWidget *mLog;
    QLabel *mStatus;

    LinTp::Decoder mDecoder;
    LinTp::Requester mRequester;
    QTimer mTimer;
    QElapsedTimer mClock;
    bool mListening = false;
    int mAnswered = 0, mUnanswered = 0;
};

#endif // LINDIAGNOSTICSWINDOW_H
