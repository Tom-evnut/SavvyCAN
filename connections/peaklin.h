#ifndef PEAKLIN_H
#define PEAKLIN_H

#include <QStringList>
#include <QTimer>
#include "canconnection.h"
#include "plinmonitor.h"
#include "linformat.h"

/*
 * LIN capture from a PEAK LIN interface (PLIN-USB, or a LIN channel of a PCAN-USB Pro FD or PCAN-USB Pro) through
 * PEAK's PLIN-API: one channel, one LIN bus (bus 0). The bit rate and mode come from the bus settings (LIN speeds,
 * 19200 to start with). In monitor mode the channel only listens; in master mode frames sent to the bus go out as a
 * header and response, or a header alone (see LinFormat::toLinForSending()). Slave mode isn't supported yet. See
 * PlinMonitor for how the channel is set up, frames are read and sent.
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
    virtual bool piReportsSentFrames() const { return true; } //the PLIN driver reports the frames it sends

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
    LinFormat::ChecksumModels mChecksumModels; //what each ID was seen using, for frames sent
};

#endif // PEAKLIN_H
