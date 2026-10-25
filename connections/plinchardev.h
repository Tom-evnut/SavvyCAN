#ifndef PLINCHARDEV_H
#define PLINCHARDEV_H

#include <QHash>
#include <QString>
#include "plinbackend.h"

/*
 * PlinBackend on PEAK's Linux LIN driver (peak-lin-driver, from peak-system.com; built and installed by the user). It
 * covers the PLIN-USB and the LIN channels of the PCAN-USB Pro FD and PCAN-USB Pro: each channel is a character device,
 * /dev/plinN, used with read()/write() for frames and ioctl()s for setting it up, rather than PLIN-API.
 *
 * The layouts and request numbers below are the driver's interface (PEAK's plin.h, peak-lin-driver 1.4.1); PEAK's
 * header itself isn't part of SavvyCAN. Differences from PLIN-API that matter here:
 *   - a received frame has the 6-bit frame ID, not the protected ID (parity errors only show in its flags), and no
 *     checksum byte: the driver says which checksum model it found and flags a wrong checksum;
 *   - a frame to send can't carry its own checksum (the driver's "custom" checksum isn't implemented), so a frame
 *     with one is sent as classic or enhanced, whichever its checksum is, and the device works the byte out.
 * Channels are found in sysfs: /sys/class/plin/plinN, under the adapter's entry (e.g. pcan-usb_pro-0), which gives the
 * device and channel numbers.
 *
 * The conversions and the channel list compile everywhere, for the unit tests; using the devices needs Linux.
 */
namespace PlinLinux {

constexpr int DATA_LENGTH = 8;

//a frame read or written (struct plin_msg)
struct Message
{
    quint16 type = Plin::mstStandard;
    quint16 flags = 0;      //received: the frame's error flags (same bits as PLIN-API's)
    quint8 id = 0;          //the 6-bit frame ID
    quint8 len = 0;
    quint8 dir = 0;
    quint8 csType = 0;
    quint64 tsUs = 0;
    quint8 data[DATA_LENGTH] = {};
    quint8 reserved[8] = {};
};
static_assert(sizeof(Message) == 32, "the driver's struct plin_msg is 32 bytes");

#pragma pack(push, 1)
struct InitHw { quint16 baudrate = 0; quint8 mode = 0; quint8 unused = 0; };
struct FrameEntry
{
    quint8 id = 0, len = 0, direction = 0, checksum = 0;
    quint16 flags = 0, unused = 0;
    quint8 d[DATA_LENGTH] = {};
};
struct IdFilter { quint8 mask[8] = {}; };
#pragma pack(pop)
static_assert(sizeof(InitHw) == 4 && sizeof(FrameEntry) == 16 && sizeof(IdFilter) == 8, "the driver's layouts");

PlinRcvMsg toRcvMsg(const Message &m);
//false (with why) if the frame can't be sent through the driver (a checksum that's neither classic nor enhanced)
bool toMessage(const PlinMsg &msg, Message &out, QString *why = nullptr);
IdFilter idFilter(quint64 mask); //bit n: frame ID n

//the channels under a sysfs class directory (normally /sys/class/plin); devRoot is where the device nodes are (/dev)
QList<PlinHardware> channels(const QString &sysClassDir, const QString &devRoot);

}

class PlinChardev : public PlinBackend
{
public:
    PlinChardev(const QString &sysClassDir = "/sys/class/plin", const QString &devRoot = "/dev");
    ~PlinChardev() override;

    bool isLoaded() const override;
    QString loadError() const override;
    QList<PlinHardware> hardware() override;

    quint32 registerClient(const QString &name, quint8 *client) override;
    quint32 removeClient(quint8 client) override;
    quint32 connectClient(quint8 client, quint16 hw) override;
    quint32 disconnectClient(quint8 client, quint16 hw) override;
    quint32 initializeHardware(quint8 client, quint16 hw, quint8 mode, quint16 baudrate) override;
    quint32 setFrameEntry(quint8 client, quint16 hw, quint8 id, quint8 length, quint8 direction,
                          quint8 checksumType) override;
    quint32 setClientFilter(quint8 client, quint16 hw, quint64 mask) override;
    quint32 read(quint8 client, PlinRcvMsg *msg) override;
    quint32 write(quint8 client, quint16 hw, const PlinMsg &msg) override;
    QString errorText(quint32 error) override;

    static PlinChardev &instance();

    //errors from the operating system come back as this plus errno; ERR_CHECKSUM: a checksum the driver can't send
    static constexpr quint32 ERR_ERRNO = 0x10000;
    static constexpr quint32 ERR_NOT_CONNECTED = 0x20001;
    static constexpr quint32 ERR_CHECKSUM = 0x20002;

private:
    quint32 ioctlFor(quint8 client, unsigned long request, void *arg);

    QString mSysClassDir;
    QString mDevRoot;
    QHash<quint8, int> mFds; //client -> open /dev/plinN
    quint8 mNextClient = 1;
};

#endif // PLINCHARDEV_H
