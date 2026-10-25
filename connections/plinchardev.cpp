#include "plinchardev.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include "can_structs.h"

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

//the driver's requests (type 'u')
static const unsigned long PLIN_HW_INIT = _IOW('u', 0, PlinLinux::InitHw);
static const unsigned long PLIN_HW_RESET = _IO('u', 1);
static const unsigned long PLIN_SET_FRAME_ENTRY = _IOW('u', 2, PlinLinux::FrameEntry);
static const unsigned long PLIN_SET_ID_FILTER = _IOW('u', 6, PlinLinux::IdFilter);
#endif

PlinRcvMsg PlinLinux::toRcvMsg(const Message &m)
{
    PlinRcvMsg out;
    out.type = quint8(m.type);
    out.frameId = CommFrame::linPidFromId(quint8(m.id & 0x3F)); //parity errors come in the flags
    out.length = m.len;
    out.direction = m.dir;
    out.checksumType = m.csType;
    for (int i = 0; i < DATA_LENGTH; i++) out.data[i] = m.data[i];
    out.hasChecksum = false; //the driver doesn't pass it on
    out.errorFlags = m.flags;
    out.timestampUs = m.tsUs;
    return out;
}

bool PlinLinux::toMessage(const PlinMsg &msg, Message &out, QString *why)
{
    out = Message();
    out.type = Plin::mstStandard;
    out.id = msg.frameId & 0x3F;
    out.len = msg.length;
    out.dir = msg.direction;
    out.csType = msg.checksumType;
    for (int i = 0; i < DATA_LENGTH; i++) out.data[i] = msg.data[i];
    if (msg.checksumType == Plin::cstCustom)
    {
        //the driver can't send a given checksum byte: send the model it was worked out with
        const QByteArray data(reinterpret_cast<const char *>(msg.data), qMin(int(msg.length), DATA_LENGTH));
        if (CommFrame::linChecksum(data, out.id, CommFrame::LinChecksumClassic) == msg.checksum)
            out.csType = Plin::cstClassic;
        else if (CommFrame::linChecksum(data, out.id, CommFrame::LinChecksumEnhanced) == msg.checksum)
            out.csType = Plin::cstEnhanced;
        else
        {
            if (why) *why = "PEAK's Linux LIN driver can only send classic or enhanced checksums, not a wrong one";
            return false;
        }
    }
    return true;
}

PlinLinux::IdFilter PlinLinux::idFilter(quint64 mask)
{
    IdFilter f;
    for (int i = 0; i < 8; i++) f.mask[i] = quint8(mask >> (8 * i));
    return f;
}

QList<PlinHardware> PlinLinux::channels(const QString &sysClassDir, const QString &devRoot)
{
    static const QRegularExpression nodeName("^plin(\\d+)$");
    static const QRegularExpression adapterName("^(.*)-(\\d+)$");
    struct Found { int minor; QString adapter; };
    QList<Found> found;

    QDir sys(sysClassDir);
    if (sys.exists())
    {
        for (const QString &entry : sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System))
        {
            const QRegularExpressionMatch m = nodeName.match(entry);
            if (!m.hasMatch()) continue;
            //the class entry links to the channel's device directory, under its adapter's
            const QString target = QFileInfo(sys.filePath(entry)).canonicalFilePath();
            const QString adapter = target.isEmpty() ? QString() : QFileInfo(QFileInfo(target).path()).fileName();
            found.append({m.captured(1).toInt(), adapter});
        }
    }
    if (found.isEmpty()) //no sysfs: the device nodes alone
    {
        for (const QString &entry : QDir(devRoot).entryList({"plin*"}, QDir::System | QDir::Files | QDir::AllEntries))
        {
            const QRegularExpressionMatch m = nodeName.match(entry);
            if (m.hasMatch()) found.append({m.captured(1).toInt(), QString()});
        }
    }
    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.minor < b.minor; });

    QList<PlinHardware> out;
    QHash<QString, int> channelOfAdapter;
    for (const Found &f : found)
    {
        PlinHardware hw;
        hw.handle = quint16(f.minor);
        const QRegularExpressionMatch a = adapterName.match(f.adapter);
        if (a.hasMatch())
        {
            hw.name = a.captured(1); //e.g. pcan-usb_pro_fd, plin-usb
            hw.device = a.captured(2).toInt();
            hw.channel = ++channelOfAdapter[f.adapter];
            if (hw.name.contains("pro_fd")) hw.type = Plin::LIN_HW_TYPE_USB_PRO_FD;
            else if (hw.name.contains("pro")) hw.type = Plin::LIN_HW_TYPE_USB_PRO;
            else hw.type = Plin::LIN_HW_TYPE_PLIN_USB;
        }
        else
        {
            hw.name = QString("plin%1").arg(f.minor);
            hw.device = 0;
            hw.channel = f.minor + 1;
        }
        out.append(hw);
    }
    return out;
}

PlinChardev::PlinChardev(const QString &sysClassDir, const QString &devRoot)
    : mSysClassDir(sysClassDir), mDevRoot(devRoot)
{
}

PlinChardev::~PlinChardev()
{
#ifdef Q_OS_LINUX
    for (int fd : std::as_const(mFds)) ::close(fd);
#endif
}

PlinChardev &PlinChardev::instance()
{
    static PlinChardev backend;
    return backend;
}

bool PlinChardev::isLoaded() const
{
#ifdef Q_OS_LINUX
    return QDir(mSysClassDir).exists() || !PlinLinux::channels(mSysClassDir, mDevRoot).isEmpty();
#else
    return false;
#endif
}

QString PlinChardev::loadError() const
{
#ifdef Q_OS_LINUX
    if (isLoaded()) return QString();
    return "PEAK's Linux LIN driver isn't loaded (peak-lin-driver from peak-system.com: build and install it, then plug "
           "in the PLIN-USB or PCAN-USB Pro (FD))";
#else
    return "PEAK's Linux LIN driver is for Linux";
#endif
}

QList<PlinHardware> PlinChardev::hardware()
{
    return PlinLinux::channels(mSysClassDir, mDevRoot);
}

quint32 PlinChardev::registerClient(const QString &, quint8 *client)
{
    *client = mNextClient++;
    if (mNextClient == 0) mNextClient = 1;
    return Plin::errOK;
}

quint32 PlinChardev::removeClient(quint8 client)
{
    return disconnectClient(client, 0);
}

quint32 PlinChardev::connectClient(quint8 client, quint16 hw)
{
#ifdef Q_OS_LINUX
    disconnectClient(client, hw);
    const QByteArray path = QString("%1/plin%2").arg(mDevRoot).arg(hw).toLocal8Bit();
    const int fd = ::open(path.constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) return ERR_ERRNO + quint32(errno);
    mFds.insert(client, fd);
    return Plin::errOK;
#else
    Q_UNUSED(client);
    Q_UNUSED(hw);
    return ERR_NOT_CONNECTED;
#endif
}

quint32 PlinChardev::disconnectClient(quint8 client, quint16)
{
#ifdef Q_OS_LINUX
    if (mFds.contains(client)) ::close(mFds.take(client));
#else
    Q_UNUSED(client);
#endif
    return Plin::errOK;
}

quint32 PlinChardev::ioctlFor(quint8 client, unsigned long request, void *arg)
{
#ifdef Q_OS_LINUX
    if (!mFds.contains(client)) return ERR_NOT_CONNECTED;
    if (::ioctl(mFds.value(client), request, arg) < 0) return ERR_ERRNO + quint32(errno);
    return Plin::errOK;
#else
    Q_UNUSED(client);
    Q_UNUSED(request);
    Q_UNUSED(arg);
    return ERR_NOT_CONNECTED;
#endif
}

quint32 PlinChardev::initializeHardware(quint8 client, quint16, quint8 mode, quint16 baudrate)
{
#ifdef Q_OS_LINUX
    //reset first (it clears the frame table, which is set up again after this), then mode and bit rate
    quint32 rc = ioctlFor(client, PLIN_HW_RESET, nullptr);
    if (rc != Plin::errOK) return rc;
    PlinLinux::InitHw init;
    init.baudrate = baudrate; //little-endian, as the host is
    init.mode = mode;
    return ioctlFor(client, PLIN_HW_INIT, &init);
#else
    Q_UNUSED(client);
    Q_UNUSED(mode);
    Q_UNUSED(baudrate);
    return ERR_NOT_CONNECTED;
#endif
}

quint32 PlinChardev::setFrameEntry(quint8 client, quint16, quint8 id, quint8 length, quint8 direction,
                                   quint8 checksumType)
{
#ifdef Q_OS_LINUX
    PlinLinux::FrameEntry entry;
    entry.id = id;
    entry.len = length;
    entry.direction = direction;
    entry.checksum = checksumType;
    return ioctlFor(client, PLIN_SET_FRAME_ENTRY, &entry);
#else
    Q_UNUSED(client);
    Q_UNUSED(id);
    Q_UNUSED(length);
    Q_UNUSED(direction);
    Q_UNUSED(checksumType);
    return ERR_NOT_CONNECTED;
#endif
}

quint32 PlinChardev::setClientFilter(quint8 client, quint16, quint64 mask)
{
#ifdef Q_OS_LINUX
    PlinLinux::IdFilter filter = PlinLinux::idFilter(mask);
    return ioctlFor(client, PLIN_SET_ID_FILTER, &filter);
#else
    Q_UNUSED(client);
    Q_UNUSED(mask);
    return ERR_NOT_CONNECTED;
#endif
}

quint32 PlinChardev::read(quint8 client, PlinRcvMsg *msg)
{
#ifdef Q_OS_LINUX
    if (!mFds.contains(client)) return ERR_NOT_CONNECTED;
    PlinLinux::Message m;
    const ssize_t n = ::read(mFds.value(client), &m, sizeof(m));
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? Plin::errRcvQueueEmpty : ERR_ERRNO + quint32(errno);
    if (n != ssize_t(sizeof(m))) return Plin::errRcvQueueEmpty;
    *msg = PlinLinux::toRcvMsg(m);
    return Plin::errOK;
#else
    Q_UNUSED(client);
    Q_UNUSED(msg);
    return ERR_NOT_CONNECTED;
#endif
}

quint32 PlinChardev::write(quint8 client, quint16, const PlinMsg &msg)
{
#ifdef Q_OS_LINUX
    if (!mFds.contains(client)) return ERR_NOT_CONNECTED;
    PlinLinux::Message m;
    if (!PlinLinux::toMessage(msg, m)) return ERR_CHECKSUM;
    if (::write(mFds.value(client), &m, sizeof(m)) < 0) return ERR_ERRNO + quint32(errno);
    return Plin::errOK;
#else
    Q_UNUSED(client);
    Q_UNUSED(msg);
    return ERR_NOT_CONNECTED;
#endif
}

QString PlinChardev::errorText(quint32 error)
{
    if (error == Plin::errOK) return "no error";
    if (error == ERR_NOT_CONNECTED) return "not connected to the channel";
    if (error == ERR_CHECKSUM) return "PEAK's Linux LIN driver can only send classic or enhanced checksums";
    if (error == Plin::errRcvQueueEmpty) return "nothing received";
    if (error >= ERR_ERRNO && error < ERR_ERRNO + 0x10000)
    {
#ifdef Q_OS_LINUX
        const int e = int(error - ERR_ERRNO);
        QString text = QString::fromLocal8Bit(strerror(e));
        if (e == EACCES || e == EPERM)
            text += " (install the driver's udev rule, 45-plin.rules, so /dev/plinN can be opened without root)";
        return text;
#endif
    }
    return QString("error %1").arg(error);
}
