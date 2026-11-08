#include "picoflexrayparser.h"

int PicoFlexRayParser::busFromSource(quint8 source)
{
    //decimal digits, FR1/FR2 first: 1, 2, 13, 14, 23, 24 (12 and longer combinations aren't sent for captured frames)
    int firstDigit = source;
    while (firstDigit >= 10) firstDigit /= 10;
    if (firstDigit == 1) return 0;
    if (firstDigit == 2) return 1;
    return -1;
}

void PicoFlexRayParser::reset()
{
    mBuffer.clear();
    mHaveDeviceTime = false;
    mHaveSeq = false;
}

qint64 PicoFlexRayParser::unwrapDeviceTime(quint32 deviceUs)
{
    if (!mHaveDeviceTime) mDevice64 = deviceUs;
    else mDevice64 += qint32(deviceUs - mLastDeviceRaw); //unwraps the 32-bit counter (~71.6 minutes)
    mLastDeviceRaw = deviceUs;
    return mDevice64;
}

//The latest frame of a transfer ended just before the transfer reached us, so it sets the offset: the lowest
//latency seen so far wins, and the mapping starts again if the clocks are more than REANCHOR_US apart.
void PicoFlexRayParser::updateOffset(qint64 latestDeviceUs, qint64 hostUs)
{
    const qint64 candidate = hostUs - latestDeviceUs;
    if (!mHaveDeviceTime || candidate < mOffsetUs || candidate - mOffsetUs > REANCHOR_US)
        mOffsetUs = candidate;
    mHaveDeviceTime = true;
}

int PicoFlexRayParser::feed(const char *data, int len, CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames)
{
    mBuffer.append(data, len);

    const unsigned char *buf = reinterpret_cast<const unsigned char *>(mBuffer.constData());
    const int bufLen = mBuffer.size();
    const qint64 hostUs = hostTimestamp.seconds() * 1000000 + hostTimestamp.microSeconds();
    const bool hostSplit = hostTimestamp.seconds() != 0; //keep the host clock's representation (see connections)
    int pos = 0;
    int added = 0;
    QList<QPair<int, qint64>> deviceTimes; //frames index, unwrapped device time

    while (pos + 2 <= bufLen)
    {
        const int lenField = buf[pos] | (buf[pos + 1] << 8);
        const bool extended = lenField & V2_LEN_FLAG; //bit 15: a versioned record (format 2 or 3), else stock format 1
        const int bodyLen = lenField & ~V2_LEN_FLAG;

        //the prefix (bytes before the FlexRay header) depends on the record version, which is the first body byte
        int prefix, version;
        if (extended)
        {
            if (pos + 3 > bufLen) break; //need the version byte to tell format 2 from format 3
            version = buf[pos + 2];
            if (version == 2) prefix = V2_HEADER_LEN;
            else if (version == 3) prefix = V3_HEADER_LEN;
            else
            {
                //not a version we know (a false start while resynchronising, or a CAN record we don't handle here)
                pos++;
                mBytesSkipped++;
                continue;
            }
        }
        else
        {
            version = 1;
            prefix = 1;
        }

        const int minLen = prefix + 5 + 3; //prefix + 5-byte FlexRay header + 3-byte frame CRC, no payload
        const int maxLen = minLen + CommFrame::FLEXRAY_MAX_PAYLOAD;
        if (bodyLen < minLen || bodyLen > maxLen)
        {
            //not a record start: skip a byte and look again, as pico-flexray's own recorder does
            pos++;
            mBytesSkipped++;
            continue;
        }

        const unsigned char *body = buf + pos + 2;
        //check the length against the header's payload length as soon as it's here, so a false start doesn't
        //wait for a whole record's worth of data
        if (pos + 2 + prefix + 3 > bufLen) break;
        const unsigned char *header = body + prefix;
        const int payloadLen = ((header[2] >> 1) & 0x7F) * 2;
        if (minLen + payloadLen != bodyLen)
        {
            pos++;
            mBytesSkipped++;
            continue;
        }
        if (pos + 2 + bodyLen > bufLen) break; //record not complete yet

        quint8 source, flags;
        const bool hasDeviceTime = version >= 2; //format 2 and 3 carry a device timestamp; format 1 does not
        if (hasDeviceTime)
        {
            flags = body[1];
            source = body[2];
            const quint32 deviceUs = quint32(body[3]) | (quint32(body[4]) << 8) | (quint32(body[5]) << 16)
                                     | (quint32(body[6]) << 24);
            //unwrap now, in stream order; mapped onto the host clock once the whole transfer is parsed
            deviceTimes.append(qMakePair(frames.size(), unwrapDeviceTime(deviceUs)));
            if (!mHaveDeviceTime) updateOffset(mDevice64, hostUs); //first ever: a provisional offset to unwrap from
            if (version == 3)
            {
                //format 3 adds a u16 fine clock (10 ns steps; unused, as SavvyCAN keeps microsecond timestamps) and a
                //u32 device sequence number. A gap in the sequence means the device's USB FIFO dropped frames.
                const quint32 seq = quint32(body[9]) | (quint32(body[10]) << 8) | (quint32(body[11]) << 16)
                                    | (quint32(body[12]) << 24);
                if (mHaveSeq)
                {
                    const quint32 lost = seq - mLastSeq - 1;
                    if (lost > 0 && lost < 0x80000000u) { mSeqGaps++; mSeqFramesLost += lost; }
                }
                mLastSeq = seq;
                mHaveSeq = true;
                mFormat3Records++;
            }
            else
                mFormat2Records++;
        }
        else
        {
            source = body[0];
            flags = V2_FLAG_FRAME_CRC_OK | V2_FLAG_HEADER_CRC_OK; //stock firmware only sends frames that pass both
        }
        const quint8 indicators = header[0] >> 3; //reserved, payload preamble, null frame, sync, startup
        const quint32 slotId = ((header[0] & 0x07) << 8) | header[1];
        const quint16 headerCrc = quint16(((header[2] & 0x01) << 10) | (header[3] << 2) | (header[4] >> 6));
        const quint8 cycle = header[4] & 0x3F;
        const unsigned char *crc = header + 5 + payloadLen;
        const quint32 frameCrc = (quint32(crc[0]) << 16) | (quint32(crc[1]) << 8) | crc[2];
        pos += 2 + bodyLen;

        //the FlexRay payload itself; the cycle count is kept as a first-class field, not prepended to it
        QByteArray payload(reinterpret_cast<const char *>(header + 5), payloadLen);

        CommFrame frame(CommFrame::FLEXRAYDataFrame);
        frame.setFrameId(slotId);
        frame.setExtendedFrameFormat(false);
        frame.setFlexRayCycle(cycle);
        frame.setPayload(payload);
        frame.setBus(qMax(0, busFromSource(source)));
        frame.setReceived(true);
        frame.setFlexRayHeader(indicators, headerCrc, frameCrc);
        frame.setFlexRayCrcStatus(flags & V2_FLAG_HEADER_CRC_OK, flags & V2_FLAG_FRAME_CRC_OK);
        frame.setFlexRayDeviceTimestamp(hasDeviceTime);
        frame.setTimeStamp(hostSplit ? CommFrame::TimeStamp::fromMicroSeconds(hostUs) : CommFrame::TimeStamp(0, hostUs));
        if (frame.isFlexRayNullFrame()) mNullFrames++;
        if (frame.hasFlexRayCrcError()) mCrcErrorFrames++;
        frames.append(frame);
        mFramesParsed++;
        added++;
    }

    if (!deviceTimes.isEmpty())
    {
        updateOffset(deviceTimes.last().second, hostUs);
        for (const auto &dt : deviceTimes)
        {
            const qint64 us = dt.second + mOffsetUs;
            frames[dt.first].setTimeStamp(hostSplit ? CommFrame::TimeStamp::fromMicroSeconds(us)
                                                    : CommFrame::TimeStamp(0, us));
        }
    }

    mBuffer.remove(0, pos);
    return added;
}
