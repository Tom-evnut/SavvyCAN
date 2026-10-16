#ifndef APGDT001SESSION_H
#define APGDT001SESSION_H

#include <QByteArray>
#include <QList>
#include <QString>
#include "apgdt001parser.h"

/*
 * Everything the APGDT001 connection does that doesn't need USB: the output reports it sends, checking that the
 * device took the configuration, and ending the last frame when the bus goes quiet. The connection (apgdt001.h)
 * only moves reports between this and hidapi, so this is what the tests exercise.
 *
 * Output reports are 64 bytes (the connection adds the HID report ID, 0): a command byte and its operands. Protocol
 * as worked out from Microchip's LIN Library API Demo (docs/lin-roadmap.md, D1) and checked on a device:
 *   01 00   cold reset          01 01   warm reset          01 02   status request          01 07   clear comms
 *   02 <29-byte configuration block> 00    write the configuration block
 * Configuration block (the bytes known so far; the rest are Microchip's defaults):
 *   byte 0      0xC0: flush the receive buffer to the host by byte count (bit 6) and by time (bit 7)
 *   byte 3      flush after this many bytes          byte 4      flush after this many 0.409 ms ticks
 *   bytes 22-23 bit-rate divisor, low byte first: 5 MHz / bit rate - 1
 *
 * Starting: cold reset, configuration block, warm reset, status request (Microchip's sequence). Changing the bit
 * rate: configuration block, clear comms, status request. Either way the status reply echoes the first 24 bytes of
 * the block, which must match what was written.
 */
class Apgdt001Session
{
public:
    static constexpr quint16 USB_VID = 0x04D8; //Microchip; Microchip's own library only checks the product ID
    static constexpr quint16 USB_PID = 0x0A04;
    static constexpr int CONTROL_BLOCK_SIZE = 29;
    static constexpr int FLUSH_BYTES = 10;      //as Microchip's demo: hand data to the host after 10 bytes...
    static constexpr int FLUSH_MS = 10;         //...or 10 ms
    static constexpr int MIN_BAUD = 1000;
    static constexpr int MAX_BAUD = 20000;
    static constexpr qint64 STATUS_TIMEOUT_US = 2000000;

    enum class Check { None, Pending, Ok, Failed };

    static int divisorForBaud(int baud);
    static int baudForDivisor(int divisor);
    static bool isValidBaud(int baud) { return baud >= MIN_BAUD && baud <= MAX_BAUD; }
    static QByteArray controlBlock(int baud);
    static QByteArray commandReport(const QByteArray &command); //padded to a 64-byte report
    static QByteArray coldResetReport() { return commandReport(QByteArray::fromHex("0100")); }
    static QByteArray warmResetReport() { return commandReport(QByteArray::fromHex("0101")); }
    static QByteArray statusRequestReport() { return commandReport(QByteArray::fromHex("0102")); }
    static QByteArray commClearReport() { return commandReport(QByteArray::fromHex("0107")); }
    static QByteArray controlBlockReport(const QByteArray &block);
    /*
     * Sending as master: 03 <n+6> 84 <n+2> <PID> <data...> <checksum> 1F 77 00, a script that sends a header and a
     * response of n data bytes (Microchip's library: the first length counts the script from 84 to 77, the second the
     * PID, data and checksum). With no data, 03 05 84 01 <PID> 1F 77 00: the same script with the PID alone, which
     * sends just the header for a slave to answer (checked on a device). Sending a response hasn't been tried yet.
     */
    static QByteArray transmitReport(quint8 id, const QByteArray &data, quint8 checksum);

    /*
     * Pacing what's sent as master. The analyser runs each transmit script as soon as it arrives, even while a slave
     * is still answering the last header (seen on a device: headers sent back to back went out 4 ms apart at 9600
     * bit/s and no slave answered), so transmit reports are queued and each waits for the slot of the frame before
     * it: the header and response at the 40 % longer frame time LIN allows, and a header alone gets the slot of the
     * longest response (it isn't known how many bytes the slave will send).
     */
    static constexpr int MAX_QUEUED_TRANSMITS = 256;
    static qint64 frameSlotUs(int baud, int dataBytes); //dataBytes 0: a header alone
    //false if the queue is full (the frame is dropped)
    bool queueTransmit(quint8 id, const QByteArray &data, quint8 checksum);
    //the transmit reports whose turn has come, to send now
    QList<QByteArray> takeDueTransmits(qint64 nowUs);
    int queuedTransmits() const { return int(mTransmits.size()); }

    //the reports to send, in order; both end with a status request whose reply is checked (check())
    QList<QByteArray> startCommands(int baud, qint64 nowUs);
    QList<QByteArray> changeBaudCommands(int baud, qint64 nowUs);

    //an input report; appends the frames it completed. nowUs is a monotonic clock for the quiet-bus timeout.
    int processReport(const char *report, int len, qint64 nowUs, CommFrame::TimeStamp hostTimestamp,
                      QList<CommFrame> &frames);
    //call regularly: ends the frame being built once the bus has been quiet for quietUs(), and times out a status
    //request that got no reply
    int poll(qint64 nowUs, CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames);

    Check check() const { return mCheck; }
    QString checkMessage() const { return mCheckMessage; }
    //true once each time a check finishes (Ok or Failed), so the connection reports it once
    bool takeCheckResult();

    int baud() const { return mBaud; }
    qint64 quietUs() const; //longest frame at the bit rate, plus the device's flush time and a margin
    Apgdt001Parser &parser() { return mParser; }
    const Apgdt001Parser &parser() const { return mParser; }
    void reset(); //new connection

private:
    void armCheck(int baud, qint64 nowUs);
    void finishCheck(Check result, const QString &message);

    Apgdt001Parser mParser;
    int mBaud = 0;
    QByteArray mExpectedBlock;
    Check mCheck = Check::None;
    QString mCheckMessage;
    bool mCheckResultNew = false;
    quint64 mRepliesAtRequest = 0;
    qint64 mRequestUs = 0;
    quint64 mStreamBytesSeen = 0;
    qint64 mLastStreamUs = 0;

    struct Transmit { QByteArray report; qint64 slotUs; };
    QList<Transmit> mTransmits;
    qint64 mNextTransmitUs = 0;
};

#endif // APGDT001SESSION_H
