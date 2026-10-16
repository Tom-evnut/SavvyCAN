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
    QCOMPARE(writer.skippedFrames(), 2);
    QVector<CommFrame> in;
    BLFHandler reader;
    QVERIFY(reader.loadBLF(dir.filePath("skipped.blf"), &in));
    QCOMPARE(in.size(), 1);
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

QTEST_MAIN(TestBlf)
#include "test_blf.moc"
