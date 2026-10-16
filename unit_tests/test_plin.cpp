#include <QtTest/QtTest>
#include "plinmonitor.h"
#include "plindll.h"

// The PEAK LIN monitor (PlinMonitor) against a fake PLIN-API backend: how a channel is set up, how received messages
// become LIN frames, timestamps, bus events and failures. Checksums: ID 0x31 (PID 0xB1) with data 01..08 is 0xDB
// classic, 0x2A enhanced (checked independently in TestLinFrame).
class FakePlin : public PlinBackend
{
public:
    bool loaded = true;
    QList<PlinHardware> hw;
    QStringList calls;
    QList<PlinRcvMsg> queue;
    quint32 failInit = Plin::errOK;
    quint32 failRead = Plin::errOK;

    bool isLoaded() const override { return loaded; }
    QString loadError() const override { return "no PLIN driver"; }
    QList<PlinHardware> hardware() override { return hw; }
    quint32 registerClient(const QString &name, quint8 *client) override
    {
        calls << "register " + name;
        *client = 7;
        return Plin::errOK;
    }
    quint32 removeClient(quint8 client) override { calls << QString("remove %1").arg(client); return Plin::errOK; }
    quint32 connectClient(quint8 client, quint16 h) override { calls << QString("connect %1 %2").arg(client).arg(h); return Plin::errOK; }
    quint32 disconnectClient(quint8 client, quint16 h) override { calls << QString("disconnect %1 %2").arg(client).arg(h); return Plin::errOK; }
    quint32 initializeHardware(quint8 client, quint16 h, quint8 mode, quint16 baud) override
    {
        calls << QString("init %1 %2 mode %3 %4").arg(client).arg(h).arg(mode).arg(baud);
        return failInit;
    }
    quint32 setFrameEntry(quint8, quint16, quint8 id, quint8 length, quint8 direction, quint8 checksumType) override
    {
        calls << QString("entry %1 len %2 dir %3 cs %4").arg(id).arg(length).arg(direction).arg(checksumType);
        return Plin::errOK;
    }
    quint32 setClientFilter(quint8, quint16, quint64 mask) override
    {
        calls << QString("filter %1").arg(mask, 0, 16);
        return Plin::errOK;
    }
    QList<PlinMsg> written;
    quint32 failWrite = Plin::errOK;
    quint32 write(quint8 client, quint16 h, const PlinMsg &msg) override
    {
        calls << QString("write %1 %2").arg(client).arg(h);
        if (failWrite != Plin::errOK) return failWrite;
        written << msg;
        return Plin::errOK;
    }
    quint32 read(quint8, PlinRcvMsg *msg) override
    {
        if (failRead != Plin::errOK) return failRead;
        if (queue.isEmpty()) return Plin::errRcvQueueEmpty;
        *msg = queue.takeFirst();
        return Plin::errOK;
    }
    QString errorText(quint32 error) override { return QString("error %1").arg(error); }
};

class TestPlin : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void testPortNames();
    void testStart();
    void testStartFailures();
    void testToFrame();
    void testToFrameErrors();
    void testPoll();
    void testReadFailure();
    void testChangeBaudrate();
    void testStop();
    void testDllWithoutDriver();
    void testMaster();
    void testSend();

private:
    FakePlin fake;
};

static PlinHardware hardware(quint16 handle, const QString &name, int device, int channel)
{
    PlinHardware hw;
    hw.handle = handle;
    hw.name = name;
    hw.device = device;
    hw.channel = channel;
    return hw;
}

static PlinRcvMsg message(quint8 pid, const QByteArray &data, quint8 checksum, quint64 timeUs, quint32 errors = 0)
{
    PlinRcvMsg msg;
    msg.type = Plin::mstStandard;
    msg.frameId = pid;
    msg.length = quint8(data.size());
    for (int i = 0; i < data.size() && i < 8; i++) msg.data[i] = quint8(data.at(i));
    msg.checksum = checksum;
    msg.errorFlags = errors;
    msg.timestampUs = timeUs;
    return msg;
}

void TestPlin::init()
{
    fake = FakePlin();
    fake.hw << hardware(11, "PLIN-USB", 1, 1) << hardware(12, "PCAN-USB Pro FD", 2, 1) << hardware(13, "PCAN-USB Pro FD", 2, 2);
}

void TestPlin::testPortNames()
{
    QCOMPARE(PlinMonitor::portName(fake.hw.at(2)), QString("PCAN-USB Pro FD, device 2, channel 2"));
    QCOMPARE(PlinMonitor::findHardware(fake.hw, "")->handle, quint16(11));
    QCOMPARE(PlinMonitor::findHardware(fake.hw, "PCAN-USB Pro FD, device 2, channel 2")->handle, quint16(13));
    QCOMPARE(PlinMonitor::findHardware(fake.hw, "renamed, device 2, channel 1")->handle, quint16(12)); // by numbers
    QVERIFY(!PlinMonitor::findHardware(fake.hw, "PLIN-USB, device 3, channel 1"));
    QVERIFY(!PlinMonitor::findHardware(fake.hw, "COM3"));
    QVERIFY(!PlinMonitor::findHardware({}, ""));
}

void TestPlin::testStart()
{
    PlinMonitor monitor(fake);
    QString error;
    QVERIFY2(monitor.start("PCAN-USB Pro FD, device 2, channel 2", 19200, &error), qPrintable(error));
    QVERIFY(monitor.isRunning());
    QCOMPARE(monitor.currentHardware().handle, quint16(13));
    QCOMPARE(fake.calls.at(0), QString("register SavvyCAN"));
    QCOMPARE(fake.calls.at(1), QString("connect 7 13"));
    QCOMPARE(fake.calls.at(2), QString("init 7 13 mode 1 19200")); // a slave: it never sends headers
    for (int id = 0; id <= 63; id++) // every ID a subscriber with automatic length and checksum: it never answers
        QCOMPARE(fake.calls.at(3 + id), QString("entry %1 len 8 dir 3 cs 3").arg(id));
    QCOMPARE(fake.calls.at(67), QString("filter ffffffffffffffff"));
    QCOMPARE(fake.calls.size(), 68);
}

void TestPlin::testStartFailures()
{
    QString error;
    {
        fake.loaded = false;
        PlinMonitor monitor(fake);
        QVERIFY(!monitor.start("", 19200, &error));
        QCOMPARE(error, QString("no PLIN driver"));
        fake.loaded = true;
    }
    {
        PlinMonitor monitor(fake);
        QVERIFY(!monitor.start("", 25000, &error));
        QVERIFY(error.contains("isn't a LIN bit rate"));
        QVERIFY(!monitor.start("PLIN-USB, device 9, channel 1", 19200, &error));
        QVERIFY(error.contains("not found"));
        QVERIFY(fake.calls.isEmpty());
    }
    {
        fake.failInit = 11; // errIllegalBaudrate
        PlinMonitor monitor(fake);
        QVERIFY(!monitor.start("", 19200, &error));
        QVERIFY2(error.contains("can't initialise the channel: error 11"), qPrintable(error));
        QVERIFY(!monitor.isRunning());
        QVERIFY(fake.calls.contains("disconnect 7 11")); // the client is given back
        QVERIFY(fake.calls.contains("remove 7"));
    }
    {
        FakePlin none;
        PlinMonitor monitor(none);
        QVERIFY(!monitor.start("", 19200, &error));
        QCOMPARE(error, QString("no PEAK LIN channel found"));
    }
}

void TestPlin::testToFrame()
{
    CommFrame f = PlinMonitor::toFrame(message(0xB1, QByteArray::fromHex("0102030405060708"), 0xDB, 0));
    QVERIFY(f.isLin());
    QCOMPARE(f.frameId(), 0x31u);
    QCOMPARE(f.payload(), QByteArray::fromHex("0102030405060708"));
    QCOMPARE(int(f.linChecksumByte()), 0xDB);
    QCOMPARE(f.linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(f.linChecksumOk());
    QCOMPARE(int(f.linPid()), 0xB1);
    QVERIFY(!f.hasLinError());
    QVERIFY(f.hasLinDeviceTimestamp());
    QVERIFY(f.isValid());
    QCOMPARE(int(f.getBus()), 0);

    CommFrame enhanced = PlinMonitor::toFrame(message(0xB1, QByteArray::fromHex("0102030405060708"), 0x2A, 0));
    QCOMPARE(enhanced.linChecksumModel(), CommFrame::LinChecksumEnhanced);

    CommFrame bad = PlinMonitor::toFrame(message(0xB1, QByteArray::fromHex("0102030405060708"), 0x00, 0, Plin::MSG_ERR_CHECKSUM));
    QVERIFY(bad.hasLinChecksumError());

    // a header nobody answered
    CommFrame header = PlinMonitor::toFrame(message(0xC1, QByteArray::fromHex("0000"), 0, 0, Plin::MSG_ERR_SLAVE_NOT_RESPONDING));
    QVERIFY(header.isLinHeaderOnly());
    QVERIFY(!header.hasLinChecksum());
    QVERIFY(!header.hasLinError()); // normal on a bus

    PlinRcvMsg tooLong = message(0xB1, QByteArray::fromHex("0102030405060708"), 0xDB, 0);
    tooLong.length = 12;
    QCOMPARE(PlinMonitor::toFrame(tooLong).payload().size(), 8);
}

void TestPlin::testToFrameErrors()
{
    const QByteArray data = QByteArray::fromHex("4A5593E5");
    QVERIFY(PlinMonitor::toFrame(message(0x50, data, 0xE6, 0, Plin::MSG_ERR_INCONSISTENT_SYNC)).linErrors() & CommFrame::LinErrSync);
    QVERIFY(PlinMonitor::toFrame(message(0x50, data, 0xE6, 0, Plin::MSG_ERR_TIMEOUT)).linErrors() & CommFrame::LinErrIncompleteResponse);
    QVERIFY(PlinMonitor::toFrame(message(0x50, data, 0xE6, 0, Plin::MSG_ERR_GND_SHORT)).linErrors() & CommFrame::LinErrDevice);
    QVERIFY(PlinMonitor::toFrame(message(0x50, data, 0xE6, 0, Plin::MSG_ERR_VBAT_SHORT)).linErrors() & CommFrame::LinErrDevice);
    CommFrame parity = PlinMonitor::toFrame(message(0x10, data, 0xE6, 0, Plin::MSG_ERR_ID_PARITY_BIT0));
    QVERIFY(parity.hasLinParityError());
    QCOMPARE(parity.frameId(), 0x10u);
    QVERIFY(PlinMonitor::toFrame(message(0x10, data, 0xE6, 0)).hasLinParityError()); // 0x10 is ID 0x10 with bad parity (PID 0x50)
}

void TestPlin::testPoll()
{
    PlinMonitor monitor(fake);
    QVERIFY(monitor.start("", 19200, nullptr));
    QList<CommFrame> frames;
    PlinRcvMsg sleep;
    sleep.type = Plin::mstBusSleep;
    PlinRcvMsg overrun;
    overrun.type = Plin::mstQueueOverrun;
    fake.queue << message(0xB1, QByteArray::fromHex("0102030405060708"), 0xDB, 1000000) << sleep
               << message(0xC1, QByteArray(), 0, 1010000, Plin::MSG_ERR_SLAVE_NOT_RESPONDING) << overrun;

    QCOMPARE(monitor.poll(CommFrame::TimeStamp(0, 5000000), frames), 2);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(monitor.framesRead(), 2ull);
    QCOMPARE(monitor.busEvents(), 1ull);
    QCOMPARE(monitor.overruns(), 1ull);
    // the latest frame maps to the read's time, and frames keep the device's spacing
    QCOMPARE(frames.at(1).timeStamp().microSeconds(), 5000000ll);
    QCOMPARE(frames.at(0).timeStamp().microSeconds(), 4990000ll);

    // a later read with more latency keeps the mapping
    fake.queue << message(0xB1, QByteArray::fromHex("0102030405060708"), 0xDB, 1100000);
    QCOMPARE(monitor.poll(CommFrame::TimeStamp(0, 5093000), frames), 1);
    QCOMPARE(frames.at(2).timeStamp().microSeconds(), 5090000ll);

    // an empty queue reads nothing
    QCOMPARE(monitor.poll(CommFrame::TimeStamp(0, 6000000), frames), 0);
    QVERIFY(monitor.isRunning());

    // reads are bounded
    for (int i = 0; i < 5; i++) fake.queue << message(0xB1, QByteArray::fromHex("01"), 0xFE, 2000000 + i);
    QCOMPARE(monitor.poll(CommFrame::TimeStamp(0, 7000000), frames, nullptr, 3), 3);
    QCOMPARE(fake.queue.size(), 2);
}

void TestPlin::testReadFailure()
{
    PlinMonitor monitor(fake);
    QVERIFY(monitor.start("", 19200, nullptr));
    fake.calls.clear();
    fake.failRead = 1003; // errManagerNotResponding
    QList<CommFrame> frames;
    QString error;
    QCOMPARE(monitor.poll(CommFrame::TimeStamp(0, 1), frames, &error), -1);
    QVERIFY2(error.contains("reading failed: error 1003"), qPrintable(error));
    QVERIFY(!monitor.isRunning());
    QCOMPARE(fake.calls, QStringList({"disconnect 7 11", "remove 7"}));
    QCOMPARE(monitor.poll(CommFrame::TimeStamp(0, 1), frames, &error), 0); // stopped: nothing more
}

void TestPlin::testChangeBaudrate()
{
    PlinMonitor monitor(fake);
    QString error;
    QVERIFY(!monitor.changeBaudrate(9600, &error)); // not running
    QVERIFY(monitor.start("", 19200, nullptr));
    fake.calls.clear();
    QVERIFY(monitor.changeBaudrate(10417, &error));
    QCOMPARE(fake.calls.first(), QString("init 7 11 mode 1 10417"));
    QCOMPARE(fake.calls.size(), 66); // and the frame entries and filter again
    QVERIFY(!monitor.changeBaudrate(500, &error));
    QVERIFY(monitor.isRunning());
}

void TestPlin::testStop()
{
    {
        PlinMonitor monitor(fake);
        QVERIFY(monitor.start("", 19200, nullptr));
        fake.calls.clear();
        monitor.stop();
        monitor.stop();
        QCOMPARE(fake.calls, QStringList({"disconnect 7 11", "remove 7"}));
        fake.calls.clear();
        QVERIFY(monitor.start("", 19200, nullptr));
        fake.calls.clear();
    } // the destructor stops too
    QCOMPARE(fake.calls, QStringList({"disconnect 7 11", "remove 7"}));
}

void TestPlin::testDllWithoutDriver()
{
    // here (Linux, no PEAK driver) the DLL backend reports why it can't be used, and every call fails cleanly
    PlinDll &dll = PlinDll::instance();
    QVERIFY(!dll.isLoaded());
    QVERIFY(!dll.loadError().isEmpty());
    QVERIFY(dll.hardware().isEmpty());
    quint8 client = 0;
    QVERIFY(dll.registerClient("x", &client) != Plin::errOK);
    PlinMonitor monitor(dll);
    QString error;
    QVERIFY(!monitor.start("", 19200, &error));
    QCOMPARE(error, dll.loadError());
}

void TestPlin::testMaster()
{
    PlinMonitor monitor(fake);
    QVERIFY(monitor.start("", 19200, nullptr, true));
    QVERIFY(monitor.isMaster());
    QCOMPARE(fake.calls.at(2), QString("init 7 11 mode 2 19200")); // a master
    fake.calls.clear();
    QVERIFY(monitor.setMaster(false, nullptr));
    QCOMPARE(fake.calls.first(), QString("init 7 11 mode 1 19200")); // back to listening, same bit rate
    QVERIFY(!monitor.isMaster());
    fake.calls.clear();
    QVERIFY(monitor.setMaster(false, nullptr)); // no change: nothing done
    QVERIFY(fake.calls.isEmpty());
    QVERIFY(monitor.changeBaudrate(9600, nullptr));
    QVERIFY(monitor.setMaster(true, nullptr));
    QCOMPARE(fake.calls.last(), QString("filter ffffffffffffffff"));
    QVERIFY(fake.calls.contains("init 7 11 mode 2 9600"));
}

void TestPlin::testSend()
{
    PlinMonitor monitor(fake);
    QString error;
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(0x31);
    frame.setPayload(QByteArray::fromHex("0102030405060708"));
    frame.setLinChecksum(0x55); // wrong on purpose: sent as given

    QVERIFY(monitor.start("", 19200, nullptr, false));
    QVERIFY(!monitor.send(frame, &error)); // listening only
    QVERIFY(error.contains("isn't a master"));
    QVERIFY(fake.written.isEmpty());

    QVERIFY(monitor.setMaster(true, nullptr));
    QVERIFY2(monitor.send(frame, &error), qPrintable(error));
    QCOMPARE(fake.written.size(), 1);
    const PlinMsg m = fake.written.first();
    QCOMPARE(int(m.frameId), 0xB1); // the protected ID
    QCOMPARE(int(m.length), 8);
    QCOMPARE(int(m.direction), int(Plin::dirPublisher));
    QCOMPARE(int(m.checksumType), int(Plin::cstCustom));
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(m.data), 8), QByteArray::fromHex("0102030405060708"));
    QCOMPARE(int(m.checksum), 0x55);

    CommFrame header(CommFrame::LINDataFrame);
    header.setFrameId(0x01);
    QVERIFY(monitor.send(header, &error));
    const PlinMsg h = fake.written.last();
    QCOMPARE(int(h.frameId), 0xC1);
    QCOMPARE(int(h.direction), int(Plin::dirSubscriberAutoLength)); // the header alone: a slave answers
    QCOMPARE(int(h.checksumType), int(Plin::cstAuto));
    QVERIFY(h.length >= 1 && h.length <= 8);

    fake.failWrite = 1; // errXmtQueueFull
    QVERIFY(!monitor.send(frame, &error));
    QVERIFY2(error.contains("sending failed: error 1"), qPrintable(error));
    QVERIFY(monitor.isRunning()); // a failed send doesn't stop listening

    monitor.stop();
    QVERIFY(!monitor.send(frame, &error));
    QCOMPARE(error, QString("not connected"));
}

QTEST_MAIN(TestPlin)
#include "test_plin.moc"
