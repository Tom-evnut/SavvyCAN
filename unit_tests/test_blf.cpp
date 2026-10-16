#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "blfhandler.h"

// BLF files: saving CAN and CAN FD, and loading them back. With BLF_DUMP_DIR set it also leaves sample.blf and
// sample.txt (the frames, one per line) there, for python-can to read and compare; with BLF_CHECK_FILE and
// BLF_CHECK_OUT set it loads that file and lists its frames the same way (to compare with what python-can wrote).
class TestBlf : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testRoundTrip();
    void testHeader();
    void testManyContainers();
    void testUnixTimestamps();
    void testSkipped();
    void testObjectSplitAcrossContainers();
    void testExternalFile();
    void testLinRoundTrip();
    void testLinObjects();
    void testLinLegacyObjects();
    void testLinReceiveErrors();
    void testLinEventsAndLegacyReceiveError();

private:
    QTemporaryDir dir;
};

static CommFrame can(quint32 id, const QByteArray &data, int bus, qint64 us, bool ext = false, bool rx = true)
{
    CommFrame f(CommFrame::CANDataFrame);
    f.setExtendedFrameFormat(ext);
    f.setFrameId(id);
    f.setPayload(data);
    f.setBus(bus);
    f.setReceived(rx);
    f.setTimeStamp(CommFrame::TimeStamp(0, us));
    return f;
}

// one line per frame: what the BLF should hold, in the same form as the python-can check prints it
static QString line(const CommFrame &f)
{
    return QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10").arg(f.timeStamp().microSeconds()).arg(int(f.getBus()))
        .arg(f.frameId(), 0, 16).arg(int(f.hasExtendedFrameFormat())).arg(int(f.frameType() == CommFrame::RemoteRequestFrame))
        .arg(int(f.hasFlexibleDataRateFormat())).arg(int(f.hasBitrateSwitch())).arg(int(f.hasErrorStateIndicator()))
        .arg(int(f.isReceived()))
        .arg(QString(f.frameType() == CommFrame::RemoteRequestFrame ? QByteArray::number(f.payload().size()) : f.payload().toHex()));
}

static QVector<CommFrame> sampleFrames()
{
    QVector<CommFrame> frames;
    frames << can(0x123, QByteArray::fromHex("0102030405060708"), 0, 1000)
           << can(0x1ABCDEF0, QByteArray::fromHex("AABB"), 1, 2500, true)
           << can(0x7FF, QByteArray(), 0, 3000, false, false)                  // no data, sent
           << can(0x10, QByteArray::fromHex("01"), 2, 4000);
    CommFrame remote(CommFrame::RemoteRequestFrame);
    remote.setFrameId(0x321);
    remote.setPayload(QByteArray(4, '\0'));
    remote.setBus(0);
    remote.setReceived(true);
    remote.setTimeStamp(CommFrame::TimeStamp(0, 5000));
    frames << remote;
    CommFrame fd = can(0x456, QByteArray::fromHex("000102030405060708090A0B"), 1, 6000);
    fd.setBitrateSwitch(true);
    frames << fd;
    QByteArray big;
    for (int i = 0; i < 64; i++) big.append(char(i));
    CommFrame fd64 = can(0x18DAF110, big, 0, 7000, true, false);
    fd64.setErrorStateIndicator(true);
    frames << fd64;
    CommFrame fdShort = can(0x22, QByteArray::fromHex("0102"), 0, 8000);
    fdShort.setFlexibleDataRateFormat(true); // a CAN FD frame with 2 bytes
    frames << fdShort;
    return frames;
}

void TestBlf::initTestCase()
{
    QVERIFY(dir.isValid());
}

void TestBlf::testRoundTrip()
{
    const QVector<CommFrame> out = sampleFrames();
    const QString name = dir.filePath("roundtrip.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QCOMPARE(writer.skippedFrames(), 0);

    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), out.size());
    for (int i = 0; i < out.size(); i++)
        QCOMPARE(line(in.at(i)), line(out.at(i)));

    const QString dump = qEnvironmentVariable("BLF_DUMP_DIR");
    if (!dump.isEmpty()) {
        QFile::remove(dump + "/sample.blf");
        QVERIFY(QFile::copy(name, dump + "/sample.blf"));
        QFile txt(dump + "/sample.txt");
        QVERIFY(txt.open(QIODevice::WriteOnly | QIODevice::Text));
        for (const CommFrame &f : out) txt.write((line(f) + "\n").toUtf8());
    }
}

void TestBlf::testHeader()
{
    const QVector<CommFrame> out = sampleFrames();
    const QString name = dir.filePath("header.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QFile f(name);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    QCOMPARE(d.left(4), QByteArray("LOGG"));
    QCOMPARE(qFromLittleEndian<quint32>(d.constData() + 4), 144u);
    QCOMPARE(qFromLittleEndian<quint64>(d.constData() + 16), quint64(d.size()));   // file size
    QCOMPARE(qFromLittleEndian<quint32>(d.constData() + 32), quint32(out.size())); // objects
    QCOMPARE(d.mid(144, 4), QByteArray("LOBJ"));                                   // the first container
    QCOMPARE(qFromLittleEndian<quint32>(d.constData() + 144 + 12), 10u);           // LOG_CONTAINER
    QCOMPARE(qFromLittleEndian<quint16>(d.constData() + 144 + 16), quint16(2));    // zlib
    QCOMPARE(d.mid(56, 16), QByteArray(16, '\0'));                                // session times: no start time
}

void TestBlf::testManyContainers()
{
    QVector<CommFrame> out;
    QByteArray data;
    for (int i = 0; i < 64; i++) data.append(char(i * 3));
    for (int i = 0; i < 5000; i++) { // 5000 CAN FD objects of 116 bytes: several 128 kB containers
        CommFrame f = can(0x100 + (i % 50), data, i % 3, 1000 + i * 10);
        f.setBitrateSwitch(i % 2);
        out << f;
    }
    const QString name = dir.filePath("many.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), out.size());
    for (int i = 0; i < out.size(); i += 499)
        QCOMPARE(line(in.at(i)), line(out.at(i)));
    QCOMPARE(line(in.last()), line(out.last()));
}

void TestBlf::testUnixTimestamps()
{
    const qint64 start = 1760620000123456ll; // 2025-10-16 in microseconds since 1970
    QVector<CommFrame> out;
    out << can(0x1, QByteArray::fromHex("01"), 0, start) << can(0x2, QByteArray::fromHex("02"), 0, start + 1500);
    const QString name = dir.filePath("unix.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QFile f(name);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray d = f.readAll();
    QCOMPARE(qFromLittleEndian<quint16>(d.constData() + 56), quint16(2025)); // start SYSTEMTIME: year
    QCOMPARE(qFromLittleEndian<quint16>(d.constData() + 58), quint16(10));   // month
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), 2);
    // objects are relative to the start time (to the millisecond), so the spacing is kept
    QCOMPARE(in.at(1).timeStamp().microSeconds() - in.at(0).timeStamp().microSeconds(), 1500ll);
    QCOMPARE(in.at(0).timeStamp().microSeconds(), 456ll);
}

void TestBlf::testSkipped()
{
    QVector<CommFrame> out;
    out << can(0x1, QByteArray::fromHex("01"), 0, 1);
    CommFrame lin(CommFrame::LINDataFrame);
    lin.setFrameId(0x10);
    out << lin;
    CommFrame err(CommFrame::ErrorFrame);
    out << err;
    BLFHandler writer;
    QVERIFY(writer.saveBLF(dir.filePath("skipped.blf"), &out));
    QCOMPARE(writer.skippedFrames(), 1); // the error frame; the LIN header is written (LIN_SND_ERROR2)
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(dir.filePath("skipped.blf"), &in));
    QCOMPARE(in.size(), 2);
}

// uncompressed log containers with one CAN_MESSAGE split between them, as python-can's fixed-size containers do
void TestBlf::testObjectSplitAcrossContainers()
{
    auto le16 = [](QByteArray &b, quint16 v) { b.append(char(v & 0xFF)); b.append(char(v >> 8)); };
    auto le32 = [&](QByteArray &b, quint32 v) { le16(b, v & 0xFFFF); le16(b, v >> 16); };
    auto le64 = [&](QByteArray &b, quint64 v) { le32(b, quint32(v)); le32(b, quint32(v >> 32)); };
    auto object = [&](quint32 id, quint8 firstByte, quint64 ns) {
        QByteArray o("LOBJ");
        le16(o, 32); le16(o, 1); le32(o, 48); le32(o, 1);   // CAN_MESSAGE, 48 bytes
        le32(o, 2); le16(o, 0); le16(o, 0); le64(o, ns);
        le16(o, 1); o.append(char(0)); o.append(char(1)); le32(o, id); // channel 1, rx, dlc 1
        o.append(char(firstByte)); o.append(QByteArray(7, '\0'));
        return o;
    };
    auto container = [&](const QByteArray &data) {
        QByteArray c("LOBJ");
        le16(c, 16); le16(c, 1); le32(c, quint32(32 + data.size())); le32(c, 10);
        le16(c, 0); c.append(QByteArray(6, '\0')); le32(c, quint32(data.size())); c.append(QByteArray(4, '\0'));
        c.append(data);
        c.append(QByteArray((32 + data.size()) % 4, '\0'));
        return c;
    };
    const QByteArray objects = object(0x10, 0xA1, 1000000) + object(0x20, 0xB2, 2000000) + object(0x30, 0xC3, 3000000);
    const int cut = 48 + 20; // in the middle of the second object
    QByteArray file("LOGG");
    le32(file, 144);
    file.append(QByteArray(136, '\0'));
    file.append(container(objects.left(cut)));
    file.append(container(objects.mid(cut)));
    const QString name = dir.filePath("split.blf");
    QFile f(name);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(file);
    f.close();

    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), 3);
    QCOMPARE(in.at(1).frameId(), 0x20u);
    QCOMPARE(in.at(1).payload(), QByteArray::fromHex("B2"));
    QCOMPARE(in.at(1).timeStamp().microSeconds(), 2000ll);
    QCOMPARE(in.at(2).payload(), QByteArray::fromHex("C3"));
    QCOMPARE(int(in.at(0).getBus()), 0); // channel 1 is bus 0
}

void TestBlf::testExternalFile()
{
    const QString file = qEnvironmentVariable("BLF_CHECK_FILE");
    const QString out = qEnvironmentVariable("BLF_CHECK_OUT");
    if (file.isEmpty() || out.isEmpty()) QSKIP("BLF_CHECK_FILE/BLF_CHECK_OUT not set");
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(file, &in));
    QFile txt(out);
    QVERIFY(txt.open(QIODevice::WriteOnly | QIODevice::Text));
    for (const CommFrame &f : in) txt.write((line(f) + "\n").toUtf8());
}

// LIN frames: what a BLF must keep (checksums from TestLinFrame: ID 0x31 data 01..08 is 0xDB classic, 0x2A enhanced)
static CommFrame lin(quint8 id, const QByteArray &data, int checksum, int bus, qint64 us, bool rx = true)
{
    CommFrame f(CommFrame::LINDataFrame);
    f.setFrameId(id);
    f.setPayload(data);
    f.setBus(bus);
    f.setReceived(rx);
    f.setTimeStamp(CommFrame::TimeStamp(0, us));
    if (checksum >= 0) {
        f.setLinChecksum(quint8(checksum));
        f.verifyLinChecksum();
    }
    return f;
}

static QString linLine(const CommFrame &f)
{
    return QString("%1 %2 %3 %4 %5 cs=%6 %7 model=%8 err=%9 rx=%10").arg(f.timeStamp().microSeconds()).arg(int(f.getBus()))
        .arg(int(f.isLin())).arg(f.frameId(), 2, 16, QChar('0')).arg(QString(f.payload().toHex()))
        .arg(f.hasLinChecksum() ? QString::number(f.linChecksumByte(), 16) : QString("-"))
        .arg(f.isLinHeaderOnly() ? "header-only" : "response").arg(int(f.linChecksumModel()))
        .arg(int(f.hasLinChecksumError())).arg(int(f.isReceived()));
}

static QVector<CommFrame> linFrames()
{
    QVector<CommFrame> frames;
    frames << lin(0x31, QByteArray::fromHex("0102030405060708"), 0xDB, 0, 1000)        // classic
           << lin(0x31, QByteArray::fromHex("0102030405060708"), 0x2A, 0, 2000)        // enhanced
           << lin(0x31, QByteArray::fromHex("0102030405060708"), 0x55, 0, 3000)        // wrong checksum
           << lin(0x01, QByteArray(), -1, 0, 4000)                                     // nobody answered
           << lin(0x10, QByteArray::fromHex("4A55"), -1, 1, 5000)                      // no checksum known
           << lin(0x10, QByteArray::fromHex("4A5593E5"), 0xE6, 1, 6000, false)         // sent
           << can(0x123, QByteArray::fromHex("0102"), 0, 7000);                        // and CAN in between
    return frames;
}

void TestBlf::testLinRoundTrip()
{
    const QVector<CommFrame> out = linFrames();
    const QString name = dir.filePath("lin.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QCOMPARE(writer.skippedFrames(), 0);
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), out.size());
    for (int i = 0; i < out.size(); i++)
        QCOMPARE(linLine(in.at(i)), linLine(out.at(i)));
    QVERIFY(in.at(2).hasLinChecksumError());
    QCOMPARE(in.at(1).linChecksumModel(), CommFrame::LinChecksumEnhanced);

    const QString dump = qEnvironmentVariable("BLF_DUMP_DIR");
    if (!dump.isEmpty()) {
        QFile::remove(dump + "/lin.blf");
        QVERIFY(QFile::copy(name, dump + "/lin.blf"));
    }
}

// the objects in the file: types, versions and sizes as Vector's tools and Wireshark expect them
void TestBlf::testLinObjects()
{
    const QVector<CommFrame> out = linFrames();
    const QString name = dir.filePath("linobjects.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QFile f(name);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray file = f.readAll();
    // one container: uncompress it
    const quint32 containerSize = qFromLittleEndian<quint32>(file.constData() + 144 + 8);
    const quint32 uncompressedSize = qFromLittleEndian<quint32>(file.constData() + 144 + 24);
    QByteArray z = file.mid(144 + 32, containerSize - 32);
    QByteArray prefix(4, '\0');
    qToBigEndian<quint32>(uncompressedSize, prefix.data());
    const QByteArray objects = qUncompress(prefix + z);
    QCOMPARE(quint32(objects.size()), uncompressedSize);

    QStringList seen;
    int pos = 0;
    while (pos + 32 <= objects.size()) {
        const quint32 size = qFromLittleEndian<quint32>(objects.constData() + pos + 8);
        const quint32 type = qFromLittleEndian<quint32>(objects.constData() + pos + 12);
        const quint16 version = qFromLittleEndian<quint16>(objects.constData() + pos + 22);
        seen << QString("%1/%2/v%3").arg(type).arg(size - 32).arg(version);
        if (seen.size() == 1) { // the first LIN_MESSAGE2: ID, DLC in the descriptor, data after it
            const char *d = objects.constData() + pos + 32;
            QCOMPARE(int(quint8(d[37])), 0x31);
            QCOMPARE(int(quint8(d[38])), 8);
            QCOMPARE(int(quint8(d[112])), 0x01);
            QCOMPARE(int(qFromLittleEndian<quint16>(d + 12)), 1); // channel 1 = bus 0
        }
        pos += size + size % 4;
    }
    QCOMPARE(seen, QStringList({"57/152/v1", "57/152/v1", "60/152/v1", "58/72/v1", "57/152/v1", "57/152/v1", "1/16/v0"}));
}

// the older LIN objects (11, 12, 15) some Vector logs hold, loaded from a hand-built file
void TestBlf::testLinLegacyObjects()
{
    auto le16 = [](QByteArray &b, quint16 v) { b.append(char(v & 0xFF)); b.append(char(v >> 8)); };
    auto le32 = [&](QByteArray &b, quint32 v) { le16(b, v & 0xFFFF); le16(b, v >> 16); };
    auto le64 = [&](QByteArray &b, quint64 v) { le32(b, quint32(v)); le32(b, quint32(v >> 32)); };
    auto object = [&](quint32 type, const QByteArray &data, quint64 ns) {
        QByteArray o("LOBJ");
        le16(o, 32); le16(o, 1); le32(o, quint32(32 + data.size())); le32(o, type);
        le32(o, 2); le16(o, 0); le16(o, 0); le64(o, ns);
        o.append(data);
        o.append(QByteArray((32 + data.size()) % 4, '\0'));
        return o;
    };
    auto linMessage = [&](quint8 id, const QByteArray &data, quint8 checksum, quint8 dir) {
        QByteArray d;
        le16(d, 2); d.append(char(id)); d.append(char(data.size()));               // channel 2, ID, DLC
        d.append(data + QByteArray(8 - data.size(), '\0'));
        d.append(QByteArray(4, '\0'));                                             // FSM ID, state, header and full time
        le16(d, checksum); d.append(char(dir)); d.append(char(0)); le32(d, 0);
        return d;
    };
    QByteArray objects = object(11, linMessage(0x31, QByteArray::fromHex("0102030405060708"), 0xDB, 0), 1000000)
                       + object(12, linMessage(0x31, QByteArray::fromHex("0102030405060708"), 0xDB, 0), 2000000)
                       + object(15, QByteArray::fromHex("0200010800000000"), 3000000);       // channel 2, ID 1
    QByteArray file("LOGG");
    le32(file, 144);
    file.append(QByteArray(136, '\0'));
    QByteArray c("LOBJ");
    le16(c, 16); le16(c, 1); le32(c, quint32(32 + objects.size())); le32(c, 10);
    le16(c, 0); c.append(QByteArray(6, '\0')); le32(c, quint32(objects.size())); c.append(QByteArray(4, '\0'));
    file.append(c + objects);
    const QString name = dir.filePath("legacy.blf");
    QFile f(name);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(file);
    f.close();

    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), 3);
    QVERIFY(in.at(0).isLin());
    QCOMPARE(int(in.at(0).getBus()), 1);
    QCOMPARE(in.at(0).payload(), QByteArray::fromHex("0102030405060708"));
    QCOMPARE(in.at(0).linChecksumModel(), CommFrame::LinChecksumClassic); // found from the checksum
    QVERIFY(in.at(0).linChecksumOk());
    QVERIFY(in.at(1).hasLinChecksumError());     // the logger said the checksum was wrong
    QVERIFY(in.at(2).isLinHeaderOnly());
    QCOMPARE(in.at(2).frameId(), 0x01u);
    QCOMPARE(in.at(2).timeStamp().microSeconds(), 3000ll);
}

// LIN frames with errors the device reported: saved as LIN_RCV_ERROR2 (61) and loaded back as receive errors
void TestBlf::testLinReceiveErrors()
{
    QVector<CommFrame> out;
    CommFrame framing = lin(0x10, QByteArray::fromHex("4A5593E5"), 0xE6, 0, 1000);
    framing.setLinErrors(CommFrame::LinErrFraming);
    CommFrame shortResponse = lin(0x11, QByteArray::fromHex("4A"), -1, 0, 2000);
    shortResponse.setLinErrors(CommFrame::LinErrIncompleteResponse);
    CommFrame headerError = lin(0x12, QByteArray(), -1, 1, 3000);
    headerError.setLinErrors(CommFrame::LinErrSync);
    CommFrame parity = lin(0x31, QByteArray::fromHex("0102030405060708"), 0xDB, 0, 4000);
    parity.setLinReceivedPid(0x31); // should be 0xB1
    out << framing << shortResponse << headerError << parity;

    const QString name = dir.filePath("linerrors.blf");
    BLFHandler writer;
    QVERIFY(writer.saveBLF(name, &out));
    QCOMPARE(writer.skippedFrames(), 0);
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(in.size(), out.size());
    for (int i = 0; i < out.size(); i++) {
        QVERIFY(in.at(i).isLin());
        QCOMPARE(in.at(i).frameId(), out.at(i).frameId());
        QCOMPARE(in.at(i).getBus(), out.at(i).getBus());
        QCOMPARE(in.at(i).payload(), out.at(i).payload());
        QVERIFY(in.at(i).linErrors() & CommFrame::LinErrFraming); // BLF says "receive error", not which
        QVERIFY(in.at(i).hasLinError());
    }
    QVERIFY(in.at(1).linErrors() & CommFrame::LinErrIncompleteResponse); // the short response is kept
    QVERIFY(!(in.at(0).linErrors() & CommFrame::LinErrIncompleteResponse));

    // the objects: four LIN_RCV_ERROR2, 152 bytes, version 1
    QFile f(name);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray file = f.readAll();
    const quint32 containerSize = qFromLittleEndian<quint32>(file.constData() + 144 + 8);
    const quint32 uncompressedSize = qFromLittleEndian<quint32>(file.constData() + 144 + 24);
    QByteArray prefix(4, '\0');
    qToBigEndian<quint32>(uncompressedSize, prefix.data());
    const QByteArray objects = qUncompress(prefix + file.mid(144 + 32, containerSize - 32));
    QStringList seen;
    for (int pos = 0; pos + 32 <= objects.size();) {
        const quint32 size = qFromLittleEndian<quint32>(objects.constData() + pos + 8);
        seen << QString("%1/%2/v%3").arg(qFromLittleEndian<quint32>(objects.constData() + pos + 12)).arg(size - 32)
                    .arg(qFromLittleEndian<quint16>(objects.constData() + pos + 22));
        pos += size + size % 4;
    }
    QCOMPARE(seen, QStringList({"61/152/v1", "61/152/v1", "61/152/v1", "61/152/v1"}));

    const QString dump = qEnvironmentVariable("BLF_DUMP_DIR");
    if (!dump.isEmpty()) {
        QFile::remove(dump + "/linerrors.blf");
        QVERIFY(QFile::copy(name, dump + "/linerrors.blf"));
    }
}

// the legacy receive error (14), and sleep (20) and wake-up (21, 62) events, which have no frame: only counted
void TestBlf::testLinEventsAndLegacyReceiveError()
{
    auto le16 = [](QByteArray &b, quint16 v) { b.append(char(v & 0xFF)); b.append(char(v >> 8)); };
    auto le32 = [&](QByteArray &b, quint32 v) { le16(b, v & 0xFFFF); le16(b, v >> 16); };
    auto le64 = [&](QByteArray &b, quint64 v) { le32(b, quint32(v)); le32(b, quint32(v >> 32)); };
    auto object = [&](quint32 type, const QByteArray &data, quint64 ns) {
        QByteArray o("LOBJ");
        le16(o, 32); le16(o, 1); le32(o, quint32(32 + data.size())); le32(o, type);
        le32(o, 2); le16(o, 0); le16(o, 0); le64(o, ns);
        o.append(data);
        o.append(QByteArray((32 + data.size()) % 4, '\0'));
        return o;
    };
    // LIN_RCV_ERROR: channel 1, ID 0x22, DLC 2, FSM ID and state, header and full time, state reason,
    // offending byte, short error 1, no DLC timeout
    const QByteArray rcvError = QByteArray::fromHex("0100" "2202" "0000" "0000" "00" "00" "01" "00");
    // LIN_WAKEUP2: SOF, bit rate, channel 1, reserved, length info, signal, external, reserved
    QByteArray wakeup2;
    le64(wakeup2, 0); le32(wakeup2, 19200); le16(wakeup2, 1); le16(wakeup2, 0);
    wakeup2.append(QByteArray::fromHex("00550000"));
    QByteArray objects = object(20, QByteArray::fromHex("01000100"), 1000000)          // sleep: go-to-sleep frame
                       + object(21, QByteArray::fromHex("01005500"), 2000000)          // wake-up
                       + object(14, rcvError, 3000000)
                       + object(62, wakeup2, 4000000);
    QByteArray file("LOGG");
    le32(file, 144);
    file.append(QByteArray(136, '\0'));
    QByteArray c("LOBJ");
    le16(c, 16); le16(c, 1); le32(c, quint32(32 + objects.size())); le32(c, 10);
    le16(c, 0); c.append(QByteArray(6, '\0')); le32(c, quint32(objects.size())); c.append(QByteArray(4, '\0'));
    file.append(c + objects);
    const QString name = dir.filePath("events.blf");
    QFile f(name);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(file);
    f.close();

    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(reader.linEvents(), 3);
    QCOMPARE(in.size(), 1);
    QVERIFY(in.at(0).isLin());
    QCOMPARE(in.at(0).frameId(), 0x22u);
    QCOMPARE(int(in.at(0).getBus()), 0);
    QVERIFY(in.at(0).payload().isEmpty());
    QCOMPARE(in.at(0).linErrors(), quint8(CommFrame::LinErrFraming | CommFrame::LinErrIncompleteResponse));
    QCOMPARE(in.at(0).timeStamp().microSeconds(), 3000ll);

    // a second load starts counting again
    QVERIFY(reader.loadBLF(name, &in));
    QCOMPARE(reader.linEvents(), 3);
}

QTEST_MAIN(TestBlf)
#include "test_blf.moc"
