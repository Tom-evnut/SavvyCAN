#ifndef CAN_STRUCTS_H
#define CAN_STRUCTS_H

#include <QObject>
#include <QtCore/qmetatype.h>
#include <QtCore/qglobal.h>
#include <QVector>
#include <stdint.h>

using Qt::staticMetaObject;

#include <QCanBusFrame>

//Now instead of inheriting from builtin QT class, we just copy it and modify it to support more cooler stuff bro
//The idea now is to support LIN, CAN, CAN-FD, and FlexRay all in the same class structure here. The payload
//for this traffic is 0 - 254 bytes and the max ID size is 29 bits.
//There is a lot of heavy copying from the QT supplied version, just extended. This should be fine as this code is
//fully open source but do note that this could some affect licensing restrictions if you try to bring this into other projects.



struct CommFrame
{
    class TimeStamp {
    public:
        constexpr TimeStamp(qint64 s = 0, qint64 usec = 0) noexcept
            : secs(s), usecs(usec) {}

        constexpr static TimeStamp fromMicroSeconds(qint64 usec) noexcept
        { return TimeStamp(usec / 1000000, usec % 1000000); }

        constexpr qint64 seconds() const noexcept { return secs; }
        constexpr qint64 microSeconds() const noexcept { return usecs; }

    private:
        qint64 secs;
        qint64 usecs;
    };

    enum FrameType {
        CANDataFrame        = 0x0,
        CANFDDataFrame      = 0x1,
        FLEXRAYDataFrame    = 0x2,
        LINDataFrame        = 0x3,
        ErrorFrame          = 0x4,
        RemoteRequestFrame  = 0x5,
        InvalidFrame        = 0x6
    };
    Q_ENUM(FrameType)

    enum FrameError {
        NoError                     = 0,
        TransmissionTimeoutError    = (1 << 0),
        LostArbitrationError        = (1 << 1),
        ControllerError             = (1 << 2),
        ProtocolViolationError      = (1 << 3),
        TransceiverError            = (1 << 4),
        MissingAcknowledgmentError  = (1 << 5),
        BusOffError                 = (1 << 6),
        BusError                    = (1 << 7),
        ControllerRestartError      = (1 << 8),
        UnknownError                = (1 << 9),
        AnyError                    = 0x1FFFFFFFU
        //only 29 bits usable
    };
    Q_DECLARE_FLAGS(FrameErrors, FrameError)
    Q_ENUM(FrameError)
    Q_FLAG(FrameErrors)

public:
    friend bool operator<(const CommFrame& l, const CommFrame& r)
    {
        qint64 lStamp = l.timeStamp().seconds() * 1000000 + l.timeStamp().microSeconds();
        qint64 rStamp = r.timeStamp().seconds() * 1000000 + r.timeStamp().microSeconds();
        return lStamp < rStamp;
    }

    explicit CommFrame(FrameType type = CANDataFrame) noexcept :
        isExtendedFrame(0x0),
        isFlexibleDataRate(0x0),
        isBitrateSwitch(0x0),
        isErrorStateIndicator(0x0),
        isLocalEcho(0x0),
        reserved0(0x0),
        bus(0),
        timedelta(0),
        frameCount(0)
    {
        Q_UNUSED(reserved0);
        setFrameId(0x0);
        setFrameType(type);
    }


    explicit CommFrame(quint32 identifier, const QByteArray &data) :
        format(data.size() > 8 ? CANFDDataFrame : CANDataFrame), //more than 8 bytes can only be CAN FD
        isExtendedFrame(0x0),
        isFlexibleDataRate(data.size() > 8 ? 0x1 : 0x0),
        isBitrateSwitch(0x0),
        isErrorStateIndicator(0x0),
        isLocalEcho(0x0),
        reserved0(0x0),
        dataBytes(data)
    {
        setFrameId(identifier);
    }

    constexpr FrameType frameType() const noexcept
    {
        switch (format) {
        case 0x0: return CANDataFrame;
        case 0x1: return CANFDDataFrame;
        case 0x2: return FLEXRAYDataFrame;
        case 0x3: return LINDataFrame;
        case 0x4: return ErrorFrame;
        case 0x5: return RemoteRequestFrame;
        case 0x6: return InvalidFrame;
            // no default to trigger warning
        }

        return InvalidFrame;
    }

    /*
     * CAN FD is represented in two places: the frame type (CANFDDataFrame) and the FD flag
     * (hasFlexibleDataRateFormat()). The setters keep them in step, so code can use either:
     *   frameType() == CANFDDataFrame  <=>  hasFlexibleDataRateFormat()
     * Setting the type to CANFDDataFrame sets the FD flag; any other type clears it (and BRS/ESI).
     * Setting the FD flag (directly, or via BRS/ESI) turns a CAN data frame into a CAN FD data frame,
     * and clearing it turns a CAN FD data frame back into a CAN data frame.
     */
    constexpr void setFrameType(FrameType newFormat) noexcept
    {
        switch (newFormat) {
        case CANDataFrame:
            format = 0x0; break;
        case CANFDDataFrame:
            format = 0x1; break;
        case FLEXRAYDataFrame:
            format = 0x2; break;
        case LINDataFrame:
            format = 0x3; break;
        case ErrorFrame:
            format = 0x4; break;
        case RemoteRequestFrame:
            format = 0x5; break;
        case InvalidFrame:
            format = 0x6; break;
        }
        isFlexibleDataRate = (format == 0x1) ? 0x1 : 0x0;
        if (!isFlexibleDataRate) {
            isBitrateSwitch = 0x0;
            isErrorStateIndicator = 0x0;
        }
    }

    //QCanBusFrame has no separate CAN FD type: a FD data frame is a DataFrame with the FD flag set,
    //so call setFlexibleDataRateFormat() (and BRS/ESI) after this to carry the FD information over.
    constexpr void setFrameTypeFromQCan(QCanBusFrame::FrameType newFormat) noexcept
    {
        switch (newFormat) {
        case QCanBusFrame::FrameType::DataFrame:
            setFrameType(CANDataFrame); return;
        case QCanBusFrame::FrameType::ErrorFrame:
            setFrameType(ErrorFrame); return;
        case QCanBusFrame::FrameType::UnknownFrame:
            setFrameType(InvalidFrame); return;
        case QCanBusFrame::FrameType::RemoteRequestFrame:
            setFrameType(RemoteRequestFrame); return;
        case QCanBusFrame::FrameType::InvalidFrame:
            setFrameType(InvalidFrame); return;

        }
    }

    //A frame carrying data: classic CAN, CAN FD or a LIN frame with a response (as opposed to remote, error, a LIN
    //header nobody answered, ...)
    bool isDataFrame() const noexcept
    {
        return format == 0x0 || format == 0x1 || (format == 0x3 && !dataBytes.isEmpty());
    }

    /*
     * LIN frames: the frame ID is the 6-bit LIN frame ID (0-63, never extended) and the payload is the response's data
     * bytes (0-8). An empty payload is a header that got no response. The protected ID (ID + 2 parity bits) is computed
     * from the frame ID; a received protected ID is kept only to flag parity errors.
     */
    static constexpr int LIN_MAX_PAYLOAD = 8;
    static constexpr quint8 LIN_ID_MASTER_REQUEST = 0x3C; //diagnostics, always classic checksum
    static constexpr quint8 LIN_ID_SLAVE_RESPONSE = 0x3D;
    constexpr bool isLin() const noexcept { return format == 0x3; }
    bool isLinHeaderOnly() const noexcept { return format == 0x3 && dataBytes.isEmpty(); }

    enum LinChecksumModel {
        LinChecksumUnknown  = 0,
        LinChecksumClassic  = 1, //data bytes only (LIN 1.x, and diagnostic frames)
        LinChecksumEnhanced = 2  //protected ID + data bytes (LIN 2.x)
    };
    enum LinPublisher {
        LinPublisherUnknown = 0,
        LinPublisherMaster  = 1, //the master sent the response
        LinPublisherSlave   = 2
    };
    //errors the device reports (checksum and parity errors have their own checks below)
    enum LinError : quint8 {
        LinNoError              = 0,
        LinErrNoResponse        = (1 << 0), //a response was expected (e.g. by the device acting as master) but none came
        LinErrIncompleteResponse = (1 << 1), //fewer bytes than expected
        LinErrSync              = (1 << 2), //bad break or sync field
        LinErrFraming           = (1 << 3), //missing stop bit
        LinErrCollision         = (1 << 4), //several slaves answered (event-triggered frames)
        LinErrParity            = (1 << 5), //protected ID parity error, as reported by the device
        LinErrBit               = (1 << 6)  //read back a different bit than sent
    };

    //protected ID for a frame ID: P0 = ID0^ID1^ID2^ID4 (bit 6), P1 = !(ID1^ID3^ID4^ID5) (bit 7)
    static constexpr quint8 linPidFromId(quint8 id) noexcept
    {
        id &= 0x3F;
        const quint8 p0 = ((id >> 0) ^ (id >> 1) ^ (id >> 2) ^ (id >> 4)) & 0x1;
        const quint8 p1 = ~((id >> 1) ^ (id >> 3) ^ (id >> 4) ^ (id >> 5)) & 0x1;
        return quint8(id | (p0 << 6) | (p1 << 7));
    }
    static constexpr bool linPidParityOk(quint8 pid) noexcept { return linPidFromId(pid & 0x3F) == pid; }

    //LIN checksum: inverted 8-bit sum with carry wrap-around, over the data (classic) or the protected ID and the data
    //(enhanced). Diagnostic frames always use the classic checksum, whatever the model asked for.
    static quint8 linChecksum(const QByteArray &data, quint8 id, LinChecksumModel model) noexcept
    {
        unsigned int sum = 0;
        if (model == LinChecksumEnhanced && (id & 0x3F) != LIN_ID_MASTER_REQUEST && (id & 0x3F) != LIN_ID_SLAVE_RESPONSE)
            sum = linPidFromId(id);
        for (char c : data) {
            sum += quint8(c);
            if (sum > 0xFF) sum -= 0xFF;
        }
        return quint8(~sum);
    }

    /*
     * LIN details from the capture device, kept with the frame (none for frames loaded from most logs). Laid out like
     * the FlexRay header details on the FlexRay branches: one packed field with accessors.
     */
    void setLinChecksum(quint8 checksum)
    {
        linInfo = (linInfo & ~(quint64(0xFF) << LIN_CS_SHIFT)) | LIN_HAS_CHECKSUM | (quint64(checksum) << LIN_CS_SHIFT);
    }
    void setLinChecksumModel(LinChecksumModel model)
    {
        linInfo = (linInfo & ~(quint64(0x3) << LIN_MODEL_SHIFT)) | (quint64(model & 0x3) << LIN_MODEL_SHIFT);
    }
    //for a checksum the device checked; verifyLinChecksum() checks it here instead
    void setLinChecksumStatus(bool ok)
    {
        linInfo = (linInfo & ~LIN_CHECKSUM_OK) | LIN_CHECKSUM_CHECKED | (ok ? LIN_CHECKSUM_OK : 0);
    }
    //the protected ID as received, to flag parity errors
    void setLinReceivedPid(quint8 pid)
    {
        linInfo = (linInfo & ~(quint64(0xFF) << LIN_PID_SHIFT)) | LIN_HAS_PID | (quint64(pid) << LIN_PID_SHIFT);
    }
    void setLinPublisher(LinPublisher publisher)
    {
        linInfo = (linInfo & ~(quint64(0x3) << LIN_PUB_SHIFT)) | (quint64(publisher & 0x3) << LIN_PUB_SHIFT);
    }
    void setLinErrors(quint8 errors)
    {
        linInfo = (linInfo & ~(quint64(0xFF) << LIN_ERR_SHIFT)) | (quint64(errors) << LIN_ERR_SHIFT);
    }
    void setLinDeviceTimestamp(bool fromDevice)
    {
        linInfo = fromDevice ? (linInfo | LIN_DEVICE_TIME) : (linInfo & ~LIN_DEVICE_TIME);
    }
    void clearLinInfo() { linInfo = 0; }

    /*
     * Checks the received checksum against the payload. With an unknown model, the model that matches is kept (the
     * classic and enhanced checksums of a response always differ), which tells you the ID's checksum model.
     * Returns false when there's nothing to check (not LIN, no response or no checksum received).
     */
    bool verifyLinChecksum()
    {
        if (!isLin() || dataBytes.isEmpty() || !hasLinChecksum())
            return false;
        const quint8 id = quint8(frameId());
        LinChecksumModel model = linChecksumModel();
        if (model == LinChecksumUnknown) {
            if (linChecksum(dataBytes, id, LinChecksumClassic) == linChecksumByte())
                model = LinChecksumClassic;
            else if (linChecksum(dataBytes, id, LinChecksumEnhanced) == linChecksumByte())
                model = LinChecksumEnhanced;
            if (model != LinChecksumUnknown)
                setLinChecksumModel(model);
            setLinChecksumStatus(model != LinChecksumUnknown);
            return true;
        }
        setLinChecksumStatus(linChecksum(dataBytes, id, model) == linChecksumByte());
        return true;
    }

    constexpr bool hasLinChecksum() const noexcept { return linInfo & LIN_HAS_CHECKSUM; }
    constexpr quint8 linChecksumByte() const noexcept { return quint8((linInfo >> LIN_CS_SHIFT) & 0xFF); }
    constexpr LinChecksumModel linChecksumModel() const noexcept
    {
        return LinChecksumModel((linInfo >> LIN_MODEL_SHIFT) & 0x3);
    }
    constexpr bool linChecksumChecked() const noexcept { return linInfo & LIN_CHECKSUM_CHECKED; }
    constexpr bool linChecksumOk() const noexcept { return linInfo & LIN_CHECKSUM_OK; }
    //checked (by the device or verifyLinChecksum()) and failed
    constexpr bool hasLinChecksumError() const noexcept { return linChecksumChecked() && !linChecksumOk(); }
    constexpr bool hasLinReceivedPid() const noexcept { return linInfo & LIN_HAS_PID; }
    //the protected ID as received, or computed from the frame ID when none was kept
    constexpr quint8 linPid() const noexcept
    {
        return hasLinReceivedPid() ? quint8((linInfo >> LIN_PID_SHIFT) & 0xFF) : linPidFromId(quint8(msgId & 0x3F));
    }
    constexpr bool hasLinParityError() const noexcept
    {
        return (linErrors() & LinErrParity) || (hasLinReceivedPid() && !linPidParityOk(linPid()));
    }
    constexpr LinPublisher linPublisher() const noexcept { return LinPublisher((linInfo >> LIN_PUB_SHIFT) & 0x3); }
    constexpr quint8 linErrors() const noexcept { return quint8((linInfo >> LIN_ERR_SHIFT) & 0xFF); }
    //any error: device-reported, checksum or parity
    constexpr bool hasLinError() const noexcept
    {
        return linErrors() != LinNoError || hasLinChecksumError() || hasLinParityError();
    }
    constexpr bool hasLinDeviceTimestamp() const noexcept { return linInfo & LIN_DEVICE_TIME; }

    constexpr QCanBusFrame::FrameType toQCanBusFrameType() noexcept
    {
        switch (format) {
        case CANDataFrame:
            return QCanBusFrame::FrameType::DataFrame;
        case CANFDDataFrame:
            return QCanBusFrame::FrameType::DataFrame;
        case FLEXRAYDataFrame:
            return QCanBusFrame::FrameType::DataFrame;
        case LINDataFrame:
            return QCanBusFrame::FrameType::DataFrame;
        case ErrorFrame:
            return QCanBusFrame::FrameType::ErrorFrame;
        case RemoteRequestFrame:
            return QCanBusFrame::FrameType::RemoteRequestFrame;
        case InvalidFrame:
            return QCanBusFrame::FrameType::InvalidFrame;
        }
        return QCanBusFrame::FrameType::UnknownFrame;
    }

    constexpr bool hasExtendedFrameFormat() const noexcept { return (isExtendedFrame & 0x1); }
    constexpr void setExtendedFrameFormat(bool isExtended) noexcept
    {
        isExtendedFrame = (isExtended & 0x1);
    }

    constexpr quint32 frameId() const noexcept
    {
        if (Q_UNLIKELY(format == ErrorFrame))
            return 0;
        return (msgId & 0x1FFFFFFFU);
    }

    constexpr void setFrameId(quint32 newFrameId)
    {
        if (Q_LIKELY(newFrameId < 0x20000000U)) {
            isValidFrameId = true;
            msgId = newFrameId;
            setExtendedFrameFormat(isExtendedFrame || (newFrameId & 0x1FFFF800U));
        } else {
            isValidFrameId = false;
            msgId = 0;
        }
    }

    constexpr quint8 getBus() const noexcept {return bus;}

    constexpr void setBus(quint8 busNum) noexcept
    {
        bus = busNum;
    }

    constexpr quint64 getTimeDelta() const noexcept {return timedelta;}

    constexpr void setTimeDelta(quint64 d) noexcept
    {
        timedelta = d;
    }

    constexpr quint32 getFrameCount() const noexcept {return frameCount;}

    constexpr void setFrameCount(quint32 cnt) noexcept
    {
        frameCount = cnt;
    }

    constexpr bool hasFlexibleDataRateFormat() const noexcept { return (isFlexibleDataRate & 0x1); }
    //keeps the frame type in step with the FD flag (see setFrameType())
    constexpr void setFlexibleDataRateFormat(bool isFlexibleData) noexcept
    {
        if (isFlexibleData) {
            if (format != 0x0 && format != 0x1) return; //only data frames can be CAN FD (there are no FD remote frames)
            format = 0x1; //CAN data frame -> CAN FD data frame
            isFlexibleDataRate = 0x1;
        } else {
            isFlexibleDataRate = 0x0;
            if (format == 0x1) format = 0x0; //CAN FD data frame -> CAN data frame
            isBitrateSwitch = 0x0;
            isErrorStateIndicator = 0x0;
        }
    }

    constexpr bool hasBitrateSwitch() const noexcept { return (isBitrateSwitch & 0x1); }
    constexpr void setBitrateSwitch(bool bitrateSwitch) noexcept
    {
        if (bitrateSwitch) setFlexibleDataRateFormat(true); //only CAN FD frames have BRS
        isBitrateSwitch = (bitrateSwitch && isFlexibleDataRate) ? 0x1 : 0x0;
    }

    constexpr bool hasErrorStateIndicator() const noexcept { return (isErrorStateIndicator & 0x1); }
    constexpr void setErrorStateIndicator(bool errorStateIndicator) noexcept
    {
        if (errorStateIndicator) setFlexibleDataRateFormat(true); //only CAN FD frames have ESI
        isErrorStateIndicator = (errorStateIndicator && isFlexibleDataRate) ? 0x1 : 0x0;
    }

    constexpr bool hasLocalEcho() const noexcept { return (isLocalEcho & 0x1); }
    constexpr void setLocalEcho(bool localEcho) noexcept
    {
        isLocalEcho = (localEcho & 0x1);
    }

    constexpr bool isReceived() const noexcept { return (!(isLocalEcho & 0x1)); }
    constexpr void setReceived(bool received) noexcept
    {
        isLocalEcho = !(received & 0x1);
    }

    //more than 8 bytes makes a CAN frame CAN FD; other frame types (LIN, ...) keep their type
    void setPayload(const QByteArray &data)
    {
        dataBytes = data;
        if (data.size() > 8)
            setFlexibleDataRateFormat(true);
    }

    QByteArray payload() const { return dataBytes; }

    constexpr void setTimeStamp(TimeStamp ts) noexcept { stamp = ts; }

    constexpr TimeStamp timeStamp() const noexcept { return stamp; }

    constexpr FrameErrors error() const noexcept
    {
        if (format != ErrorFrame)
            return NoError;

        return FrameErrors(msgId & 0x1FFFFFFFU);
    }

    constexpr void setError(FrameErrors e)
    {
        if (format != ErrorFrame)
            return;
        msgId = (e & AnyError).toInt();
    }

    bool isValid() const noexcept
    {
        if (format == InvalidFrame)
            return false;

        // long id used, but extended flag not set
        if (!isExtendedFrame && (msgId & 0x1FFFF800U))
            return false;

        if (!isValidFrameId)
            return false;

        // maximum permitted payload size in CAN or CAN FD
        const qsizetype length = dataBytes.size();
        if (format == LINDataFrame) // 6-bit frame ID, response data only (none = header without response)
            return !isExtendedFrame && msgId <= 0x3FU && length <= LIN_MAX_PAYLOAD;

        if (isFlexibleDataRate) {
            if (format == RemoteRequestFrame)
                return false;

            return length <= 8 || length == 12 || length == 16 || length == 20
                   || length == 24 || length == 32 || length == 48 || length == 64;
        }

        return length <= 8;
    }

private:
    quint32 msgId:29; // acts as container for error codes too
    quint8 format:3; // max of 8 frame types

    quint8 isExtendedFrame:1;
    quint8 isValidFrameId:1;
    quint8 isFlexibleDataRate:1;
    quint8 isBitrateSwitch:1;
    quint8 isErrorStateIndicator:1;
    quint8 isLocalEcho:1;
    quint8 reserved0:2;

    QByteArray dataBytes;
    TimeStamp stamp;
    int bus;
    uint64_t timedelta;
    uint32_t frameCount; //used in overwrite mode

    //LIN details (see setLinChecksum() and friends), packed: flags in bits 0-4, checksum model 5-6, publisher 7-8,
    //checksum 9-16, received protected ID 17-24, device errors 25-32
    static constexpr quint64 LIN_HAS_CHECKSUM = 1ull << 0;
    static constexpr quint64 LIN_CHECKSUM_CHECKED = 1ull << 1;
    static constexpr quint64 LIN_CHECKSUM_OK = 1ull << 2;
    static constexpr quint64 LIN_DEVICE_TIME = 1ull << 3;
    static constexpr quint64 LIN_HAS_PID = 1ull << 4;
    static constexpr int LIN_MODEL_SHIFT = 5;
    static constexpr int LIN_PUB_SHIFT = 7;
    static constexpr int LIN_CS_SHIFT = 9;
    static constexpr int LIN_PID_SHIFT = 17;
    static constexpr int LIN_ERR_SHIFT = 25;
    quint64 linInfo = 0;
};


class CANFltObserver
{
public:
    quint32 id;
    quint32 mask;
    QObject * observer; //used to target the specific object that setup this filter

    bool operator ==(const CANFltObserver &b) const
    {
        if ( (id == b.id) && (mask == b.mask) && (observer == b.observer) ) return true;

        return false;
    }
};


Q_DECLARE_TYPEINFO(CommFrame, Q_RELOCATABLE_TYPE);
Q_DECLARE_TYPEINFO(CommFrame::FrameError, Q_PRIMITIVE_TYPE);
Q_DECLARE_TYPEINFO(CommFrame::FrameType, Q_PRIMITIVE_TYPE);
Q_DECLARE_TYPEINFO(CommFrame::TimeStamp, Q_PRIMITIVE_TYPE);

Q_DECLARE_OPERATORS_FOR_FLAGS(CommFrame::FrameErrors)
Q_DECLARE_METATYPE(CommFrame::FrameType)
Q_DECLARE_METATYPE(CommFrame::FrameErrors)

#endif // CAN_STRUCTS_H
