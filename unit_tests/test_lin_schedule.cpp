#include <QtTest/QtTest>
#include "dbc/ldfparser.h"
#include "dbc/linschedule.h"
#include "linschedulerunner.h"
#include "linformat.h"

// LDF schedule tables as kept for sending (LinScheduleSet) and the schedule runner (LinScheduleRunner), with a
// made-up clock.
class TestLinSchedule : public QObject
{
    Q_OBJECT

private slots:
    void testPutBits();
    void testSlotsFromLdf();
    void testInitialData();
    void testChecksumModelFromVersion();
    void testFrameFor();
    void testRunnerTiming();
    void testRunnerLateAndBehind();
    void testRunnerCycles();
    void testRunnerData();
    void testSendingKeepsScheduleChecksum();
    void testSignalValues();
};

static const char *LDF = R"(
LIN_description_file;
LIN_protocol_version = "2.1";
LIN_language_version = "2.1";
LIN_speed = 19.2 kbps;
Nodes {
  Master: Ctrl, 5 ms, 0.1 ms;
  Slaves: Seat;
}
Signals {
  CmdMode: 4, 5, Ctrl, Seat;
  CmdLevel: 8, 171, Ctrl, Seat;
  CmdBytes: 16, {1, 2}, Ctrl, Seat;
  SeatPos: 8, 0, Seat, Ctrl;
  SeatEvt: 8, 0, Seat, Ctrl;
}
Diagnostic_signals {
  MasterReqB0: 8, 0;
  MasterReqB1: 8, 0;
  SlaveRespB0: 8, 0;
}
Frames {
  Command: 0x10, Ctrl, 5 {
    CmdMode, 0;
    CmdLevel, 8;
    CmdBytes, 24;
  }
  SeatStatus: 0x20, Seat, 1 {
    SeatPos, 0;
  }
  SeatEvent: 0x21, Seat, 2 {
    SeatEvt, 8;
  }
}
Sporadic_frames {
  CmdSporadic: Command;
}
Event_triggered_frames {
  Events: 0x3A, SeatEvent;
}
Diagnostic_frames {
  MasterReq: 0x3c {
    MasterReqB0, 0;
    MasterReqB1, 8;
  }
  SlaveResp: 0x3d {
    SlaveRespB0, 0;
  }
}
Schedule_tables {
  Normal {
    Command delay 10 ms;
    SeatStatus delay 20 ms;
    Events delay 5 ms;
    CmdSporadic delay 5 ms;
  }
  Diag {
    MasterReq delay 20 ms;
    SlaveResp delay 20 ms;
    FreeFormat { 0x7f, 0x06, 0xb5, 0xff, 0x7f, 0xff, 0xff, 0xff } delay 20 ms;
    AssignNAD { Seat } delay 20 ms;
    NoSuchFrame delay 15 ms;
  }
}
)";

static LinScheduleSet scheduleSet(const QString &text, QStringList *warnings = nullptr)
{
    LdfDocument doc;
    QString error;
    if (!doc.parse(text, &error)) qWarning() << error;
    return LinScheduleSet::fromLdf(doc, warnings);
}

// a schedule of three slots: a master frame for 10 ms, a header for 20 ms, nothing for 5 ms (35 ms a cycle)
static LinSchedule threeSlots()
{
    LinSchedule s;
    s.name = "Test";
    LinScheduleSlot a;
    a.kind = LinScheduleSlot::MasterFrame;
    a.name = "A";
    a.id = 0x10;
    a.data = QByteArray::fromHex("0102");
    a.delayMs = 10;
    LinScheduleSlot b;
    b.kind = LinScheduleSlot::Header;
    b.name = "B";
    b.id = 0x20;
    b.delayMs = 20;
    LinScheduleSlot c;
    c.name = "C";
    c.delayMs = 5;
    s.entries = {a, b, c};
    return s;
}

static QList<quint32> ids(const QList<CommFrame> &frames)
{
    QList<quint32> out;
    for (const CommFrame &f : frames) out << f.frameId();
    return out;
}

void TestLinSchedule::testPutBits()
{
    QByteArray data(3, char(0xFF));
    LinScheduleSet::putBits(data, 0, 4, 0x5);    // low nibble of byte 0
    LinScheduleSet::putBits(data, 12, 8, 0xA5);  // across bytes 1 and 2, LSB first
    QCOMPARE(data, QByteArray::fromHex("F55FFA"));
    LinScheduleSet::putBits(data, 20, 8, 0x00);  // runs past the end: the rest is dropped
    QCOMPARE(data, QByteArray::fromHex("F55F0A"));
}

void TestLinSchedule::testSlotsFromLdf()
{
    QStringList warnings;
    const LinScheduleSet set = scheduleSet(LDF, &warnings);
    QCOMPARE(set.schedules.size(), 2);
    QCOMPARE(set.checksumModel, CommFrame::LinChecksumEnhanced);

    const LinSchedule *normal = set.find("Normal");
    QVERIFY(normal);
    QCOMPARE(normal->cycleMs(), 40.0);
    QCOMPARE(normal->entries.size(), 4);
    QCOMPARE(normal->entries.at(0).kind, LinScheduleSlot::MasterFrame); // the master publishes Command
    QCOMPARE(normal->entries.at(0).id, quint8(0x10));
    QCOMPARE(normal->entries.at(0).delayMs, 10.0);
    QCOMPARE(normal->entries.at(1).kind, LinScheduleSlot::Header);      // a slave publishes SeatStatus
    QCOMPARE(normal->entries.at(1).id, quint8(0x20));
    QCOMPARE(normal->entries.at(2).kind, LinScheduleSlot::Header);      // event-triggered
    QCOMPARE(normal->entries.at(2).id, quint8(0x3A));
    QCOMPARE(normal->entries.at(3).kind, LinScheduleSlot::Silent);      // sporadic: not sent yet
    QVERIFY(!normal->entries.at(3).note.isEmpty());

    const LinSchedule *diag = set.find("Diag");
    QVERIFY(diag);
    QCOMPARE(diag->entries.at(0).kind, LinScheduleSlot::MasterFrame);   // MasterReq: its initial data
    QCOMPARE(diag->entries.at(0).id, quint8(0x3C));
    QCOMPARE(diag->entries.at(0).data, QByteArray::fromHex("0000FFFFFFFFFFFF"));
    QCOMPARE(diag->entries.at(1).kind, LinScheduleSlot::Header);        // SlaveResp
    QCOMPARE(diag->entries.at(1).id, quint8(0x3D));
    QCOMPARE(diag->entries.at(2).kind, LinScheduleSlot::MasterFrame);   // FreeFormat: its eight bytes
    QCOMPARE(diag->entries.at(2).id, quint8(0x3C));
    QCOMPARE(diag->entries.at(2).data, QByteArray::fromHex("7F06B5FF7FFFFFFF"));
    QCOMPARE(diag->entries.at(3).kind, LinScheduleSlot::Silent);        // AssignNAD: not sent yet
    QCOMPARE(diag->entries.at(4).kind, LinScheduleSlot::Silent);        // not in the file: warned about
    QCOMPARE(warnings.size(), 1);
    QVERIFY(warnings.first().contains("NoSuchFrame"));
    QVERIFY(!set.find("Missing"));
}

void TestLinSchedule::testInitialData()
{
    // Command: CmdMode 5 in bits 0-3, bits 4-7 unused (1), CmdLevel 0xAB, byte 2 unused, CmdBytes {1, 2}
    const LinScheduleSet set = scheduleSet(LDF);
    QCOMPARE(set.find("Normal")->entries.at(0).data, QByteArray::fromHex("F5ABFF0102"));
}

void TestLinSchedule::testChecksumModelFromVersion()
{
    QString text = QString(LDF);
    text.replace("LIN_protocol_version = \"2.1\"", "LIN_protocol_version = \"1.3\"");
    QCOMPARE(scheduleSet(text).checksumModel, CommFrame::LinChecksumClassic);
    text.replace("LIN_protocol_version = \"1.3\"", "LIN_protocol_version = \"J2602_1_1.0\"");
    QCOMPARE(scheduleSet(text).checksumModel, CommFrame::LinChecksumEnhanced);
}

void TestLinSchedule::testFrameFor()
{
    LinScheduleSlot slot;
    slot.kind = LinScheduleSlot::MasterFrame;
    slot.id = 0x2F;
    // the BMW flap motors' move command for motor 5, as their controller sends it
    const QByteArray move = QByteArray::fromHex("251EBD05");
    CommFrame f = LinScheduleRunner::frameFor(slot, move, CommFrame::LinChecksumClassic, 2);
    QVERIFY(f.isLin());
    QCOMPARE(f.frameId(), quint32(0x2F));
    QCOMPARE(f.getBus(), 2);
    QVERIFY(!f.isReceived());
    QCOMPARE(f.payload(), move);
    QCOMPARE(f.linChecksumByte(), CommFrame::linChecksum(move, 0x2F, CommFrame::LinChecksumClassic));
    QCOMPARE(f.linChecksumModel(), CommFrame::LinChecksumClassic);

    f = LinScheduleRunner::frameFor(slot, move, CommFrame::LinChecksumEnhanced, 0);
    QCOMPARE(f.linChecksumByte(), CommFrame::linChecksum(move, 0x2F, CommFrame::LinChecksumEnhanced));
    QCOMPARE(f.linChecksumModel(), CommFrame::LinChecksumEnhanced);

    slot.id = 0x3C; // the master request: always classic
    f = LinScheduleRunner::frameFor(slot, QByteArray::fromHex("7F06B5FF7FFFFFFF"), CommFrame::LinChecksumEnhanced, 0);
    QCOMPARE(f.linChecksumModel(), CommFrame::LinChecksumClassic);

    slot.kind = LinScheduleSlot::Header; // a header alone: no data, no checksum
    slot.id = 0x02;
    f = LinScheduleRunner::frameFor(slot, QByteArray(), CommFrame::LinChecksumClassic, 0);
    QVERIFY(f.payload().isEmpty());
    QVERIFY(!f.hasLinChecksum());
}

void TestLinSchedule::testRunnerTiming()
{
    LinScheduleRunner r;
    r.setSchedule(threeSlots(), CommFrame::LinChecksumEnhanced);
    QVERIFY(!r.isRunning());
    QVERIFY(r.due(0).isEmpty());

    r.start(1000);
    QVERIFY(r.isRunning());
    QCOMPARE(ids(r.due(1000)), QList<quint32>({0x10}));     // slot 1 at once
    QCOMPARE(r.nextDueUs(), qint64(11000));
    QVERIFY(r.due(10999).isEmpty());
    QCOMPARE(ids(r.due(11000)), QList<quint32>({0x20}));    // slot 2 10 ms later
    QCOMPARE(r.nextDueUs(), qint64(31000));
    QVERIFY(r.due(31000).isEmpty());                         // slot 3 sends nothing...
    QCOMPARE(r.nextDueUs(), qint64(36000));                  // ...but takes its 5 ms
    QCOMPARE(r.completedCycles(), quint64(1));
    QCOMPARE(ids(r.due(36000)), QList<quint32>({0x10}));
    QCOMPARE(r.framesSent(), quint64(3));
    QCOMPARE(r.nextSlot(), 1);

    r.stop();
    QVERIFY(!r.isRunning());
    QVERIFY(r.due(100000).isEmpty());
}

void TestLinSchedule::testRunnerLateAndBehind()
{
    LinScheduleRunner r;
    r.setSchedule(threeSlots(), CommFrame::LinChecksumEnhanced);
    r.start(0);
    r.due(0);
    // 3 ms late for slot 2: it goes now, and slot 3 keeps its time (no drift)
    QCOMPARE(ids(r.due(13000)), QList<quint32>({0x20}));
    QCOMPARE(r.nextDueUs(), qint64(30000));
    // late by less than a cycle: what's due goes out in order
    QCOMPARE(ids(r.due(46000)), QList<quint32>({0x10, 0x20}));
    QCOMPARE(r.nextDueUs(), qint64(65000));
    // more than a cycle behind: carries on from now, with no burst
    QCOMPARE(ids(r.due(1000000)), QList<quint32>());         // slot 3 (silent), now
    QCOMPARE(r.nextDueUs(), qint64(1005000));
    QCOMPARE(ids(r.due(1005000)), QList<quint32>({0x10}));
}

void TestLinSchedule::testRunnerCycles()
{
    LinScheduleRunner r;
    r.setSchedule(threeSlots(), CommFrame::LinChecksumEnhanced);
    r.start(0, 2);
    QList<quint32> sent;
    for (qint64 t = 0; t < 200000; t += 1000) sent << ids(r.due(t));
    QCOMPARE(sent, QList<quint32>({0x10, 0x20, 0x10, 0x20}));
    QVERIFY(!r.isRunning());
    QCOMPARE(r.completedCycles(), quint64(2));

    // an empty schedule doesn't run; a new schedule stops the run
    r.setSchedule(LinSchedule(), CommFrame::LinChecksumEnhanced);
    r.start(0);
    QVERIFY(!r.isRunning());
    r.setSchedule(threeSlots(), CommFrame::LinChecksumEnhanced);
    r.start(0);
    r.setSchedule(threeSlots(), CommFrame::LinChecksumEnhanced);
    QVERIFY(!r.isRunning());
}

void TestLinSchedule::testRunnerData()
{
    LinScheduleRunner r;
    const LinSchedule s = threeSlots();
    r.setSchedule(s, CommFrame::LinChecksumClassic);
    QCOMPARE(r.dataFor(s.entries.at(0)), QByteArray::fromHex("0102"));
    r.setData("A", QByteArray::fromHex("0A0B0C0D0E0F10111213")); // at most 8 bytes are kept
    QCOMPARE(r.dataFor(s.entries.at(0)), QByteArray::fromHex("0A0B0C0D0E0F1011"));
    r.setData("A", QByteArray::fromHex("2100C005"));
    r.start(0);
    const QList<CommFrame> frames = r.due(0);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().payload(), QByteArray::fromHex("2100C005"));
    // kept when the schedule changes, until cleared
    r.setSchedule(s, CommFrame::LinChecksumClassic);
    QCOMPARE(r.dataFor(s.entries.at(0)), QByteArray::fromHex("2100C005"));
    r.clearData();
    QCOMPARE(r.dataFor(s.entries.at(0)), QByteArray::fromHex("0102"));
}

void TestLinSchedule::testSendingKeepsScheduleChecksum()
{
    // a schedule's frame (classic, from a LIN 1.3 LDF) sent on a bus whose checksum setting is enhanced keeps its
    // checksum and model
    LinScheduleSlot slot;
    slot.kind = LinScheduleSlot::MasterFrame;
    slot.id = 0x2F;
    const CommFrame scheduled = LinScheduleRunner::frameFor(slot, QByteArray::fromHex("251EBD05"),
                                                            CommFrame::LinChecksumClassic, 0);
    CommFrame out;
    QVERIFY(LinFormat::toLinForSending(scheduled, CommFrame::LinChecksumEnhanced, out));
    QCOMPARE(out.linChecksumByte(), scheduled.linChecksumByte());
    QCOMPARE(out.linChecksumModel(), CommFrame::LinChecksumClassic);

    // a header alone stays a header alone
    slot.kind = LinScheduleSlot::Header;
    slot.id = 0x02;
    QVERIFY(LinFormat::toLinForSending(LinScheduleRunner::frameFor(slot, QByteArray(), CommFrame::LinChecksumClassic, 0),
                                       CommFrame::LinChecksumEnhanced, out));
    QVERIFY(out.payload().isEmpty());
}

// setting signals from typed values (the runner's signal editor)
void TestLinSchedule::testSignalValues()
{
    // the BMW flap motors' move command: address (8 bits, value table), position (16 bits), speed (8 bits)
    LinSignal::Spec address;
    address.startBit = 0;
    address.size = 8;
    address.values = {{0x21, "Motor 1"}, {0x22, "Motor 2"}};
    LinSignal::Spec position;
    position.startBit = 8;
    position.size = 16;
    QByteArray data = QByteArray::fromHex("2100C005");
    QCOMPARE(LinSignal::rawValue(data, address), qint64(0x21));
    QCOMPARE(LinSignal::rawValue(data, position), qint64(0xC000));

    quint64 raw = 0;
    QString why;
    QVERIFY(LinSignal::rawFromText("motor 2", address, raw)); // value table names, any case
    QCOMPARE(raw, quint64(0x22));
    QVERIFY(LinSignal::rawFromText("0xBD1E", position, raw)); // raw in hex
    QCOMPARE(raw, quint64(0xBD1E));
    LinScheduleSet::putBits(data, position.startBit, position.size, raw);
    QCOMPARE(data, QByteArray::fromHex("211EBD05"));
    QVERIFY(LinSignal::rawFromText("48384", position, raw)); // a physical value, factor 1
    QCOMPARE(raw, quint64(48384));
    QVERIFY(!LinSignal::rawFromText("0x10000", position, raw, &why)); // too big for 16 bits
    QVERIFY(!why.isEmpty());
    QVERIFY(!LinSignal::rawFromText("70000", position, raw, &why));
    QVERIFY(!LinSignal::rawFromText("Motor 9", address, raw, &why)); // not a value or a number
    QVERIFY(!LinSignal::rawFromText("  ", address, raw, &why));

    // scaled: 0.5 per bit, -40 offset (a temperature)
    LinSignal::Spec temp;
    temp.startBit = 4;
    temp.size = 8;
    temp.factor = 0.5;
    temp.bias = -40;
    QVERIFY(LinSignal::rawFromText("21.5", temp, raw));
    QCOMPARE(raw, quint64(123));
    QVERIFY(LinSignal::rawFromText("-40", temp, raw));
    QCOMPARE(raw, quint64(0));
    QVERIFY(!LinSignal::rawFromText("-41", temp, raw)); // below the raw range
    QByteArray t(2, char(0xFF));
    QVERIFY(LinSignal::rawFromText("21.5", temp, raw));
    LinScheduleSet::putBits(t, temp.startBit, temp.size, raw);
    QCOMPARE(t, QByteArray::fromHex("BFF7")); // 123 = 0x7B at bit 4, the other bits untouched
    QCOMPARE(LinSignal::rawValue(t, temp), qint64(123));

    // signed: two's complement in the signal's size
    LinSignal::Spec offset;
    offset.size = 6;
    offset.isSigned = true;
    QVERIFY(LinSignal::rawFromText("-3", offset, raw));
    QCOMPARE(raw, quint64(0x3D));
    QVERIFY(!LinSignal::rawFromText("32", offset, raw)); // -32..31
    QVERIFY(LinSignal::rawFromText("-32", offset, raw));
    QByteArray o(1, '\0');
    LinScheduleSet::putBits(o, 0, 6, 0x3D);
    QCOMPARE(LinSignal::rawValue(o, offset), qint64(-3));
}

QTEST_MAIN(TestLinSchedule)
#include "test_lin_schedule.moc"
