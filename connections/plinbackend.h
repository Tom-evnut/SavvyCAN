#ifndef PLINBACKEND_H
#define PLINBACKEND_H

#include <QList>
#include <QString>

/*
 * The part of PEAK's PLIN-API (PLIN-USB, PCAN-USB Pro FD and PCAN-USB Pro LIN channels) that SavvyCAN uses, behind an
 * interface: PlinDll calls the real API, tests use a fake. The values and layouts below are what the PLIN-API
 * documentation (PLINAPI_enu.pdf, v3.1) specifies; PEAK's own files aren't part of SavvyCAN, as their licence only
 * allows using them with PEAK hardware.
 */
namespace Plin {

//TLINError
constexpr quint32 errOK = 0;
constexpr quint32 errRcvQueueEmpty = 3;

//TLINMsgType
constexpr quint8 mstStandard = 0;
constexpr quint8 mstBusSleep = 1;
constexpr quint8 mstBusWakeUp = 2;
constexpr quint8 mstAutobaudrateTimeOut = 3;
constexpr quint8 mstAutobaudrateReply = 4;
constexpr quint8 mstOverrun = 5;
constexpr quint8 mstQueueOverrun = 6;
constexpr quint8 mstClientQueueOverrun = 7;

//TLINDirection
constexpr quint8 dirDisabled = 0;
constexpr quint8 dirPublisher = 1;
constexpr quint8 dirSubscriber = 2;
constexpr quint8 dirSubscriberAutoLength = 3;

//TLINChecksumType
constexpr quint8 cstCustom = 0;
constexpr quint8 cstClassic = 1;
constexpr quint8 cstEnhanced = 2;
constexpr quint8 cstAuto = 3;

//TLINHardwareMode
constexpr quint8 modNone = 0;
constexpr quint8 modSlave = 1;
constexpr quint8 modMaster = 2;

//TLINMsgErrors
constexpr quint32 MSG_ERR_INCONSISTENT_SYNC = 0x1;
constexpr quint32 MSG_ERR_ID_PARITY_BIT0 = 0x2;
constexpr quint32 MSG_ERR_ID_PARITY_BIT1 = 0x4;
constexpr quint32 MSG_ERR_SLAVE_NOT_RESPONDING = 0x8;
constexpr quint32 MSG_ERR_TIMEOUT = 0x10;
constexpr quint32 MSG_ERR_CHECKSUM = 0x20;
constexpr quint32 MSG_ERR_GND_SHORT = 0x40;
constexpr quint32 MSG_ERR_VBAT_SHORT = 0x80;
constexpr quint32 MSG_ERR_SLOT_DELAY = 0x100;
constexpr quint32 MSG_ERR_OTHER_RESPONSE = 0x200;

//hardware types (hwpType)
constexpr int LIN_HW_TYPE_USB_PRO = 1;
constexpr int LIN_HW_TYPE_USB_PRO_FD = 2;
constexpr int LIN_HW_TYPE_PLIN_USB = 3;

constexpr int MIN_BAUDRATE = 1000;
constexpr int MAX_BAUDRATE = 20000;
constexpr int MAX_FRAME_ID = 63;

}

//one received LIN message (TLINRcvMsg)
struct PlinRcvMsg
{
    quint8 type = Plin::mstStandard;
    quint8 frameId = 0;           //with the parity bits
    quint8 length = 0;
    quint8 direction = 0;
    quint8 checksumType = 0;
    quint8 data[8] = {};
    quint8 checksum = 0;
    bool hasChecksum = true;      //false: PEAK's Linux driver, which gives the model (checksumType) and the
                                  //MSG_ERR_CHECKSUM flag but not the byte
    quint32 errorFlags = 0;
    quint64 timestampUs = 0;      //falling edge of the break
    quint16 hardware = 0;
};

//one LIN message to send as master (TLINMsg)
struct PlinMsg
{
    quint8 frameId = 0;           //the protected ID: the API sends it as given
    quint8 length = 0;            //1-8
    quint8 direction = Plin::dirPublisher;
    quint8 checksumType = Plin::cstCustom;
    quint8 data[8] = {};
    quint8 checksum = 0;          //sent as given with cstCustom
};

//a LIN channel on PEAK hardware
struct PlinHardware
{
    quint16 handle = 0;
    QString name;
    int device = 0;               //device number, as set with PEAK's tools
    int channel = 0;              //1 or 2 on the PCAN-USB Pro (FD)
    int type = 0;
};

class PlinBackend
{
public:
    virtual ~PlinBackend() = default;

    virtual bool isLoaded() const = 0;
    virtual QString loadError() const = 0;
    virtual QList<PlinHardware> hardware() = 0;

    //PLIN-API calls; each returns a TLINError
    virtual quint32 registerClient(const QString &name, quint8 *client) = 0;
    virtual quint32 removeClient(quint8 client) = 0;
    virtual quint32 connectClient(quint8 client, quint16 hw) = 0;
    virtual quint32 disconnectClient(quint8 client, quint16 hw) = 0;
    virtual quint32 initializeHardware(quint8 client, quint16 hw, quint8 mode, quint16 baudrate) = 0;
    virtual quint32 setFrameEntry(quint8 client, quint16 hw, quint8 id, quint8 length, quint8 direction,
                                  quint8 checksumType) = 0;
    virtual quint32 setClientFilter(quint8 client, quint16 hw, quint64 mask) = 0;
    virtual quint32 read(quint8 client, PlinRcvMsg *msg) = 0;
    virtual quint32 write(quint8 client, quint16 hw, const PlinMsg &msg) = 0; //master only: one header (and response)
    virtual QString errorText(quint32 error) = 0;
};

#endif // PLINBACKEND_H
