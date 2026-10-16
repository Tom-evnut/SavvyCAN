#ifndef CANBus_H
#define CANBus_H
#include <QDataStream>
#include "can_structs.h"

class CANBus
{
public:
    //what kind of bus this is: set by the driver, not saved
    enum class BusType {
        CAN = 0,     //classic CAN, or CAN FD where isCanFDCapable()
        LIN = 1,
        FlexRay = 2
    };
    //how a LIN interface takes part in the bus
    enum class LinMode {
        Monitor = 0, //listen only
        Master = 1,  //sends headers (and the responses it publishes)
        Slave = 2    //answers headers from its response table
    };
    static constexpr int LIN_DEFAULT_SPEED = 19200;

private:
    int speed;
    bool listenOnly;
    bool singleWire;
    bool active; //is this bus turned on?
    bool canFD;
    int dataRate;
    bool canFDCapable; //the device reported that this bus can do CAN FD (set by the driver, not saved)
    BusType busType;
    LinMode linMode;
    CommFrame::LinChecksumModel linChecksumModel; //for frames this bus sends; Unknown = per ID, as received
    friend QDataStream& operator<<(QDataStream & pStream, const CANBus& pCanBus);
    friend QDataStream& operator>>(QDataStream & pStream, CANBus& pCanBus);
public:
    CANBus();

    bool operator==(const CANBus&) const;

    void setSpeed(int); // new speed
    void setListenOnly(bool); //bool for whether to only listen
    void setSingleWire(bool); //bool for whether to use single wire mode
    void setActive(bool); //whether this bus should be enabled or not.
    void setCanFD(bool); // enable or disable CANFD support
    void setDataRate(int newSpeed);
    void setCanFDCapable(bool);
    int getSpeed() const;
    int getDataRate() const;
    bool isListenOnly() const;
    bool isSingleWire() const;
    bool isActive() const;
    bool isCanFD() const;
    bool isCanFDCapable() const;

    void setBusType(BusType);
    BusType getBusType() const;
    bool isLin() const;
    void setLinMode(LinMode);
    LinMode getLinMode() const;
    void setLinChecksumModel(CommFrame::LinChecksumModel);
    CommFrame::LinChecksumModel getLinChecksumModel() const;
};

QDataStream& operator<<(QDataStream & pStream, const CANBus& pCanBus);
QDataStream& operator>>(QDataStream & pStream, CANBus& pCanBus);

Q_DECLARE_METATYPE(CANBus);

struct BusData {
    CANBus             mBus;
    bool               mConfigured = {};
    QVector<CANFltObserver>    mTargettedFrames;
};

#endif // CANBus_H
