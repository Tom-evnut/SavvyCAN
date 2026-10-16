#ifndef LINTP_H
#define LINTP_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include "can_structs.h"

/*
 * The LIN diagnostic transport layer (LIN 2.x / ISO 17987-2): requests go in master request frames (ID 0x3C), answers
 * in slave response frames (0x3D), 8 bytes each: NAD, PCI, then the message, padded with 0xFF.
 *   single frame       PCI 0x0L (L = 1-6 bytes)         NAD 0L SID D1..D5
 *   first frame        PCI 0x1H, LEN (12-bit length)    NAD 1H LL SID D1..D4
 *   consecutive frame  PCI 0x2N (N = 1, 2, ... 15, 0)   NAD 2N D1..D6
 * There's no flow control: the master sends a slave response header for each frame it wants back. A positive
 * response's SID is the request's + 0x40; a negative one is 7F <request SID> <code>. A master request whose first
 * byte is 0 is the go-to-sleep command, not a transport layer message.
 *
 * Node configuration (LIN 2.1) uses SIDs 0xB0-0xB7; anything else is a diagnostic service (UDS over LIN, ISO 14229).
 */
namespace LinTp {

constexpr quint8 MASTER_REQUEST = 0x3C;
constexpr quint8 SLAVE_RESPONSE = 0x3D;
constexpr quint8 NAD_SLEEP = 0x00;      //first byte of a go-to-sleep command
constexpr quint8 NAD_FUNCTIONAL = 0x7E;
constexpr quint8 NAD_BROADCAST = 0x7F;  //every slave
constexpr int MAX_LENGTH = 4095;

//node configuration services
constexpr quint8 SID_ASSIGN_NAD = 0xB0;
constexpr quint8 SID_ASSIGN_FRAME_ID = 0xB1;    //LIN 2.0
constexpr quint8 SID_READ_BY_ID = 0xB2;
constexpr quint8 SID_CONDITIONAL_CHANGE_NAD = 0xB3;
constexpr quint8 SID_DATA_DUMP = 0xB4;
constexpr quint8 SID_SAVE_CONFIGURATION = 0xB6;
constexpr quint8 SID_ASSIGN_FRAME_ID_RANGE = 0xB7;
constexpr quint8 SID_NEGATIVE_RESPONSE = 0x7F;
constexpr quint16 SUPPLIER_WILDCARD = 0x7FFF;
constexpr quint16 FUNCTION_WILDCARD = 0xFFFF;

struct Message
{
    bool request = true;        //a master request (0x3C) or a slave response (0x3D)
    bool sleep = false;         //a go-to-sleep command (no NAD or payload)
    quint8 nad = 0;
    QByteArray payload;         //SID (or response SID) and data
    int bus = 0;
    CommFrame::TimeStamp time;  //of the message's last frame
};

//the frames of a request: the data of each master request frame (8 bytes), single or first + consecutive frames
QList<QByteArray> frames(quint8 nad, const QByteArray &payload);
QByteArray goToSleep(); //a whole master request frame's data

//node configuration requests (the message: SID and data)
QByteArray readByIdentifier(quint8 identifier, quint16 supplier = SUPPLIER_WILDCARD, quint16 function = FUNCTION_WILDCARD);
QByteArray assignNad(quint8 newNad, quint16 supplier = SUPPLIER_WILDCARD, quint16 function = FUNCTION_WILDCARD);
QByteArray conditionalChangeNad(quint8 identifier, quint8 byte, quint8 mask, quint8 invert, quint8 newNad);
QByteArray saveConfiguration();
QByteArray assignFrameIdRange(quint8 startIndex, const QList<quint8> &pids); //up to 4 protected IDs (0xFF: keep)

/*
 * Reassembling messages from LIN frames: feed every frame in order (LIN frames with ID 0x3C and 0x3D; others and
 * headers nobody answered are ignored). Requests and responses are put together separately for each bus.
 */
class Decoder
{
public:
    QList<Message> feed(const CommFrame &frame);
    void reset() { mPartial.clear(); mErrors = 0; }
    int errors() const { return mErrors; } //consecutive frames out of sequence, frames that don't make sense

private:
    struct Partial { Message message; int length = 0; int nextSequence = 1; };
    QMap<QPair<int, bool>, Partial> mPartial; //(bus, request)
    int mErrors = 0;
};

/*
 * Sending requests as the LIN master and collecting the answers: each request's master request frames go out one
 * slot apart, then slave response headers, one every poll, until the answer is complete (a segmented answer takes a
 * header per frame) or maxPolls headers went unanswered. "Response pending" (7F xx 78) starts the polls again.
 * Requests run one after another, so a scan is a list of requests. No timer here: the LIN Diagnostics window calls
 * due() and passes on what the decoder finds, so it can be tested with a made-up clock.
 */
class Requester
{
public:
    struct Request { int bus = 0; quint8 nad = NAD_BROADCAST; QByteArray payload; };
    struct Result { Request request; bool answered = false; Message response; };

    qint64 slotUs = 20000;  //between frames sent: a frame's slot at slow bit rates
    int maxPolls = 10;      //slave response headers before giving up on an answer

    void start(const QList<Request> &requests, qint64 nowUs);
    void stop() { mQueue.clear(); mRunning = false; }
    bool isRunning() const { return mRunning; }
    QList<CommFrame> due(qint64 nowUs);           //the frames to send now
    void received(const Message &message);         //a message the decoder put together
    QList<Result> takeResults();                   //requests finished since the last call
    int remaining() const { return int(mQueue.size()) + (mRunning ? 1 : 0); }

    static CommFrame masterRequest(int bus, const QByteArray &data); //classic checksum, as diagnostic frames use
    static CommFrame slaveResponseHeader(int bus);

private:
    void next(qint64 nowUs);

    QList<Request> mQueue;
    Request mCurrent;
    bool mRunning = false;
    QList<QByteArray> mFrames;  //the current request's frames still to send
    int mPolls = 0;
    qint64 mNextUs = 0;
    QList<Result> mResults;
};

QString serviceName(quint8 sid);          //a request SID: node configuration or UDS service name ("" if unknown)
QString negativeResponseName(quint8 code); //UDS negative response code
//one line about a message: the service, positive or negative, and what node configuration answers say
QString describe(const Message &message);

}

#endif // LINTP_H
