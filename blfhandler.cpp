#include "blfhandler.h"
#include <QDebug>
#include <QFile>
#include <QString>
#include <QtEndian>
#include <QDateTime>
#include <QTimeZone>

#define BLF_REMOTE_FLAG 0x80

//BLF channels count from 1 (python-can and Vector's tools write 1 for the first); SavvyCAN's buses from 0
static int busFromChannel(int channel)
{
    return channel > 0 ? channel - 1 : 0;
}

BLFHandler::BLFHandler()
{

}

/*
 Written while peeking at source code here:
https://python-can.readthedocs.io/en/latest/_modules/can/io/blf.html
https://bitbucket.org/tobylorenz/vector_blf/

All the code actually below is freshly written but heavily based upon things seen in those
two source repos.
*/
bool BLFHandler::loadBLF(QString filename, QVector<CommFrame>* frames)
{
    BLF_OBJ_HEADER objHeader;
    QByteArray fileData;
    QByteArray uncompressedData;
    QByteArray junk;
    BLF_OBJECT obj;
    uint32_t pos;
    BLF_CAN_OBJ canObject;
    BLF_CAN_OBJ2 canObject2;

    QFile *inFile = new QFile(filename);

    if (!inFile->open(QIODevice::ReadOnly))
    {
        delete inFile;
        return false;
    }
    inFile->read((char *)&header, sizeof(header));
    if (qFromLittleEndian(header.sig) == 0x47474F4C)
    {
        qDebug() << "Proper BLF file header token";
    }
    else return false;

    while (!inFile->atEnd())
    {
        qDebug() << "Position within file: " << inFile->pos();
        inFile->read((char *)&objHeader.base, sizeof(BLF_OBJ_HEADER_BASE));
        if (qFromLittleEndian(objHeader.base.sig) == 0x4A424F4C)
        {
            int readSize = objHeader.base.objSize - sizeof(BLF_OBJ_HEADER_BASE);
            qDebug() << "Proper object header token. Read Size: " << readSize;
            fileData = inFile->read(readSize);
            junk = inFile->read(readSize % 4); //file is padded so sizes must always end up on even multiple of 4
            //qDebug() << "Fudge bytes in readSize: " << (readSize % 4);

            switch (objHeader.base.objType)
            {
                case BLF_CONTAINER:
                qDebug() << "Object is a container.";
                memcpy(&objHeader.containerObj, fileData.constData(), sizeof(BLF_OBJ_HEADER_CONTAINER));
                fileData.remove(0, sizeof(BLF_OBJ_HEADER_CONTAINER));
                if (objHeader.containerObj.compressionMethod == BLF_CONT_NO_COMPRESSION)
                {
                    qDebug() << "Container is not compressed";
                    uncompressedData += fileData; //after what's left of an object the last container started
                }
                else if (objHeader.containerObj.compressionMethod == BLF_CONT_ZLIB_COMPRESSION)
                {
                    qDebug() << "Compressed container. Unpacking it.";
                    fileData.prepend(objHeader.containerObj.uncompressedSize & 0xFF);
                    fileData.prepend((objHeader.containerObj.uncompressedSize >> 8) & 0xFF);
                    fileData.prepend((objHeader.containerObj.uncompressedSize >> 16) & 0xFF);
                    fileData.prepend((objHeader.containerObj.uncompressedSize >> 24) & 0xFF);
                    uncompressedData += qUncompress(fileData);
                }
                else
                {
                    qDebug() << "Dunno what this is... " << objHeader.containerObj.compressionMethod;
                }
                qDebug() << "Uncompressed size: " << uncompressedData.length();
                qDebug() << "Currently loaded frames at this point: " << frames->count();
                pos = 0;
                //bool foundHeader = false;
                //first skip forward to find a header signature - usually not necessary
                while ( (int)(pos + sizeof(BLF_OBJ_HEADER)) < uncompressedData.length())
                {
                    int32_t *headerSig = (int32_t *)(uncompressedData.constData() + pos);
                    if (*headerSig == 0x4A424F4C) break;
                    pos += 4;
                }
                //then process all the objects
                while ( (int)(pos + sizeof(BLF_OBJ_HEADER)) < uncompressedData.length())
                {
                    memcpy(&obj.header.base, (uncompressedData.constData() + pos), sizeof(BLF_OBJ_HEADER_BASE));
                    memcpy(&obj.header.v1Obj, (uncompressedData.constData() + pos) + sizeof(BLF_OBJ_HEADER_BASE), sizeof(BLF_OBJ_HEADER_V1));
                    if (obj.header.base.objSize < sizeof(BLF_OBJ_HEADER_BASE) + sizeof(BLF_OBJ_HEADER_V1))
                    {
                        qDebug() << "Object smaller than its header, aborting";
                        return false;
                    }
                    //an object can carry on in the next container (python-can splits containers at a fixed size):
                    //keep it for then
                    if ((qint64)pos + obj.header.base.objSize > uncompressedData.length()) break;
                    //if (obj.header.base.objType != 1)
                        //qDebug() << "Pos: " << pos << " Type: " << obj.header.base.objType << "Obj Size: " << obj.header.base.objSize;
                    if (qFromLittleEndian(objHeader.base.sig) == 0x4A424F4C)
                    {
                        fileData = uncompressedData.mid(pos + sizeof(BLF_OBJ_HEADER_BASE) + sizeof(BLF_OBJ_HEADER_V1), obj.header.base.objSize - sizeof(BLF_OBJ_HEADER_BASE) - sizeof(BLF_OBJ_HEADER_V1));
                        if (obj.header.base.objType == BLF_CAN_MSG)
                        {
                            memcpy(&canObject, fileData.constData(), sizeof(BLF_CAN_OBJ));
                            CommFrame frame;
                            frame.setBus(busFromChannel(canObject.channel));
                            frame.setExtendedFrameFormat((canObject.id & 0x80000000ull)?true:false);
                            frame.setFrameId(canObject.id & 0x1FFFFFFFull);
                            frame.setReceived(!(canObject.flags & 0x01)); //bit 0: sent
                            QByteArray bytes(canObject.dlc, 0);

                            if (canObject.flags & BLF_REMOTE_FLAG) {
                                frame.setFrameType(CommFrame::RemoteRequestFrame);
                            } else {
                                frame.setFrameType(CommFrame::CANDataFrame);
                                for (int i = 0; i < canObject.dlc; i++) bytes[i] = canObject.data[i];
                            }
                            frame.setPayload(bytes);
                            //Should we divide by a thousand or a million? Unsure here. It appears some logs are stamped in microseconds and some in milliseconds?
                            frame.setTimeStamp(CommFrame::TimeStamp(0, qRound64(obj.header.v1Obj.uncompSize / 1000.0))); //uncompsize field also used for timestamp oddly enough
                            frames->append(frame);
                        }
                        else if (obj.header.base.objType == BLF_CAN_MSG2)
                        {
                            memcpy(&canObject2, fileData.constData(), sizeof(BLF_CAN_OBJ2));
                            CommFrame frame;
                            frame.setBus(busFromChannel(canObject2.channel));
                            frame.setExtendedFrameFormat((canObject2.id & 0x80000000ull)?true:false);
                            frame.setFrameId(canObject2.id & 0x1FFFFFFFull);
                            frame.setReceived(!(canObject2.flags & 0x01)); //bit 0: sent
                            QByteArray bytes(canObject2.dlc, 0);

                            if (canObject2.flags & BLF_REMOTE_FLAG) {
                                frame.setFrameType(CommFrame::RemoteRequestFrame);
                            } else {
                                frame.setFrameType(CommFrame::CANDataFrame);
                                for (int i = 0; i < canObject2.dlc; i++) bytes[i] = canObject2.data[i];
                            }
                            frame.setPayload(bytes);
                            //Should we divide by a thousand or a million? Unsure here. It appears some logs are stamped in microseconds and some in milliseconds?
                            frame.setTimeStamp(CommFrame::TimeStamp(0, qRound64(obj.header.v1Obj.uncompSize / 1000.0))); //uncompsize field also used for timestamp oddly enough
                            frames->append(frame);
                        }
                        else if (obj.header.base.objType == BLF_CAN_FD_MSG && fileData.length() >= (int)sizeof(BLF_CANFD_OBJ))
                        {
                            BLF_CANFD_OBJ fdObject;
                            memcpy(&fdObject, fileData.constData(), sizeof(BLF_CANFD_OBJ));
                            CommFrame frame;
                            frame.setBus(busFromChannel(fdObject.channel));
                            frame.setExtendedFrameFormat((fdObject.id & 0x80000000ull)?true:false);
                            frame.setFrameId(fdObject.id & 0x1FFFFFFFull);
                            frame.setReceived(!(fdObject.flags & 0x01)); //bit 0: sent
                            int len = qMin((int)fdObject.validDataBytes, 64);
                            if (fdObject.flags & BLF_REMOTE_FLAG) {
                                frame.setFrameType(CommFrame::RemoteRequestFrame);
                                frame.setPayload(QByteArray(qMin((int)fdObject.dlc, 8), 0)); //no data: the DLC is the length
                            } else {
                                frame.setPayload(QByteArray(reinterpret_cast<const char *>(fdObject.data), len));
                                //classic frames on a CAN FD channel are logged with this object type too
                                if (fdObject.fdFlags & 0x01)
                                {
                                    frame.setFlexibleDataRateFormat(true);
                                    frame.setBitrateSwitch(fdObject.fdFlags & 0x02);
                                    frame.setErrorStateIndicator(fdObject.fdFlags & 0x04);
                                }
                            }
                            frame.setTimeStamp(CommFrame::TimeStamp(0, qRound64(obj.header.v1Obj.uncompSize / 1000.0)));
                            frames->append(frame);
                        }
                        else if (obj.header.base.objType == BLF_CAN_FD_MSG64 && fileData.length() >= (int)sizeof(BLF_CANFD64_OBJ))
                        {
                            BLF_CANFD64_OBJ fdObject;
                            memcpy(&fdObject, fileData.constData(), sizeof(BLF_CANFD64_OBJ));
                            CommFrame frame;
                            frame.setBus(busFromChannel(fdObject.channel));
                            frame.setExtendedFrameFormat((fdObject.id & 0x80000000ull)?true:false);
                            frame.setFrameId(fdObject.id & 0x1FFFFFFFull);
                            frame.setReceived(fdObject.dir == 0); //0: received, 1: sent
                            int len = qMin((int)fdObject.validDataBytes, 64);
                            len = qMin(len, (int)(fileData.length() - sizeof(BLF_CANFD64_OBJ))); //data follows the structure
                            if (len < 0) len = 0;
                            if (fdObject.flags & 0x0010) {
                                frame.setFrameType(CommFrame::RemoteRequestFrame);
                                frame.setPayload(QByteArray(qMin((int)fdObject.dlc, 8), 0)); //no data: the DLC is the length
                            } else {
                                frame.setPayload(fileData.mid(sizeof(BLF_CANFD64_OBJ), len));
                                if (fdObject.flags & 0x1000) //EDL; classic frames on a CAN FD channel don't have it
                                {
                                    frame.setFlexibleDataRateFormat(true);
                                    frame.setBitrateSwitch(fdObject.flags & 0x2000);
                                    frame.setErrorStateIndicator(fdObject.flags & 0x4000);
                                }
                            }
                            frame.setTimeStamp(CommFrame::TimeStamp(0, qRound64(obj.header.v1Obj.uncompSize / 1000.0)));
                            frames->append(frame);
                        }
                        else
                        {
                            qDebug() << "Not a can frame! ObjType: " << obj.header.base.objType;
                            if (obj.header.base.objType > 0xFFFF)
                                return false;
                        }
                        pos += obj.header.base.objSize + (obj.header.base.objSize % 4);
                    }
                    else
                    {
                        qDebug() << "Unexpected object header signature, aborting";
                        return false;
                    }
                }
                uncompressedData.remove(0, pos);
                qDebug() << "After removing used data uncompressedData is now this big: " << uncompressedData.length();

                break;
            }
        }
        else return false;
    }
    return true;
}

namespace {

constexpr int BLF_FILE_HEADER_SIZE = 144;
constexpr int BLF_OBJ_HEADER_SIZE = 32;          //base + V1 header
constexpr int BLF_CONTAINER_HEADER_SIZE = 32;    //base + container header
constexpr int BLF_MAX_CONTAINER = 128 * 1024;    //uncompressed bytes per log container, as python-can writes
constexpr quint32 BLF_TIME_ONE_NANS = 2;
constexpr quint8 BLF_DIR_TX = 0x01;
constexpr quint8 BLF_FD_EDL = 0x01, BLF_FD_BRS = 0x02, BLF_FD_ESI = 0x04;
constexpr qint64 UNIX_TIME_MIN_US = 631152000ll * 1000000ll; //1990: older timestamps aren't Unix times

void put8(QByteArray &b, quint8 v) { b.append(char(v)); }
void put16(QByteArray &b, quint16 v) { put8(b, v & 0xFF); put8(b, v >> 8); }
void put32(QByteArray &b, quint32 v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }
void put64(QByteArray &b, quint64 v) { put32(b, quint32(v)); put32(b, quint32(v >> 32)); }

//Windows SYSTEMTIME, all zeros when the time isn't known
void putSystemTime(QByteArray &b, qint64 unixUs)
{
    if (unixUs < UNIX_TIME_MIN_US)
    {
        b.append(QByteArray(16, '\0'));
        return;
    }
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(unixUs / 1000, QTimeZone::utc());
    put16(b, quint16(t.date().year()));
    put16(b, quint16(t.date().month()));
    put16(b, quint16(t.date().dayOfWeek() % 7)); //Sunday = 0
    put16(b, quint16(t.date().day()));
    put16(b, quint16(t.time().hour()));
    put16(b, quint16(t.time().minute()));
    put16(b, quint16(t.time().second()));
    put16(b, quint16(t.time().msec()));
}

int fdDlc(int length)
{
    if (length <= 8) return length;
    static const int sizes[] = {12, 16, 20, 24, 32, 48, 64};
    for (int i = 0; i < 7; i++) if (length <= sizes[i]) return 9 + i;
    return 15;
}

//objects are gathered into zlib-compressed log containers; an object never straddles two containers
class BlfWriter
{
public:
    explicit BlfWriter(QFile &file) : mFile(file) {}

    void add(quint32 type, const QByteArray &data, quint64 timestampNs)
    {
        QByteArray obj;
        obj.append("LOBJ", 4);
        put16(obj, BLF_OBJ_HEADER_SIZE);
        put16(obj, 1); //header version
        put32(obj, quint32(BLF_OBJ_HEADER_SIZE + data.size()));
        put32(obj, type);
        put32(obj, BLF_TIME_ONE_NANS);
        put16(obj, 0); //client index
        put16(obj, 0); //object version
        put64(obj, timestampNs);
        obj.append(data);
        obj.append(QByteArray((BLF_OBJ_HEADER_SIZE + data.size()) % 4, '\0')); //padding, as readers skip it
        if (mBuffer.size() + obj.size() > BLF_MAX_CONTAINER) flush();
        mBuffer.append(obj);
        mObjects++;
    }

    bool flush()
    {
        if (mBuffer.isEmpty()) return true;
        const QByteArray compressed = qCompress(mBuffer).mid(4); //zlib stream, without Qt's length prefix
        QByteArray container;
        container.append("LOBJ", 4);
        put16(container, 16); //base header only
        put16(container, 1);
        put32(container, quint32(BLF_CONTAINER_HEADER_SIZE + compressed.size()));
        put32(container, BLF_CONTAINER);
        put16(container, BLF_CONT_ZLIB_COMPRESSION);
        container.append(QByteArray(6, '\0'));
        put32(container, quint32(mBuffer.size()));
        container.append(QByteArray(4, '\0'));
        container.append(compressed);
        container.append(QByteArray((BLF_CONTAINER_HEADER_SIZE + compressed.size()) % 4, '\0'));
        mUncompressed += BLF_CONTAINER_HEADER_SIZE + quint64(mBuffer.size());
        mBuffer.clear();
        return mFile.write(container) == container.size();
    }

    quint32 objects() const { return mObjects; }
    quint64 uncompressedSize() const { return BLF_FILE_HEADER_SIZE + mUncompressed; }

private:
    QFile &mFile;
    QByteArray mBuffer;
    quint32 mObjects = 0;
    quint64 mUncompressed = 0;
};

}

bool BLFHandler::saveBLF(QString filename, const QVector<CommFrame> *frames)
{
    QFile outFile(filename);
    if (!outFile.open(QIODevice::WriteOnly)) return false;
    outFile.write(QByteArray(BLF_FILE_HEADER_SIZE, '\0')); //written for real once the sizes are known

    //Unix times: the start goes in the file header (to the millisecond) and objects are relative to it. Other
    //timestamps (SavvyCAN's session time) are kept as they are, with no start time.
    qint64 firstUs = frames->isEmpty() ? 0 : frames->first().timeStamp().microSeconds();
    for (const CommFrame &f : *frames) firstUs = qMin(firstUs, f.timeStamp().microSeconds());
    const qint64 baseUs = firstUs >= UNIX_TIME_MIN_US ? (firstUs / 1000) * 1000 : 0;
    qint64 lastUs = baseUs;

    BlfWriter writer(outFile);
    mSkipped = 0;
    for (const CommFrame &frame : *frames)
    {
        const qint64 us = frame.timeStamp().microSeconds();
        lastUs = qMax(lastUs, us);
        const quint64 ns = quint64(qMax(qint64(0), us - baseUs)) * 1000;
        const quint32 id = frame.frameId() | (frame.hasExtendedFrameFormat() ? 0x80000000u : 0);
        const quint8 dir = frame.isReceived() ? 0 : BLF_DIR_TX;
        const QByteArray payload = frame.payload();
        QByteArray data;
        CommFrame::FrameType type = frame.frameType();
        //CAN FD by its flag: a CAN frame given more than 8 bytes is marked FD before its type follows
        if (type == CommFrame::CANDataFrame && (frame.hasFlexibleDataRateFormat() || payload.size() > 8))
            type = CommFrame::CANFDDataFrame;
        switch (type) {
        case CommFrame::CANDataFrame:
        case CommFrame::RemoteRequestFrame:
        {
            //CAN_MESSAGE: channel, flags, dlc, id, data[8]
            const bool remote = frame.frameType() == CommFrame::RemoteRequestFrame;
            put16(data, quint16(frame.getBus() + 1));
            put8(data, dir | (remote ? BLF_REMOTE_FLAG : 0));
            put8(data, quint8(qMin(int(payload.size()), 8)));
            put32(data, id);
            QByteArray bytes = remote ? QByteArray() : payload.left(8);
            bytes.append(QByteArray(8 - bytes.size(), '\0'));
            data.append(bytes);
            writer.add(BLF_CAN_MSG, data, ns);
            break;
        }
        case CommFrame::CANFDDataFrame:
        {
            //CAN_FD_MESSAGE: channel, flags, dlc, id, frame length, bit count, FD flags, valid data bytes, 5 reserved,
            //data[64]
            const int len = qMin(int(payload.size()), 64);
            put16(data, quint16(frame.getBus() + 1));
            put8(data, dir);
            put8(data, quint8(fdDlc(len)));
            put32(data, id);
            put32(data, 0);
            put8(data, 0);
            put8(data, BLF_FD_EDL | (frame.hasBitrateSwitch() ? BLF_FD_BRS : 0) | (frame.hasErrorStateIndicator() ? BLF_FD_ESI : 0));
            put8(data, quint8(len));
            data.append(QByteArray(5, '\0'));
            QByteArray bytes = payload.left(64);
            bytes.append(QByteArray(64 - bytes.size(), '\0'));
            data.append(bytes);
            writer.add(BLF_CAN_FD_MSG, data, ns);
            break;
        }
        default: //error frames, LIN, FlexRay: not written yet
            mSkipped++;
            break;
        }
    }
    if (!writer.flush()) return false;
    if (mSkipped) qDebug() << "BLF: skipped" << mSkipped << "frames BLF can't hold yet";

    //the file header: signature, size, application (python-can's ID: Vector's tools read its files), BLF version
    //2.6.8.1, file and uncompressed sizes, object counts, start and stop times
    QByteArray head;
    head.append("LOGG", 4);
    put32(head, BLF_FILE_HEADER_SIZE);
    put8(head, 5);
    put8(head, 0); put8(head, 0); put8(head, 0);
    put8(head, 2); put8(head, 6); put8(head, 8); put8(head, 1);
    put64(head, quint64(outFile.size()));
    put64(head, writer.uncompressedSize());
    put32(head, writer.objects());
    put32(head, 0);
    putSystemTime(head, baseUs);
    putSystemTime(head, baseUs ? lastUs : 0);
    head.append(QByteArray(BLF_FILE_HEADER_SIZE - head.size(), '\0'));
    outFile.seek(0);
    return outFile.write(head) == head.size();
}
