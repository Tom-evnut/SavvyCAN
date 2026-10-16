#include "apgdt001parser.h"

void Apgdt001Parser::reset()
{
    mStream.clear();
    mBuilding = false;
    mFrameBytes.clear();
    mRollovers = 0;
    mHaveMarker = false;
    mHaveOffset = false;
    mMeasuredDivisor = -1;
    mStatusBlock.clear();
}

int Apgdt001Parser::measuredBaud() const
{
    return mMeasuredDivisor < 0 ? 0 : (BAUD_CLOCK + (mMeasuredDivisor + 1) / 2) / (mMeasuredDivisor + 1);
}

//Length of the tag at the start of tag, operands included: -1 for an unknown tag, 0 if more bytes are needed to tell.
int Apgdt001Parser::tagLength(const unsigned char *tag, int available)
{
    switch (tag[0]) {
    case 0x14: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1B:
    case 0x83: case 0x84: case 0x88:
        return 1;
    case 0x10: case 0x12: case 0x1A: case 0x1C:
    case 0x80: case 0x81: case 0x82: case 0x86: case 0x87:
        return 2;
    case 0x13: case 0x15: case 0x85:
        return 3;
    case 0x11:
        return available < 2 ? 0 : 2 + tag[1];
    default:
        return -1;
    }
}

//The latest timer marker of a report was taken before the report reached us, so it sets the offset: the lowest
//latency seen so far wins, and the mapping starts again if the clocks are more than REANCHOR_US apart.
void Apgdt001Parser::updateOffset(qint64 latestDeviceUs, qint64 hostUs)
{
    const qint64 candidate = hostUs - latestDeviceUs;
    if (!mHaveOffset || candidate < mOffsetUs || candidate - mOffsetUs > REANCHOR_US)
        mOffsetUs = candidate;
    mHaveOffset = true;
}

void Apgdt001Parser::finishFrame(QList<CommFrame> &frames, QList<QPair<int, qint64>> &deviceTimes, qint64 hostUs,
                                 bool hostSplit)
{
    mBuilding = false;
    if (mFrameBytes.isEmpty())
    {
        mEmptyBreaks++;
        return;
    }

    const quint8 pid = quint8(mFrameBytes.at(0));
    const QByteArray response = mFrameBytes.mid(1);
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(pid & 0x3F);
    frame.setLinReceivedPid(pid);
    frame.setBus(0);
    frame.setReceived(true);

    quint8 errors = mFrameErrors;
    if (response.isEmpty())
    {
        mHeaderOnlyFrames++;
    }
    else if (response.size() == 1)
    {
        //a response has at least one data byte and a checksum
        frame.setPayload(response);
        errors |= CommFrame::LinErrIncompleteResponse;
    }
    else
    {
        frame.setPayload(response.left(response.size() - 1));
        frame.setLinChecksum(quint8(response.back()));
        frame.verifyLinChecksum();
        if (frame.hasLinChecksumError()) mChecksumErrorFrames++;
    }
    frame.setLinErrors(errors);
    if (frame.hasLinParityError()) mParityErrorFrames++;

    if (mFrameHasTime)
    {
        frame.setLinDeviceTimestamp(true);
        deviceTimes.append(qMakePair(int(frames.size()), mFrameDeviceUs));
    }
    else
    {
        frame.setTimeStamp(hostSplit ? CommFrame::TimeStamp::fromMicroSeconds(hostUs) : CommFrame::TimeStamp(0, hostUs));
    }
    frames.append(frame);
    mFramesParsed++;
}

void Apgdt001Parser::processStream(QList<CommFrame> &frames, QList<QPair<int, qint64>> &deviceTimes, qint64 hostUs,
                                   bool hostSplit)
{
    const unsigned char *buf = reinterpret_cast<const unsigned char *>(mStream.constData());
    const int bufLen = mStream.size();
    int pos = 0;

    while (pos < bufLen)
    {
        const unsigned char *tag = buf + pos;
        const int len = tagLength(tag, bufLen - pos);
        if (len < 0)
        {
            //can't tell where the next tag starts
            mBytesSkipped += bufLen - pos;
            pos = bufLen;
            break;
        }
        if (len == 0 || pos + len > bufLen) break; //the rest comes in the next report

        switch (tag[0]) {
        case 0x83: //break: a new frame
            if (mBuilding) finishFrame(frames, deviceTimes, hostUs, hostSplit);
            mBuilding = true;
            mFrameBytes.clear();
            mFrameHasTime = false;
            mFrameErrors = 0;
            break;
        case 0x10: //byte from the bus
            if (!mBuilding)
            {
                mStrayBytes++;
                break;
            }
            mFrameBytes.append(char(tag[1]));
            if (mFrameBytes.size() == MAX_FRAME_BYTES) finishFrame(frames, deviceTimes, hostUs, hostSplit);
            break;
        case 0x13: //event timer
        {
            const quint64 ticks = (mRollovers << 16) | (tag[1] | (tag[2] << 8));
            const qint64 deviceUs = qint64(ticks) * TICK_US;
            mHaveMarker = true;
            mLatestMarkerUs = deviceUs;
            if (mBuilding && !mFrameHasTime)
            {
                mFrameHasTime = true;
                mFrameDeviceUs = deviceUs;
            }
            break;
        }
        case 0x14: //event timer rollover
            mRollovers++;
            break;
        case 0x85: //measured bit rate, as a divisor
            mMeasuredDivisor = tag[1] | (tag[2] << 8);
            break;
        case 0x82: //status error
            mStatusErrors++;
            if (mBuilding)
            {
                mFrameErrors |= CommFrame::LinErrDevice;
                finishFrame(frames, deviceTimes, hostUs, hostSplit);
            }
            break;
        case 0x1C: //script status
            if (tag[1] == 0x77) mScriptsFinished++;
            break;
        default: //known, not used
            break;
        }
        pos += len;
    }

    mStream.remove(0, pos);
}

void Apgdt001Parser::stampFrames(QList<CommFrame> &frames, const QList<QPair<int, qint64>> &deviceTimes, qint64 hostUs,
                                 bool hostSplit)
{
    if (mHaveMarker) updateOffset(mLatestMarkerUs, hostUs);
    for (const auto &dt : deviceTimes)
    {
        const qint64 us = mHaveOffset ? dt.second + mOffsetUs : hostUs;
        frames[dt.first].setTimeStamp(hostSplit ? CommFrame::TimeStamp::fromMicroSeconds(us) : CommFrame::TimeStamp(0, us));
    }
}

int Apgdt001Parser::feed(const char *report, int len, CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames)
{
    const unsigned char *r = reinterpret_cast<const unsigned char *>(report);
    const qint64 hostUs = hostTimestamp.seconds() * 1000000 + hostTimestamp.microSeconds();
    const bool hostSplit = hostTimestamp.seconds() != 0; //keep the host clock's representation (see connections)
    const int before = frames.size();
    QList<QPair<int, qint64>> deviceTimes; //frames index, device time
    mHaveMarker = false;

    int i = 0;
    while (i < len)
    {
        int size = 0;
        switch (r[i]) {
        case 0x80: //end of report
            i = len;
            continue;
        case 0x81: size = 3; break;
        case 0x82: //configuration block echo
            size = 1 + STATUS_BLOCK_SIZE;
            if (i + size <= len) mStatusBlock = QByteArray(report + i + 1, STATUS_BLOCK_SIZE);
            break;
        case 0x83: size = 21; break;
        case 0x84: //last packet of a status reply
            size = 7;
            if (i + size <= len) mStatusReplies++;
            break;
        case 0x88: size = 2; break;
        case 0x85: case 0x86: case 0x87: //receive stream
            if (i + 2 > len || i + 2 + r[i + 1] > len)
            {
                mUnknownPackets++; //cut short: not a packet we understand
                i = len;
                continue;
            }
            mStream.append(report + i + 2, r[i + 1]);
            mStreamBytes += r[i + 1];
            i += 2 + r[i + 1];
            continue;
        default:
            mUnknownPackets++;
            i = len;
            continue;
        }
        mStatusPackets++;
        i += size;
    }

    processStream(frames, deviceTimes, hostUs, hostSplit);
    stampFrames(frames, deviceTimes, hostUs, hostSplit);
    return int(frames.size()) - before;
}

int Apgdt001Parser::flush(CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames)
{
    if (!mBuilding) return 0;
    const qint64 hostUs = hostTimestamp.seconds() * 1000000 + hostTimestamp.microSeconds();
    const bool hostSplit = hostTimestamp.seconds() != 0;
    const int before = frames.size();
    QList<QPair<int, qint64>> deviceTimes;
    mHaveMarker = false; //no new marker: frames use the existing clock mapping
    finishFrame(frames, deviceTimes, hostUs, hostSplit);
    stampFrames(frames, deviceTimes, hostUs, hostSplit);
    return int(frames.size()) - before;
}
