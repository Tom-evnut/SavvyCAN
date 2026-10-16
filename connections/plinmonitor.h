#ifndef PLINMONITOR_H
#define PLINMONITOR_H

#include <QList>
#include <QString>
#include "can_structs.h"
#include "plinbackend.h"

/*
 * Listening to a LIN bus through one PEAK LIN channel, without USB or the DLL in the way (that's the PlinBackend):
 *
 * Starting: register a client, connect it to the channel, initialise the channel as a slave (or, to send, as a master)
 * at the bit rate, enter
 * every frame ID (0-63) as a subscriber with automatic length and checksum type, and let all IDs through the client
 * filter. A subscriber never transmits, so the bus isn't touched; "automatic length" means no response timeout, and
 * the last byte received is taken as the checksum (PLIN-API documentation, "Message as Subscriber-AutoLength").
 *
 * Sending (master only): each frame sent is one LIN_Write, which puts a header on the bus. A frame with data is a
 * publisher message: the response follows with the checksum exactly as given (checksum type "custom"), so a wrong
 * one can be sent on purpose. A frame without data is an auto-length subscriber message: the header alone, for a
 * slave to answer; the answer comes back like any other frame.
 *
 * Reading: standard messages become LIN CommFrames: the ID and the protected ID as received, the response's data,
 * the checksum (checked here, which also finds each ID's checksum model) and PEAK's error flags as LIN error flags.
 * A header nobody answered is a frame with no data. Bus events (sleep, wake-up, overruns) are counted, not framed.
 * Timestamps are the break's falling edge in microseconds on the device, mapped onto the host clock as the other
 * USB parsers do: the latest message of each read is never later than the read, the offset follows the lowest
 * latency seen, and the mapping starts again if the clocks are more than a second apart.
 */
class PlinMonitor
{
public:
    static constexpr qint64 REANCHOR_US = 1000000;

    explicit PlinMonitor(PlinBackend &backend) : mBackend(backend) {}
    ~PlinMonitor() { stop(); }

    //"<name>, device <n>, channel <m>": what the new-connection dialog lists and connections save as their port
    static QString portName(const PlinHardware &hw);
    //the channel a port names (an empty port: the first one), or nullptr
    static const PlinHardware *findHardware(const QList<PlinHardware> &hardware, const QString &port);
    static CommFrame toFrame(const PlinRcvMsg &msg); //a standard message, without its timestamp
    static PlinMsg toMessage(const CommFrame &lin);  //a LIN frame to send (see the class comment)

    bool start(const QString &port, int baudrate, QString *error, bool master = false);
    bool changeBaudrate(int baudrate, QString *error); //while running
    bool setMaster(bool master, QString *error);       //while running: initialises the channel again
    bool isMaster() const { return mMaster; }
    bool send(const CommFrame &lin, QString *error);   //master only
    void stop();
    bool isRunning() const { return mRunning; }
    PlinHardware currentHardware() const { return mHardware; }

    //Reads what the client's queue holds (up to maxMessages) and appends the frames. Returns the number of frames,
    //or -1 when reading failed (the channel was unplugged, ...): error says why, and the monitor has stopped.
    int poll(CommFrame::TimeStamp hostTimestamp, QList<CommFrame> &frames, QString *error = nullptr, int maxMessages = 1000);

    quint64 framesRead() const { return mFramesRead; }
    quint64 busEvents() const { return mBusEvents; }   //sleep, wake-up, auto baud rate replies
    quint64 overruns() const { return mOverruns; }     //bus, queue and client queue overruns

private:
    bool configure(int baudrate, QString *error);
    bool fail(quint32 rc, const char *what, QString *error);
    void updateOffset(qint64 latestDeviceUs, qint64 hostUs);

    PlinBackend &mBackend;
    bool mRunning = false;
    bool mMaster = false;
    int mBaudrate = 0;
    quint8 mClient = 0;
    PlinHardware mHardware;

    bool mHaveOffset = false;
    qint64 mOffsetUs = 0;  //host time = device time + offset

    quint64 mFramesRead = 0;
    quint64 mBusEvents = 0;
    quint64 mOverruns = 0;
};

#endif // PLINMONITOR_H
