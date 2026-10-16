#include "lintp.h"

namespace LinTp {

namespace {

QByteArray padded(QByteArray frame)
{
    frame = frame.left(8);
    frame.append(QByteArray(8 - frame.size(), char(0xFF)));
    return frame;
}

void put16(QByteArray &b, quint16 v)
{
    b.append(char(v & 0xFF));
    b.append(char(v >> 8));
}

QString hex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

quint16 le16(const QByteArray &b, int at)
{
    return quint16(quint8(b.at(at)) | (quint8(b.at(at + 1)) << 8));
}

}

QList<QByteArray> frames(quint8 nad, const QByteArray &payload)
{
    QList<QByteArray> out;
    const QByteArray message = payload.left(MAX_LENGTH);
    if (message.size() <= 6)
    {
        QByteArray f;
        f.append(char(nad));
        f.append(char(message.size()));
        f.append(message);
        out << padded(f);
        return out;
    }
    QByteArray first;
    first.append(char(nad));
    first.append(char(0x10 | (message.size() >> 8)));
    first.append(char(message.size() & 0xFF));
    first.append(message.left(5));
    out << padded(first);
    int sequence = 1;
    for (int at = 5; at < message.size(); at += 6)
    {
        QByteArray f;
        f.append(char(nad));
        f.append(char(0x20 | (sequence & 0x0F)));
        f.append(message.mid(at, 6));
        out << padded(f);
        sequence++;
    }
    return out;
}

QByteArray goToSleep()
{
    return padded(QByteArray(1, char(NAD_SLEEP)));
}

QByteArray readByIdentifier(quint8 identifier, quint16 supplier, quint16 function)
{
    QByteArray m(1, char(SID_READ_BY_ID));
    m.append(char(identifier));
    put16(m, supplier);
    put16(m, function);
    return m;
}

QByteArray assignNad(quint8 newNad, quint16 supplier, quint16 function)
{
    QByteArray m(1, char(SID_ASSIGN_NAD));
    put16(m, supplier);
    put16(m, function);
    m.append(char(newNad));
    return m;
}

QByteArray conditionalChangeNad(quint8 identifier, quint8 byte, quint8 mask, quint8 invert, quint8 newNad)
{
    QByteArray m(1, char(SID_CONDITIONAL_CHANGE_NAD));
    m.append(char(identifier));
    m.append(char(byte));
    m.append(char(mask));
    m.append(char(invert));
    m.append(char(newNad));
    return m;
}

QByteArray saveConfiguration()
{
    return QByteArray(1, char(SID_SAVE_CONFIGURATION));
}

QByteArray assignFrameIdRange(quint8 startIndex, const QList<quint8> &pids)
{
    QByteArray m(1, char(SID_ASSIGN_FRAME_ID_RANGE));
    m.append(char(startIndex));
    for (int i = 0; i < 4; i++) m.append(char(i < pids.size() ? pids.at(i) : 0xFF));
    return m;
}

QList<Message> Decoder::feed(const CommFrame &frame)
{
    QList<Message> out;
    const quint32 id = frame.frameId() & 0x3F;
    if (!frame.isLin() || (id != MASTER_REQUEST && id != SLAVE_RESPONSE)) return out;
    const QByteArray d = frame.payload();
    if (d.isEmpty()) return out; //a header nobody answered
    const bool request = id == MASTER_REQUEST;
    const QPair<int, bool> key(frame.getBus(), request);

    if (request && quint8(d.at(0)) == NAD_SLEEP)
    {
        Message m;
        m.sleep = true;
        m.bus = frame.getBus();
        m.time = frame.timeStamp();
        mPartial.remove(key);
        out << m;
        return out;
    }
    if (d.size() < 2)
    {
        mErrors++;
        return out;
    }
    const quint8 nad = quint8(d.at(0));
    const quint8 pci = quint8(d.at(1));
    Message base;
    base.request = request;
    base.nad = nad;
    base.bus = frame.getBus();
    base.time = frame.timeStamp();

    switch (pci >> 4)
    {
    case 0x0: //single frame
    {
        const int length = pci & 0x0F;
        if (length < 1 || length > 6 || d.size() < 2 + length)
        {
            mErrors++;
            return out;
        }
        if (mPartial.contains(key))
        {
            mErrors++; //a message left unfinished
            mPartial.remove(key);
        }
        base.payload = d.mid(2, length);
        out << base;
        break;
    }
    case 0x1: //first frame
    {
        if (d.size() < 3)
        {
            mErrors++;
            return out;
        }
        const int length = ((pci & 0x0F) << 8) | quint8(d.at(2));
        if (length < 7)
        {
            mErrors++;
            return out;
        }
        if (mPartial.contains(key)) mErrors++;
        Partial p;
        p.message = base;
        p.length = length;
        p.message.payload = d.mid(3, qMin(5, length));
        mPartial.insert(key, p);
        break;
    }
    case 0x2: //consecutive frame
    {
        auto it = mPartial.find(key);
        if (it == mPartial.end() || (pci & 0x0F) != (it->nextSequence & 0x0F) || it->message.nad != nad)
        {
            mErrors++;
            mPartial.remove(key);
            return out;
        }
        it->message.payload.append(d.mid(2, qMin(6, it->length - int(it->message.payload.size()))));
        it->message.time = frame.timeStamp();
        it->nextSequence++;
        if (it->message.payload.size() >= it->length)
        {
            out << it->message;
            mPartial.erase(it);
        }
        break;
    }
    default:
        mErrors++;
    }
    return out;
}

CommFrame Requester::masterRequest(int bus, const QByteArray &data)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(MASTER_REQUEST);
    frame.setBus(bus);
    frame.setReceived(false);
    frame.setPayload(data.left(8));
    frame.setLinChecksum(CommFrame::linChecksum(frame.payload(), MASTER_REQUEST, CommFrame::LinChecksumClassic));
    frame.setLinChecksumModel(CommFrame::LinChecksumClassic);
    return frame;
}

CommFrame Requester::slaveResponseHeader(int bus)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(SLAVE_RESPONSE);
    frame.setBus(bus);
    frame.setReceived(false);
    return frame;
}

void Requester::start(const QList<Request> &requests, qint64 nowUs)
{
    mQueue = requests;
    mResults.clear();
    mRunning = false;
    next(nowUs);
}

void Requester::next(qint64 nowUs)
{
    mRunning = !mQueue.isEmpty();
    if (!mRunning) return;
    mCurrent = mQueue.takeFirst();
    mFrames = frames(mCurrent.nad, mCurrent.payload);
    mPolls = 0;
    mNextUs = nowUs;
}

QList<CommFrame> Requester::due(qint64 nowUs)
{
    QList<CommFrame> out;
    if (!mRunning || nowUs < mNextUs) return out;
    mNextUs = nowUs + slotUs;
    if (!mFrames.isEmpty())
    {
        out << masterRequest(mCurrent.bus, mFrames.takeFirst());
        return out;
    }
    if (mPolls >= maxPolls) //nobody answered
    {
        Result r;
        r.request = mCurrent;
        mResults << r;
        next(nowUs);
        return due(nowUs);
    }
    mPolls++;
    out << slaveResponseHeader(mCurrent.bus);
    return out;
}

void Requester::received(const Message &message)
{
    if (!mRunning || message.request || !mFrames.isEmpty() || message.bus != mCurrent.bus) return;
    //the slave asked, or any slave for a broadcast or functional request
    if (message.nad != mCurrent.nad && mCurrent.nad != NAD_BROADCAST && mCurrent.nad != NAD_FUNCTIONAL) return;
    const QByteArray &p = message.payload;
    if (p.size() >= 3 && quint8(p.at(0)) == SID_NEGATIVE_RESPONSE && quint8(p.at(2)) == 0x78)
    {
        mPolls = 0; //response pending: keep asking
        return;
    }
    Result r;
    r.request = mCurrent;
    r.answered = true;
    r.response = message;
    mResults << r;
    next(mNextUs);
}

QList<Requester::Result> Requester::takeResults()
{
    const QList<Result> out = mResults;
    mResults.clear();
    return out;
}

QString serviceName(quint8 sid)
{
    switch (sid) {
    case SID_ASSIGN_NAD: return "AssignNAD";
    case SID_ASSIGN_FRAME_ID: return "AssignFrameIdentifier";
    case SID_READ_BY_ID: return "ReadByIdentifier";
    case SID_CONDITIONAL_CHANGE_NAD: return "ConditionalChangeNAD";
    case SID_DATA_DUMP: return "DataDump";
    case 0xB5: return "AssignNAD via SNPD";
    case SID_SAVE_CONFIGURATION: return "SaveConfiguration";
    case SID_ASSIGN_FRAME_ID_RANGE: return "AssignFrameIdentifierRange";
    case 0x10: return "DiagnosticSessionControl";
    case 0x11: return "ECUReset";
    case 0x14: return "ClearDiagnosticInformation";
    case 0x19: return "ReadDTCInformation";
    case 0x22: return "ReadDataByIdentifier";
    case 0x23: return "ReadMemoryByAddress";
    case 0x27: return "SecurityAccess";
    case 0x28: return "CommunicationControl";
    case 0x2E: return "WriteDataByIdentifier";
    case 0x2F: return "InputOutputControlByIdentifier";
    case 0x31: return "RoutineControl";
    case 0x34: return "RequestDownload";
    case 0x35: return "RequestUpload";
    case 0x36: return "TransferData";
    case 0x37: return "RequestTransferExit";
    case 0x3D: return "WriteMemoryByAddress";
    case 0x3E: return "TesterPresent";
    case 0x85: return "ControlDTCSetting";
    default: return QString();
    }
}

QString negativeResponseName(quint8 code)
{
    switch (code) {
    case 0x10: return "general reject";
    case 0x11: return "service not supported";
    case 0x12: return "sub-function not supported";
    case 0x13: return "incorrect message length or invalid format";
    case 0x14: return "response too long";
    case 0x21: return "busy, repeat request";
    case 0x22: return "conditions not correct";
    case 0x24: return "request sequence error";
    case 0x31: return "request out of range";
    case 0x33: return "security access denied";
    case 0x35: return "invalid key";
    case 0x36: return "exceeded number of attempts";
    case 0x37: return "required time delay not expired";
    case 0x70: return "upload/download not accepted";
    case 0x72: return "general programming failure";
    case 0x78: return "response pending";
    case 0x7E: return "sub-function not supported in active session";
    case 0x7F: return "service not supported in active session";
    default: return "code 0x" + QString("%1").arg(code, 2, 16, QChar('0')).toUpper();
    }
}

QString describe(const Message &message)
{
    if (message.sleep) return "Go to sleep";
    if (message.payload.isEmpty()) return QString();
    const QByteArray &p = message.payload;
    const quint8 sid = quint8(p.at(0));
    if (message.request)
    {
        const QString name = serviceName(sid);
        QString text = name.isEmpty() ? QString("Service 0x%1").arg(sid, 2, 16, QChar('0')) : name;
        if (sid == SID_READ_BY_ID && p.size() >= 6)
            text += QString(" %1 (supplier 0x%2, function 0x%3)").arg(quint8(p.at(1)))
                        .arg(le16(p, 2), 4, 16, QChar('0')).arg(le16(p, 4), 4, 16, QChar('0'));
        else if (sid == SID_ASSIGN_NAD && p.size() >= 6)
            text += QString(": new NAD 0x%1 (supplier 0x%2, function 0x%3)").arg(quint8(p.at(5)), 2, 16, QChar('0'))
                        .arg(le16(p, 1), 4, 16, QChar('0')).arg(le16(p, 3), 4, 16, QChar('0'));
        else if (sid == SID_CONDITIONAL_CHANGE_NAD && p.size() >= 6)
            text += QString(": new NAD 0x%1 if identifier %2 byte %3").arg(quint8(p.at(5)), 2, 16, QChar('0'))
                        .arg(quint8(p.at(1))).arg(quint8(p.at(2)));
        else if (p.size() > 1)
            text += ": " + hex(p.mid(1));
        return text;
    }
    if (sid == SID_NEGATIVE_RESPONSE && p.size() >= 3)
    {
        const QString name = serviceName(quint8(p.at(1)));
        return QString("Negative response to %1: %2")
            .arg(name.isEmpty() ? QString("0x%1").arg(quint8(p.at(1)), 2, 16, QChar('0')) : name,
                 negativeResponseName(quint8(p.at(2))));
    }
    const quint8 requestSid = quint8(sid - 0x40);
    const QString name = serviceName(requestSid);
    QString text = QString("Positive response to %1").arg(name.isEmpty() ? QString("0x%1").arg(requestSid, 2, 16, QChar('0')) : name);
    if (requestSid == SID_READ_BY_ID && p.size() >= 6)
        text += QString(": supplier 0x%1, function 0x%2, variant %3").arg(le16(p, 1), 4, 16, QChar('0'))
                    .arg(le16(p, 3), 4, 16, QChar('0')).arg(quint8(p.at(5)));
    else if (p.size() > 1)
        text += ": " + hex(p.mid(1));
    return text;
}

}
