#include <QtTest/QtTest>
#include <QVector>
#include "can_structs.h"
#include "re/flexrayscheduleanalysis.h"

/*
 * Unit test for the pure FlexRay schedule analysis (FrScheduleAnalysis) - the GUI-free aggregation
 * and P2 logic behind the slot x cycle matrix view: aggregate(), classifyCell(), summarizeSlot(),
 * inferRepetition() and slotPassesFilters(). No GUI/QApplication, no hardware.
 */
class TestFlexRaySchedule : public QObject
{
    Q_OBJECT

private slots:
    void testAggregate();              // P1: fold frames into the (channel, slot, cycle) matrix
    void testClassifyCell();           // P2: empty / static / changing / error
    void testCycleMultiplexDetection(); // P2: payload differs between cycles -> headline flag
    void testRepetitionInference();    // P2: inferred period + base cycle
    void testSummaryAndFilters();      // P2: per-slot summary + the four filters

private:
    //build a FlexRay frame with the given channel/slot/cycle/payload/timestamp
    static CommFrame frFrame(quint8 bus, quint32 slot, quint8 cycle,
                             const QByteArray &payload, quint64 timeUs)
    {
        CommFrame f(CommFrame::FLEXRAYDataFrame);
        f.setBus(bus);
        f.setFrameId(slot);
        f.setFlexRayCycle(cycle);
        f.setPayload(payload);
        f.setTimeStamp(CommFrame::TimeStamp::fromMicroSeconds(static_cast<qint64>(timeUs)));
        return f;
    }

    //set one cell of a slot row directly (for the pure-logic tests that don't need frames)
    static void setCell(FrSlotRow &row, int cycle, quint32 count, const QByteArray &lastPayload,
                        bool payloadVaries = false, quint32 nullCount = 0, bool crcFail = false)
    {
        FrSlotCycleCell &c = row.cells[cycle];
        c.count = count;
        c.nullCount = nullCount;
        c.lastPayload = lastPayload;
        c.payloadVaries = payloadVaries;
        c.anyNull = nullCount > 0;
        c.anyCrcFail = crcFail;
    }
};

void TestFlexRaySchedule::testAggregate()
{
    QVector<CommFrame> frames;

    // static slot 0x100 on cycle 0: same payload twice -> count 2, payload stable
    frames.append(frFrame(0, 0x100, 0, QByteArray::fromHex("1122"), 1000));
    frames.append(frFrame(0, 0x100, 0, QByteArray::fromHex("1122"), 2000));

    // slot 0x300 on cycle 0: two DIFFERENT payloads over time -> payloadVaries
    frames.append(frFrame(0, 0x300, 0, QByteArray::fromHex("AA"), 1500));
    frames.append(frFrame(0, 0x300, 0, QByteArray::fromHex("BB"), 2500));

    // cycle-multiplexed slot 0x200: distinct payload on each of cycles 0..3
    frames.append(frFrame(0, 0x200, 0, QByteArray::fromHex("00"), 100));
    frames.append(frFrame(0, 0x200, 1, QByteArray::fromHex("01"), 200));
    frames.append(frFrame(0, 0x200, 2, QByteArray::fromHex("02"), 300));
    frames.append(frFrame(0, 0x200, 3, QByteArray::fromHex("03"), 400));

    // every-4-cycles slot 0x400: cycles 0, 4, 8 only
    frames.append(frFrame(0, 0x400, 0, QByteArray::fromHex("77"), 10));
    frames.append(frFrame(0, 0x400, 4, QByteArray::fromHex("77"), 20));
    frames.append(frFrame(0, 0x400, 8, QByteArray::fromHex("77"), 30));

    // CRC-failed frame: slot 0x500 cycle 0
    {
        CommFrame f = frFrame(0, 0x500, 0, QByteArray::fromHex("DE"), 5000);
        f.setFlexRayCrcStatus(false, false); //checked by device, failed
        frames.append(f);
    }

    // null frame: slot 0x600 cycle 0
    {
        CommFrame f = frFrame(0, 0x600, 0, QByteArray(), 6000);
        f.setFlexRayNullFrame(true);
        frames.append(f);
    }

    // a second channel (bus 1): slot 0x100 cycle 0
    frames.append(frFrame(1, 0x100, 0, QByteArray::fromHex("EE"), 7000));

    // a non-FlexRay CAN frame on a bus with no FlexRay traffic: must be ignored
    {
        CommFrame can(0x123, QByteArray::fromHex("0102"));
        can.setBus(5);
        frames.append(can);
    }

    const QMap<quint8, FrChannelSchedule> sched = FrScheduleAnalysis::aggregate(frames);

    QVERIFY(sched.contains(0));
    QVERIFY(sched.contains(1));
    QVERIFY(!sched.contains(5));

    const FrChannelSchedule &fr0 = sched[0];

    const FrSlotCycleCell *c100 = fr0.cell(0x100, 0);
    QVERIFY(c100 != nullptr);
    QVERIFY(c100->present());
    QCOMPARE(c100->count, quint32(2));
    QCOMPARE(c100->payloadVaries, false);
    QCOMPARE(c100->lastPayload, QByteArray::fromHex("1122"));
    QCOMPARE(c100->lastTimeUs, quint64(2000));

    const FrSlotCycleCell *c300 = fr0.cell(0x300, 0);
    QVERIFY(c300 != nullptr);
    QCOMPARE(c300->count, quint32(2));
    QCOMPARE(c300->payloadVaries, true);
    QCOMPARE(c300->lastPayload, QByteArray::fromHex("BB"));

    for (int cyc = 0; cyc <= 3; ++cyc)
    {
        const FrSlotCycleCell *cell = fr0.cell(0x200, cyc);
        QVERIFY(cell != nullptr);
        QVERIFY(cell->present());
        QCOMPARE(cell->count, quint32(1));
        QCOMPARE(cell->payloadVaries, false);
        QCOMPARE(cell->lastPayload, QByteArray(1, static_cast<char>(cyc)));
    }
    QVERIFY(!fr0.cell(0x200, 4)->present());

    QVERIFY(fr0.cell(0x400, 0)->present());
    QVERIFY(fr0.cell(0x400, 4)->present());
    QVERIFY(fr0.cell(0x400, 8)->present());
    QVERIFY(!fr0.cell(0x400, 1)->present());

    const FrSlotCycleCell *c500 = fr0.cell(0x500, 0);
    QVERIFY(c500 != nullptr);
    QCOMPARE(c500->anyCrcFail, true);
    QCOMPARE(c500->anyNull, false);

    const FrSlotCycleCell *c600 = fr0.cell(0x600, 0);
    QVERIFY(c600 != nullptr);
    QVERIFY(c600->present());
    QCOMPARE(c600->anyNull, true);
    QCOMPARE(c600->nullCount, quint32(1));   // P2: null frames are counted
    QCOMPARE(c600->hasData(), false);        // the only frame was null

    QVERIFY(fr0.cell(0x100, -1) == nullptr);
    QVERIFY(fr0.cell(0x100, FR_NUM_CYCLES) == nullptr);
    QVERIFY(fr0.cell(0x7AA, 0) == nullptr);

    const FrChannelSchedule &fr1 = sched[1];
    QCOMPARE(fr1.slotRows.size(), 1);
    QCOMPARE(fr1.cell(0x100, 0)->lastPayload, QByteArray::fromHex("EE"));

    QCOMPARE(fr0.slotRows.size(), 6);
}

void TestFlexRaySchedule::testClassifyCell()
{
    FrSlotCycleCell empty;
    QVERIFY(FrScheduleAnalysis::classifyCell(empty) == FrCellState::Empty);

    FrSlotCycleCell stat;
    stat.count = 3; stat.lastPayload = QByteArray::fromHex("AA");
    QVERIFY(FrScheduleAnalysis::classifyCell(stat) == FrCellState::Static);

    FrSlotCycleCell changing;
    changing.count = 3; changing.payloadVaries = true; changing.lastPayload = QByteArray::fromHex("BB");
    QVERIFY(FrScheduleAnalysis::classifyCell(changing) == FrCellState::Changing);

    FrSlotCycleCell crc;
    crc.count = 1; crc.anyCrcFail = true; crc.lastPayload = QByteArray::fromHex("CC");
    QVERIFY(FrScheduleAnalysis::classifyCell(crc) == FrCellState::Error);

    FrSlotCycleCell allNull;
    allNull.count = 2; allNull.nullCount = 2; allNull.anyNull = true; // every frame null -> no data
    QVERIFY(FrScheduleAnalysis::classifyCell(allNull) == FrCellState::Error);

    FrSlotCycleCell mixedNull;
    mixedNull.count = 3; mixedNull.nullCount = 1; mixedNull.anyNull = true;
    mixedNull.lastPayload = QByteArray::fromHex("DD"); // still carries real data frames
    QVERIFY(FrScheduleAnalysis::classifyCell(mixedNull) == FrCellState::Static);
}

void TestFlexRaySchedule::testCycleMultiplexDetection()
{
    // same payload across two cycles -> NOT multiplexed
    FrSlotRow stat; stat.slotId = 0x10;
    setCell(stat, 0, 5, QByteArray::fromHex("AA"));
    setCell(stat, 1, 5, QByteArray::fromHex("AA"));
    FrSlotSummary sStat = FrScheduleAnalysis::summarizeSlot(stat);
    QCOMPARE(sStat.cycleMultiplexed, false);
    QVERIFY(sStat.isStatic());

    // different payload per cycle -> multiplexed (the headline flag)
    FrSlotRow mux; mux.slotId = 0x20;
    setCell(mux, 0, 5, QByteArray::fromHex("AA"));
    setCell(mux, 1, 5, QByteArray::fromHex("BB"));
    FrSlotSummary sMux = FrScheduleAnalysis::summarizeSlot(mux);
    QCOMPARE(sMux.cycleMultiplexed, true);
    QVERIFY(!sMux.isStatic());

    // only one active cycle -> not multiplexed
    FrSlotRow single; single.slotId = 0x30;
    setCell(single, 7, 2, QByteArray::fromHex("CC"));
    FrSlotSummary sSingle = FrScheduleAnalysis::summarizeSlot(single);
    QCOMPARE(sSingle.cycleMultiplexed, false);

    // null-only cells must not count as distinct payloads
    FrSlotRow withNull; withNull.slotId = 0x40;
    setCell(withNull, 0, 3, QByteArray::fromHex("AA"));
    setCell(withNull, 2, 2, QByteArray(), /*varies*/false, /*nullCount*/2); // all null here
    FrSlotSummary sNull = FrScheduleAnalysis::summarizeSlot(withNull);
    QCOMPARE(sNull.cycleMultiplexed, false); // only one real payload (AA); the null cell is ignored
    QCOMPARE(sNull.anyNull, true);
}

void TestFlexRaySchedule::testRepetitionInference()
{
    int base = -99;

    QCOMPARE(FrScheduleAnalysis::inferRepetition(QVector<int>{0, 1, 2, 3}, base), 1);
    QCOMPARE(base, 0);

    QCOMPARE(FrScheduleAnalysis::inferRepetition(QVector<int>{0, 2, 4, 6}, base), 2);
    QCOMPARE(base, 0);

    QCOMPARE(FrScheduleAnalysis::inferRepetition(QVector<int>{1, 5, 9, 13}, base), 4);
    QCOMPARE(base, 1);

    QCOMPARE(FrScheduleAnalysis::inferRepetition(QVector<int>{2, 10, 18}, base), 8);
    QCOMPARE(base, 2);

    // single active cycle -> no period, base is that cycle
    QCOMPARE(FrScheduleAnalysis::inferRepetition(QVector<int>{3}, base), 0);
    QCOMPARE(base, 3);

    // empty -> no period, base -1
    QCOMPARE(FrScheduleAnalysis::inferRepetition(QVector<int>{}, base), 0);
    QCOMPARE(base, -1);

    // the human-readable string
    FrSlotRow every4; every4.slotId = 0x50;
    setCell(every4, 1, 1, QByteArray::fromHex("AA"));
    setCell(every4, 5, 1, QByteArray::fromHex("AA"));
    setCell(every4, 9, 1, QByteArray::fromHex("AA"));
    FrSlotSummary s4 = FrScheduleAnalysis::summarizeSlot(every4);
    QCOMPARE(s4.period, 4);
    QCOMPARE(s4.baseCycle, 1);
    QCOMPARE(s4.repeatText(), QString("every 4 (base 1)"));
}

void TestFlexRaySchedule::testSummaryAndFilters()
{
    // a plain static slot: one cycle, stable payload
    FrSlotRow stat; stat.slotId = 0x01;
    setCell(stat, 0, 10, QByteArray::fromHex("AA"));
    FrSlotSummary sStat = FrScheduleAnalysis::summarizeSlot(stat);
    QCOMPARE(sStat.totalCount, quint64(10));
    QVERIFY(sStat.isStatic());

    // a cycle-multiplexed slot
    FrSlotRow mux; mux.slotId = 0x02;
    setCell(mux, 0, 4, QByteArray::fromHex("AA"));
    setCell(mux, 1, 4, QByteArray::fromHex("BB"));
    FrSlotSummary sMux = FrScheduleAnalysis::summarizeSlot(mux);
    QCOMPARE(sMux.totalCount, quint64(8));
    QVERIFY(sMux.cycleMultiplexed);

    // a null-only slot
    FrSlotRow nullSlot; nullSlot.slotId = 0x03;
    setCell(nullSlot, 0, 3, QByteArray(), false, 3);
    FrSlotSummary sNull = FrScheduleAnalysis::summarizeSlot(nullSlot);
    QCOMPARE(sNull.totalCount, quint64(3));
    QCOMPARE(sNull.nullTotal, quint64(3));

    // a low-count slot
    FrSlotRow few; few.slotId = 0x04;
    setCell(few, 0, 1, QByteArray::fromHex("CC"));
    FrSlotSummary sFew = FrScheduleAnalysis::summarizeSlot(few);

    // no filters -> everything passes
    FrFilterSettings none;
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sStat, none));
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sMux, none));
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sNull, none));

    // hide static: drops the static slot, keeps the mux slot
    FrFilterSettings hideStatic; hideStatic.hideStatic = true;
    QVERIFY(!FrScheduleAnalysis::slotPassesFilters(sStat, hideStatic));
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sMux, hideStatic));

    // only multiplexed: keeps only the mux slot
    FrFilterSettings onlyMux; onlyMux.onlyMultiplexed = true;
    QVERIFY(!FrScheduleAnalysis::slotPassesFilters(sStat, onlyMux));
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sMux, onlyMux));

    // hide null: drops the null-only slot, keeps a real one
    FrFilterSettings hideNull; hideNull.hideNull = true;
    QVERIFY(!FrScheduleAnalysis::slotPassesFilters(sNull, hideNull));
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sStat, hideNull));

    // min-count threshold: drops the 1-frame slot, keeps the 10-frame slot
    FrFilterSettings minCnt; minCnt.minCount = 5;
    QVERIFY(!FrScheduleAnalysis::slotPassesFilters(sFew, minCnt));
    QVERIFY(FrScheduleAnalysis::slotPassesFilters(sStat, minCnt));
}

QTEST_MAIN(TestFlexRaySchedule)
#include "test_flexray_schedule.moc"
