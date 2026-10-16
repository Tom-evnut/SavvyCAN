#include <QtTest/QtTest>
#include "apgdt001parser.h"

// The APGDT001 input report parser, fed hand-built reports (see apgdt001parser.h for the format).
// Checksums: ID 0x31 (PID 0xB1) with data 01..08 is 0xDB classic, 0x2A enhanced; 4A 55 93 E5 is 0xE6 classic
// (both checked independently in TestLinFrame).
class TestApgdt001Parser : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void testClassicFrame();
    void testEnhancedFrameDetected();
    void testTenBytesEndFrame();
    void testHeaderOnly();
    void testIncompleteResponse();
    void testChecksumError();
    void testParityError();
    void testStatusErrorEndsFrame();
    void testFlush();
    void testEmptyBreakAndStrayByte();
    void testTagsSplitAcrossReports();
    void testStatusPacketsAndEndOfReport();
    void testUnknownPacketStopsReport();
    void testUnknownTagDropsStream();
    void testDeviceTimestamps();
    void testTimerRollover();
    void testNoMarkerUsesHostTime();
    void testMeasuredBaudAndScriptStatus();
    void testReset();

private:
    int feed(const QList<QByteArray> &packets, qint64 hostUs = 1000000);

    Apgdt001Parser parser;
    QList<CommFrame> frames;
};

// one receive stream packet
static QByteArray stream(const QByteArray &tags)
{
    QByteArray packet;
    packet.append(char(0x85));
    packet.append(char(tags.size()));
    packet.append(tags);
    return packet;
}

// 0x10 tags for bytes from the bus
static QByteArray busBytes(const QByteArray &bytes)
{
    QByteArray tags;
    for (char b : bytes) {
        tags.append(char(0x10));
        tags.append(b);
    }
    return tags;
}

static QByteArray brk() { return QByteArray(1, char(0x83)); }

static QByteArray marker(quint16 ticks)
{
    QByteArray tag(1, char(0x13));
    tag.append(char(ticks & 0xFF));
    tag.append(char(ticks >> 8));
    return tag;
}

void TestApgdt001Parser::init()
{
    parser = Apgdt001Parser();
    frames.clear();
}

// a report: the packets, the end marker, zero padding to 64 bytes
int TestApgdt001Parser::feed(const QList<QByteArray> &packets, qint64 hostUs)
{
    QByteArray report;
    for (const QByteArray &p : packets) report.append(p);
    if (report.size() < Apgdt001Parser::REPORT_SIZE) report.append(char(0x80));
    report.append(QByteArray(qMax(0, int(Apgdt001Parser::REPORT_SIZE - report.size())), '\0'));
    return parser.feed(report.constData(), int(report.size()), CommFrame::TimeStamp(0, hostUs), frames);
}

void TestApgdt001Parser::testClassicFrame()
{
    QCOMPARE(feed({stream(brk() + marker(100) + busBytes(QByteArray::fromHex("B10102030405060708")))}), 0);
    QCOMPARE(feed({stream(busBytes(QByteArray::fromHex("DB")))}), 1); // 10 bytes: done without the next break

    const CommFrame &f = frames.at(0);
    QCOMPARE(f.frameType(), CommFrame::LINDataFrame);
    QCOMPARE(f.frameId(), 0x31u);
    QCOMPARE(f.payload(), QByteArray::fromHex("0102030405060708"));
    QVERIFY(f.isValid());
    QVERIFY(f.isDataFrame());
    QVERIFY(f.isReceived());
    QCOMPARE(int(f.getBus()), 0);
    QCOMPARE(int(f.linChecksumByte()), 0xDB);
    QCOMPARE(f.linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(f.linChecksumOk());
    QCOMPARE(int(f.linPid()), 0xB1);
    QVERIFY(!f.hasLinError());
    QVERIFY(f.hasLinDeviceTimestamp());
    QCOMPARE(parser.framesParsed(), 1ull);
}

void TestApgdt001Parser::testEnhancedFrameDetected()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("B101020304050607082A")))});
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).linChecksumModel(), CommFrame::LinChecksumEnhanced);
    QVERIFY(frames.at(0).linChecksumOk());
}

void TestApgdt001Parser::testTenBytesEndFrame()
{
    // a tenth byte doesn't belong to the frame
    feed({stream(brk() + busBytes(QByteArray::fromHex("B10102030405060708DB")) + busBytes("\x55"))});
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).payload().size(), 8);
    QCOMPARE(parser.strayBytes(), 1ull);
}

void TestApgdt001Parser::testHeaderOnly()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("C1")) + brk())});
    QCOMPARE(frames.size(), 1);
    const CommFrame &f = frames.at(0);
    QCOMPARE(f.frameId(), 0x01u);
    QVERIFY(f.isLinHeaderOnly());
    QVERIFY(!f.isDataFrame());
    QVERIFY(f.isValid());
    QVERIFY(!f.hasLinChecksum());
    QVERIFY(!f.hasLinError());
    QCOMPARE(parser.headerOnlyFrames(), 1ull);
}

void TestApgdt001Parser::testIncompleteResponse()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("C1AA")) + brk())});
    QCOMPARE(frames.size(), 1);
    const CommFrame &f = frames.at(0);
    QCOMPARE(f.payload(), QByteArray::fromHex("AA"));
    QVERIFY(!f.hasLinChecksum());
    QVERIFY(f.linErrors() & CommFrame::LinErrIncompleteResponse);
    QVERIFY(f.hasLinError());
}

void TestApgdt001Parser::testChecksumError()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("504A5593E500")) + brk())});
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("4A5593E5"));
    QVERIFY(frames.at(0).hasLinChecksumError());
    QCOMPARE(parser.checksumErrorFrames(), 1ull);

    feed({stream(busBytes(QByteArray::fromHex("504A5593E5E6")) + brk())});
    QCOMPARE(frames.size(), 2);
    QVERIFY(frames.at(1).linChecksumOk());
    QCOMPARE(parser.checksumErrorFrames(), 1ull);
}

void TestApgdt001Parser::testParityError()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("41")) + brk())}); // ID 1 with P1 flipped (0xC1 is right)
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0).frameId(), 0x01u);
    QCOMPARE(int(frames.at(0).linPid()), 0x41);
    QVERIFY(frames.at(0).hasLinParityError());
    QCOMPARE(parser.parityErrorFrames(), 1ull);
}

void TestApgdt001Parser::testStatusErrorEndsFrame()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("504A55")) + QByteArray::fromHex("8201"))});
    QCOMPARE(frames.size(), 1);
    QVERIFY(frames.at(0).linErrors() & CommFrame::LinErrDevice);
    QCOMPARE(parser.statusErrors(), 1ull);

    // outside a frame it's only counted
    feed({stream(QByteArray::fromHex("8201"))});
    QCOMPARE(frames.size(), 1);
    QCOMPARE(parser.statusErrors(), 2ull);
}

void TestApgdt001Parser::testFlush()
{
    QCOMPARE(feed({stream(brk() + busBytes(QByteArray::fromHex("504A5593E5E6")))}), 0);
    QCOMPARE(parser.flush(CommFrame::TimeStamp(0, 2000000), frames), 1);
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("4A5593E5"));
    QVERIFY(frames.at(0).linChecksumOk());
    QCOMPARE(frames.at(0).timeStamp().microSeconds(), 2000000); // no marker: host time
    QCOMPARE(parser.flush(CommFrame::TimeStamp(0, 2000000), frames), 0);
}

void TestApgdt001Parser::testEmptyBreakAndStrayByte()
{
    feed({stream(busBytes("\x11") + brk() + brk())});
    QCOMPARE(frames.size(), 0);
    QCOMPARE(parser.strayBytes(), 1ull);
    QCOMPARE(parser.emptyBreaks(), 1ull);
}

void TestApgdt001Parser::testTagsSplitAcrossReports()
{
    const QByteArray tags = brk() + marker(0x1234) + busBytes(QByteArray::fromHex("504A5593E5E6")) + brk();
    for (int split = 1; split < tags.size(); split++) {
        init();
        feed({stream(tags.left(split))});
        feed({stream(tags.mid(split))});
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("4A5593E5"));
        QVERIFY(frames.at(0).linChecksumOk());
        QVERIFY(frames.at(0).hasLinDeviceTimestamp());
    }
}

void TestApgdt001Parser::testStatusPacketsAndEndOfReport()
{
    QByteArray status81(3, '\0');
    status81[0] = char(0x81);
    QByteArray status84(7, '\0');
    status84[0] = char(0x84);
    QByteArray status88(2, '\0');
    status88[0] = char(0x88);
    feed({status81, stream(brk() + busBytes(QByteArray::fromHex("C1"))), status84, status88, stream(brk())});
    QCOMPARE(frames.size(), 1);
    QCOMPARE(parser.statusPackets(), 3ull);

    // nothing after the end marker is read
    QByteArray report = stream(brk() + busBytes(QByteArray::fromHex("C1")));
    report.append(char(0x80));
    report.append(stream(brk()));
    report.append(QByteArray(Apgdt001Parser::REPORT_SIZE - report.size(), '\0'));
    parser.feed(report.constData(), int(report.size()), CommFrame::TimeStamp(0, 1), frames);
    QCOMPARE(frames.size(), 1);
}

void TestApgdt001Parser::testUnknownPacketStopsReport()
{
    feed({QByteArray::fromHex("4201"), stream(brk() + busBytes(QByteArray::fromHex("C1")) + brk())});
    QCOMPARE(frames.size(), 0);
    QCOMPARE(parser.unknownPackets(), 1ull);

    // a stream packet longer than the report is cut short
    QByteArray report = QByteArray::fromHex("8550") + QByteArray(10, '\0');
    parser.feed(report.constData(), int(report.size()), CommFrame::TimeStamp(0, 1), frames);
    QCOMPARE(parser.unknownPackets(), 2ull);
}

void TestApgdt001Parser::testUnknownTagDropsStream()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("C1")) + QByteArray::fromHex("1F0102"))});
    QCOMPARE(parser.bytesSkipped(), 3ull);
    // the frame being built survives; the stream picks up again with the next report
    feed({stream(brk() + busBytes(QByteArray::fromHex("C1")) + brk())});
    QCOMPARE(frames.size(), 2);
    QVERIFY(frames.at(0).isLinHeaderOnly());
}

void TestApgdt001Parser::testDeviceTimestamps()
{
    // two frames 100 ticks apart; the report arrives at host time 5 s
    feed({stream(brk() + marker(1000) + busBytes(QByteArray::fromHex("C1")) + brk() + marker(1100)
                 + busBytes(QByteArray::fromHex("C1")) + brk())},
         5000000);
    QCOMPARE(frames.size(), 2);
    const qint64 t0 = frames.at(0).timeStamp().microSeconds();
    const qint64 t1 = frames.at(1).timeStamp().microSeconds();
    QCOMPARE(t1 - t0, qint64(100 * Apgdt001Parser::TICK_US));
    QVERIFY(t1 <= 5000000); // the latest marker maps no later than the report's arrival
    QVERIFY(frames.at(0).hasLinDeviceTimestamp());

    // a later report with more latency doesn't move the mapping
    feed({stream(brk() + marker(2100) + busBytes(QByteArray::fromHex("C1")) + brk())}, 5000000 + 1000 * 409 + 3000);
    QCOMPARE(frames.at(2).timeStamp().microSeconds() - t1, qint64(1000 * Apgdt001Parser::TICK_US));
}

void TestApgdt001Parser::testTimerRollover()
{
    feed({stream(brk() + marker(0xFFF0) + busBytes(QByteArray::fromHex("C1")) + brk()
                 + QByteArray(1, char(0x14)) + marker(0x0010) + busBytes(QByteArray::fromHex("C1")) + brk())});
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(1).timeStamp().microSeconds() - frames.at(0).timeStamp().microSeconds(),
             qint64(0x20 * Apgdt001Parser::TICK_US));
}

void TestApgdt001Parser::testNoMarkerUsesHostTime()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("C1")) + brk())}, 7000000);
    QCOMPARE(frames.size(), 1);
    QVERIFY(!frames.at(0).hasLinDeviceTimestamp());
    QCOMPARE(frames.at(0).timeStamp().microSeconds(), 7000000);
}

void TestApgdt001Parser::testMeasuredBaudAndScriptStatus()
{
    feed({stream(QByteArray::fromHex("85004B") + QByteArray::fromHex("1C77") + QByteArray::fromHex("1C01"))});
    QCOMPARE(parser.measuredBaud(), 19200);
    QCOMPARE(parser.scriptsFinished(), 1ull);
}

void TestApgdt001Parser::testReset()
{
    feed({stream(brk() + busBytes(QByteArray::fromHex("C1")) + QByteArray(1, char(0x13)))}); // frame + half a tag
    parser.reset();
    feed({stream(brk())});
    QCOMPARE(frames.size(), 0);
    QCOMPARE(parser.emptyBreaks(), 0ull); // the frame was dropped, and the half tag with it
}

QTEST_MAIN(TestApgdt001Parser)
#include "test_apgdt001_parser.moc"
