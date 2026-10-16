#include <QRegularExpression>
#include "plinmonitor.h"

QString PlinMonitor::portName(const PlinHardware &hw)
{
    return QString("%1, device %2, channel %3").arg(hw.name.isEmpty() ? QString("PEAK LIN") : hw.name)
                                               .arg(hw.device).arg(hw.channel);
}

const PlinHardware *PlinMonitor::findHardware(const QList<PlinHardware> &hardware, const QString &port)
{
    if (hardware.isEmpty()) return nullptr;
    if (port.trimmed().isEmpty()) return &hardware.first();
    static const QRegularExpression re("device (\\d+), channel (\\d+)");
    const QRegularExpressionMatch m = re.match(port);
    if (!m.hasMatch()) return nullptr;
    const int device = m.captured(1).toInt();
    const int channel = m.captured(2).toInt();
    for (const PlinHardware &hw : hardware)
        if (hw.device == device && hw.channel == channel) return &hw;
    return nullptr;
}

CommFrame PlinMonitor::toFrame(const PlinRcvMsg &msg)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(msg.frameId & 0x3F);
    frame.setLinReceivedPid(msg.frameId);
    frame.setBus(0);
    frame.setReceived(true);
    frame.setLinDeviceTimestamp(true);

    const int length = qMin(int(msg.length), CommFrame::LIN_MAX_PAYLOAD);
    if (!(msg.errorFlags & Plin::MSG_ERR_SLAVE_NOT_RESPONDING) && length > 0)
    {
        frame.setPayload(QByteArray(reinterpret_cast<const char *>(msg.data), length));
        frame.setLinChecksum(msg.checksum);
        frame.verifyLinChecksum(); //the device's checksum check is done again here, finding the ID's model too
    }

    quint8 errors = 0;
    if (msg.errorFlags & Plin::MSG_ERR_INCONSISTENT_SYNC) errors |= CommFrame::LinErrSync;
    if (msg.errorFlags & (Plin::MSG_ERR_ID_PARITY_BIT0 | Plin::MSG_ERR_ID_PARITY_BIT1)) errors |= CommFrame::LinErrParity;
    if (msg.errorFlags & Plin::MSG_ERR_TIMEOUT) errors |= CommFrame::LinErrIncompleteResponse;
    if (msg.errorFlags & (Plin::MSG_ERR_GND_SHORT | Plin::MSG_ERR_VBAT_SHORT | Plin::MSG_ERR_SLOT_DELAY |
                          Plin::MSG_ERR_OTHER_RESPONSE))
        errors |= CommFrame::LinErrDevice;
    frame.setLinErrors(errors);
    return frame;
}

bool PlinMonitor::fail(quint32 rc, const char *what, QString *error)
{
    if (error) *error = QString("%1: %2").arg(what, mBackend.errorText(rc));
    return false;
}

bool PlinMonitor::configure(int baudrate, QString *error)
{
    quint32 rc = mBackend.initializeHardware(mClient, mHardware.handle, mMaster ? Plin::modMaster : Plin::modSlave,
                                             quint16(baudrate));
    if (rc != Plin::errOK) return fail(rc, "can't initialise the channel", error);
    mBaudrate = baudrate;
    for (int id = 0; id <= Plin::MAX_FRAME_ID; id++)
    {
        rc = mBackend.setFrameEntry(mClient, mHardware.handle, quint8(id), 8, Plin::dirSubscriberAutoLength, Plin::cstAuto);
        if (rc != Plin::errOK) return fail(rc, "can't set up the frame IDs", error);
    }
    rc = mBackend.setClientFilter(mClient, mHardware.handle, ~quint64(0));
    if (rc != Plin::errOK) return fail(rc, "can't set the message filter", error);
    return true;
}

bool PlinMonitor::start(const QString &port, int baudrate, QString *error, bool master)
{
    stop();
    mMaster = master;
    if (!mBackend.isLoaded())
    {
        if (error) *error = mBackend.loadError();
        return false;
    }
    if (baudrate < Plin::MIN_BAUDRATE || baudrate > Plin::MAX_BAUDRATE)
    {
        if (error) *error = QString("%1 bit/s isn't a LIN bit rate (%2 to %3)").arg(baudrate).arg(Plin::MIN_BAUDRATE)
                                .arg(Plin::MAX_BAUDRATE);
        return false;
    }
    const QList<PlinHardware> hardware = mBackend.hardware();
    const PlinHardware *hw = findHardware(hardware, port);
    if (!hw)
    {
        if (error) *error = port.trimmed().isEmpty() ? QString("no PEAK LIN channel found")
                                                     : QString("%1 not found").arg(port);
        return false;
    }
    mHardware = *hw;

    quint32 rc = mBackend.registerClient("SavvyCAN", &mClient);
    if (rc != Plin::errOK) return fail(rc, "can't register with the PLIN driver", error);
    rc = mBackend.connectClient(mClient, mHardware.handle);
    if (rc != Plin::errOK)
    {
        mBackend.removeClient(mClient);
        return fail(rc, "can't connect to the channel", error);
    }
    mRunning = true;
    if (!configure(baudrate, error))
    {
        stop();
        return false;
    }
    mHaveOffset = false;
    return true;
}

bool PlinMonitor::changeBaudrate(int baudrate, QString *error)
{
    if (!mRunning) return false;
    if (baudrate < Plin::MIN_BAUDRATE || baudrate > Plin::MAX_BAUDRATE)
    {
        if (error) *error = QString("%1 bit/s isn't a LIN bit rate").arg(baudrate);
        return false;
    }
    return configure(baudrate, error);
}

bool PlinMonitor::setMaster(bool master, QString *error)
{
    if (!mRunning) return false;
    if (master == mMaster) return true;
    mMaster = master;
    return configure(mBaudrate, error);
}

PlinMsg PlinMonitor::toMessage(const CommFrame &lin)
{
    PlinMsg msg;
    msg.frameId = CommFrame::linPidFromId(quint8(lin.frameId()));
    const QByteArray data = lin.payload().left(CommFrame::LIN_MAX_PAYLOAD);
    if (data.isEmpty())
    {
        msg.direction = Plin::dirSubscriberAutoLength; //the header alone
        msg.length = 8;                                //not used for auto length, but must be 1-8
        msg.checksumType = Plin::cstAuto;
        return msg;
    }
    msg.direction = Plin::dirPublisher;
    msg.length = quint8(data.size());
    msg.checksumType = Plin::cstCustom;
    for (int i = 0; i < data.size(); i++) msg.data[i] = quint8(data.at(i));
    msg.checksum = lin.hasLinChecksum() ? lin.linChecksumByte()
                                        : CommFrame::linChecksum(data, quint8(lin.frameId()), CommFrame::LinChecksumEnhanced);
    return msg;
}

bool PlinMonitor::send(const CommFrame &lin, QString *error)
{
    if (!mRunning || !mMaster)
    {
        if (error) *error = mRunning ? "the channel isn't a master" : "not connected";
        return false;
    }
    const quint32 rc = mBackend.write(mClient, mHardware.handle, toMessage(lin));
    if (rc != Plin::errOK) return fail(rc, "sending failed", error);
    return true;
}

void PlinMonitor::stop()
{
    if (!mRunning) return;
    mBackend.disconnectClient(mClient, mHardware.handle);
    mBackend.removeClient(mClient);
    mRunning = false;
}

void PlinMonitor::updateOffset(qint64 latestDeviceUs, qint64 hostUs)
{
    const qint64 candidate = hostUs - latestDeviceUs;
    if (!mHaveOffset || candidate < mOffsetUs || candidate - mOffsetUs > REANCHOR_US)
        mOffsetUs = candidate;
    mHaveOffset = true;
}

int PlinMonitor::poll(CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames, QString *error, int maxMessages)
{
    if (!mRunning) return 0;
    const qint64 hostUs = hostTimestamp.seconds() * 1000000 + hostTimestamp.microSeconds();
    const bool hostSplit = hostTimestamp.seconds() != 0; //keep the host clock's representation (see connections)
    const int before = frames.size();
    QList<qint64> deviceTimes;

    for (int n = 0; n < maxMessages; n++)
    {
        PlinRcvMsg msg;
        const quint32 rc = mBackend.read(mClient, &msg);
        if (rc == Plin::errRcvQueueEmpty) break;
        if (rc != Plin::errOK)
        {
            fail(rc, "reading failed", error);
            stop();
            return -1;
        }
        switch (msg.type) {
        case Plin::mstStandard:
            frames.append(toFrame(msg));
            deviceTimes.append(qint64(msg.timestampUs));
            mFramesRead++;
            break;
        case Plin::mstOverrun:
        case Plin::mstQueueOverrun:
        case Plin::mstClientQueueOverrun:
            mOverruns++;
            break;
        default:
            mBusEvents++;
            break;
        }
    }

    if (!deviceTimes.isEmpty())
    {
        updateOffset(deviceTimes.last(), hostUs);
        for (int i = 0; i < deviceTimes.size(); i++)
        {
            const qint64 us = deviceTimes.at(i) + mOffsetUs;
            frames[before + i].setTimeStamp(hostSplit ? CommFrame::TimeStamp::fromMicroSeconds(us) : CommFrame::TimeStamp(0, us));
        }
    }
    return int(frames.size()) - before;
}
