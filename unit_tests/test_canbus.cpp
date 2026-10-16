#include <QtTest/QtTest>
#include "canbus.h"

// CANBus: the bus type and LIN settings, and the stream format staying readable for version 1 data.
class TestCanBus : public QObject
{
    Q_OBJECT

private slots:
    void testDefaults();
    void testLinSettings();
    void testEquality();
    void testStreamRoundTrip();
    void testStreamVersion1Loads();
    void testStreamUnknownVersion();
    void testStreamBadLinValuesIgnored();
};

void TestCanBus::testDefaults()
{
    CANBus bus;
    QCOMPARE(bus.getBusType(), CANBus::BusType::CAN);
    QVERIFY(!bus.isLin());
    QCOMPARE(bus.getLinMode(), CANBus::LinMode::Monitor);
    QCOMPARE(bus.getLinChecksumModel(), CommFrame::LinChecksumUnknown);
    QCOMPARE(bus.getSpeed(), 500000);
}

void TestCanBus::testLinSettings()
{
    CANBus bus;
    bus.setBusType(CANBus::BusType::LIN);
    bus.setSpeed(CANBus::LIN_DEFAULT_SPEED);
    bus.setLinMode(CANBus::LinMode::Slave);
    bus.setLinChecksumModel(CommFrame::LinChecksumEnhanced);
    QVERIFY(bus.isLin());
    QCOMPARE(bus.getSpeed(), 19200);
    QCOMPARE(bus.getLinMode(), CANBus::LinMode::Slave);
    QCOMPARE(bus.getLinChecksumModel(), CommFrame::LinChecksumEnhanced);
}

void TestCanBus::testEquality()
{
    CANBus a, b;
    QVERIFY(a == b);
    b.setBusType(CANBus::BusType::LIN);
    QVERIFY(!(a == b));
    b = a;
    b.setLinMode(CANBus::LinMode::Master);
    QVERIFY(!(a == b));
    b = a;
    b.setLinChecksumModel(CommFrame::LinChecksumClassic);
    QVERIFY(!(a == b));
}

void TestCanBus::testStreamRoundTrip()
{
    CANBus out;
    out.setSpeed(19200);
    out.setListenOnly(true);
    out.setSingleWire(true);
    out.setActive(true);
    out.setCanFD(true);
    out.setDataRate(4000000);
    out.setLinMode(CANBus::LinMode::Master);
    out.setLinChecksumModel(CommFrame::LinChecksumClassic);
    out.setBusType(CANBus::BusType::LIN); // not streamed: comes from the driver

    QByteArray data;
    {
        QDataStream s(&data, QIODevice::WriteOnly);
        s << out;
    }
    CANBus in;
    QDataStream s(data);
    s >> in;
    QCOMPARE(s.status(), QDataStream::Ok);
    QVERIFY(s.atEnd());
    QCOMPARE(in.getSpeed(), 19200);
    QVERIFY(in.isListenOnly());
    QVERIFY(in.isSingleWire());
    QVERIFY(in.isActive());
    QVERIFY(in.isCanFD());
    QCOMPARE(in.getDataRate(), 4000000);
    QCOMPARE(in.getLinMode(), CANBus::LinMode::Master);
    QCOMPARE(in.getLinChecksumModel(), CommFrame::LinChecksumClassic);
    QCOMPARE(in.getBusType(), CANBus::BusType::CAN);
}

void TestCanBus::testStreamVersion1Loads()
{
    // what the old operator<< wrote: speed, listen only, single wire, active
    QByteArray data;
    {
        QDataStream s(&data, QIODevice::WriteOnly);
        s << qint32(250000) << true << false << true;
    }
    CANBus in;
    QDataStream s(data);
    s >> in;
    QCOMPARE(s.status(), QDataStream::Ok);
    QVERIFY(s.atEnd());
    QCOMPARE(in.getSpeed(), 250000);
    QVERIFY(in.isListenOnly());
    QVERIFY(!in.isSingleWire());
    QVERIFY(in.isActive());
    QCOMPARE(in.getLinMode(), CANBus::LinMode::Monitor); // defaults for what version 1 didn't have
    QCOMPARE(in.getDataRate(), 2000000);
}

void TestCanBus::testStreamUnknownVersion()
{
    QByteArray data;
    {
        QDataStream s(&data, QIODevice::WriteOnly);
        s << qint32(-7) << qint32(1);
    }
    CANBus in;
    QDataStream s(data);
    s >> in;
    QCOMPARE(s.status(), QDataStream::ReadCorruptData);
    QVERIFY(in == CANBus());
}

void TestCanBus::testStreamBadLinValuesIgnored()
{
    QByteArray data;
    {
        QDataStream s(&data, QIODevice::WriteOnly);
        s << qint32(-2) << qint32(19200) << false << false << true << false << qint32(2000000) << qint32(9) << qint32(-1);
    }
    CANBus in;
    QDataStream s(data);
    s >> in;
    QCOMPARE(s.status(), QDataStream::Ok);
    QCOMPARE(in.getSpeed(), 19200);
    QCOMPARE(in.getLinMode(), CANBus::LinMode::Monitor);
    QCOMPARE(in.getLinChecksumModel(), CommFrame::LinChecksumUnknown);
}

QTEST_MAIN(TestCanBus)
#include "test_canbus.moc"
