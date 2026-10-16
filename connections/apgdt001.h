#ifndef APGDT001_H
#define APGDT001_H

#include <QElapsedTimer>
#include <QStringList>
#include <QTimer>
#include "canconnection.h"
#include "apgdt001session.h"
#include "linformat.h"

struct hid_device_;

/*
 * LIN capture from a Microchip APGDT001 LIN Serial Analyzer over USB HID (hidapi). One LIN bus (bus 0); the bit rate
 * and mode come from the bus settings (LIN speeds, 19200 by default). In monitor mode it only listens; in master mode
 * frames sent to the bus go out as a header and response, or a header alone (see LinFormat::toLinForSending()).
 * Slave mode isn't supported yet. See Apgdt001Session for the commands sent and Apgdt001Parser for how its reports
 * become frames.
 *
 * The device needs no driver of its own (plain HID). Built only when SavvyCAN is built with hidapi (HAVE_HIDAPI);
 * otherwise the connection reports that it isn't available.
 *
 * The port is the device's USB serial number (its HID path if it has none), or empty for the first one found.
 */
class Apgdt001 : public CANConnection
{
    Q_OBJECT

public:
    Apgdt001(QString port, int busSpeed);
    virtual ~Apgdt001();

    static bool isAvailable(); //built with hidapi
    static QStringList availableDevices(); //serial numbers (or HID paths) of the connected APGDT001s

protected:
    virtual void piStarted();
    virtual void piStop();
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus);
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus);
    virtual void piSuspend(bool pSuspend);
    virtual bool piSendFrame(const CommFrame&);
    virtual bool piReportsSentFrames() const { return true; } //the receive stream has the frames it sends

private slots:
    void readDevice();
    void tryConnect();

private:
    bool openDevice();
    void closeDevice();
    void reportStatus();
    bool sendReports(const QList<QByteArray> &reports);
    bool sendDueTransmits();
    void deliverFrames();
    void reportCheck();
    CommFrame::TimeStamp hostTimestamp() const;

    Apgdt001Session mSession;
    QTimer *mReadTimer = nullptr;
    QTimer *mReconnectTimer = nullptr;
    QElapsedTimer mClock;
    hid_device_ *mDevice = nullptr;
    QList<CommFrame> mFrames;
    QString mLastError;
    LinFormat::ChecksumModels mChecksumModels; //what each ID was seen using, for frames sent
};

#endif // APGDT001_H
