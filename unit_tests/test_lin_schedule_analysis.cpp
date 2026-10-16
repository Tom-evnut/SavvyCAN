#include <QtTest/QtTest>
#include <QDir>
#include "linscheduleanalysis.h"

// Working out a LIN schedule from a capture (linscheduleanalysis.h), with made-up captures and, when they're in the
// git-ignored test/lin-data/captures folder, real LIN text logs.
class TestLinScheduleAnalysis : public QObject
{
    Q_OBJECT

private slots:
    void testFindPeriod();
    void testRegularSchedule();
    void testCycleStartsAtLowestId();
    void testRepeatedIdInCycle();
    void testNoCycle();
    void testBusAndEmpty();
    void testLdfSchedule();
    void testRealCaptures();
};

static CommFrame lin(quint8 id, qint64 us, const QByteArray &data, bool rx = true, int bus = 0)
{
    CommFrame f(CommFrame::LINDataFrame);
    f.setFrameId(id);
    f.setPayload(data);
    f.setBus(bus);
    f.setReceived(rx);
    f.setTimeStamp(CommFrame::TimeStamp(0, us));
    if (!data.isEmpty()) {
        f.setLinChecksum(CommFrame::linChecksum(data, id, CommFrame::LinChecksumClassic));
        f.verifyLinChecksum();
    }
    return f;
}

// the BMW E90 flap motors' poll: 2, 6, 7, 8 every 50 ms (each motor every 200 ms); motor 8 misses every fourth
// header, and now and then (cycles 3, 10, 17...) a move command (0x2F, sent by SavvyCAN) goes out 20 ms after
// motor 8's slot
static QVector<CommFrame> e90Capture(int cycles, int startSlot = 0)
{
    QVector<CommFrame> frames;
    const quint8 ids[] = {0x02, 0x06, 0x07, 0x08};
    qint64 t = 0;
    int answered8 = 0;
    for (int c = 0; c < cycles; c++) {
        for (int k = 0; k < 4; k++) {
            if (c == 0 && k < startSlot) continue;
            const quint8 id = ids[k];
            const bool answer = id != 0x08 || (answered8++ % 4) != 3;
            frames << lin(id, t + (k * 50000), answer ? QByteArray::fromHex("0080") : QByteArray());
            if (k == 3 && c % 7 == 3) frames << lin(0x2F, t + k * 50000 + 20000, QByteArray::fromHex("2200C005"), false);
        }
        t += 200000;
    }
    return frames;
}

static QList<quint8> cycleIds(const LinScheduleAnalysis::Result &r)
{
    QList<quint8> out;
    for (const auto &s : r.cycle) out << s.id;
    return out;
}

void TestLinScheduleAnalysis::testFindPeriod()
{
    using LinScheduleAnalysis::findPeriod;
    QList<quint8> seq;
    for (int i = 0; i < 40; i++) seq << quint8(i % 4);
    QCOMPARE(findPeriod(seq), 4);
    QList<quint8> same(20, 5);
    QCOMPARE(findPeriod(same), 1);
    // a frame slipped in now and then still leaves the period
    QList<quint8> noisy;
    for (int i = 0; i < 200; i++) {
        noisy << quint8(i % 4);
        if (i % 40 == 39) noisy << 0x2F;
    }
    QCOMPARE(findPeriod(noisy), 4);
    QCOMPARE(findPeriod(QList<quint8>{1, 2, 3}), 0); // too short to repeat
}

void TestLinScheduleAnalysis::testRegularSchedule()
{
    const LinScheduleAnalysis::Result r = LinScheduleAnalysis::analyse(e90Capture(40));
    QCOMPARE(r.frames, 40 * 4 + 6);
    QCOMPARE(cycleIds(r), QList<quint8>({0x02, 0x06, 0x07, 0x08}));
    for (const auto &slot : r.cycle) QCOMPARE(slot.delayMs, 50.0); // the 0x2F frames don't count
    QCOMPARE(r.cycleMs, 200.0);
    QVERIFY(r.followRate > 0.9 && r.followRate < 1.0); // the 0x2F frames break two steps each
    QCOMPARE(r.ids.size(), 5);

    const auto &m2 = r.ids.at(0);
    QCOMPARE(m2.id, quint8(0x02));
    QCOMPARE(m2.headers, 40);
    QCOMPARE(m2.answered, 40);
    QCOMPARE(m2.meanPeriodMs, 200.0);
    QCOMPARE(m2.jitterMs, 0.0);
    QCOMPARE(m2.lengths, QList<int>({2}));
    QCOMPARE(m2.classic, 40);
    QCOMPARE(m2.enhanced, 0);

    const auto &m8 = r.ids.at(3);
    QCOMPARE(m8.id, quint8(0x08));
    QCOMPARE(m8.answered, 30);
    QCOMPARE(m8.responseRate(), 0.75);
    QCOMPARE(r.cycle.at(3).responseRate, 0.75);

    const auto &move = r.ids.at(4);
    QCOMPARE(move.id, quint8(0x2F));
    QCOMPARE(move.headers, 6);
    QCOMPARE(move.sent, 6);
    QCOMPARE(move.meanPeriodMs, 1400.0);
    QCOMPARE(move.lengths, QList<int>({4}));
}

void TestLinScheduleAnalysis::testCycleStartsAtLowestId()
{
    // the capture starts in the middle of the table: the cycle still starts at its lowest ID
    const LinScheduleAnalysis::Result r = LinScheduleAnalysis::analyse(e90Capture(20, 2));
    QCOMPARE(cycleIds(r), QList<quint8>({0x02, 0x06, 0x07, 0x08}));
}

void TestLinScheduleAnalysis::testRepeatedIdInCycle()
{
    // 0x10 is polled twice a cycle, with different gaps after it
    QVector<CommFrame> frames;
    qint64 t = 0;
    for (int c = 0; c < 30; c++) {
        frames << lin(0x10, t, QByteArray::fromHex("01")) << lin(0x20, t + 10000, QByteArray::fromHex("02"))
               << lin(0x10, t + 20000, QByteArray::fromHex("01")) << lin(0x21, t + 25000, QByteArray());
        t += 40000;
    }
    const LinScheduleAnalysis::Result r = LinScheduleAnalysis::analyse(frames);
    QCOMPARE(cycleIds(r), QList<quint8>({0x10, 0x20, 0x10, 0x21}));
    QCOMPARE(r.cycle.at(0).delayMs, 10.0);
    QCOMPARE(r.cycle.at(1).delayMs, 10.0);
    QCOMPARE(r.cycle.at(2).delayMs, 5.0);
    QCOMPARE(r.cycle.at(3).delayMs, 15.0);
    QCOMPARE(r.cycleMs, 40.0);
    QCOMPARE(r.followRate, 1.0);
    QCOMPARE(r.ids.at(0).meanPeriodMs, 20.0);          // 0x10: every 20 ms on average...
    QCOMPARE(r.ids.at(0).minPeriodMs, 20.0);
    QCOMPARE(r.ids.at(2).answered, 0);                 // 0x21 never answered
}

void TestLinScheduleAnalysis::testNoCycle()
{
    QVector<CommFrame> frames;
    quint32 x = 12345;
    for (int i = 0; i < 200; i++) {
        x = x * 1103515245u + 12345u;
        frames << lin(quint8((x >> 16) % 40), i * 10000, QByteArray::fromHex("00"));
    }
    const LinScheduleAnalysis::Result r = LinScheduleAnalysis::analyse(frames);
    QCOMPARE(r.frames, 200);
    QVERIFY(r.cycle.isEmpty());
    QCOMPARE(r.cycleMs, 0.0);
    QCOMPARE(r.medianGapMs, 10.0);
}

void TestLinScheduleAnalysis::testBusAndEmpty()
{
    QVector<CommFrame> frames = e90Capture(10);
    frames << lin(0x30, 5000, QByteArray::fromHex("01"), true, 1);
    CommFrame can(0x02, QByteArray::fromHex("0102"));
    frames << can; // CAN frames are left out
    QCOMPARE(LinScheduleAnalysis::analyse(frames, 0).frames, 41);
    QCOMPARE(LinScheduleAnalysis::analyse(frames, 1).frames, 1);
    QCOMPARE(LinScheduleAnalysis::analyse(frames).frames, 42);
    const LinScheduleAnalysis::Result none = LinScheduleAnalysis::analyse(frames, 2);
    QCOMPARE(none.frames, 0);
    QVERIFY(none.ids.isEmpty() && none.cycle.isEmpty());
    QVERIFY(LinScheduleAnalysis::analyse(QVector<CommFrame>()).ids.isEmpty());
}

void TestLinScheduleAnalysis::testLdfSchedule()
{
    const LinScheduleAnalysis::Result r = LinScheduleAnalysis::analyse(e90Capture(10));
    const QString named = r.ldfSchedule("E90", [](quint8 id) {
        return id == 0x02 ? QString("Motor02_Status") : QString();
    });
    QCOMPARE(named, QString("Schedule_tables {\n  E90 {\n"
                            "    Motor02_Status delay 50 ms;\n"
                            "    Frame_0x06 delay 50 ms;\n"
                            "    Frame_0x07 delay 50 ms;\n"
                            "    Frame_0x08 delay 50 ms;\n"
                            "  }\n}\n"));
    QVERIFY(r.ldfSchedule("T").contains("Frame_0x02 delay 50 ms;"));
}

// real LIN text logs ("count timestamp d ID data", microseconds), if there are any
void TestLinScheduleAnalysis::testRealCaptures()
{
    QDir captures(QStringLiteral(SAVVYCAN_SOURCE_DIR "/test/lin-data/captures"));
    const QStringList logs = captures.entryList({"*.txt"}, QDir::Files);
    if (logs.isEmpty()) QSKIP("no LIN text logs in test/lin-data/captures");
    for (const QString &name : logs) {
        QFile file(captures.filePath(name));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QVector<CommFrame> frames;
        while (!file.atEnd()) {
            const QStringList t = QString::fromLatin1(file.readLine()).simplified().split(' ');
            bool countOk = false, timeOk = false, idOk = false;
            if (t.size() < 4) continue;
            t.at(0).toInt(&countOk);
            const qint64 us = t.at(1).toLongLong(&timeOk);
            const uint id = t.at(3).toUInt(&idOk, 16);
            if (!countOk || !timeOk || !idOk || id > 0x3F) continue;
            frames << lin(quint8(id), us, QByteArray::fromHex(t.mid(4).join("").toLatin1()));
        }
        const LinScheduleAnalysis::Result r = LinScheduleAnalysis::analyse(frames);
        QVERIFY2(r.frames > 0, qPrintable(name));
        QStringList cycle;
        for (const auto &s : r.cycle) cycle << QString("0x%1 %2 ms").arg(s.id, 2, 16, QChar('0')).arg(s.delayMs);
        qInfo().noquote() << name << ":" << r.frames << "frames," << r.ids.size() << "IDs, cycle"
                          << (cycle.isEmpty() ? QString("none") : cycle.join(", ")) << "=" << r.cycleMs << "ms, followed"
                          << qRound(r.followRate * 100) << "%";
    }
}

QTEST_MAIN(TestLinScheduleAnalysis)
#include "test_lin_schedule_analysis.moc"
