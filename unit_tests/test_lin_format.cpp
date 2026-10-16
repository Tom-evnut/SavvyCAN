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
    void testToLinForSending();
    void testChecksumModelsForSending();
    void testSaveFormatsKeepingLin();
    void testPcapLinPacket();
    void testPcapngFile();
    void testClassicPcap();
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

void TestLinFormat::testToLinForSending()
{
    CommFrame out;
    QString why;
    CommFrame data = canFrame(0x31, QByteArray::fromHex("0102030405060708"), 1);
    QVERIFY(LinFormat::toLinForSending(data, CommFrame::LinChecksumClassic, out, &why));
    QVERIFY(out.isLin());
    QCOMPARE(out.frameId(), 0x31u);
    QCOMPARE(int(out.getBus()), 1);
    QVERIFY(!out.isReceived());
    QCOMPARE(out.payload(), QByteArray::fromHex("0102030405060708"));
    QCOMPARE(int(out.linChecksumByte()), 0xDB);
    QVERIFY(LinFormat::toLinForSending(data, CommFrame::LinChecksumEnhanced, out, &why));
    QCOMPARE(int(out.linChecksumByte()), 0x2A);
    QCOMPARE(out.linChecksumModel(), CommFrame::LinChecksumEnhanced);

    // diagnostic frames always use the classic checksum
    QVERIFY(LinFormat::toLinForSending(canFrame(0x3C, QByteArray::fromHex("1006B200FF7FFFFF"), 0), CommFrame::LinChecksumEnhanced, out));
    QCOMPARE(int(out.linChecksumByte()), 0xB7);
    QCOMPARE(out.linChecksumModel(), CommFrame::LinChecksumClassic);

    // a header alone: no data, or a remote frame
    QVERIFY(LinFormat::toLinForSending(canFrame(0x01, QByteArray(), 0), CommFrame::LinChecksumEnhanced, out));
    QVERIFY(out.isLinHeaderOnly());
    CommFrame remote(CommFrame::RemoteRequestFrame);
    remote.setFrameId(0x21);
    remote.setPayload(QByteArray(4, '\0'));
    QVERIFY(LinFormat::toLinForSending(remote, CommFrame::LinChecksumEnhanced, out));
    QVERIFY(out.isLinHeaderOnly());
    QCOMPARE(out.frameId(), 0x21u);

    // a LIN frame keeps its checksum, even a wrong one (played back from a log)
    CommFrame bad = received(0x31, QByteArray::fromHex("0102030405060708"), 0x55);
    QVERIFY(LinFormat::toLinForSending(bad, CommFrame::LinChecksumClassic, out));
    QCOMPARE(int(out.linChecksumByte()), 0x55);

    // what can't be LIN
    QVERIFY(!LinFormat::toLinForSending(canFrame(0x40, QByteArray::fromHex("01"), 0), CommFrame::LinChecksumClassic, out, &why));
    QVERIFY(why.contains("isn't a LIN ID"));
    CommFrame ext = canFrame(0x05, QByteArray::fromHex("01"), 0);
    ext.setExtendedFrameFormat(true);
    QVERIFY(!LinFormat::toLinForSending(ext, CommFrame::LinChecksumClassic, out, &why));
    QVERIFY(!LinFormat::toLinForSending(canFrame(0x05, QByteArray(12, '\0'), 0), CommFrame::LinChecksumClassic, out, &why));
    QVERIFY(why.contains("classic CAN"));
    CommFrame err(CommFrame::ErrorFrame);
    QVERIFY(!LinFormat::toLinForSending(err, CommFrame::LinChecksumClassic, out, &why));
}

void TestLinFormat::testChecksumModelsForSending()
{
    LinFormat::ChecksumModels models;
    QCOMPARE(models.forSending(0x31, CommFrame::LinChecksumUnknown), CommFrame::LinChecksumEnhanced); // not seen yet
    models.learn(received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB)); // classic seen on the bus
    QCOMPARE(models.forSending(0x31, CommFrame::LinChecksumUnknown), CommFrame::LinChecksumClassic);
    QCOMPARE(models.forSending(0x31, CommFrame::LinChecksumEnhanced), CommFrame::LinChecksumEnhanced); // the bus says
    models.learn(received(0x31, QByteArray::fromHex("0102030405060708"), 0x00)); // a bad checksum teaches nothing
    QCOMPARE(models.forSending(0x31, CommFrame::LinChecksumUnknown), CommFrame::LinChecksumClassic);
    models.learn(received(0x31, QByteArray::fromHex("0102030405060708"), 0x2A));
    QCOMPARE(models.forSending(0x31, CommFrame::LinChecksumUnknown), CommFrame::LinChecksumEnhanced);
    QCOMPARE(models.forSending(0x10, CommFrame::LinChecksumUnknown), CommFrame::LinChecksumEnhanced);
}

void TestLinFormat::testSaveFormatsKeepingLin()
{
    // native CSV (0) and BLF (14) mark LIN frames; saving to the others asks first
    QVERIFY(FrameFileIO::saveFormatKeepsLin(0));
    QVERIFY(FrameFileIO::saveFormatKeepsLin(14));
    for (int format : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, -1})
        QVERIFY2(!FrameFileIO::saveFormatKeepsLin(format), qPrintable(QString::number(format)));

    QVector<CommFrame> frames;
    QCOMPARE(FrameFileIO::linFrameCount(&frames), 0);
    QCOMPARE(FrameFileIO::linFrameCount(nullptr), 0);
    frames << CommFrame(0x10, QByteArray::fromHex("01")) << linFrame(0x10, QByteArray::fromHex("01"))
           << linFrame(0x11, QByteArray());
    QCOMPARE(FrameFileIO::linFrameCount(&frames), 2);
}

// Wireshark LINKTYPE_LIN packets (LinFormat::toPcapLin/fromPcapLin)
void TestLinFormat::testPcapLinPacket()
{
    // ID 0x31 (PID 0xB1), 8 bytes, classic checksum 0xDB, received
    CommFrame f = received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB);
    QByteArray p = LinFormat::toPcapLin(f);
    QCOMPARE(p, QByteArray::fromHex("01000000" "81B1DB00" "0102030405060708")); // 8 bytes, frame, classic
    CommFrame back;
    QVERIFY(LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(p.constData()), int(p.size()), back));
    QVERIFY(back.isLin());
    QCOMPARE(back.frameId(), quint32(0x31));
    QCOMPARE(back.payload(), f.payload());
    QCOMPARE(back.linChecksumByte(), quint8(0xDB));
    QVERIFY(back.linChecksumOk());
    QCOMPARE(back.linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(!back.hasLinError());

    // enhanced: 0x2A
    p = LinFormat::toPcapLin(received(0x31, QByteArray::fromHex("0102030405060708"), 0x2A));
    QCOMPARE(quint8(p.at(4)), quint8(0x82));

    // a wrong checksum: type 0 and the checksum error bit; it loads as a checksum error
    p = LinFormat::toPcapLin(received(0x31, QByteArray::fromHex("0102030405060708"), 0x55));
    QCOMPARE(quint8(p.at(4)), quint8(0x80));
    QCOMPARE(quint8(p.at(7)), quint8(0x08));
    QVERIFY(LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(p.constData()), int(p.size()), back));
    QVERIFY(back.hasLinChecksumError());

    // a header nobody answered: no data, the no-response bit; it loads as a header alone, not as an error
    CommFrame header = linFrame(0x02, QByteArray());
    p = LinFormat::toPcapLin(header);
    QCOMPARE(p, QByteArray::fromHex("01000000" "00420001"));
    QVERIFY(LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(p.constData()), int(p.size()), back));
    QVERIFY(back.isLinHeaderOnly());
    QCOMPARE(back.frameId(), quint32(0x02));
    QVERIFY(!back.hasLinError());

    // a protected ID with bad parity: kept, and flagged both ways
    CommFrame badPid = received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB, 0x31);
    p = LinFormat::toPcapLin(badPid);
    QCOMPARE(quint8(p.at(5)), quint8(0x31));
    QVERIFY(quint8(p.at(7)) & 0x04);
    QVERIFY(LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(p.constData()), int(p.size()), back));
    QVERIFY(back.hasLinParityError());

    // events, other revisions and short packets aren't frames
    const QByteArray sleep = QByteArray::fromHex("01000000" "4C000000" "B0B00001");
    QVERIFY(!LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(sleep.constData()), int(sleep.size()), back));
    const QByteArray rev2 = QByteArray::fromHex("02000000" "00420001");
    QVERIFY(!LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(rev2.constData()), int(rev2.size()), back));
    QVERIFY(!LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(p.constData()), 7, back));
    // a length longer than the packet: only what's there
    const QByteArray cut = QByteArray::fromHex("01000000" "81B1DB00" "0102");
    QVERIFY(LinFormat::fromPcapLin(reinterpret_cast<const unsigned char *>(cut.constData()), int(cut.size()), back));
    QCOMPARE(back.payload(), QByteArray::fromHex("0102"));
}

// a pcapng file with CAN, CAN FD and LIN frames on several buses, saved and loaded back
void TestLinFormat::testPcapngFile()
{
    QVector<CommFrame> frames;
    auto add = [&](CommFrame f, int bus, qint64 us, bool rx = true) {
        f.setBus(bus);
        f.setTimeStamp(CommFrame::TimeStamp(0, us));
        f.setReceived(rx);
        frames.append(f);
    };
    CommFrame can(0x123, QByteArray::fromHex("1122334455"));
    add(can, 0, 1000);
    CommFrame ext(0x18DAF110, QByteArray::fromHex("0102030405060708"));
    ext.setExtendedFrameFormat(true);
    add(ext, 0, 2000, false); // sent
    CommFrame fd(0x200, QByteArray(12, char(0x5A)));
    fd.setFrameType(CommFrame::CANFDDataFrame);
    fd.setFlexibleDataRateFormat(true);
    fd.setBitrateSwitch(true);
    add(fd, 2, 3000);
    CommFrame remote(CommFrame::RemoteRequestFrame);
    remote.setFrameId(0x321);
    add(remote, 0, 3500);
    add(received(0x31, QByteArray::fromHex("0102030405060708"), 0xDB), 1, 4000);
    add(linFrame(0x02, QByteArray()), 1, 5000);
    add(received(0x10, QByteArray::fromHex("AA"), 0x55), 0, 6000); // LIN on bus 0 too, beside CAN
    add(received(0x08, QByteArray::fromHex("FEFF"), 0x01), 1, 7000, false); // a LIN frame sent as master

    QTemporaryDir dir;
    const QString path = dir.filePath("mixed.pcapng");
    QVERIFY(FrameFileIO::saveWiresharkPcapng(path, &frames));
    if (!qEnvironmentVariable("PCAP_DUMP_DIR").isEmpty()) // for checking with tshark
        QFile::copy(path, qEnvironmentVariable("PCAP_DUMP_DIR") + "/mixed.pcapng");

    QVERIFY(FrameFileIO::isWiresharkSocketCANFile(path));
    QVector<CommFrame> loaded;
    QVERIFY(FrameFileIO::loadWiresharkSocketCANFile(path, &loaded));
    QCOMPARE(loaded.size(), frames.size());
    for (int i = 0; i < frames.size(); i++)
    {
        const CommFrame &a = frames.at(i);
        const CommFrame &b = loaded.at(i);
        const QByteArray what = QByteArray::number(i);
        QVERIFY2(a.isLin() == b.isLin(), what);
        QVERIFY2(a.frameId() == b.frameId(), what);
        QVERIFY2(a.getBus() == b.getBus(), what);
        QVERIFY2(a.isReceived() == b.isReceived(), what);
        QVERIFY2(a.payload() == b.payload(), what);
        QVERIFY2(b.timeStamp().microSeconds() == a.timeStamp().microSeconds() - 1000, what); // from the first frame
        if (a.isLin())
        {
            QVERIFY2(a.isLinHeaderOnly() == b.isLinHeaderOnly(), what);
            QVERIFY2(a.linChecksumByte() == b.linChecksumByte(), what);
            QVERIFY2(a.hasLinChecksumError() == b.hasLinChecksumError(), what);
            QVERIFY2(a.linChecksumModel() == b.linChecksumModel(), what);
        }
        else
        {
            QVERIFY2(a.frameType() == b.frameType(), what);
            QVERIFY2(a.hasExtendedFrameFormat() == b.hasExtendedFrameFormat(), what);
            QVERIFY2(a.hasFlexibleDataRateFormat() == b.hasFlexibleDataRateFormat(), what);
            QVERIFY2(a.hasBitrateSwitch() == b.hasBitrateSwitch(), what);
        }
    }

    // autodetected as a Wireshark CAN/LIN capture
    QVector<CommFrame> detected;
    QVERIFY(FrameFileIO::autoDetectLoadFile(path, &detected));
    QCOMPARE(detected.size(), frames.size());
    QVERIFY(detected.at(4).isLin());
}

// classic pcap files (one link type each): SocketCAN and LIN, built by hand
void TestLinFormat::testClassicPcap()
{
    auto pcapFile = [](quint32 linkType, const QList<QPair<quint32, QByteArray>> &packets) {
        QByteArray f;
        auto le32 = [&f](quint32 v) { for (int i = 0; i < 4; i++) f.append(char((v >> (8 * i)) & 0xFF)); };
        auto le16 = [&f](quint16 v) { f.append(char(v & 0xFF)); f.append(char(v >> 8)); };
        le32(0xA1B2C3D4); le16(2); le16(4); le32(0); le32(0); le32(65535); le32(linkType);
        for (const auto &p : packets) {
            le32(p.first / 1000000); le32(p.first % 1000000);
            le32(quint32(p.second.size())); le32(quint32(p.second.size()));
            f.append(p.second);
        }
        return f;
    };
    QTemporaryDir dir;
    auto write = [&dir](const QString &name, const QByteArray &data) {
        QFile f(dir.filePath(name));
        f.open(QIODevice::WriteOnly);
        f.write(data);
        return dir.filePath(name);
    };

    const QString can = write("can.pcap", pcapFile(227, {
        {1000, QByteArray::fromHex("00000123" "03000000" "AABBCC0000000000")},
        {3500, QByteArray::fromHex("98DAF110" "02000000" "1122000000000000")}}));
    QVERIFY(FrameFileIO::isWiresharkSocketCANFile(can));
    QVector<CommFrame> frames;
    QVERIFY(FrameFileIO::loadWiresharkSocketCANFile(can, &frames));
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).frameId(), quint32(0x123));
    QCOMPARE(frames.at(0).payload(), QByteArray::fromHex("AABBCC"));
    QVERIFY(!frames.at(0).isLin());
    QVERIFY(frames.at(1).hasExtendedFrameFormat());
    QCOMPARE(frames.at(1).frameId(), quint32(0x18DAF110));
    QCOMPARE(frames.at(1).timeStamp().microSeconds(), qint64(2500));

    const QString lin = write("lin.pcap", pcapFile(212, {
        {0, QByteArray::fromHex("01000000" "81B1DB00" "0102030405060708")},
        {10000, QByteArray::fromHex("01000000" "4C000000" "B0B00001")}, // go to sleep: skipped
        {20000, QByteArray::fromHex("01000000" "00420001")}}));
    QVERIFY(FrameFileIO::isWiresharkSocketCANFile(lin));
    frames.clear();
    QVERIFY(FrameFileIO::loadWiresharkSocketCANFile(lin, &frames));
    QCOMPARE(frames.size(), 2);
    QVERIFY(frames.at(0).isLin());
    QVERIFY(frames.at(0).linChecksumOk());
    QVERIFY(frames.at(1).isLinHeaderOnly());
    QCOMPARE(frames.at(1).timeStamp().microSeconds(), qint64(20000));

    // other link types aren't CAN/LIN captures (Ethernet here)
    const QString eth = write("eth.pcap", pcapFile(1, {{0, QByteArray(60, '\0')}}));
    QVERIFY(!FrameFileIO::isWiresharkSocketCANFile(eth));

    // a packet longer than SavvyCAN's buffer is cut, not overrun, and the next one is still read
    const QString big = write("big.pcap", pcapFile(227, {
        {0, QByteArray(1000, char(0x01))},
        {1000, QByteArray::fromHex("00000123" "01000000" "7700000000000000")}}));
    frames.clear();
    QVERIFY(FrameFileIO::loadWiresharkSocketCANFile(big, &frames));
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(1).payload(), QByteArray::fromHex("77"));
}

QTEST_MAIN(TestLinFormat)
#include "test_lin_format.moc"
