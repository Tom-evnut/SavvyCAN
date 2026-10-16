#include "apgdt001session.h"

int Apgdt001Session::divisorForBaud(int baud)
{
    if (baud <= 0) return 0xFFFF;
    const int divisor = (Apgdt001Parser::BAUD_CLOCK + baud / 2) / baud - 1;
    return qBound(0, divisor, 0xFFFF);
}

int Apgdt001Session::baudForDivisor(int divisor)
{
    return (Apgdt001Parser::BAUD_CLOCK + (divisor + 1) / 2) / (divisor + 1);
}

QByteArray Apgdt001Session::controlBlock(int baud)
{
    //Microchip's library defaults, with the demo's buffer flushing and the bit rate
    QByteArray block(CONTROL_BLOCK_SIZE, '\0');
    block[0] = char(0xC0);
    block[3] = char(FLUSH_BYTES);
    block[4] = char(qRound(FLUSH_MS / 0.409));
    block[8] = char(0x0A);
    block[9] = char(0x33);
    block[10] = char(0x20);
    block[16] = char(0xC8);
    block[17] = char(0x98);
    block[18] = char(0x0F);
    const int divisor = divisorForBaud(baud);
    block[22] = char(divisor & 0xFF);
    block[23] = char(divisor >> 8);
    return block;
}

QByteArray Apgdt001Session::commandReport(const QByteArray &command)
{
    QByteArray report = command.left(Apgdt001Parser::REPORT_SIZE);
    report.append(QByteArray(Apgdt001Parser::REPORT_SIZE - report.size(), '\0'));
    return report;
}

QByteArray Apgdt001Session::controlBlockReport(const QByteArray &block)
{
    QByteArray command(1, char(0x02));
    command.append(block.left(CONTROL_BLOCK_SIZE));
    command.append(QByteArray(1 + CONTROL_BLOCK_SIZE - command.size(), '\0')); //short block: zeros
    command.append('\0');
    return commandReport(command);
}

QByteArray Apgdt001Session::transmitReport(quint8 id, const QByteArray &data, quint8 checksum)
{
    const QByteArray bytes = data.left(CommFrame::LIN_MAX_PAYLOAD);
    const int counted = 1 + int(bytes.size()) + (bytes.isEmpty() ? 0 : 1); //PID, data, checksum
    QByteArray command(1, char(0x03));
    command.append(char(counted + 4));  //84, the count, ..., 1F, 77
    command.append(char(0x84));
    command.append(char(counted));
    command.append(char(CommFrame::linPidFromId(id)));
    command.append(bytes);
    if (!bytes.isEmpty()) command.append(char(checksum));
    command.append(QByteArray::fromHex("1F7700"));
    return commandReport(command);
}

qint64 Apgdt001Session::frameSlotUs(int baud, int dataBytes)
{
    //34-bit header, then 10 bits for each data byte and the checksum; LIN allows 40 % more, and a little space after
    if (baud <= 0) baud = 19200;
    const int bytes = dataBytes > 0 ? qMin(dataBytes, int(CommFrame::LIN_MAX_PAYLOAD)) : CommFrame::LIN_MAX_PAYLOAD;
    return (34 + (bytes + 1) * 10) * 14 * 100000ll / baud + 1000;
}

bool Apgdt001Session::queueTransmit(quint8 id, const QByteArray &data, quint8 checksum)
{
    if (mTransmits.size() >= MAX_QUEUED_TRANSMITS) return false;
    mTransmits.append({transmitReport(id, data, checksum), frameSlotUs(mBaud, int(data.size()))});
    return true;
}

QList<QByteArray> Apgdt001Session::takeDueTransmits(qint64 nowUs)
{
    //one at a time: the slot starts when the frame is actually sent, which may be after it was due
    QList<QByteArray> due;
    if (!mTransmits.isEmpty() && nowUs >= mNextTransmitUs)
    {
        const Transmit t = mTransmits.takeFirst();
        due.append(t.report);
        mNextTransmitUs = nowUs + t.slotUs;
    }
    return due;
}

void Apgdt001Session::armCheck(int baud, qint64 nowUs)
{
    mBaud = baud;
    mExpectedBlock = controlBlock(baud).left(Apgdt001Parser::STATUS_BLOCK_SIZE);
    mCheck = Check::Pending;
    mCheckMessage.clear();
    mCheckResultNew = false;
    mRepliesAtRequest = mParser.statusReplies();
    mRequestUs = nowUs;
}

QList<QByteArray> Apgdt001Session::startCommands(int baud, qint64 nowUs)
{
    armCheck(baud, nowUs);
    return {coldResetReport(), controlBlockReport(controlBlock(baud)), warmResetReport(), statusRequestReport()};
}

QList<QByteArray> Apgdt001Session::changeBaudCommands(int baud, qint64 nowUs)
{
    armCheck(baud, nowUs);
    return {controlBlockReport(controlBlock(baud)), commClearReport(), statusRequestReport()};
}

void Apgdt001Session::finishCheck(Check result, const QString &message)
{
    mCheck = result;
    mCheckMessage = message;
    mCheckResultNew = true;
}

bool Apgdt001Session::takeCheckResult()
{
    const bool isNew = mCheckResultNew;
    mCheckResultNew = false;
    return isNew;
}

qint64 Apgdt001Session::quietUs() const
{
    //a full frame: 34-bit header and 9 response bytes of 10 bits, plus the 40 % the LIN spec allows on top
    const int baud = mBaud > 0 ? mBaud : 19200;
    const qint64 frameUs = (34 + 9 * 10) * 14 * 100000ll / baud;
    return frameUs + FLUSH_MS * 1000 + 5000;
}

int Apgdt001Session::processReport(const char *report, int len, qint64 nowUs, CommFrame::TimeStamp hostTimestamp,
                                   QList<CommFrame> &frames)
{
    const int added = mParser.feed(report, len, hostTimestamp, frames);
    if (mParser.streamBytes() != mStreamBytesSeen)
    {
        mStreamBytesSeen = mParser.streamBytes();
        mLastStreamUs = nowUs;
    }

    if (mCheck == Check::Pending && mParser.statusReplies() != mRepliesAtRequest)
    {
        const QByteArray echoed = mParser.lastStatusBlock();
        int mismatch = -1;
        for (int i = 0; i < mExpectedBlock.size() && mismatch < 0; i++)
            if (i >= echoed.size() || echoed.at(i) != mExpectedBlock.at(i)) mismatch = i;
        if (mismatch < 0)
            finishCheck(Check::Ok, QString("configuration accepted (%1 bit/s)").arg(mBaud));
        else
            finishCheck(Check::Failed,
                        QString("configuration byte %1 reads back as %2, expected %3")
                            .arg(mismatch)
                            .arg(mismatch < echoed.size() ? quint8(echoed.at(mismatch)) : 0, 2, 16, QChar('0'))
                            .arg(quint8(mExpectedBlock.at(mismatch)), 2, 16, QChar('0')));
    }
    return added;
}

int Apgdt001Session::poll(qint64 nowUs, CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames)
{
    if (mCheck == Check::Pending && nowUs - mRequestUs > STATUS_TIMEOUT_US)
        finishCheck(Check::Failed, "no reply to the status request");

    if (mParser.isBuildingFrame() && nowUs - mLastStreamUs > quietUs())
        return mParser.flush(hostTimestamp, frames);
    return 0;
}

void Apgdt001Session::reset()
{
    mParser.reset();
    mCheck = Check::None;
    mCheckMessage.clear();
    mCheckResultNew = false;
    mStreamBytesSeen = mParser.streamBytes();
    mTransmits.clear();
    mNextTransmitUs = 0;
}
