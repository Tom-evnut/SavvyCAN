#include <QDateTime>
#include <QDebug>

#include "canconmanager.h"
#include "apgdt001.h"

#ifdef HAVE_HIDAPI
#include <hidapi.h>
#endif

namespace {
constexpr int READ_TIMEOUT_MS = 10; //keeps the connection thread responsive while the bus is quiet

#ifdef HAVE_HIDAPI
//Opens the APGDT001 whose serial number (or path) matches, the first one found if wanted is empty, or lists them
//when found is given. Microchip's library matches on the product ID alone; we prefer Microchip's vendor ID but
//accept any, as it does.
hid_device *findDevice(const QString &wanted, QStringList *found, QString *error)
{
    hid_device_info *list = hid_enumerate(0, 0);
    hid_device *result = nullptr;
    QString openError;
    for (int pass = 0; pass < 2 && !result; pass++) //Microchip's vendor ID first, then any
    {
        for (hid_device_info *d = list; d && !result; d = d->next)
        {
            if (d->product_id != Apgdt001Session::USB_PID) continue;
            if ((d->vendor_id == Apgdt001Session::USB_VID) != (pass == 0)) continue;
            const QString serial = d->serial_number ? QString::fromWCharArray(d->serial_number) : QString();
            const QString name = serial.isEmpty() ? QString::fromLocal8Bit(d->path) : serial;
            if (found)
            {
                found->append(name);
                continue;
            }
            if (!wanted.isEmpty() && wanted != name) continue;
            result = hid_open_path(d->path);
            if (!result) openError = QString::fromWCharArray(hid_error(nullptr));
        }
    }
    hid_free_enumeration(list);

    if (!result && error)
    {
        if (!openError.isEmpty())
            *error = QString("found an APGDT001 but can't open it (%1). On Linux, check the udev permissions for "
                             "hidraw devices.").arg(openError);
        else if (wanted.isEmpty()) *error = "no APGDT001 found";
        else *error = QString("APGDT001 %1 not found").arg(wanted);
    }
    return result;
}
#endif
}

Apgdt001::Apgdt001(QString port, int busSpeed) :
    CANConnection(port, "apgdt001", CANCon::APGDT001, 0, busSpeed, false, 0, 1, 16000, true,
                  true /* listen only */, true)
{
    CANBus bus;
    bus.setBusType(CANBus::BusType::LIN);
    bus.setSpeed(Apgdt001Session::isValidBaud(busSpeed) ? busSpeed : CANBus::LIN_DEFAULT_SPEED);
    bus.setActive(true);
    bus.setListenOnly(true);
    setBusConfig(0, bus);
}

Apgdt001::~Apgdt001()
{
    stop();
}

bool Apgdt001::isAvailable()
{
#ifdef HAVE_HIDAPI
    return true;
#else
    return false;
#endif
}

QStringList Apgdt001::availableDevices()
{
    QStringList names;
#ifdef HAVE_HIDAPI
    if (hid_init() == 0) findDevice(QString(), &names, nullptr);
#endif
    return names;
}

void Apgdt001::piStarted()
{
    //created here, in the connection's thread
    mClock.start();
    mReadTimer = new QTimer(this);
    mReadTimer->setInterval(0);
    connect(mReadTimer, &QTimer::timeout, this, &Apgdt001::readDevice);
    mReconnectTimer = new QTimer(this);
    mReconnectTimer->setInterval(1000);
    connect(mReconnectTimer, &QTimer::timeout, this, &Apgdt001::tryConnect);

#ifdef HAVE_HIDAPI
    if (hid_init() != 0)
    {
        emit debugOutput("APGDT001: hidapi failed to start");
        reportStatus();
        return;
    }
    tryConnect();
#else
    emit debugOutput("APGDT001: this SavvyCAN was built without hidapi, so the APGDT001 isn't supported");
    reportStatus();
#endif
}

void Apgdt001::piStop()
{
    if (mReadTimer) mReadTimer->stop();
    if (mReconnectTimer) mReconnectTimer->stop();
    closeDevice();
    delete mReadTimer;
    mReadTimer = nullptr;
    delete mReconnectTimer;
    mReconnectTimer = nullptr;
}

void Apgdt001::tryConnect()
{
    if (mDevice) return;
    if (openDevice())
    {
        if (mReconnectTimer) mReconnectTimer->stop();
        if (mReadTimer) mReadTimer->start();
    }
    else if (mReconnectTimer && !mReconnectTimer->isActive()) mReconnectTimer->start(); //keep looking
    reportStatus();
}

bool Apgdt001::openDevice()
{
#ifdef HAVE_HIDAPI
    QString error;
    mDevice = findDevice(getPort(), nullptr, &error);
    if (!mDevice)
    {
        if (error != mLastError) emit debugOutput("APGDT001: " + error); //once, not on every retry
        mLastError = error;
        return false;
    }
    mLastError.clear();

    mSession.reset();
    CANBus bus;
    getBusConfig(0, bus);
    if (!sendReports(mSession.startCommands(bus.getSpeed(), mClock.nsecsElapsed() / 1000)))
    {
        closeDevice();
        return false;
    }
    emit debugOutput(QString("APGDT001: connected, configuring for %1 bit/s").arg(bus.getSpeed()));
    return true;
#else
    return false;
#endif
}

void Apgdt001::closeDevice()
{
#ifdef HAVE_HIDAPI
    if (mDevice)
    {
        hid_close(mDevice);
        const Apgdt001Parser &p = mSession.parser();
        emit debugOutput(QString("APGDT001: closed. %1 frames (%2 header only, %3 checksum errors, %4 parity errors), "
                                 "%5 status errors, %6 stray bytes, %7 stream bytes skipped")
                             .arg(p.framesParsed()).arg(p.headerOnlyFrames()).arg(p.checksumErrorFrames())
                             .arg(p.parityErrorFrames()).arg(p.statusErrors()).arg(p.strayBytes())
                             .arg(p.bytesSkipped()));
    }
#endif
    mDevice = nullptr;
}

bool Apgdt001::sendReports(const QList<QByteArray> &reports)
{
#ifdef HAVE_HIDAPI
    if (!mDevice) return false;
    for (const QByteArray &report : reports)
    {
        QByteArray out(1, '\0'); //report ID: the device's reports aren't numbered
        out.append(report);
        if (hid_write(mDevice, reinterpret_cast<const unsigned char *>(out.constData()), size_t(out.size())) < 0)
        {
            emit debugOutput(QString("APGDT001: USB write failed (%1)").arg(QString::fromWCharArray(hid_error(mDevice))));
            return false;
        }
    }
    return true;
#else
    Q_UNUSED(reports);
    return false;
#endif
}

CommFrame::TimeStamp Apgdt001::hostTimestamp() const
{
    //on the same clocks as the other connections: the session's elapsed time, or the system time if that's chosen
    return useSystemTime ? CommFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ll)
                         : CommFrame::TimeStamp(0, CANConManager::getInstance()->getElapsedUs());
}

void Apgdt001::readDevice()
{
#ifdef HAVE_HIDAPI
    if (!mDevice) return;

    unsigned char report[Apgdt001Parser::REPORT_SIZE];
    const int n = hid_read_timeout(mDevice, report, sizeof(report), READ_TIMEOUT_MS);
    if (n < 0)
    {
        emit debugOutput(QString("APGDT001: USB read failed (%1), reconnecting")
                             .arg(QString::fromWCharArray(hid_error(mDevice))));
        mReadTimer->stop();
        closeDevice();
        reportStatus();
        mReconnectTimer->start();
        return;
    }

    const qint64 nowUs = mClock.nsecsElapsed() / 1000;
    mFrames.clear();
    if (n > 0) mSession.processReport(reinterpret_cast<const char *>(report), n, nowUs, hostTimestamp(), mFrames);
    mSession.poll(nowUs, hostTimestamp(), mFrames);
    for (const CommFrame &frame : std::as_const(mFrames)) mChecksumModels.learn(frame);
    reportCheck();
    deliverFrames();
#endif
}

void Apgdt001::reportCheck()
{
    if (!mSession.takeCheckResult()) return;
    if (mSession.check() == Apgdt001Session::Check::Ok)
        emit debugOutput("APGDT001: " + mSession.checkMessage());
    else
        emit debugOutput("APGDT001: the device may not have taken the configuration: " + mSession.checkMessage());
}

void Apgdt001::deliverFrames()
{
    if (mFrames.isEmpty() || isCapSuspended()) return;
    CANBus bus;
    if (getBusConfig(0, bus) && !bus.isActive()) return;
    for (const CommFrame &frame : std::as_const(mFrames))
    {
        CommFrame *frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "APGDT001: frame queue full, dropping frames";
            break;
        }
        *frame_p = frame;
        checkTargettedFrame(*frame_p);
        getQueue().queue();
    }
}

void Apgdt001::reportStatus()
{
    CANCon::status newStatus = mDevice ? CANCon::CONNECTED : CANCon::NOT_CONNECTED;
    if (newStatus == getStatus()) return;
    setStatus(newStatus);
    CANConStatus stats;
    stats.conStatus = newStatus;
    stats.numHardwareBuses = 1;
    emit status(stats);
}

void Apgdt001::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool Apgdt001::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void Apgdt001::piSetBusSettings(int pBusIdx, CANBus bus)
{
    if (pBusIdx != 0) return;
    CANBus old;
    getBusConfig(0, old);

    bus.setBusType(CANBus::BusType::LIN);
    if (!Apgdt001Session::isValidBaud(bus.getSpeed()))
    {
        emit debugOutput(QString("APGDT001: %1 bit/s isn't a LIN speed (%2 to %3); keeping %4")
                             .arg(bus.getSpeed()).arg(Apgdt001Session::MIN_BAUD).arg(Apgdt001Session::MAX_BAUD)
                             .arg(old.getSpeed()));
        bus.setSpeed(old.getSpeed());
    }
    if (bus.getLinMode() == CANBus::LinMode::Slave)
    {
        emit debugOutput("APGDT001: slave mode isn't supported yet; staying in " +
                         QString(old.getLinMode() == CANBus::LinMode::Master ? "master" : "monitor") + " mode");
        bus.setLinMode(old.getLinMode());
    }
    if (bus.getLinMode() != old.getLinMode())
        emit debugOutput(bus.getLinMode() == CANBus::LinMode::Master ? "APGDT001: master mode: frames sent to this bus go out"
                                                                     : "APGDT001: monitor mode: listening only");
    bus.setListenOnly(bus.getLinMode() == CANBus::LinMode::Monitor);
    setBusConfig(0, bus);

    if (mDevice && bus.getSpeed() != old.getSpeed())
    {
        emit debugOutput(QString("APGDT001: changing to %1 bit/s").arg(bus.getSpeed()));
        sendReports(mSession.changeBaudCommands(bus.getSpeed(), mClock.nsecsElapsed() / 1000));
    }
}

//master mode: a header and response, or a header alone (see LinFormat::toLinForSending())
bool Apgdt001::piSendFrame(const CommFrame &frame)
{
    CANBus bus;
    getBusConfig(0, bus);
    if (bus.getLinMode() != CANBus::LinMode::Master)
    {
        emit debugOutput("APGDT001: not sent: the bus is in monitor mode (set master mode in the bus settings to send)");
        return false;
    }
    CommFrame lin;
    QString why;
    if (!LinFormat::toLinForSending(frame, mChecksumModels.forSending(frame.frameId(), bus.getLinChecksumModel()), lin, &why))
    {
        emit debugOutput("APGDT001: not sent: " + why);
        return false;
    }
#ifdef HAVE_HIDAPI
    if (!mDevice) return false;
    return sendReports({Apgdt001Session::transmitReport(quint8(lin.frameId()), lin.payload(), lin.linChecksumByte())});
#else
    return false;
#endif
}
