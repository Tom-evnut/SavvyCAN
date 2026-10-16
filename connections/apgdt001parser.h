#ifndef APGDT001PARSER_H
#define APGDT001PARSER_H

#include <QByteArray>
#include <QList>
#include "can_structs.h"

/*
 * Parser for the input reports of a Microchip APGDT001 LIN Serial Analyzer (USB HID, PICkit Serial Analyzer style
 * firmware). The protocol was worked out from Microchip's LIN Library API Demo and checked on a device (an APGDT001
 * polling LIN slaves at 9600 and 19200 bit/s: status replies, breaks, bytes, timer markers, script status);
 * see docs/lin-roadmap.md (D1) on the lin-roadmap branch.
 *
 * Each 64-byte input report holds packets, each starting with a type byte:
 *   0x80                end of the report
 *   0x81 0x82 0x83 0x84 status packets of 3, 25, 21 and 7 bytes (type byte included), the device's reply to a status
 *                       request: 0x82 carries the first 24 bytes of its configuration block (kept: lastStatusBlock()),
 *                       and 0x84 ends the reply (counted: statusReplies())
 *   0x88                a 2-byte packet, skipped
 *   0x85 0x86 0x87      [type][n][n bytes of receive stream]
 *
 * The receive stream is a sequence of tags, which can be split across reports:
 *   0x83                a break: a new LIN frame starts (and the frame being built, if any, ends)
 *   0x10 b              a byte from the bus: the protected ID first, then the response's data bytes and checksum
 *   0x13 lo hi          event timer, in TICK_US ticks; the first one after a break timestamps the frame
 *   0x14                event timer rollover (65536 ticks)
 *   0x85 lo hi          bit rate the device measured, as a divisor: 5 MHz / (divisor + 1)
 *   0x82 s              status error: the frame being built ends, flagged LinErrDevice
 *   0x1C s              script status (0x77 = a script, e.g. a transmit, finished)
 *   0x11 n [n bytes]    a block of bytes, not frame data
 *   0x12 0x15 0x1A 0x80 0x81 0x86 0x87 carry operands (1, 2, 1, 1, 1, 1 and 1 bytes) that aren't used;
 *   0x16-0x19 0x1B 0x84 0x88 stand alone
 * An unknown tag can't be skipped (its length is unknown), so the rest of the buffered stream is dropped.
 *
 * The device doesn't delimit frames: a frame ends at the next break, after 10 bytes (ID + 8 data + checksum), on a
 * status error, or when the connection calls flush() after the bus has been quiet for a frame time. The last byte
 * after the ID is the checksum; an ID alone is a header nobody answered; one byte after the ID is an incomplete
 * response (LinErrIncompleteResponse, kept as data, no checksum). The device doesn't check the checksum or the ID
 * parity: the parser does, with CommFrame::verifyLinChecksum() (which also finds each frame's checksum model) and
 * the received protected ID.
 *
 * Timestamps: frames with an event timer marker get the device time, mapped onto the host clock as
 * PicoFlexRayParser does: the device clock is unwrapped to 64 bits and offset so that the latest marker of each
 * report is never later than the report reached the host; the offset follows the lowest latency seen and starts
 * again if the clocks are more than a second apart. Frames without a marker get the host timestamp.
 */
class Apgdt001Parser
{
public:
    static constexpr int REPORT_SIZE = 64;
    static constexpr int STATUS_BLOCK_SIZE = 24;   //configuration block bytes echoed in a 0x82 status packet
    static constexpr int BAUD_CLOCK = 5000000;     //bit rate = BAUD_CLOCK / (divisor + 1), set and measured
    static constexpr int TICK_US = 409;            //event timer tick (Microchip's library: 0.409 ms; matches a device)
    static constexpr int MAX_FRAME_BYTES = 1 + CommFrame::LIN_MAX_PAYLOAD + 1; //protected ID, data, checksum
    static constexpr qint64 REANCHOR_US = 1000000;

    //Parses one input report (without the HID report ID) and appends the frames it completed to frames.
    //hostTimestamp is when the report arrived. Returns the number of frames appended.
    int feed(const char *report, int len, CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames);
    //Ends the frame being built, if any (call when the bus has been quiet for longer than a frame).
    int flush(CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames);
    void reset(); //new connection: drops the frame being built, buffered stream bytes and the clock mapping

    bool isBuildingFrame() const { return mBuilding; }
    int measuredBaud() const; //bit rate the device last measured, 0 until it reports one
    QByteArray lastStatusBlock() const { return mStatusBlock; } //empty until a status reply arrives
    quint64 statusReplies() const { return mStatusReplies; }
    quint64 streamBytes() const { return mStreamBytes; } //receive stream bytes seen, to tell when the bus is quiet
    quint64 framesParsed() const { return mFramesParsed; }
    quint64 headerOnlyFrames() const { return mHeaderOnlyFrames; }
    quint64 checksumErrorFrames() const { return mChecksumErrorFrames; }
    quint64 parityErrorFrames() const { return mParityErrorFrames; }
    quint64 statusErrors() const { return mStatusErrors; }
    quint64 emptyBreaks() const { return mEmptyBreaks; }       //breaks with no ID after them
    quint64 strayBytes() const { return mStrayBytes; }         //bytes outside a frame (or after a full one)
    quint64 statusPackets() const { return mStatusPackets; }
    quint64 scriptsFinished() const { return mScriptsFinished; }
    quint64 unknownPackets() const { return mUnknownPackets; } //reports cut short by an unknown packet type
    quint64 bytesSkipped() const { return mBytesSkipped; }     //stream bytes dropped after an unknown tag

private:
    static int tagLength(const unsigned char *tag, int available); //-1 = unknown tag, 0 = needs more bytes
    void processStream(QList<CommFrame> &frames, QList<QPair<int, qint64>> &deviceTimes, qint64 hostUs,
                       bool hostSplit);
    void finishFrame(QList<CommFrame> &frames, QList<QPair<int, qint64>> &deviceTimes, qint64 hostUs, bool hostSplit);
    void stampFrames(QList<CommFrame> &frames, const QList<QPair<int, qint64>> &deviceTimes, qint64 hostUs,
                     bool hostSplit);
    void updateOffset(qint64 latestDeviceUs, qint64 hostUs);

    QByteArray mStream;     //receive stream bytes not processed yet (an incomplete tag)

    //frame being built
    bool mBuilding = false;
    QByteArray mFrameBytes; //protected ID, data, checksum
    bool mFrameHasTime = false;
    qint64 mFrameDeviceUs = 0;
    quint8 mFrameErrors = 0;

    //device clock
    quint64 mRollovers = 0;
    bool mHaveMarker = false;   //a timer marker arrived in the current report
    qint64 mLatestMarkerUs = 0;
    bool mHaveOffset = false;
    qint64 mOffsetUs = 0;       //host time = device time + offset

    int mMeasuredDivisor = -1;
    QByteArray mStatusBlock;
    quint64 mStatusReplies = 0;
    quint64 mStreamBytes = 0;
    quint64 mFramesParsed = 0;
    quint64 mHeaderOnlyFrames = 0;
    quint64 mChecksumErrorFrames = 0;
    quint64 mParityErrorFrames = 0;
    quint64 mStatusErrors = 0;
    quint64 mEmptyBreaks = 0;
    quint64 mStrayBytes = 0;
    quint64 mStatusPackets = 0;
    quint64 mScriptsFinished = 0;
    quint64 mUnknownPackets = 0;
    quint64 mBytesSkipped = 0;
};

#endif // APGDT001PARSER_H
