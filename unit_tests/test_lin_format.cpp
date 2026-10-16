#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "linformat.h"
#include "framefileio.h"

// How LIN frames are shown and saved (linformat.h), and native CSV files with LIN frames through FrameFileIO.
// Checksums: ID 0x31 with data 01..08 is 0xDB classic, 0x2A enhanced (checked independently in TestLinFrame).
class TestLinFormat : public QObject
{
    Q_OBJECT

private slots:
    void testTypeLabel();
    void testCsvFlags();
    void testApplyCsvFlags();
    void testCsvFlagsRoundTrip();
    void testSummary();
    void testSummaryNoLin();
    void testNativeCsvFile();
    void testLinTextLog();
    void testConvertToLin();
    void testConvertToLinWithChecksum();
};

static CommFrame linFrame(quint8 id, const QByteArray &data)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(id);
    frame.setPayload(data);
    return frame;
}

static CommFrame received(quint8 id, const QByteArray &data, int checksum, quint8 pid = 0)
{
    CommFrame frame = linFrame(id, data);
    frame.setLinReceivedPid(pid ? pid : CommFrame::linPidFromId(id));
    if (checksum >= 0) frame.setLinChecksum(quint8(checksum));
    frame.verifyLinChecksum();
    return frame;
}

void TestLinFormat::testTypeLabel()
{
    QCOMPARE(LinFormat::typeLabel(received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)), QString("L"));
    QCOMPARE(LinFormat::typeLabel(received(0x31, QByteArray(), -1)), QString("LN"));
    QCOMPARE(LinFormat::typeLabel(received(0x31, QByteArray::fromHex("0102030405060708"), 0x00)), QString("L!"));
    QCOMPARE(LinFormat::typeLabel(received(0x01, QByteArray(), -1, 0x41)), QString("LN!")); // bad parity
    CommFrame device = received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB);
    device.setLinErrors(CommFrame::LinErrDevice);
    QCOMPARE(LinFormat::typeLabel(device), QString("L!"));
    QCOMPARE(LinFormat::typeLabel(linFrame(0x10, QByteArray::fromHex("01"))), QString("L")); // nothing known
}

void TestLinFormat::testCsvFlags()
{
    QCOMPARE(LinFormat::csvFlags(received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)),
             QByteArray("LIN+CS=DB+CLASSIC"));
    QCOMPARE(LinFormat::csvFlags(received(0x31, QByteArray::fromHex("0102030405060708"), 0x2A)),
             QByteArray("LIN+CS=2A+ENHANCED"));
    QCOMPARE(LinFormat::csvFlags(received(0x31, QByteArray::fromHex("0102030405060708"), 0x05)),
             QByteArray("LIN+CS=05")); // matches neither model
    QCOMPARE(LinFormat::csvFlags(received(0x31, QByteArray(), -1)), QByteArray("LIN"));
    QCOMPARE(LinFormat::csvFlags(received(0x01, QByteArray(), -1, 0x41)), QByteArray("LIN+PID=41"));
    CommFrame errors = received(0x31, QByteArray::fromHex("AA"), -1);
    errors.setLinErrors(CommFrame::LinErrIncompleteResponse | CommFrame::LinErrDevice);
    QCOMPARE(LinFormat::csvFlags(errors), QByteArray("LIN+INCOMPLETE+DEVERR"));
}

void TestLinFormat::testApplyCsvFlags()
{
    QVERIFY(LinFormat::isCsvFlags("LIN"));
    QVERIFY(LinFormat::isCsvFlags(" lin+cs=db "));
    QVERIFY(!LinFormat::isCsvFlags("FD+BRS"));
    QVERIFY(!LinFormat::isCsvFlags("LINX"));
    QVERIFY(!LinFormat::isCsvFlags(""));

    CommFrame frame(0x31, QByteArray::fromHex("0102030405060708")); // as the loader builds it: a CAN frame
    frame.setExtendedFrameFormat(true);
    LinFormat::applyCsvFlags(frame, "LIN+CS=DB");
    QVERIFY(frame.isLin());
    QVERIFY(!frame.hasExtendedFrameFormat());
    QVERIFY(frame.isValid());
    QCOMPARE(int(frame.linChecksumByte()), 0xDB);
    QCOMPARE(frame.linChecksumModel(), CommFrame::LinChecksumClassic); // found again by checking
    QVERIFY(frame.linChecksumOk());

    CommFrame wrongModel(0x31, QByteArray::fromHex("0102030405060708"));
    LinFormat::applyCsvFlags(wrongModel, "LIN+CS=DB+ENHANCED"); // the file says enhanced: checked as that
    QVERIFY(wrongModel.hasLinChecksumError());

    CommFrame junk(0x01, QByteArray());
    LinFormat::applyCsvFlags(junk, "LIN+CS=ZZ+PID=100+WHATEVER+COLLISION");
    QVERIFY(!junk.hasLinChecksum());
    QVERIFY(!junk.hasLinReceivedPid());
    QCOMPARE(int(junk.linErrors()), int(CommFrame::LinErrCollision));
}

void TestLinFormat::testCsvFlagsRoundTrip()
{
    QList<CommFrame> frames;
    frames << received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)
           << received(0x31, QByteArray::fromHex("0102030405060708"), 0x2A)
           << received(0x31, QByteArray::fromHex("0102030405060708"), 0x77)
           << received(0x01, QByteArray(), -1, 0x41)
           << received(0x3C, QByteArray::fromHex("AA"), -1);
    CommFrame all = received(0x22, QByteArray::fromHex("AABB"), 0x10);
    quint8 every = 0;
    for (const LinFormat::ErrorName &e : LinFormat::errorNames()) every |= e.flag;
    all.setLinErrors(every);
    frames << all;

    for (const CommFrame &out : frames) {
        CommFrame in(out.frameId(), out.payload());
        LinFormat::applyCsvFlags(in, LinFormat::csvFlags(out));
        QCOMPARE(LinFormat::csvFlags(in), LinFormat::csvFlags(out));
        QCOMPARE(LinFormat::typeLabel(in), LinFormat::typeLabel(out));
        QCOMPARE(in.linErrors(), out.linErrors());
        QCOMPARE(in.hasLinChecksumError(), out.hasLinChecksumError());
        QCOMPARE(in.hasLinParityError(), out.hasLinParityError());
        QCOMPARE(int(in.linPid()), int(out.linPid()));
    }
}

void TestLinFormat::testSummary()
{
    QVector<CommFrame> frames;
    frames << received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)
           << received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)
           << received(0x31, QByteArray::fromHex("0102030405060708"), 0x00) // checksum error
           << received(0x31, QByteArray(), -1)                                // no response
           << CommFrame(0x31, QByteArray::fromHex("FF"));                     // a CAN frame with the same ID
    CommFrame device = received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB);
    device.setLinErrors(CommFrame::LinErrFraming);
    frames << device;

    const QStringList lines = LinFormat::summary(frames);
    QCOMPARE(lines.size(), 6);
    QCOMPARE(lines.at(0), QString("Protected ID: 0xB1"));
    QCOMPARE(lines.at(1), QString("Responses: 4, headers with no response: 1"));
    QCOMPARE(lines.at(2), QString("Checksum model: classic (LIN 1.x or diagnostic)"));
    QCOMPARE(lines.at(3), QString("Checksum errors: 1"));
    QCOMPARE(lines.at(4), QString("Parity errors: 0"));
    QCOMPARE(lines.at(5), QString("Device errors: framing 1"));

    frames << received(0x31, QByteArray::fromHex("0102030405060708"), 0x2A);
    QCOMPARE(LinFormat::summary(frames).at(2), QString("Checksum model: both seen (classic 3, enhanced 1)"));
}

void TestLinFormat::testSummaryNoLin()
{
    QVERIFY(LinFormat::summary({CommFrame(0x31, QByteArray::fromHex("01"))}).isEmpty());
    QStringList headerOnly = LinFormat::summary({received(0x05, QByteArray(), -1)});
    QCOMPARE(headerOnly.at(2), QString("Checksum model: unknown (no valid checksum seen)"));
}

void TestLinFormat::testNativeCsvFile()
{
    QVector<CommFrame> out;
    out << received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)
        << received(0x01, QByteArray(), -1, 0x41)
        << CommFrame(0x123, QByteArray::fromHex("0102"))   // classic CAN in between: no LIN details carried over
        << received(0x10, QByteArray::fromHex("4A5593E5"), 0x00);
    out[2].setFrameType(CommFrame::CANDataFrame);
    for (int i = 0; i < out.size(); i++) {
        out[i].setTimeStamp(CommFrame::TimeStamp(0, 1000 * (i + 1)));
        out[i].setBus(0);
        out[i].setReceived(true);
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString name = dir.filePath("lin.csv");
    QVERIFY(FrameFileIO::saveNativeCSVFile(name, &out));
    QFile file(name);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QList<QByteArray> rows = file.readAll().split('\n');
    QVERIFY(rows.at(1).endsWith(",01,02,03,04,05,06,07,08,LIN+CS=DB+CLASSIC,"));
    QVERIFY(rows.at(2).contains(",0,00,00,00,00,00,00,00,00,LIN+PID=41,"));
    QVERIFY(rows.at(3).endsWith(",01,02,00,00,00,00,00,00,"));
    file.close();

    QVector<CommFrame> in;
    QVERIFY(FrameFileIO::loadNativeCSVFile(name, &in));
    QCOMPARE(in.size(), out.size());
    for (int i = 0; i < out.size(); i++) {
        QCOMPARE(in.at(i).frameType(), out.at(i).frameType());
        QCOMPARE(in.at(i).frameId(), out.at(i).frameId());
        QCOMPARE(in.at(i).payload(), out.at(i).payload());
        QCOMPARE(in.at(i).timeStamp().microSeconds(), out.at(i).timeStamp().microSeconds());
    }
    QVERIFY(in.at(0).linChecksumOk());
    QCOMPARE(in.at(0).linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(in.at(1).isLinHeaderOnly());
    QVERIFY(in.at(1).hasLinParityError());
    QVERIFY(!in.at(2).hasLinChecksum());     // the CAN row after LIN rows
    QVERIFY(!in.at(2).hasLinReceivedPid());
    QVERIFY(in.at(3).hasLinChecksumError());
    QVERIFY(!in.at(3).hasLinReceivedPid()); // its PID was fine, so the file didn't keep it
}

void TestLinFormat::testLinTextLog()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString name = dir.filePath("lin.txt");
    QFile file(name);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("     count    timestamp d ID data\n"
               "------------------------------------------------------------------------------\n"
               "         1    100000000 d SLEEP\n"
               "         2    100500000 s WAKEUP\n"
               "         3    100600000 S 0a ff 11 \n"
               "         4    100610000 S 2a 09 8f 00 41 32 45 00 00 \n"
               "         5    100620000 S 3c\n"          // a header nobody answered
               "         6    100630000 S 40 01\n"       // not a LIN ID
               "         7    100640000 S 10 zz\n"       // not hex
               "         8    100650000 S 11 00 01 02 03 04 05 06 07 08\n" // 9 data bytes
               "         9    100660000 S 3d d0 00 01 0d 00 00 04 00\n"
               "\n");
    file.close();

    QVERIFY(FrameFileIO::isLinTextLog(name));
    QVector<CommFrame> frames;
    QVERIFY(FrameFileIO::loadLinTextLog(name, &frames));
    QCOMPARE(frames.size(), 4);
    QCOMPARE(frames.at(0).frameId(), 0x0Au);
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("ff11"));
    QCOMPARE(frames.at(0).timeStamp().microSeconds(), 100600000ll);
    QVERIFY(frames.at(0).isLin());
    QVERIFY(frames.at(0).isValid());
    QVERIFY(frames.at(0).isReceived());
    QVERIFY(!frames.at(0).hasLinChecksum()); // the log has no checksums
    QCOMPARE(LinFormat::typeLabel(frames.at(0)), QString("L"));
    QCOMPARE(frames.at(1).payload(), QByteArray::fromHex("098f004132450000"));
    QVERIFY(frames.at(2).isLinHeaderOnly());
    QCOMPARE(frames.at(3).frameId(), 0x3Du);

    // auto-detection picks it, and other files aren't taken for it
    QVector<CommFrame> detected;
    QVERIFY(FrameFileIO::autoDetectLoadFile(name, &detected));
    QCOMPARE(detected.size(), 4);
    QVERIFY(detected.at(0).isLin());

    const QString csv = dir.filePath("other.csv");
    QFile other(csv);
    QVERIFY(other.open(QIODevice::WriteOnly | QIODevice::Text));
    other.write("Time Stamp,ID,Extended,Dir,Bus,LEN,D1,D2,D3,D4,D5,D6,D7,D8\n1000,00000031,false,Rx,0,1,01,00,00,00,00,00,00,00,\n");
    other.close();
    QVERIFY(!FrameFileIO::isLinTextLog(csv));
}

static CommFrame canFrame(quint32 id, const QByteArray &data, int bus)
{
    CommFrame frame(CommFrame::CANDataFrame);
    frame.setFrameId(id);
    frame.setPayload(data); // more than 8 bytes: CAN FD
    frame.setBus(bus);
    return frame;
}

void TestLinFormat::testConvertToLin()
{
    QVector<CommFrame> frames;
    frames << canFrame(0x10, QByteArray::fromHex("4A55"), 0)      // converted
           << canFrame(0x3F, QByteArray(), 0)                      // converted: a header with no response
           << canFrame(0x10, QByteArray::fromHex("01"), 1)         // other bus
           << canFrame(0x40, QByteArray::fromHex("01"), 0)         // ID too big
           << canFrame(0x123, QByteArray::fromHex("01"), 0)        // ID too big
           << canFrame(0x05, QByteArray(12, '\0'), 0);             // CAN FD
    CommFrame extended = canFrame(0x05, QByteArray::fromHex("01"), 0);
    extended.setExtendedFrameFormat(true);
    CommFrame remote(CommFrame::RemoteRequestFrame);
    remote.setFrameId(0x05);
    remote.setBus(0);
    CommFrame alreadyLin = received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB);
    alreadyLin.setBus(0);
    frames << extended << remote << alreadyLin;

    const LinFormat::Conversion result = LinFormat::convertToLin(frames, 0, false);
    QCOMPARE(result.converted, 2);
    QCOMPARE(result.notLin, 4); // 0x40, 0x123, FD, extended; the remote frame isn't counted
    QVERIFY(frames.at(0).isLin());
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("4A55"));
    QVERIFY(!frames.at(0).hasLinChecksum());
    QVERIFY(frames.at(0).isValid());
    QVERIFY(frames.at(1).isLinHeaderOnly());
    QVERIFY(!frames.at(2).isLin());
    QVERIFY(!frames.at(3).isLin());
    QVERIFY(!frames.at(5).isLin());
    QVERIFY(!frames.at(6).isLin());
    QCOMPARE(frames.at(7).frameType(), CommFrame::RemoteRequestFrame);
    QVERIFY(frames.at(8).linChecksumOk()); // LIN frames are left alone

    // all buses, and running it again changes nothing
    QCOMPARE(LinFormat::convertToLin(frames, -1, false).converted, 1);
    QCOMPARE(LinFormat::convertToLin(frames, -1, false).converted, 0);
    QVERIFY(frames.at(2).isLin());
}

void TestLinFormat::testConvertToLinWithChecksum()
{
    QVector<CommFrame> frames;
    frames << canFrame(0x31, QByteArray::fromHex("0102030405060708DB"), 0) // 9 bytes: loaded as CAN FD
           << canFrame(0x31, QByteArray::fromHex("01020304050607082A"), 0)
           << canFrame(0x31, QByteArray::fromHex("010203040506070800"), 0) // wrong checksum
           << canFrame(0x10, QByteArray::fromHex("4A5593E5E6"), 0)
           << canFrame(0x10, QByteArray::fromHex("AA"), 0)                 // one byte: kept as data
           << canFrame(0x10, QByteArray(10, '\0'), 0);                     // too long even with a checksum
    QCOMPARE(frames.at(0).frameType(), CommFrame::CANFDDataFrame);

    const LinFormat::Conversion result = LinFormat::convertToLin(frames, -1, true);
    QCOMPARE(result.converted, 5);
    QCOMPARE(result.notLin, 1);
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("0102030405060708"));
    QCOMPARE(frames.at(0).linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(frames.at(0).linChecksumOk());
    QVERIFY(!frames.at(0).hasFlexibleDataRateFormat());
    QVERIFY(frames.at(0).isValid());
    QCOMPARE(frames.at(1).linChecksumModel(), CommFrame::LinChecksumEnhanced);
    QVERIFY(frames.at(2).hasLinChecksumError());
    QCOMPARE(frames.at(3).payload(), QByteArray::fromHex("4A5593E5"));
    QVERIFY(frames.at(3).linChecksumOk());
    QCOMPARE(frames.at(4).payload(), QByteArray::fromHex("AA"));
    QVERIFY(!frames.at(4).hasLinChecksum());
    QVERIFY(!frames.at(5).isLin());

    // without the option, 9 bytes can't be LIN
    QVector<CommFrame> nine;
    nine << canFrame(0x31, QByteArray::fromHex("0102030405060708DB"), 0);
    QCOMPARE(LinFormat::convertToLin(nine, -1, false).notLin, 1);
}

QTEST_MAIN(TestLinFormat)
#include "test_lin_format.moc"
