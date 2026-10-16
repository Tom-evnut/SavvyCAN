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
