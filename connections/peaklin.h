#ifndef PEAKLIN_H
#define PEAKLIN_H

#include <QStringList>
#include <QTimer>
#include "canconnection.h"
#include "plinmonitor.h"

/*
 * LIN capture from a PEAK LIN interface (PLIN-USB, or a LIN channel of a PCAN-USB Pro FD or PCAN-USB Pro) through
 * PEAK's PLIN-API: one channel, one LIN bus (bus 0), listening only for now. The bit rate comes from the bus settings
 * (LIN speeds, 19200 to start with). See PlinMonitor for how the channel is set up and frames are read.
 *
 * Needs PEAK's PLIN driver, which installs PLinApi.dll (Windows); without it the connection reports that it isn't
 * available. The port is "<name>, device <n>, channel <m>", as the new-connection dialog lists the channels, or empty
 * for the first one found.
 */
class PeakLin : public CANConnection
{
    Q_OBJECT

public:
    PeakLin(QString port, int busSpeed);
    virtual ~PeakLin();

    static bool isAvailable();             //PLinApi.dll loaded
    static QString unavailableReason();
    static QStringList availableChannels(); //ports of the connected PEAK LIN channels

protected:
    virtual void piStarted();
    virtual void piStop();
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus);
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus);
    virtual void piSuspend(bool pSuspend);
    virtual bool piSendFrame(const CommFrame&);

private slots:
    void readChannel();
    void tryConnect();

private:
    void reportStatus();
    CommFrame::TimeStamp hostTimestamp() const;

    PlinMonitor *mMonitor = nullptr;
    QTimer *mReadTimer = nullptr;
    QTimer *mReconnectTimer = nullptr;
    QList<CommFrame> mFrames;
    QString mLastError;
    bool mSendWarned = false;
};

#endif // PEAKLIN_H
