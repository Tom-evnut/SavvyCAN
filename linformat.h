#ifndef LINFORMAT_H
#define LINFORMAT_H

#include <QByteArray>
#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include "can_structs.h"

/*
 * How LIN frames are shown and saved: the frame list's type column, the native CSV flags column and the LIN part of
 * the Frame Info window. In one place so the three agree and can be tested on their own.
 */
namespace LinFormat {

//device-reported LIN errors and their names in the native CSV flags column
struct ErrorName { quint8 flag; const char *name; };
inline const QList<ErrorName> &errorNames()
{
    static const QList<ErrorName> names = {
        {CommFrame::LinErrNoResponse, "NORESP"},
        {CommFrame::LinErrIncompleteResponse, "INCOMPLETE"},
        {CommFrame::LinErrSync, "SYNC"},
        {CommFrame::LinErrFraming, "FRAMING"},
        {CommFrame::LinErrCollision, "COLLISION"},
        {CommFrame::LinErrParity, "PARITY"},
        {CommFrame::LinErrBit, "BIT"},
        {CommFrame::LinErrDevice, "DEVERR"},
    };
    return names;
}

//frame list type column: "L", "LN" for a header nobody answered, "!" added for any error (checksum, parity, device)
inline QString typeLabel(const CommFrame &frame)
{
    QString label("L");
    if (frame.isLinHeaderOnly()) label.append('N');
    if (frame.hasLinError()) label.append('!');
    return label;
}

/*
 * Native CSV flags column for a LIN frame: "LIN", then
 *   +CS=xx                 the checksum received (hex)
 *   +CLASSIC / +ENHANCED   its checksum model, when known
 *   +PID=xx                the protected ID received, when its parity is wrong (otherwise it follows from the ID)
 *   +NORESP +INCOMPLETE +SYNC +FRAMING +COLLISION +PARITY +BIT +DEVERR   errors the device reported
 * Whether the checksum is right isn't saved: loading checks it again.
 */
inline QByteArray csvFlags(const CommFrame &frame)
{
    QByteArray flags("LIN");
    if (frame.hasLinChecksum())
        flags.append("+CS=" + QByteArray::number(frame.linChecksumByte(), 16).toUpper().rightJustified(2, '0'));
    if (frame.linChecksumModel() == CommFrame::LinChecksumClassic) flags.append("+CLASSIC");
    if (frame.linChecksumModel() == CommFrame::LinChecksumEnhanced) flags.append("+ENHANCED");
    if (frame.hasLinReceivedPid() && !CommFrame::linPidParityOk(frame.linPid()))
        flags.append("+PID=" + QByteArray::number(frame.linPid(), 16).toUpper().rightJustified(2, '0'));
    for (const ErrorName &e : errorNames())
        if (frame.linErrors() & e.flag) flags.append(QByteArray("+") + e.name);
    return flags;
}

inline bool isCsvFlags(const QByteArray &flags)
{
    const QByteArray f = flags.trimmed().toUpper();
    return f == "LIN" || f.startsWith("LIN+");
}

//makes frame a LIN frame with the details in flags (see csvFlags()); the payload must already be set
inline void applyCsvFlags(CommFrame &frame, const QByteArray &flags)
{
    frame.setFrameType(CommFrame::LINDataFrame);
    frame.setExtendedFrameFormat(false);
    frame.clearLinInfo();
    quint8 errors = 0;
    const QList<QByteArray> parts = flags.trimmed().toUpper().split('+');
    for (int i = 1; i < parts.size(); i++)
    {
        const QByteArray &p = parts.at(i);
        bool ok = false;
        if (p.startsWith("CS="))
        {
            const uint cs = p.mid(3).toUInt(&ok, 16);
            if (ok && cs <= 0xFF) frame.setLinChecksum(quint8(cs));
        }
        else if (p.startsWith("PID="))
        {
            const uint pid = p.mid(4).toUInt(&ok, 16);
            if (ok && pid <= 0xFF) frame.setLinReceivedPid(quint8(pid));
        }
        else if (p == "CLASSIC") frame.setLinChecksumModel(CommFrame::LinChecksumClassic);
        else if (p == "ENHANCED") frame.setLinChecksumModel(CommFrame::LinChecksumEnhanced);
        else
        {
            for (const ErrorName &e : errorNames())
                if (p == e.name) errors |= e.flag;
        }
    }
    frame.setLinErrors(errors);
    frame.verifyLinChecksum();
}

/*
 * Turning frames loaded as CAN into LIN frames, for LIN captures from files that can't say they're LIN (CSV without
 * LIN flags, other tools' logs): a classic CAN data frame with a standard ID of 0-63 and at most 8 bytes on the
 * chosen bus (-1: any bus) becomes a LIN frame. With lastByteIsChecksum, up to 9 bytes, and the last one is taken
 * as the checksum received and checked (finding the ID's checksum model); otherwise all bytes are data and there's
 * no checksum.
 */
struct Conversion
{
    int converted = 0;
    int notLin = 0; //on the bus but can't be LIN: extended ID, ID over 63, CAN FD, more than 8 bytes
};

//with the checksum kept, a full LIN frame is 9 bytes, which loaders take for CAN FD (more than 8 bytes, no BRS)
inline bool canBeLin(const CommFrame &frame, bool lastByteIsChecksum)
{
    const int size = int(frame.payload().size());
    const bool fdOnlyByLength = frame.frameType() == CommFrame::CANFDDataFrame && !frame.hasBitrateSwitch()
                                && lastByteIsChecksum && size == CommFrame::LIN_MAX_PAYLOAD + 1;
    return (frame.frameType() == CommFrame::CANDataFrame || fdOnlyByLength) && !frame.hasExtendedFrameFormat()
           && frame.frameId() <= 0x3F && size <= CommFrame::LIN_MAX_PAYLOAD + (lastByteIsChecksum ? 1 : 0);
}

inline Conversion convertToLin(QVector<CommFrame> &frames, int bus, bool lastByteIsChecksum)
{
    Conversion result;
    for (CommFrame &frame : frames)
    {
        if (bus >= 0 && frame.getBus() != bus) continue;
        if (frame.isLin()) continue;
        if (!canBeLin(frame, lastByteIsChecksum))
        {
            if (frame.isDataFrame()) result.notLin++;
            continue;
        }
        const QByteArray bytes = frame.payload();
        frame.setFrameType(CommFrame::LINDataFrame);
        frame.clearLinInfo();
        if (lastByteIsChecksum && bytes.size() >= 2)
        {
            frame.setPayload(bytes.left(bytes.size() - 1));
            frame.setLinChecksum(quint8(bytes.back()));
            frame.verifyLinChecksum();
        }
        result.converted++;
    }
    return result;
}

/*
 * Sending on a LIN bus. The Send Frames window, scripts and playback send CAN frames; on a LIN bus they become LIN
 * frames: a data frame with a standard ID of 0-63 and up to 8 bytes is a header and that response, a remote frame
 * (CAN's "please send this") or a frame with no data is a header alone, for a slave to answer. The checksum is
 * calculated with the model given.
 */
inline bool toLinForSending(const CommFrame &in, CommFrame::LinChecksumModel model, CommFrame &out, QString *why = nullptr)
{
    const bool remote = in.frameType() == CommFrame::RemoteRequestFrame;
    if (!(in.isLin() || in.frameType() == CommFrame::CANDataFrame || remote) || in.hasFlexibleDataRateFormat())
    {
        if (why) *why = "only classic CAN data and remote frames (or LIN frames) can be sent on a LIN bus";
        return false;
    }
    if (in.hasExtendedFrameFormat() || in.frameId() > 0x3F)
    {
        if (why) *why = QString("ID 0x%1 isn't a LIN ID (0 to 0x3F)").arg(in.frameId(), 0, 16);
        return false;
    }
    const QByteArray data = remote ? QByteArray() : in.payload();
    if (data.size() > CommFrame::LIN_MAX_PAYLOAD)
    {
        if (why) *why = QString("%1 bytes: a LIN response has at most 8").arg(data.size());
        return false;
    }
    out = CommFrame(CommFrame::LINDataFrame);
    out.setFrameId(in.frameId());
    out.setBus(in.getBus());
    out.setReceived(false);
    out.setTimeStamp(in.timeStamp());
    out.setPayload(data);
    if (!data.isEmpty())
    {
        const quint8 id = quint8(in.frameId());
        const CommFrame::LinChecksumModel idModel =
            id == CommFrame::LIN_ID_MASTER_REQUEST || id == CommFrame::LIN_ID_SLAVE_RESPONSE ? CommFrame::LinChecksumClassic
                                                                                            : model;
        //a LIN frame that already has a checksum (a LIN log played back, a schedule's frame) keeps it, and its model
        if (in.isLin() && in.hasLinChecksum())
        {
            out.setLinChecksum(in.linChecksumByte());
            out.setLinChecksumModel(in.linChecksumModel() != CommFrame::LinChecksumUnknown ? in.linChecksumModel() : idModel);
        }
        else
        {
            out.setLinChecksum(CommFrame::linChecksum(data, id, model));
            out.setLinChecksumModel(idModel);
        }
    }
    return true;
}

//The checksum model to send each ID with: the bus's, if it has one; otherwise the one the ID was last seen using
//on the bus; enhanced (LIN 2.x) for IDs not seen yet.
class ChecksumModels
{
public:
    void learn(const CommFrame &frame)
    {
        if (frame.isLin() && frame.linChecksumOk() && frame.linChecksumModel() != CommFrame::LinChecksumUnknown)
            mModels[frame.frameId() & 0x3F] = frame.linChecksumModel();
    }
    CommFrame::LinChecksumModel forSending(quint32 id, CommFrame::LinChecksumModel busModel) const
    {
        if (busModel != CommFrame::LinChecksumUnknown) return busModel;
        const CommFrame::LinChecksumModel seen = mModels[id & 0x3F];
        return seen != CommFrame::LinChecksumUnknown ? seen : CommFrame::LinChecksumEnhanced;
    }

private:
    CommFrame::LinChecksumModel mModels[64] = {};
};

//the LIN lines of the Frame Info window for the frames of one ID (frames that aren't LIN are ignored); empty if
//there are no LIN frames
inline QStringList summary(const QVector<CommFrame> &frames)
{
    auto tr = [](const char *text) { return QCoreApplication::translate("LinFormat", text); };
    int lin = 0, headerOnly = 0, classic = 0, enhanced = 0, checksumErrors = 0, parityErrors = 0;
    quint32 id = 0;
    QList<int> errorCounts(errorNames().size(), 0);
    for (const CommFrame &f : frames)
    {
        if (!f.isLin()) continue;
        id = f.frameId();
        lin++;
        if (f.isLinHeaderOnly()) headerOnly++;
        if (f.hasLinChecksumError()) checksumErrors++;
        else if (f.linChecksumOk() && f.linChecksumModel() == CommFrame::LinChecksumClassic) classic++;
        else if (f.linChecksumOk() && f.linChecksumModel() == CommFrame::LinChecksumEnhanced) enhanced++;
        if (f.hasLinParityError()) parityErrors++;
        for (int i = 0; i < errorNames().size(); i++)
            if (f.linErrors() & errorNames().at(i).flag) errorCounts[i]++;
    }
    QStringList lines;
    if (lin == 0) return lines;

    lines << tr("Protected ID: 0x%1").arg(QString::number(CommFrame::linPidFromId(quint8(id)), 16).toUpper().rightJustified(2, '0'));
    lines << tr("Responses: %1, headers with no response: %2").arg(lin - headerOnly).arg(headerOnly);
    if (classic && enhanced)
        lines << tr("Checksum model: both seen (classic %1, enhanced %2)").arg(classic).arg(enhanced);
    else if (classic)
        lines << tr("Checksum model: classic (LIN 1.x or diagnostic)");
    else if (enhanced)
        lines << tr("Checksum model: enhanced (LIN 2.x)");
    else
        lines << tr("Checksum model: unknown (no valid checksum seen)");
    lines << tr("Checksum errors: %1").arg(checksumErrors);
    lines << tr("Parity errors: %1").arg(parityErrors);
    QStringList others;
    for (int i = 0; i < errorNames().size(); i++)
        if (errorCounts.at(i) && errorNames().at(i).flag != CommFrame::LinErrParity)
            others << QString("%1 %2").arg(QString::fromLatin1(errorNames().at(i).name).toLower()).arg(errorCounts.at(i));
    if (!others.isEmpty()) lines << tr("Device errors: ") + others.join(", ");
    return lines;
}

}

#endif // LINFORMAT_H
