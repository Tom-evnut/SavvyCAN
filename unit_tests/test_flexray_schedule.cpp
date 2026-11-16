#include <QtTest/QtTest>
#include <QVector>
#include "can_structs.h"
#include "re/flexrayslotcyclemodel.h"

/*
 * Unit test for the FlexRay slot x cycle aggregator (FlexRaySlotCycleModel::aggregate) - the pure,
 * GUI-free fold from a frame list into the per-channel (slot, cycle) matrix that backs the view.
 *
 * Covers: a static slot, a time-varying payload on one (slot, cycle), a cycle-multiplexed slot,
 * an every-4-cycles slot, a CRC-failed frame, a null frame, per-channel separation, and that
 * non-FlexRay frames are ignored.
 */
class TestFlexRaySchedule : public QObject
{
    Q_OBJECT

private slots:
    void testAggregate();

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
};

void TestFlexRaySchedule::testAggregate()
{
    QVector<CommFrame> frames;

    // --- static slot 0x100 on cycle 0: same payload twice -> count 2, payload stable ---
    frames.append(frFrame(0, 0x100, 0, QByteArray::fromHex("1122"), 1000));
    frames.append(frFrame(0, 0x100, 0, QByteArray::fromHex("1122"), 2000));

    // --- slot 0x300 on cycle 0: two DIFFERENT payloads over time -> payloadVaries ---
    frames.append(frFrame(0, 0x300, 0, QByteArray::fromHex("AA"), 1500));
    frames.append(frFrame(0, 0x300, 0, QByteArray::fromHex("BB"), 2500));

    // --- cycle-multiplexed slot 0x200: distinct payload on each of cycles 0..3 ---
    frames.append(frFrame(0, 0x200, 0, QByteArray::fromHex("00"), 100));
    frames.append(frFrame(0, 0x200, 1, QByteArray::fromHex("01"), 200));
    frames.append(frFrame(0, 0x200, 2, QByteArray::fromHex("02"), 300));
    frames.append(frFrame(0, 0x200, 3, QByteArray::fromHex("03"), 400));

    // --- every-4-cycles slot 0x400: cycles 0, 4, 8 only ---
    frames.append(frFrame(0, 0x400, 0, QByteArray::fromHex("77"), 10));
    frames.append(frFrame(0, 0x400, 4, QByteArray::fromHex("77"), 20));
    frames.append(frFrame(0, 0x400, 8, QByteArray::fromHex("77"), 30));

    // --- CRC-failed frame: slot 0x500 cycle 0 ---
    {
        CommFrame f = frFrame(0, 0x500, 0, QByteArray::fromHex("DE"), 5000);
        f.setFlexRayCrcStatus(false, false); //checked by device, failed
        frames.append(f);
    }

    // --- null frame: slot 0x600 cycle 0 ---
    {
        CommFrame f = frFrame(0, 0x600, 0, QByteArray(), 6000);
        f.setFlexRayNullFrame(true);
        frames.append(f);
    }

    // --- a second channel (bus 1): slot 0x100 cycle 0 ---
    frames.append(frFrame(1, 0x100, 0, QByteArray::fromHex("EE"), 7000));

    // --- a non-FlexRay CAN frame on a bus with no FlexRay traffic: must be ignored ---
    {
        CommFrame can(0x123, QByteArray::fromHex("0102"));
        can.setBus(5);
        frames.append(can);
    }

    const QMap<quint8, FrChannelSchedule> sched = FlexRaySlotCycleModel::aggregate(frames);

    // two FlexRay channels only (bus 0 and bus 1); the CAN frame's bus 5 must not appear
    QVERIFY(sched.contains(0));
    QVERIFY(sched.contains(1));
    QVERIFY(!sched.contains(5));

    const FrChannelSchedule &fr0 = sched[0];

    // static slot
    const FrSlotCycleCell *c100 = fr0.cell(0x100, 0);
    QVERIFY(c100 != nullptr);
    QVERIFY(c100->present());
    QCOMPARE(c100->count, quint32(2));
    QCOMPARE(c100->payloadVaries, false);
    QCOMPARE(c100->lastPayload, QByteArray::fromHex("1122"));
    QCOMPARE(c100->lastTimeUs, quint64(2000)); //most recent frame's timestamp

    // time-varying payload on one (slot, cycle)
    const FrSlotCycleCell *c300 = fr0.cell(0x300, 0);
    QVERIFY(c300 != nullptr);
    QCOMPARE(c300->count, quint32(2));
    QCOMPARE(c300->payloadVaries, true);
    QCOMPARE(c300->lastPayload, QByteArray::fromHex("BB"));

    // cycle-multiplexed slot: present on cycles 0..3 with distinct payloads, each cell stable
    for (int cyc = 0; cyc <= 3; ++cyc)
    {
        const FrSlotCycleCell *cell = fr0.cell(0x200, cyc);
        QVERIFY(cell != nullptr);
        QVERIFY(cell->present());
        QCOMPARE(cell->count, quint32(1));
        QCOMPARE(cell->payloadVaries, false);
        QCOMPARE(cell->lastPayload, QByteArray(1, static_cast<char>(cyc)));
    }
    // cycle 4 was never seen for 0x200
    QVERIFY(!fr0.cell(0x200, 4)->present());

    // every-4-cycles slot: present on 0/4/8, empty on the cycles in between
    QVERIFY(fr0.cell(0x400, 0)->present());
    QVERIFY(fr0.cell(0x400, 4)->present());
    QVERIFY(fr0.cell(0x400, 8)->present());
    QVERIFY(!fr0.cell(0x400, 1)->present());
    QVERIFY(!fr0.cell(0x400, 2)->present());
    QVERIFY(!fr0.cell(0x400, 3)->present());

    // CRC-failed frame flagged
    const FrSlotCycleCell *c500 = fr0.cell(0x500, 0);
    QVERIFY(c500 != nullptr);
    QVERIFY(c500->present());
    QCOMPARE(c500->anyCrcFail, true);
    QCOMPARE(c500->anyNull, false);

    // null frame flagged (still counted)
    const FrSlotCycleCell *c600 = fr0.cell(0x600, 0);
    QVERIFY(c600 != nullptr);
    QVERIFY(c600->present());
    QCOMPARE(c600->anyNull, true);

    // out-of-range cycle lookups are safe (return nullptr), not a crash
    QVERIFY(fr0.cell(0x100, -1) == nullptr);
    QVERIFY(fr0.cell(0x100, FR_NUM_CYCLES) == nullptr);
    // a slot that was never seen
    QVERIFY(fr0.cell(0x7AA, 0) == nullptr);

    // channel separation: bus 1 carries its own 0x100, independent of bus 0
    const FrChannelSchedule &fr1 = sched[1];
    QCOMPARE(fr1.slots.size(), 1);
    const FrSlotCycleCell *c100b1 = fr1.cell(0x100, 0);
    QVERIFY(c100b1 != nullptr);
    QCOMPARE(c100b1->lastPayload, QByteArray::fromHex("EE"));

    // bus 0 holds exactly the six FlexRay slots we injected
    QCOMPARE(fr0.slots.size(), 6);
}

QTEST_MAIN(TestFlexRaySchedule)
#include "test_flexray_schedule.moc"
