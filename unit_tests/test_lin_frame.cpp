#include <QtTest/QtTest>
#include "can_structs.h"

// LIN frames in CommFrame: protected ID parity, checksums, validity and the packed LIN details.
// Expected values were worked out independently of the code under test (the classic checksum example is the
// LIN 2.2A specification's).
class TestLinFrame : public QObject
{
    Q_OBJECT

private slots:
    void testPidFromId_data();
    void testPidFromId();
    void testPidParity();
    void testChecksum_data();
    void testChecksum();
    void testValidity();
    void testDataFrame();
    void testPayloadNeverCanFd();
    void testFrameTypeRoundTrip();
    void testVerifyChecksumKnownModel();
    void testVerifyChecksumDetectsModel();
    void testVerifyChecksumNothingToCheck();
    void testParityError();
    void testDetailsAreIndependent();
};

static CommFrame linFrame(quint8 id, const QByteArray &data)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(id);
    frame.setPayload(data);
    return frame;
}

void TestLinFrame::testPidFromId_data()
{
    QTest::addColumn<int>("id");
    QTest::addColumn<int>("pid");

    QTest::newRow("0x00") << 0x00 << 0x80;
    QTest::newRow("0x01") << 0x01 << 0xC1;
    QTest::newRow("0x02") << 0x02 << 0x42;
    QTest::newRow("0x03") << 0x03 << 0x03;
    QTest::newRow("0x10") << 0x10 << 0x50;
    QTest::newRow("0x31") << 0x31 << 0xB1;
    QTest::newRow("master request") << 0x3C << 0x3C;
    QTest::newRow("slave response") << 0x3D << 0x7D;
    QTest::newRow("0x3F") << 0x3F << 0xBF;
}

void TestLinFrame::testPidFromId()
{
    QFETCH(int, id);
    QFETCH(int, pid);
    QCOMPARE(int(CommFrame::linPidFromId(quint8(id))), pid);
    QCOMPARE(int(CommFrame::linPidFromId(quint8(id | 0xC0))), pid); // parity bits in the input are ignored
}

void TestLinFrame::testPidParity()
{
    for (int id = 0; id < 64; id++) {
        const quint8 pid = CommFrame::linPidFromId(quint8(id));
        QVERIFY(CommFrame::linPidParityOk(pid));
        QVERIFY(!CommFrame::linPidParityOk(pid ^ 0x40));
        QVERIFY(!CommFrame::linPidParityOk(pid ^ 0x80));
    }
}

void TestLinFrame::testChecksum_data()
{
    QTest::addColumn<int>("id");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("model");
    QTest::addColumn<int>("checksum");

    QTest::newRow("spec classic example") << 0x00 << QByteArray::fromHex("4A5593E5")
                                          << int(CommFrame::LinChecksumClassic) << 0xE6;
    QTest::newRow("classic") << 0x31 << QByteArray::fromHex("0102030405060708")
                             << int(CommFrame::LinChecksumClassic) << 0xDB;
    QTest::newRow("enhanced") << 0x31 << QByteArray::fromHex("0102030405060708")
                              << int(CommFrame::LinChecksumEnhanced) << 0x2A;
    QTest::newRow("carry wraps") << 0x00 << QByteArray::fromHex("FFFF") << int(CommFrame::LinChecksumClassic) << 0x00;
    QTest::newRow("zero byte") << 0x00 << QByteArray::fromHex("00") << int(CommFrame::LinChecksumClassic) << 0xFF;
    // diagnostic frames always use the classic checksum
    QTest::newRow("master request enhanced") << 0x3C << QByteArray::fromHex("1006B200FF7FFFFF")
                                             << int(CommFrame::LinChecksumEnhanced) << 0xB7;
    QTest::newRow("master request classic") << 0x3C << QByteArray::fromHex("1006B200FF7FFFFF")
                                            << int(CommFrame::LinChecksumClassic) << 0xB7;
}

void TestLinFrame::testChecksum()
{
    QFETCH(int, id);
    QFETCH(QByteArray, data);
    QFETCH(int, model);
    QFETCH(int, checksum);
    QCOMPARE(int(CommFrame::linChecksum(data, quint8(id), CommFrame::LinChecksumModel(model))), checksum);
}

void TestLinFrame::testValidity()
{
    QVERIFY(linFrame(0x00, QByteArray()).isValid()); // header without response
    QVERIFY(linFrame(0x3F, QByteArray(8, '\x55')).isValid());
    QVERIFY(!linFrame(0x40, QByteArray(2, '\x00')).isValid()); // IDs are 6 bits
    QVERIFY(!linFrame(0x10, QByteArray(9, '\x00')).isValid()); // at most 8 data bytes

    CommFrame extended = linFrame(0x10, QByteArray(2, '\x00'));
    extended.setExtendedFrameFormat(true);
    QVERIFY(!extended.isValid());
}

void TestLinFrame::testDataFrame()
{
    QVERIFY(linFrame(0x10, QByteArray::fromHex("01")).isDataFrame());
    QVERIFY(!linFrame(0x10, QByteArray()).isDataFrame());
    QVERIFY(linFrame(0x10, QByteArray()).isLinHeaderOnly());
    QVERIFY(!linFrame(0x10, QByteArray::fromHex("01")).isLinHeaderOnly());
    QVERIFY(linFrame(0x10, QByteArray()).isLin());

    CommFrame can(0x10, QByteArray::fromHex("01"));
    QVERIFY(can.isDataFrame());
    QVERIFY(!can.isLin());
    QVERIFY(!can.isLinHeaderOnly());
}

void TestLinFrame::testPayloadNeverCanFd()
{
    CommFrame frame = linFrame(0x10, QByteArray(12, '\x00'));
    QCOMPARE(frame.frameType(), CommFrame::LINDataFrame);
    QVERIFY(!frame.hasFlexibleDataRateFormat());
    QVERIFY(!frame.isValid()); // too long for LIN, but still LIN

    // CAN frames still become CAN FD frames
    CommFrame can(CommFrame::CANDataFrame);
    can.setPayload(QByteArray(12, '\x00'));
    QCOMPARE(can.frameType(), CommFrame::CANFDDataFrame);
    QVERIFY(can.hasFlexibleDataRateFormat());
}

void TestLinFrame::testFrameTypeRoundTrip()
{
    CommFrame frame(CommFrame::CANFDDataFrame);
    frame.setFrameType(CommFrame::LINDataFrame);
    QCOMPARE(frame.frameType(), CommFrame::LINDataFrame);
    QVERIFY(!frame.hasFlexibleDataRateFormat());
    QCOMPARE(frame.toQCanBusFrameType(), QCanBusFrame::DataFrame);
}

void TestLinFrame::testVerifyChecksumKnownModel()
{
    CommFrame frame = linFrame(0x31, QByteArray::fromHex("0102030405060708"));
    frame.setLinChecksumModel(CommFrame::LinChecksumEnhanced);
    frame.setLinChecksum(0x2A);
    QVERIFY(frame.verifyLinChecksum());
    QVERIFY(frame.linChecksumChecked());
    QVERIFY(frame.linChecksumOk());
    QVERIFY(!frame.hasLinChecksumError());
    QVERIFY(!frame.hasLinError());

    frame.setLinChecksum(0xDB); // the classic checksum, but the model says enhanced
    QVERIFY(frame.verifyLinChecksum());
    QVERIFY(frame.hasLinChecksumError());
    QVERIFY(frame.hasLinError());
    QCOMPARE(frame.linChecksumModel(), CommFrame::LinChecksumEnhanced);
}

void TestLinFrame::testVerifyChecksumDetectsModel()
{
    CommFrame classic = linFrame(0x31, QByteArray::fromHex("0102030405060708"));
    classic.setLinChecksum(0xDB);
    QVERIFY(classic.verifyLinChecksum());
    QCOMPARE(classic.linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(classic.linChecksumOk());

    CommFrame enhanced = linFrame(0x31, QByteArray::fromHex("0102030405060708"));
    enhanced.setLinChecksum(0x2A);
    QVERIFY(enhanced.verifyLinChecksum());
    QCOMPARE(enhanced.linChecksumModel(), CommFrame::LinChecksumEnhanced);
    QVERIFY(enhanced.linChecksumOk());

    CommFrame neither = linFrame(0x31, QByteArray::fromHex("0102030405060708"));
    neither.setLinChecksum(0x00);
    QVERIFY(neither.verifyLinChecksum());
    QCOMPARE(neither.linChecksumModel(), CommFrame::LinChecksumUnknown);
    QVERIFY(neither.hasLinChecksumError());
}

void TestLinFrame::testVerifyChecksumNothingToCheck()
{
    CommFrame noChecksum = linFrame(0x31, QByteArray::fromHex("01"));
    QVERIFY(!noChecksum.verifyLinChecksum());
    QVERIFY(!noChecksum.linChecksumChecked());

    CommFrame headerOnly = linFrame(0x31, QByteArray());
    headerOnly.setLinChecksum(0xFF);
    QVERIFY(!headerOnly.verifyLinChecksum());

    CommFrame can(0x31, QByteArray::fromHex("01"));
    can.setLinChecksum(0xFE);
    QVERIFY(!can.verifyLinChecksum());
}

void TestLinFrame::testParityError()
{
    CommFrame frame = linFrame(0x01, QByteArray::fromHex("01"));
    QCOMPARE(int(frame.linPid()), 0xC1); // computed from the ID
    QVERIFY(!frame.hasLinReceivedPid());
    QVERIFY(!frame.hasLinParityError());

    frame.setLinReceivedPid(0xC1);
    QVERIFY(!frame.hasLinParityError());

    frame.setLinReceivedPid(0x41); // P1 flipped
    QCOMPARE(int(frame.linPid()), 0x41);
    QVERIFY(frame.hasLinParityError());
    QVERIFY(frame.hasLinError());

    CommFrame reported = linFrame(0x01, QByteArray::fromHex("01"));
    reported.setLinErrors(CommFrame::LinErrParity);
    QVERIFY(reported.hasLinParityError());
}

void TestLinFrame::testDetailsAreIndependent()
{
    CommFrame frame = linFrame(0x22, QByteArray::fromHex("AABB"));
    frame.setLinChecksum(0xA5);
    frame.setLinChecksumModel(CommFrame::LinChecksumEnhanced);
    frame.setLinReceivedPid(CommFrame::linPidFromId(0x22));
    frame.setLinPublisher(CommFrame::LinPublisherSlave);
    frame.setLinErrors(CommFrame::LinErrFraming | CommFrame::LinErrCollision);
    frame.setLinDeviceTimestamp(true);
    frame.setLinChecksumStatus(true);

    QCOMPARE(int(frame.linChecksumByte()), 0xA5);
    QCOMPARE(frame.linChecksumModel(), CommFrame::LinChecksumEnhanced);
    QCOMPARE(int(frame.linPid()), int(CommFrame::linPidFromId(0x22)));
    QCOMPARE(frame.linPublisher(), CommFrame::LinPublisherSlave);
    QCOMPARE(int(frame.linErrors()), int(CommFrame::LinErrFraming | CommFrame::LinErrCollision));
    QVERIFY(frame.hasLinDeviceTimestamp());
    QVERIFY(frame.linChecksumOk());

    // overwriting one detail leaves the others alone
    frame.setLinChecksum(0x5A);
    frame.setLinPublisher(CommFrame::LinPublisherMaster);
    frame.setLinErrors(CommFrame::LinNoError);
    frame.setLinDeviceTimestamp(false);
    QCOMPARE(int(frame.linChecksumByte()), 0x5A);
    QCOMPARE(frame.linChecksumModel(), CommFrame::LinChecksumEnhanced);
    QCOMPARE(int(frame.linPid()), int(CommFrame::linPidFromId(0x22)));
    QCOMPARE(frame.linPublisher(), CommFrame::LinPublisherMaster);
    QCOMPARE(int(frame.linErrors()), int(CommFrame::LinNoError));
    QVERIFY(!frame.hasLinDeviceTimestamp());
    QVERIFY(frame.linChecksumOk());

    frame.clearLinInfo();
    QVERIFY(!frame.hasLinChecksum());
    QVERIFY(!frame.hasLinReceivedPid());
    QVERIFY(!frame.linChecksumChecked());
    QCOMPARE(frame.linChecksumModel(), CommFrame::LinChecksumUnknown);
    QCOMPARE(frame.payload(), QByteArray::fromHex("AABB")); // the frame itself is untouched
}

QTEST_MAIN(TestLinFrame)
#include "test_lin_frame.moc"
