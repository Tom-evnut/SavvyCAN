#include <QObject>
#include <QDebug>
#include "canbus.h"

CANBus::CANBus()
{
    speed       = 500000;
    listenOnly  = false;
    singleWire  = false;
    active      = false;
    canFD       = false;
    dataRate    = 2000000;
    canFDCapable = false;
    busType     = BusType::CAN;
    linMode     = LinMode::Monitor;
    linChecksumModel = CommFrame::LinChecksumUnknown;
}


bool CANBus::operator==(const CANBus& bus) const{
    return  speed == bus.speed &&
            listenOnly == bus.listenOnly &&
            singleWire == bus.singleWire &&
            active == bus.active &&
            canFD == bus.canFD &&
            dataRate == bus.dataRate &&
            canFDCapable == bus.canFDCapable &&
            busType == bus.busType &&
            linMode == bus.linMode &&
            linChecksumModel == bus.linChecksumModel;
}

void CANBus::setSpeed(int newSpeed){
    //qDebug() << "CANBUS SetSpeed = " << newSpeed;
    speed = newSpeed;
}

void CANBus::setListenOnly(bool mode){
    //qDebug() << "CANBUS SetListenOnly = " << mode;
    listenOnly = mode;
}

void CANBus::setSingleWire(bool mode){
    //qDebug() << "CANBUS SetSingleWire = " << mode;
    singleWire = mode;
}

void CANBus::setActive(bool mode){
    //qDebug() << "CANBUS SetEnabled = " << mode;
    active = mode;
}

void CANBus::setCanFD(bool mode){
    //qDebug() << "CANBUS setCanFD = " << mode;
    canFD = mode;
}

int CANBus::getSpeed() const {
    return speed;
}

int CANBus::getDataRate() const {
    return dataRate;
}

void CANBus::setDataRate(int newSpeed){
    //qDebug() << "CANBUS SetSpeed = " << newSpeed;
    dataRate = newSpeed;
}

bool CANBus::isListenOnly() const {
    return listenOnly;
}

bool CANBus::isSingleWire() const {
    return singleWire;
}

bool CANBus::isActive() const {
    return active;
}

bool CANBus::isCanFD() const {
    return canFD;
}

void CANBus::setCanFDCapable(bool capable){
    canFDCapable = capable;
}

bool CANBus::isCanFDCapable() const {
    return canFDCapable;
}

void CANBus::setBusType(BusType type){
    busType = type;
}

CANBus::BusType CANBus::getBusType() const {
    return busType;
}

bool CANBus::isLin() const {
    return busType == BusType::LIN;
}

void CANBus::setLinMode(LinMode mode){
    linMode = mode;
}

CANBus::LinMode CANBus::getLinMode() const {
    return linMode;
}

void CANBus::setLinChecksumModel(CommFrame::LinChecksumModel model){
    linChecksumModel = model;
}

CommFrame::LinChecksumModel CANBus::getLinChecksumModel() const {
    return linChecksumModel;
}


/*
 * Stream format. Version 1 (no marker) was speed, listen only, single wire, active. Version 2 starts with a negative
 * marker, which a version 1 speed never is, so both load: marker -2, then the version 1 fields, CAN FD, data rate,
 * LIN mode and LIN checksum model. The bus type and CAN FD capability come from the driver and aren't streamed.
 */
static constexpr qint32 STREAM_V2_MARKER = -2;

QDataStream& operator<<(QDataStream & pStream, const CANBus& pCanBus)
{
    pStream << STREAM_V2_MARKER;
    pStream << pCanBus.speed;
    pStream << pCanBus.listenOnly;
    pStream << pCanBus.singleWire;
    pStream << pCanBus.active;
    pStream << pCanBus.canFD;
    pStream << pCanBus.dataRate;
    pStream << qint32(pCanBus.linMode);
    pStream << qint32(pCanBus.linChecksumModel);
    return pStream;
}

QDataStream & operator>>(QDataStream & pStream, CANBus& pCanBus)
{
    qint32 first;
    pStream >> first;
    if (first >= 0) //version 1: the speed
    {
        pCanBus.speed = first;
        pStream >> pCanBus.listenOnly;
        pStream >> pCanBus.singleWire;
        pStream >> pCanBus.active;
        return pStream;
    }
    if (first != STREAM_V2_MARKER)
    {
        pStream.setStatus(QDataStream::ReadCorruptData); //a later version: its fields are unknown here
        return pStream;
    }

    qint32 linMode, linChecksumModel;
    pStream >> pCanBus.speed;
    pStream >> pCanBus.listenOnly;
    pStream >> pCanBus.singleWire;
    pStream >> pCanBus.active;
    pStream >> pCanBus.canFD;
    pStream >> pCanBus.dataRate;
    pStream >> linMode;
    pStream >> linChecksumModel;
    if (pStream.status() != QDataStream::Ok) return pStream;
    if (linMode >= 0 && linMode <= qint32(CANBus::LinMode::Slave))
        pCanBus.linMode = CANBus::LinMode(linMode);
    if (linChecksumModel >= 0 && linChecksumModel <= CommFrame::LinChecksumEnhanced)
        pCanBus.linChecksumModel = CommFrame::LinChecksumModel(linChecksumModel);
    return pStream;
}
