#include <QtTest/QtTest>
#include "bus_protocols/lintp.h"

// The LIN diagnostic transport layer (bus_protocols/lintp.h): requests split into master request frames, messages
// put back together from the frames on the bus, the node configuration requests, and the descriptions.
class TestLinTp : public QObject
{
    Q_OBJECT

private slots:
    void testSingleFrame();
    void testSegmented();
    void testNodeConfigurationRequests();
    void testDecodeRoundTrip();
    void testDecodeResponsesAndBuses();
    void testDecodeErrors();
    void testSleep();
    void testDescribe();
    void testRequester();
    void testRequesterScanAndPending();
};

static CommFrame lin(quint8 id, const QByteArray &data, int bus = 0)
{
    CommFrame f(CommFrame::LINDataFrame);
    f.setFrameId(id);
    f.setPayload(data);
    f.setBus(bus);
    return f;
}

void TestLinTp::testSingleFrame()
{
    const QList<QByteArray> f = LinTp::frames(0x10, QByteArray::fromHex("B200FF7FFFFF"));
    QCOMPARE(f.size(), 1);
    QCOMPARE(f.first(), QByteArray::fromHex("1006B200FF7FFFFF"));
    // shorter: padded with 0xFF
    QCOMPARE(LinTp::frames(0x7F, QByteArray::fromHex("B6")).first(), QByteArray::fromHex("7F01B6FFFFFFFFFF"));
}

void TestLinTp::testSegmented()
{
    // 10 bytes: a first frame with 5, a consecutive frame with the other 5
    const QByteArray message = QByteArray::fromHex("2EF190010203040506");
    QCOMPARE(message.size(), 9);
    QList<QByteArray> f = LinTp::frames(0x22, message);
    QCOMPARE(f.size(), 2);
    QCOMPARE(f.at(0), QByteArray::fromHex("2210092EF1900102"));
    QCOMPARE(f.at(1), QByteArray::fromHex("222103040506FFFF"));

    // 100 bytes: 1 + ceil(95 / 6) = 17 frames, the sequence numbers wrap after 15
    QByteArray big;
    for (int i = 0; i < 100; i++) big.append(char(i));
    f = LinTp::frames(0x01, big);
    QCOMPARE(f.size(), 17);
    QCOMPARE(quint8(f.at(0).at(1)), quint8(0x10));
    QCOMPARE(quint8(f.at(0).at(2)), quint8(100));
    QCOMPARE(quint8(f.at(15).at(1)), quint8(0x2F));
    QCOMPARE(quint8(f.at(16).at(1)), quint8(0x20)); // 16 wraps to 0
}

void TestLinTp::testNodeConfigurationRequests()
{
    // LIN 2.1: ReadByIdentifier 0 for any supplier and function
    QCOMPARE(LinTp::readByIdentifier(0), QByteArray::fromHex("B200FF7FFFFF"));
    QCOMPARE(LinTp::readByIdentifier(1, 0x1234, 0x5678), QByteArray::fromHex("B20134127856"));
    QCOMPARE(LinTp::assignNad(0x10, 0x1234, 0x5678), QByteArray::fromHex("B03412785610"));
    QCOMPARE(LinTp::conditionalChangeNad(1, 2, 0xFF, 0, 0x11), QByteArray::fromHex("B30102FF0011"));
    QCOMPARE(LinTp::saveConfiguration(), QByteArray::fromHex("B6"));
    QCOMPARE(LinTp::assignFrameIdRange(0, {0xC1, 0x42}), QByteArray::fromHex("B700C142FFFF"));
    QCOMPARE(LinTp::goToSleep(), QByteArray::fromHex("00FFFFFFFFFFFFFF"));
}

void TestLinTp::testDecodeRoundTrip()
{
    LinTp::Decoder decoder;
    for (const QByteArray &message : {QByteArray::fromHex("B200FF7FFFFF"), QByteArray::fromHex("2EF190010203040506"),
                                      QByteArray(40, char(0x5A))}) {
        QList<LinTp::Message> got;
        for (const QByteArray &f : LinTp::frames(0x22, message)) got << decoder.feed(lin(0x3C, f));
        QCOMPARE(got.size(), 1);
        QVERIFY(got.first().request);
        QCOMPARE(got.first().nad, quint8(0x22));
        QCOMPARE(got.first().payload, message);
    }
    QCOMPARE(decoder.errors(), 0);
}

void TestLinTp::testDecodeResponsesAndBuses()
{
    LinTp::Decoder decoder;
    // a segmented response on bus 0, with a single-frame response on bus 1 in between
    const QList<QByteArray> response = LinTp::frames(0x22, QByteArray::fromHex("6EF19001020304"));
    QCOMPARE(response.size(), 2);
    QVERIFY(decoder.feed(lin(0x3D, response.at(0), 0)).isEmpty());
    QList<LinTp::Message> other = decoder.feed(lin(0x3D, QByteArray::fromHex("1006F23412785601"), 1));
    QCOMPARE(other.size(), 1);
    QCOMPARE(other.first().bus, 1);
    QVERIFY(!other.first().request);
    QList<LinTp::Message> got = decoder.feed(lin(0x3D, response.at(1), 0));
    QCOMPARE(got.size(), 1);
    QCOMPARE(got.first().payload, QByteArray::fromHex("6EF19001020304"));
    // a header nobody answered, and other IDs and CAN frames, are ignored
    QVERIFY(decoder.feed(lin(0x3D, QByteArray())).isEmpty());
    QVERIFY(decoder.feed(lin(0x10, QByteArray::fromHex("1006B200FF7FFFFF"))).isEmpty());
    QVERIFY(decoder.feed(CommFrame(0x3C, QByteArray::fromHex("1006B200FF7FFFFF"))).isEmpty());
    QCOMPARE(decoder.errors(), 0);
}

void TestLinTp::testDecodeErrors()
{
    LinTp::Decoder decoder;
    const QList<QByteArray> f = LinTp::frames(0x22, QByteArray(20, char(1)));
    QCOMPARE(f.size(), 4);
    decoder.feed(lin(0x3C, f.at(0)));
    QVERIFY(decoder.feed(lin(0x3C, f.at(2))).isEmpty()); // frame 2 missing: out of sequence
    QCOMPARE(decoder.errors(), 1);
    QVERIFY(decoder.feed(lin(0x3C, f.at(3))).isEmpty()); // the rest of that message is dropped
    QCOMPARE(decoder.errors(), 2);
    // a consecutive frame from another NAD doesn't continue the message
    decoder.reset();
    decoder.feed(lin(0x3C, f.at(0)));
    QByteArray otherNad = f.at(1);
    otherNad[0] = char(0x23);
    QVERIFY(decoder.feed(lin(0x3C, otherNad)).isEmpty());
    QCOMPARE(decoder.errors(), 1);
    // single frames with a bad length
    QVERIFY(decoder.feed(lin(0x3C, QByteArray::fromHex("2207B2FFFFFFFFFF"))).isEmpty());
    QVERIFY(decoder.feed(lin(0x3C, QByteArray::fromHex("2200B2FFFFFFFFFF"))).isEmpty());
    QCOMPARE(decoder.errors(), 3);
}

void TestLinTp::testSleep()
{
    LinTp::Decoder decoder;
    const QList<LinTp::Message> got = decoder.feed(lin(0x3C, LinTp::goToSleep()));
    QCOMPARE(got.size(), 1);
    QVERIFY(got.first().sleep);
    QCOMPARE(LinTp::describe(got.first()), QString("Go to sleep"));
}

void TestLinTp::testDescribe()
{
    LinTp::Message request;
    request.payload = LinTp::readByIdentifier(0);
    QCOMPARE(LinTp::describe(request), QString("ReadByIdentifier 0 (supplier 0x7fff, function 0xffff)"));
    LinTp::Message response;
    response.request = false;
    response.payload = QByteArray::fromHex("F23412785601");
    QCOMPARE(LinTp::describe(response),
             QString("Positive response to ReadByIdentifier: supplier 0x1234, function 0x5678, variant 1"));
    response.payload = QByteArray::fromHex("7F2231");
    QCOMPARE(LinTp::describe(response), QString("Negative response to ReadDataByIdentifier: request out of range"));
    response.payload = QByteArray::fromHex("7FB299");
    QCOMPARE(LinTp::describe(response), QString("Negative response to ReadByIdentifier: code 0x99"));
    response.payload = QByteArray::fromHex("62F19041");
    QCOMPARE(LinTp::describe(response), QString("Positive response to ReadDataByIdentifier: F1 90 41"));
    request.payload = QByteArray::fromHex("A001");
    QCOMPARE(LinTp::describe(request), QString("Service 0xa0: 01"));
    QCOMPARE(LinTp::serviceName(0x3E), QString("TesterPresent"));
}

static LinTp::Message response(quint8 nad, const QByteArray &payload, int bus = 0)
{
    LinTp::Message m;
    m.request = false;
    m.nad = nad;
    m.payload = payload;
    m.bus = bus;
    return m;
}

void TestLinTp::testRequester()
{
    LinTp::Requester r;
    r.maxPolls = 3;
    r.start({{0, 0x10, LinTp::readByIdentifier(0)}}, 0);
    QVERIFY(r.isRunning());
    QList<CommFrame> f = r.due(0);
    QCOMPARE(f.size(), 1);
    QCOMPARE(f.first().frameId(), quint32(0x3C));
    QCOMPARE(f.first().payload(), QByteArray::fromHex("1006B200FF7FFFFF"));
    QCOMPARE(f.first().linChecksumModel(), CommFrame::LinChecksumClassic);
    QVERIFY(!f.first().isReceived());
    QVERIFY(r.due(19999).isEmpty());
    f = r.due(20000);
    QCOMPARE(f.size(), 1);
    QCOMPARE(f.first().frameId(), quint32(0x3D)); // a header for the answer
    QVERIFY(f.first().payload().isEmpty());
    r.received(response(0x11, QByteArray::fromHex("F23412785601"))); // another slave: not the answer
    r.received(response(0x10, QByteArray::fromHex("F23412785601"), 1)); // another bus
    QVERIFY(r.takeResults().isEmpty());
    r.received(response(0x10, QByteArray::fromHex("F23412785601")));
    const QList<LinTp::Requester::Result> results = r.takeResults();
    QCOMPARE(results.size(), 1);
    QVERIFY(results.first().answered);
    QCOMPARE(results.first().response.payload, QByteArray::fromHex("F23412785601"));
    QVERIFY(!r.isRunning());
    QVERIFY(r.due(100000).isEmpty());

    // nobody answers: three headers, then the request is given up
    r.start({{0, 0x20, LinTp::saveConfiguration()}}, 0);
    int headers = 0;
    for (qint64 t = 0; t <= 200000; t += 20000)
        for (const CommFrame &frame : r.due(t)) if (frame.frameId() == 0x3D) headers++;
    QCOMPARE(headers, 3);
    QVERIFY(!r.isRunning());
    const QList<LinTp::Requester::Result> none = r.takeResults();
    QCOMPARE(none.size(), 1);
    QVERIFY(!none.first().answered);

    // a segmented request: both frames before any header; an answer can't come before then
    r.start({{0, 0x22, QByteArray::fromHex("2EF190010203040506")}}, 0);
    QCOMPARE(r.due(0).first().frameId(), quint32(0x3C));
    r.received(response(0x22, QByteArray::fromHex("6EF190")));
    QVERIFY(r.isRunning());
    QCOMPARE(r.due(20000).first().payload(), QByteArray::fromHex("222103040506FFFF"));
    QCOMPARE(r.due(40000).first().frameId(), quint32(0x3D));
    r.received(response(0x22, QByteArray::fromHex("6EF190")));
    QVERIFY(!r.isRunning());
}

void TestLinTp::testRequesterScanAndPending()
{
    LinTp::Requester r;
    r.maxPolls = 2;
    QList<LinTp::Requester::Request> scan;
    for (quint8 nad = 1; nad <= 3; nad++) scan << LinTp::Requester::Request{0, nad, LinTp::readByIdentifier(0)};
    r.start(scan, 0);
    QCOMPARE(r.remaining(), 3);
    qint64 t = 0;
    QList<quint8> asked;
    while (r.isRunning() && t < 1000000) {
        for (const CommFrame &frame : r.due(t)) {
            if (frame.frameId() == 0x3C) asked << quint8(frame.payload().at(0));
            // NAD 2 answers its first header, after saying "response pending" once
            if (frame.frameId() == 0x3D && asked.last() == 2) {
                static int polls = 0;
                if (polls++ == 0) r.received(response(2, QByteArray::fromHex("7FB278")));
                else r.received(response(2, QByteArray::fromHex("F2FFFFFFFF00")));
            }
        }
        t += 20000;
    }
    QCOMPARE(asked, QList<quint8>({1, 2, 3}));
    const QList<LinTp::Requester::Result> results = r.takeResults();
    QCOMPARE(results.size(), 3);
    QVERIFY(!results.at(0).answered);
    QVERIFY(results.at(1).answered);
    QCOMPARE(results.at(1).response.payload, QByteArray::fromHex("F2FFFFFFFF00"));
    QVERIFY(!results.at(2).answered);

    // a broadcast request takes any slave's answer; stop() ends a scan
    r.start({{0, LinTp::NAD_BROADCAST, LinTp::readByIdentifier(0)}, {0, 5, LinTp::saveConfiguration()}}, 0);
    r.due(0);
    r.due(20000);
    r.received(response(0x33, QByteArray::fromHex("F2FFFFFFFF00")));
    QCOMPARE(r.takeResults().first().response.nad, quint8(0x33));
    QVERIFY(r.isRunning());
    r.stop();
    QVERIFY(!r.isRunning());
    QVERIFY(r.due(100000).isEmpty());
}

QTEST_MAIN(TestLinTp)
#include "test_lintp.moc"
