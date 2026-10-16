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

QTEST_MAIN(TestLinFormat)
#include "test_lin_format.moc"
