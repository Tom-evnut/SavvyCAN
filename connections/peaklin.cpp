#include <QDateTime>
#include <QDebug>

#include "canconmanager.h"
#include "peaklin.h"
#include "plindll.h"
#include "plinchardev.h"

namespace {
constexpr int READ_INTERVAL_MS = 5;  //the driver queues frames; this only sets how often they're collected

//PEAK's LIN driver: PLinApi.dll on Windows, the /dev/plinN character devices on Linux
PlinBackend &backend()
{
#ifdef Q_OS_LINUX
    return PlinChardev::instance();
#else
    return PlinDll::instance();
#endif
}
}

PeakLin::PeakLin(QString port, int busSpeed) :
    CANConnection(port, "plin", CANCon::PEAKLIN, 0, busSpeed, false, 0, 1, 16000, true, true /* listen only */, true)
{
    CANBus bus;
    bus.setBusType(CANBus::BusType::LIN);
    bus.setSpeed(busSpeed >= Plin::MIN_BAUDRATE && busSpeed <= Plin::MAX_BAUDRATE ? busSpeed : CANBus::LIN_DEFAULT_SPEED);
    bus.setActive(true);
    bus.setListenOnly(true);
    setBusConfig(0, bus);
}

PeakLin::~PeakLin()
{
    stop();
}

bool PeakLin::isAvailable()
{
    return backend().isLoaded();
}

QString PeakLin::unavailableReason()
{
    return backend().loadError();
}

QStringList PeakLin::availableChannels()
{
    QStringList ports;
    for (const PlinHardware &hw : backend().hardware()) ports << PlinMonitor::portName(hw);
    return ports;
}

void PeakLin::piStarted()
{
    //created here, in the connection's thread
    mMonitor = new PlinMonitor(backend());
    mReadTimer = new QTimer(this);
    mReadTimer->setInterval(READ_INTERVAL_MS);
    connect(mReadTimer, &QTimer::timeout, this, &PeakLin::readChannel);
    mReconnectTimer = new QTimer(this);
    mReconnectTimer->setInterval(1000);
    connect(mReconnectTimer, &QTimer::timeout, this, &PeakLin::tryConnect);

    if (!isAvailable())
    {
        emit debugOutput("PEAK LIN: " + unavailableReason());
        reportStatus();
        return;
    }
    tryConnect();
}

void PeakLin::piStop()
{
    if (mReadTimer) mReadTimer->stop();
    if (mReconnectTimer) mReconnectTimer->stop();
    if (mMonitor)
    {
        if (mMonitor->isRunning())
            emit debugOutput(QString("PEAK LIN: closed. %1 frames, %2 bus events, %3 overruns")
                                 .arg(mMonitor->framesRead()).arg(mMonitor->busEvents()).arg(mMonitor->overruns()));
        delete mMonitor;
        mMonitor = nullptr;
    }
    delete mReadTimer;
    mReadTimer = nullptr;
    delete mReconnectTimer;
    mReconnectTimer = nullptr;
}

void PeakLin::tryConnect()
{
    if (!mMonitor || mMonitor->isRunning()) return;
    CANBus bus;
    getBusConfig(0, bus);
    QString error;
    if (mMonitor->start(getPort(), bus.getSpeed(), &error, bus.getLinMode() == CANBus::LinMode::Master))
    {
        mLastError.clear();
        emit debugOutput(QString("PEAK LIN: %1 on %2 at %3 bit/s").arg(mMonitor->isMaster() ? "master" : "listening")
                             .arg(PlinMonitor::portName(mMonitor->currentHardware())).arg(bus.getSpeed()));
        if (mReconnectTimer) mReconnectTimer->stop();
        if (mReadTimer) mReadTimer->start();
    }
    else
    {
        if (error != mLastError) emit debugOutput("PEAK LIN: " + error); //once, not on every retry
        mLastError = error;
        if (mReconnectTimer && !mReconnectTimer->isActive()) mReconnectTimer->start(); //keep looking
    }
    reportStatus();
}

CommFrame::TimeStamp PeakLin::hostTimestamp() const
{
    //on the same clocks as the other connections: the session's elapsed time, or the system time if that's chosen
    return useSystemTime ? CommFrame::TimeStamp::fromMicroSeconds(QDateTime::currentMSecsSinceEpoch() * 1000ll)
                         : CommFrame::TimeStamp(0, CANConManager::getInstance()->getElapsedUs());
}

void PeakLin::readChannel()
{
    if (!mMonitor || !mMonitor->isRunning()) return;
    mFrames.clear();
    QString error;
    if (mMonitor->poll(hostTimestamp(), mFrames, &error) < 0)
    {
        emit debugOutput("PEAK LIN: " + error + ", reconnecting");
        mReadTimer->stop();
        reportStatus();
        mReconnectTimer->start();
        return;
    }
    for (const CommFrame &frame : std::as_const(mFrames)) mChecksumModels.learn(frame);
    if (mFrames.isEmpty() || isCapSuspended()) return;
    CANBus bus;
    if (getBusConfig(0, bus) && !bus.isActive()) return;
    for (const CommFrame &frame : std::as_const(mFrames))
    {
        CommFrame *frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "PEAK LIN: frame queue full, dropping frames";
            break;
        }
        *frame_p = frame;
        checkTargettedFrame(*frame_p);
        getQueue().queue();
    }
}

void PeakLin::reportStatus()
{
    CANCon::status newStatus = (mMonitor && mMonitor->isRunning()) ? CANCon::CONNECTED : CANCon::NOT_CONNECTED;
    if (newStatus == getStatus()) return;
    setStatus(newStatus);
    CANConStatus stats;
    stats.conStatus = newStatus;
    stats.numHardwareBuses = 1;
    emit status(stats);
}

void PeakLin::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);
    if (isCapSuspended()) getQueue().flush();
}

bool PeakLin::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void PeakLin::piSetBusSettings(int pBusIdx, CANBus bus)
{
    if (pBusIdx != 0) return;
    CANBus old;
    getBusConfig(0, old);

    bus.setBusType(CANBus::BusType::LIN);
    if (bus.getSpeed() < Plin::MIN_BAUDRATE || bus.getSpeed() > Plin::MAX_BAUDRATE)
    {
        emit debugOutput(QString("PEAK LIN: %1 bit/s isn't a LIN speed (%2 to %3); keeping %4")
                             .arg(bus.getSpeed()).arg(Plin::MIN_BAUDRATE).arg(Plin::MAX_BAUDRATE).arg(old.getSpeed()));
        bus.setSpeed(old.getSpeed());
    }
    if (bus.getLinMode() == CANBus::LinMode::Slave)
    {
        emit debugOutput("PEAK LIN: slave mode isn't supported yet; staying in " +
                         QString(old.getLinMode() == CANBus::LinMode::Master ? "master" : "monitor") + " mode");
        bus.setLinMode(old.getLinMode());
    }
    bus.setListenOnly(bus.getLinMode() == CANBus::LinMode::Monitor);
    setBusConfig(0, bus);

    if (mMonitor && mMonitor->isRunning() && bus.getLinMode() != old.getLinMode())
    {
        QString error;
        const bool master = bus.getLinMode() == CANBus::LinMode::Master;
        if (mMonitor->setMaster(master, &error))
            emit debugOutput(master ? "PEAK LIN: master mode: frames sent to this bus go out"
                                    : "PEAK LIN: monitor mode: listening only");
        else
            emit debugOutput("PEAK LIN: " + error);
    }

    if (mMonitor && mMonitor->isRunning() && bus.getSpeed() != old.getSpeed())
    {
        QString error;
        if (mMonitor->changeBaudrate(bus.getSpeed(), &error))
            emit debugOutput(QString("PEAK LIN: now at %1 bit/s").arg(bus.getSpeed()));
        else
            emit debugOutput("PEAK LIN: " + error);
    }
}

//master mode: a header and response, or a header alone (see LinFormat::toLinForSending())
bool PeakLin::piSendFrame(const CommFrame &frame)
{
    CANBus bus;
    getBusConfig(0, bus);
    if (bus.getLinMode() != CANBus::LinMode::Master)
    {
        emit debugOutput("PEAK LIN: not sent: the bus is in monitor mode (set master mode in the bus settings to send)");
        return false;
    }
    CommFrame lin;
    QString why;
    if (!LinFormat::toLinForSending(frame, mChecksumModels.forSending(frame.frameId(), bus.getLinChecksumModel()), lin, &why))
    {
        emit debugOutput("PEAK LIN: not sent: " + why);
        return false;
    }
    if (!mMonitor || !mMonitor->send(lin, &why))
    {
        emit debugOutput("PEAK LIN: not sent: " + (mMonitor ? why : QString("not connected")));
        return false;
    }
    return true;
}
