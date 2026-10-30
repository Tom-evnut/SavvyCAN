#ifndef PICOFLEXRAYPARSER_H
#define PICOFLEXRAYPARSER_H

#include <QByteArray>
#include <QList>
#include "can_structs.h"

/*
 * Parser for the frame stream a pico-flexray device (https://github.com/dynm/pico-flexray) sends on its bulk IN
 * endpoint. The stream is a sequence of variable-length records, which can be split across USB transfers, in one of
 * three formats:
 *
 *   format 1 (stock firmware, and every firmware until the host asks for format 2):
 *     [u16 body length LE][u8 source][5-byte FlexRay header][payload][3-byte frame CRC, big endian]
 *   format 2 (bit 15 of the length set; firmware with record format support, after PICOFLEX_SET_RECORD_FORMAT 2):
 *     [u16 0x8000|body length LE][u8 version = 2][u8 flags][u8 source][u32 device time, us, LE][header][payload][CRC]
 *     flags: bit 0 = frame CRC correct, bit 1 = header CRC correct
 *   format 3 (bit 15 set, version = 3; the timestamped-capture firmware, after PICOFLEX_SET_RECORD_FORMAT 3):
 *     [u16 0x8000|body length LE][u8 version = 3][u8 flags][u8 source][u32 device time, us, LE]
 *       [u16 fine clock][u32 sequence number][header][payload][CRC]
 *     flags as format 2 plus bit 2 = hardware timestamp. The u16 fine clock (10 ns steps on the device's 100 MHz
 *     clock) is not used: SavvyCAN stores microsecond timestamps, so format 3 frames are timestamped exactly like
 *     format 2. The u32 sequence number counts frames on the device; a gap means its USB FIFO dropped frames
 *     (counted as seqGaps() / seqFramesLost()).
 *
 * The source is the capture channel as decimal digits: 1 = FR1, 2 = FR2, and an FR3/FR4 tag can follow
 * (13 = seen on FR1, tagged FR3). Frames are captured on FR1 (bus 0) or FR2 (bus 1); the FR3/FR4 tag isn't kept.
 *
 * Each record becomes a CommFrame::FLEXRAYDataFrame (see CommFrame::isFlexRay()): ID = slot ID, payload = cycle count
 * followed by the FlexRay payload. The header's indicators and CRCs and the device's CRC check go into the frame's
 * FlexRay header details (CommFrame::setFlexRayHeader()). Null frames are kept: they're marked by their indicators
 * (CommFrame::isFlexRayNullFrame()) and aren't data frames. Stock firmware only sends frames that passed both CRCs.
 *
 * Timestamps: format 1 frames get the host timestamp passed to feed(). Format 2 and 3 frames get their device time,
 * mapped onto the host clock: the device clock is unwrapped to 64 bits and offset so that the latest frame of each
 * transfer is never stamped later than the transfer reached the host. The offset follows the lowest latency seen, and
 * starts again if the two clocks are more than a second apart (e.g. after a device reset). Frames keep their device
 * spacing, so slot and cycle timing is the device's. (Format 3's 10 ns fine clock is read but not applied, since the
 * timestamp is stored at microsecond resolution.)
 */
class PicoFlexRayParser
{
public:
    static constexpr int V1_MIN_BODY_LEN = 1 + 5 + 3; //source + header + CRC, no payload
    static constexpr int V1_MAX_BODY_LEN = V1_MIN_BODY_LEN + CommFrame::FLEXRAY_MAX_PAYLOAD;
    static constexpr int V2_HEADER_LEN = 7;           //version, flags, source, u32 device time
    static constexpr int V2_MIN_BODY_LEN = V2_HEADER_LEN + 5 + 3;
    static constexpr int V2_MAX_BODY_LEN = V2_MIN_BODY_LEN + CommFrame::FLEXRAY_MAX_PAYLOAD;
    static constexpr int V3_HEADER_LEN = 13;          //version, flags, source, u32 device time, u16 fine clock, u32 sequence
    static constexpr int V3_MIN_BODY_LEN = V3_HEADER_LEN + 5 + 3;
    static constexpr int V3_MAX_BODY_LEN = V3_MIN_BODY_LEN + CommFrame::FLEXRAY_MAX_PAYLOAD;
    static constexpr quint16 V2_LEN_FLAG = 0x8000;    //length bit 15: an extended, versioned record (format 2 or 3)
    static constexpr quint8 V2_FLAG_FRAME_CRC_OK = 0x01;
    static constexpr quint8 V2_FLAG_HEADER_CRC_OK = 0x02;
    static constexpr qint64 REANCHOR_US = 1000000;

    //Adds bytes read from the device and appends the frames completed by them to frames. hostTimestamp is when
    //these bytes arrived: the timestamp of format 1 frames, and the reference for mapping format 2 device times.
    //Returns the number of frames appended.
    int feed(const char *data, int len, CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames);
    void reset(); //new connection: drops buffered bytes and the device clock mapping

    quint64 framesParsed() const { return mFramesParsed; }
    quint64 nullFrames() const { return mNullFrames; }
    quint64 crcErrorFrames() const { return mCrcErrorFrames; }
    quint64 format2Records() const { return mFormat2Records; }
    quint64 format3Records() const { return mFormat3Records; }
    quint64 deviceTimeRecords() const { return mFormat2Records + mFormat3Records; } //frames carrying a device timestamp
    quint64 seqGaps() const { return mSeqGaps; }               //format 3: USB transfers with a sequence-number gap
    quint64 seqFramesLost() const { return mSeqFramesLost; }   //format 3: frames the device's USB FIFO dropped
    quint64 bytesSkipped() const { return mBytesSkipped; }     //bytes dropped while resynchronising on a bad record

    //bus number for a record's source byte, -1 if it names neither FR1 nor FR2
    static int busFromSource(quint8 source);

private:
    qint64 unwrapDeviceTime(quint32 deviceUs);
    void updateOffset(qint64 latestDeviceUs, qint64 hostUs);

    QByteArray mBuffer;
    quint64 mFramesParsed = 0;
    quint64 mNullFrames = 0;
    quint64 mCrcErrorFrames = 0;
    quint64 mFormat2Records = 0;
    quint64 mFormat3Records = 0;
    quint64 mSeqGaps = 0;        //format 3: USB transfers that lost one or more frames (a jump in the sequence number)
    quint64 mSeqFramesLost = 0;  //format 3: total frames lost (sum of the sequence-number jumps)
    quint64 mBytesSkipped = 0;

    //device clock mapping (format 2 and 3)
    bool mHaveDeviceTime = false;
    quint32 mLastDeviceRaw = 0;
    qint64 mDevice64 = 0;  //unwrapped device time
    qint64 mOffsetUs = 0;  //host time = device time + offset

    //device sequence tracking (format 3)
    bool mHaveSeq = false;
    quint32 mLastSeq = 0;
};

#endif // PICOFLEXRAYPARSER_H
