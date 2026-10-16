#include <QtTest/QtTest>
#include "apgdt001session.h"

// Apgdt001Session: the reports the APGDT001 connection sends, the configuration read-back check and the quiet-bus
// flush, without USB (see apgdt001session.h for the protocol).
class TestApgdt001Session : public QObject
{
    Q_OBJECT

private slots:
    void testDivisor_data();
    void testDivisor();
    void testControlBlock();
    void testReports();
    void testStartAndChangeBaudCommands();
    void testCheckOk();
    void testCheckMismatch();
    void testCheckTimeout();
    void testEarlierReplyDoesNotCount();
    void testQuietBusFlush();
    void testQuietTime();
};

// a status reply: 81, 82 (the 24 echoed configuration bytes), 83, 84, end of report
static QByteArray statusReply(const QByteArray &block24)
{
    QByteArray r = QByteArray::fromHex("810000");
    r.append(char(0x82));
    r.append(block24.left(24));
    r.append(char(0x83));
    r.append(QByteArray(20, '\0'));
    r.append(char(0x84));
    r.append(QByteArray(6, '\0'));
    r.append(char(0x80));
    r.append(QByteArray(Apgdt001Parser::REPORT_SIZE - r.size(), '\0'));
    return r;
}

static QByteArray streamReport(const QByteArray &tags)
{
    QByteArray r(1, char(0x85));
    r.append(char(tags.size()));
    r.append(tags);
    r.append(char(0x80));
    r.append(QByteArray(Apgdt001Parser::REPORT_SIZE - r.size(), '\0'));
    return r;
}

static int process(Apgdt001Session &s, const QByteArray &report, qint64 nowUs, QList<CommFrame> &frames)
{
    return s.processReport(report.constData(), int(report.size()), nowUs, CommFrame::TimeStamp(0, nowUs), frames);
}

void TestApgdt001Session::testDivisor_data()
{
    QTest::addColumn<int>("baud");
    QTest::addColumn<int>("divisor");
    QTest::addColumn<int>("actual");

    QTest::newRow("19200") << 19200 << 259 << 19231;
    QTest::newRow("10417") << 10417 << 479 << 10417;
    QTest::newRow("9600") << 9600 << 520 << 9597;
    QTest::newRow("2400") << 2400 << 2082 << 2400;
    QTest::newRow("20000") << 20000 << 249 << 20000;
}

void TestApgdt001Session::testDivisor()
{
    QFETCH(int, baud);
    QFETCH(int, divisor);
    QFETCH(int, actual);
    QCOMPARE(Apgdt001Session::divisorForBaud(baud), divisor);
    QCOMPARE(Apgdt001Session::baudForDivisor(divisor), actual);
    // Microchip's default block (divisor 0x01F3) is 10 kbit/s
    QCOMPARE(Apgdt001Session::baudForDivisor(0x01F3), 10000);
}

void TestApgdt001Session::testControlBlock()
{
    const QByteArray block = Apgdt001Session::controlBlock(19200);
    QCOMPARE(block.size(), Apgdt001Session::CONTROL_BLOCK_SIZE);
    QCOMPARE(quint8(block.at(0)), quint8(0xC0));
    QCOMPARE(int(block.at(3)), 10);  // flush after 10 bytes
    QCOMPARE(int(block.at(4)), 24);  // or 24 ticks of 0.409 ms
    QCOMPARE(quint8(block.at(8)), quint8(0x0A));
    QCOMPARE(quint8(block.at(9)), quint8(0x33));
    QCOMPARE(quint8(block.at(10)), quint8(0x20));
    QCOMPARE(quint8(block.at(16)), quint8(0xC8));
    QCOMPARE(quint8(block.at(17)), quint8(0x98));
    QCOMPARE(quint8(block.at(18)), quint8(0x0F));
    QCOMPARE(quint8(block.at(22)), quint8(259 & 0xFF));
    QCOMPARE(quint8(block.at(23)), quint8(259 >> 8));
    QCOMPARE(block.count('\0'), Apgdt001Session::CONTROL_BLOCK_SIZE - 11);
}

void TestApgdt001Session::testReports()
{
    QCOMPARE(Apgdt001Session::coldResetReport(), QByteArray::fromHex("0100") + QByteArray(62, '\0'));
    QCOMPARE(Apgdt001Session::warmResetReport().left(2), QByteArray::fromHex("0101"));
    QCOMPARE(Apgdt001Session::statusRequestReport().left(2), QByteArray::fromHex("0102"));
    QCOMPARE(Apgdt001Session::commClearReport().left(2), QByteArray::fromHex("0107"));

    const QByteArray block = Apgdt001Session::controlBlock(10417);
    const QByteArray report = Apgdt001Session::controlBlockReport(block);
    QCOMPARE(report.size(), Apgdt001Parser::REPORT_SIZE);
    QCOMPARE(quint8(report.at(0)), quint8(0x02));
    QCOMPARE(report.mid(1, Apgdt001Session::CONTROL_BLOCK_SIZE), block);
    QCOMPARE(report.mid(1 + Apgdt001Session::CONTROL_BLOCK_SIZE), QByteArray(64 - 30, '\0'));
}

void TestApgdt001Session::testStartAndChangeBaudCommands()
{
    Apgdt001Session s;
    const QList<QByteArray> start = s.startCommands(19200, 0);
    QCOMPARE(start.size(), 4);
    QCOMPARE(start.at(0), Apgdt001Session::coldResetReport());
    QCOMPARE(start.at(1), Apgdt001Session::controlBlockReport(Apgdt001Session::controlBlock(19200)));
    QCOMPARE(start.at(2), Apgdt001Session::warmResetReport());
    QCOMPARE(start.at(3), Apgdt001Session::statusRequestReport());
    QCOMPARE(s.check(), Apgdt001Session::Check::Pending);
    QCOMPARE(s.baud(), 19200);

    const QList<QByteArray> change = s.changeBaudCommands(9600, 0);
    QCOMPARE(change.size(), 3);
    QCOMPARE(change.at(0), Apgdt001Session::controlBlockReport(Apgdt001Session::controlBlock(9600)));
    QCOMPARE(change.at(1), Apgdt001Session::commClearReport());
    QCOMPARE(change.at(2), Apgdt001Session::statusRequestReport());
    QCOMPARE(s.baud(), 9600);
}

void TestApgdt001Session::testCheckOk()
{
    Apgdt001Session s;
    QList<CommFrame> frames;
    s.startCommands(19200, 0);
    QVERIFY(!s.takeCheckResult());
    process(s, statusReply(Apgdt001Session::controlBlock(19200)), 1000, frames);
    QCOMPARE(s.check(), Apgdt001Session::Check::Ok);
    QVERIFY(s.checkMessage().contains("19200"));
    QVERIFY(s.takeCheckResult());
    QVERIFY(!s.takeCheckResult()); // reported once
}

void TestApgdt001Session::testCheckMismatch()
{
    Apgdt001Session s;
    QList<CommFrame> frames;
    s.startCommands(19200, 0);
    QByteArray echoed = Apgdt001Session::controlBlock(19200);
    echoed[22] = char(0xF3); // the device kept another divisor
    process(s, statusReply(echoed), 1000, frames);
    QCOMPARE(s.check(), Apgdt001Session::Check::Failed);
    QVERIFY(s.checkMessage().contains("byte 22"));
    QVERIFY(s.checkMessage().contains("f3"));
    QVERIFY(s.takeCheckResult());
}

void TestApgdt001Session::testCheckTimeout()
{
    Apgdt001Session s;
    QList<CommFrame> frames;
    s.startCommands(19200, 0);
    s.poll(Apgdt001Session::STATUS_TIMEOUT_US, CommFrame::TimeStamp(0, 0), frames);
    QCOMPARE(s.check(), Apgdt001Session::Check::Pending);
    s.poll(Apgdt001Session::STATUS_TIMEOUT_US + 1, CommFrame::TimeStamp(0, 0), frames);
    QCOMPARE(s.check(), Apgdt001Session::Check::Failed);
    QVERIFY(s.checkMessage().contains("no reply"));
}

void TestApgdt001Session::testEarlierReplyDoesNotCount()
{
    Apgdt001Session s;
    QList<CommFrame> frames;
    process(s, statusReply(QByteArray(24, '\0')), 0, frames); // before any request
    s.startCommands(19200, 10);
    QCOMPARE(s.check(), Apgdt001Session::Check::Pending);
    process(s, statusReply(Apgdt001Session::controlBlock(19200)), 20, frames);
    QCOMPARE(s.check(), Apgdt001Session::Check::Ok);
}

void TestApgdt001Session::testQuietBusFlush()
{
    Apgdt001Session s;
    QList<CommFrame> frames;
    s.startCommands(19200, 0);
    // a frame with no break after it: ID 0x10 (PID 0x50), 4A 55 93 E5, classic checksum E6
    const QByteArray tags = QByteArray::fromHex("83") + QByteArray::fromHex("1050104A1055109310E510E6");
    QCOMPARE(process(s, streamReport(tags), 100000, frames), 0);
    QCOMPARE(s.poll(100000 + s.quietUs(), CommFrame::TimeStamp(0, 0), frames), 0);
    QCOMPARE(s.poll(100000 + s.quietUs() + 1, CommFrame::TimeStamp(0, 0), frames), 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("4A5593E5"));
    QVERIFY(frames.at(0).linChecksumOk());

    // more bus data restarts the wait
    process(s, streamReport(QByteArray::fromHex("831050")), 500000, frames);
    process(s, streamReport(QByteArray::fromHex("104A")), 500000 + s.quietUs(), frames);
    QCOMPARE(s.poll(500000 + s.quietUs() + 1, CommFrame::TimeStamp(0, 0), frames), 0);
    QCOMPARE(s.poll(500000 + 2 * s.quietUs() + 1, CommFrame::TimeStamp(0, 0), frames), 1);
}

void TestApgdt001Session::testQuietTime()
{
    Apgdt001Session s;
    s.startCommands(19200, 0);
    // 124 bits + 40 % at 19200 bit/s, the 10 ms flush and 5 ms margin
    QCOMPARE(s.quietUs(), qint64(124 * 14 * 100000ll / 19200 + 15000));
    s.changeBaudCommands(2400, 0);
    QVERIFY(s.quietUs() > 80000);
}

QTEST_MAIN(TestApgdt001Session)
#include "test_apgdt001_session.moc"
