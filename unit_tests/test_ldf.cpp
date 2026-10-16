#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "ldfparser.h"
#include "ldftodbc.h"
#include "dbchandler.h"
#include "framefileio.h"

// LDF files: the parser (LdfDocument), turning them into DBC classes (LdfToDbc), decoding LIN frames with them,
// matching messages on frame type (LIN or CAN), and the BusType attribute in DBC files.
class TestLdf : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void testHeaderAndNodes();
    void testSignals();
    void testFrames();
    void testEventTriggeredAndSporadic();
    void testNodeAttributes();
    void testSchedules();
    void testEncodings();
    void testLin13Lengths();
    void testErrors_data();
    void testErrors();

    void testLoadAsDbc();
    void testDecode();
    void testEventTriggeredDecode();
    void testMatchingOnFrameType();
    void testDbcBusType();
    void testSaveAsDbc();
    void testRealLdf();
    void testRealCaptures();

private:
    QString writeFile(const QString &name, const QString &text);
    void dumpLayout(DBCFile &file, const QString &name);
    QTemporaryDir dir;
};

// a made-up LIN 2.1 network with one of everything
static const char *SAMPLE_LDF = R"LDF(
/* sample LDF for SavvyCAN's tests */
LIN_description_file;
LIN_protocol_version = "2.1";
LIN_language_version = "2.1";
LIN_speed = 19.2 kbps;
Channel_name = "Body";

Nodes {
  Master: BCM, 5 ms, 0.1 ms;   // the master
  Slaves: Seat, Mirror;
}

Signals {
  SeatPosition: 8, 255, Seat, BCM;
  SeatHeat: 2, 0, BCM, Seat;
  SeatTemp: 8, 0, Seat, BCM, Mirror;
  SeatSerial: 24, {1, 2, 3}, Seat, BCM;
  SeatFault: 1, 0, Seat, BCM;
  MirrorFold: 1, 0, BCM, Mirror;
  MirrorFault: 1, 0, Mirror, BCM;
  MirrorCode: 8, 0, Mirror, BCM;
}

Diagnostic_signals {
  MasterReqB0: 8, 0;
  MasterReqB1: 8, 0;
  SlaveRespB0: 8, 0;
}

Signal_groups {
  Old: 8 { SeatHeat, 0; }
}

Frames {
  SeatStatus: 0x10, Seat, 6 {
    SeatPosition, 0;
    SeatTemp, 8;
    SeatSerial, 16;
    SeatFault, 40;
  }
  SeatCmd: 0x11, BCM, 1 {
    SeatHeat, 0;
  }
  MirrorCmd: 0x12, BCM, 1 {
    MirrorFold, 0;
  }
  SeatEvent: 0x20, Seat, 2 {
    SeatFault, 8;
  }
  MirrorEvent: 0x21, Mirror, 3 {
    MirrorFault, 8;
    MirrorCode, 9;
  }
}

Sporadic_frames {
  CmdSporadic: SeatCmd, MirrorCmd;
}

Event_triggered_frames {
  BodyEvents: Collisions, 0x3A, SeatEvent, MirrorEvent;
  OldStyleEvents: 0x3B, SeatEvent;
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

Node_attributes {
  Seat {
    LIN_protocol = "2.1";
    configured_NAD = 0x10;
    product_id = 0x1234, 0x5678, 1;
    response_error = SeatFault;
    P2_min = 50 ms;
    configurable_frames {
      SeatStatus;
      SeatCmd;
    }
  }
  Mirror {
    LIN_protocol = "2.1";
    configured_NAD = 0x11;
    product_id = 0x1234, 0x0002, 0;
    response_error = MirrorFault;
  }
}

Schedule_tables {
  Normal {
    SeatStatus delay 10 ms;
    SeatCmd delay 5 ms;
    BodyEvents delay 10 ms;
  }
  Collisions {
    SeatEvent delay 10 ms;
    MirrorEvent delay 10 ms;
  }
  Config {
    AssignNAD { Seat } delay 20 ms;
    FreeFormat { 0x7f, 0x06, 0xb5, 0xff, 0x7f, 0xff, 0xff, 0xff } delay 20 ms;
    MasterReq delay 20 ms;
  }
}

Signal_encoding_types {
  Position {
    physical_value, 0, 200, 0.5, 0, "%";
    logical_value, 255, "SNA";
  }
  Temp {
    physical_value, 0, 100, 1, -40, "degC";
    physical_value, 101, 254, 2, -141, "degC";
  }
  HeatLevel {
    logical_value, 0, "Off";
    logical_value, 1, "Low";
    logical_value, 2;
  }
  Code { bcd_value; }
  Serial { ascii_value; }
}

Signal_representation {
  Position: SeatPosition;
  Temp: SeatTemp;
  HeatLevel: SeatHeat;
  Code: MirrorCode;
}
)LDF";

// LIN 1.3: no frame lengths (they follow from the ID)
static const char *LIN13_LDF = R"LDF(
LIN_description_file;
LIN_protocol_version = "1.3";
LIN_language_version = "1.3";
LIN_speed = 9600 bps;
Nodes { Master: M, 10 ms, 0 ms; Slaves: S; }
Signals { A: 8, 0, M, S; B: 8, 0, S, M; C: 8, 0, S, M; }
Frames {
  F1: 0x05, M { A, 0; }
  F2: 0x25, S { B, 0; }
  F3: 0x35, S { C, 0; }
}
)LDF";

// a CAN DBC with a message on ID 0x10, as an LDF frame has
static const char *CAN_DBC = R"DBC(VERSION ""

NS_ :

BS_:

BU_: ECU

BO_ 16 CanMsg: 8 ECU
 SG_ CanSig : 0|8@1+ (1,0) [0|255] "" Vector__XXX

)DBC";

QString TestLdf::writeFile(const QString &name, const QString &text)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
    f.write(text.toUtf8());
    return path;
}

static CommFrame linFrame(quint8 id, const QByteArray &data)
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(id);
    frame.setPayload(data);
    return frame;
}

void TestLdf::initTestCase()
{
    QVERIFY(dir.isValid());
}

void TestLdf::cleanup()
{
    DBCHandler::getReference()->removeAllFiles();
}

void TestLdf::testHeaderAndNodes()
{
    LdfDocument doc;
    QString error;
    QVERIFY2(doc.parse(SAMPLE_LDF, &error), qPrintable(error));
    QCOMPARE(doc.protocolVersion, QString("2.1"));
    QCOMPARE(doc.languageVersion, QString("2.1"));
    QCOMPARE(doc.speedBps, 19200.0);
    QCOMPARE(doc.channelName, QString("Body"));
    QCOMPARE(doc.master, QString("BCM"));
    QCOMPARE(doc.timeBaseMs, 5.0);
    QCOMPARE(doc.jitterMs, 0.1);
    QCOMPARE(doc.slaves, QStringList({"Seat", "Mirror"}));
    QVERIFY(doc.warnings.isEmpty()); // Signal_groups is skipped quietly
}

void TestLdf::testSignals()
{
    LdfDocument doc;
    QVERIFY(doc.parse(SAMPLE_LDF));
    QCOMPARE(doc.allSignals.size(), 11);
    const LdfSignal *temp = doc.findSignal("SeatTemp");
    QVERIFY(temp);
    QCOMPARE(temp->size, 8);
    QCOMPARE(temp->publisher, QString("Seat"));
    QCOMPARE(temp->subscribers, QStringList({"BCM", "Mirror"}));
    const LdfSignal *serial = doc.findSignal("SeatSerial");
    QVERIFY(serial->byteArray);
    QCOMPARE(serial->initValues, QList<qint64>({1, 2, 3}));
    const LdfSignal *diag = doc.findSignal("MasterReqB1");
    QVERIFY(diag->diagnostic);
    QCOMPARE(diag->size, 8);
    QVERIFY(diag->publisher.isEmpty());
}

void TestLdf::testFrames()
{
    LdfDocument doc;
    QVERIFY(doc.parse(SAMPLE_LDF));
    QCOMPARE(doc.frames.size(), 7); // 5 unconditional + 2 diagnostic
    const LdfFrame *status = doc.findFrame("SeatStatus");
    QCOMPARE(status->id, 0x10);
    QCOMPARE(status->publisher, QString("Seat"));
    QCOMPARE(status->length, 6);
    QVERIFY(status->lengthGiven);
    QCOMPARE(status->frameSignals.size(), 4);
    QCOMPARE(status->frameSignals.at(2).name, QString("SeatSerial"));
    QCOMPARE(status->frameSignals.at(2).offset, 16);
    const LdfFrame *req = doc.findFrame("MasterReq");
    QVERIFY(req->diagnostic);
    QCOMPARE(req->id, 0x3C);
    QCOMPARE(req->length, 8);
}

void TestLdf::testEventTriggeredAndSporadic()
{
    LdfDocument doc;
    QVERIFY(doc.parse(SAMPLE_LDF));
    QCOMPARE(doc.eventTriggeredFrames.size(), 2);
    QCOMPARE(doc.eventTriggeredFrames.at(0).name, QString("BodyEvents"));
    QCOMPARE(doc.eventTriggeredFrames.at(0).collisionTable, QString("Collisions"));
    QCOMPARE(doc.eventTriggeredFrames.at(0).id, 0x3A);
    QCOMPARE(doc.eventTriggeredFrames.at(0).frames, QStringList({"SeatEvent", "MirrorEvent"}));
    QVERIFY(doc.eventTriggeredFrames.at(1).collisionTable.isEmpty()); // LIN 2.0 form
    QCOMPARE(doc.eventTriggeredFrames.at(1).id, 0x3B);
    QCOMPARE(doc.sporadicFrames.size(), 1);
    QCOMPARE(doc.sporadicFrames.at(0).frames, QStringList({"SeatCmd", "MirrorCmd"}));
}

void TestLdf::testNodeAttributes()
{
    LdfDocument doc;
    QVERIFY(doc.parse(SAMPLE_LDF));
    QCOMPARE(doc.nodeAttributes.size(), 2);
    QCOMPARE(doc.nodeAttributes.at(1).node, QString("Mirror"));
    const LdfNodeAttributes &na = doc.nodeAttributes.at(0);
    QCOMPARE(na.node, QString("Seat"));
    QCOMPARE(na.values.value("LIN_protocol"), QString("2.1"));
    QCOMPARE(na.values.value("configured_NAD"), QString("0x10"));
    QCOMPARE(na.values.value("product_id"), QString("0x1234, 0x5678, 1"));
    QCOMPARE(na.values.value("P2_min"), QString("50 ms"));
    QCOMPARE(na.configurableFrames, QStringList({"SeatStatus", "SeatCmd"}));

    // LIN 2.0 gives configurable frames a message ID
    LdfDocument lin20;
    QString error;
    QVERIFY2(lin20.parse("Signals { A: 8, 0, M, S; } Frames { F: 1, M, 1 { A, 0; } }\n"
                         "Node_attributes { S { LIN_protocol = 2.0; configurable_frames { F = 0x1234; } } }", &error),
             qPrintable(error));
    QCOMPARE(lin20.nodeAttributes.at(0).configurableFrames, QStringList({"F"}));
    QCOMPARE(lin20.nodeAttributes.at(0).values.value("LIN_protocol"), QString("2.0"));
}

void TestLdf::testSchedules()
{
    LdfDocument doc;
    QVERIFY(doc.parse(SAMPLE_LDF));
    QCOMPARE(doc.schedules.size(), 3);
    const LdfSchedule &normal = doc.schedules.at(0);
    QCOMPARE(normal.name, QString("Normal"));
    QCOMPARE(normal.entries.size(), 3);
    QCOMPARE(normal.entries.at(1).command, QString("SeatCmd"));
    QCOMPARE(normal.entries.at(1).delayMs, 5.0);
    const LdfSchedule &config = doc.schedules.at(2);
    QCOMPARE(config.entries.at(0).command, QString("AssignNAD"));
    QCOMPARE(config.entries.at(0).arguments, QStringList({"Seat"}));
    QCOMPARE(config.entries.at(1).command, QString("FreeFormat"));
    QCOMPARE(config.entries.at(1).arguments.size(), 8);
    QCOMPARE(config.entries.at(1).arguments.at(2), QString("0xb5"));
}

void TestLdf::testEncodings()
{
    LdfDocument doc;
    QVERIFY(doc.parse(SAMPLE_LDF));
    QCOMPARE(doc.encodings.size(), 5);
    const LdfEncoding *pos = doc.findEncoding("Position");
    QCOMPARE(pos->values.size(), 2);
    QCOMPARE(pos->values.at(0).kind, LdfEncodingValue::Physical);
    QCOMPARE(pos->values.at(0).scale, 0.5);
    QCOMPARE(pos->values.at(0).unit, QString("%"));
    QCOMPARE(pos->values.at(1).kind, LdfEncodingValue::Logical);
    QCOMPARE(pos->values.at(1).value, qint64(255));
    QCOMPARE(pos->values.at(1).text, QString("SNA"));
    QCOMPARE(doc.findEncoding("HeatLevel")->values.at(2).text, QString()); // no text given
    QCOMPARE(doc.findEncoding("Code")->values.at(0).kind, LdfEncodingValue::Bcd);
    QCOMPARE(doc.findEncoding("Serial")->values.at(0).kind, LdfEncodingValue::Ascii);
    QCOMPARE(doc.representation.value("SeatTemp"), QString("Temp"));
}

void TestLdf::testLin13Lengths()
{
    LdfDocument doc;
    QString error;
    QVERIFY2(doc.parse(LIN13_LDF, &error), qPrintable(error));
    QCOMPARE(doc.speedBps, 9600.0);
    QCOMPARE(doc.findFrame("F1")->length, 2);
    QCOMPARE(doc.findFrame("F2")->length, 4);
    QCOMPARE(doc.findFrame("F3")->length, 8);
    QVERIFY(!doc.findFrame("F1")->lengthGiven);
}

void TestLdf::testErrors_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("message");

    QTest::newRow("missing ;") << "Signals { A: 8, 0, M, S }" << "line 1";
    QTest::newRow("bad character") << "LIN_description_file;\nSignals { A: 8, 0, M, S; } @" << "line 2";
    QTest::newRow("open comment") << "/* never closed" << "comment never closed";
    QTest::newRow("open block") << "Signals { A: 8, 0, M, S;" << "never closed";
    QTest::newRow("ID too big") << "Signals { A: 8, 0, M, S; } Frames { F: 64, M, 1 { A, 0; } }" << "isn't 0 to 63";
    QTest::newRow("length too big") << "Signals { A: 8, 0, M, S; } Frames { F: 1, M, 9 { A, 0; } }" << "isn't 1 to 8";
    QTest::newRow("signal too big") << "Signals { A: 65, 0, M, S; }" << "isn't 1 to 64";
    QTest::newRow("not an LDF") << "LIN_description_file;" << "not a LIN description file";
    QTest::newRow("a DBC") << QString(CAN_DBC) << "line";
}

void TestLdf::testErrors()
{
    QFETCH(QString, text);
    QFETCH(QString, message);
    LdfDocument doc;
    QString error;
    QVERIFY(!doc.parse(text, &error));
    QVERIFY2(error.contains(message), qPrintable(error));
}

void TestLdf::testLoadAsDbc()
{
    DBCFile file;
    QVERIFY(file.loadFile(writeFile("sample.ldf", SAMPLE_LDF)));
    dumpLayout(file, "sample.ldf");
    if (!qEnvironmentVariable("LDF_DUMP_DIR").isEmpty()) // the sample itself, for the python parser
        QFile::copy(dir.filePath("sample.ldf"), qEnvironmentVariable("LDF_DUMP_DIR") + "/sample.ldf");
    QVERIFY(file.isLin());
    QCOMPARE(file.getFilename(), QString("sample.ldf"));
    QCOMPARE(file.getFilenameNoExt(), QString("sample"));
    QCOMPARE(file.messageHandler->getCount(), 9); // 5 frames, 2 diagnostic, 2 event-triggered

    QVERIFY(file.findNodeByName("BCM"));
    QVERIFY(file.findNodeByName("Seat")->comment.contains("configured_NAD = 0x10"));
    QVERIFY(file.findAttributeByName("LIN_speed", ATTR_TYPE_GENERAL));
    QCOMPARE(file.findAttributeByName("LIN_speed", ATTR_TYPE_GENERAL)->defaultValue.toInt(), 19200);

    DBC_MESSAGE *status = file.messageHandler->findMsgByID(0x10);
    QVERIFY(status);
    QCOMPARE(status->name, QString("SeatStatus"));
    QCOMPARE(status->len, 6u);
    QCOMPARE(status->sender->name, QString("Seat"));
    QVERIFY(!status->extendedID);
    QCOMPARE(status->sigHandler->getCount(), 4);

    DBC_SIGNAL *pos = status->sigHandler->findSignalByName("SeatPosition");
    QCOMPARE(pos->startBit, 0);
    QCOMPARE(pos->signalSize, 8);
    QVERIFY(pos->intelByteOrder);
    QCOMPARE(pos->valType, UNSIGNED_INT);
    QCOMPARE(pos->factor, 0.5);
    QCOMPARE(pos->bias, 0.0);
    QCOMPARE(pos->min, 0.0);
    QCOMPARE(pos->max, 100.0);
    QCOMPARE(pos->unitName, QString("%"));
    QCOMPARE(pos->valList.size(), 1);
    QCOMPARE(pos->valList.at(0).descript, QString("SNA"));
    QCOMPARE(pos->receiver->name, QString("BCM"));

    DBC_SIGNAL *temp = status->sigHandler->findSignalByName("SeatTemp");
    QCOMPARE(temp->bias, -40.0);        // the first physical range
    QVERIFY(temp->comment.contains("raw 101-254")); // the second is noted
    DBC_SIGNAL *serial = status->sigHandler->findSignalByName("SeatSerial");
    QCOMPARE(serial->signalSize, 24);
    QVERIFY(serial->comment.contains("initial value 1, 2, 3"));
    QCOMPARE(serial->max, double(0xFFFFFF));

    DBC_SIGNAL *heat = file.messageHandler->findMsgByID(0x11)->sigHandler->findSignalByName("SeatHeat");
    QCOMPARE(heat->valList.size(), 3);
    QCOMPARE(heat->valList.at(2).descript, QString("2")); // a logical value without text

    DBC_MESSAGE *req = file.messageHandler->findMsgByID(0x3C);
    QCOMPARE(req->name, QString("MasterReq"));
    QCOMPARE(req->len, 8u);
    QCOMPARE(req->sender->name, QString("BCM"));
    QVERIFY(file.messageHandler->findMsgByID(0x11)->comment.contains("sporadic frame CmdSporadic"));

    // the schedule tables are kept for sending (LinScheduleSet has its own tests), and go with copies of the file
    QCOMPARE(file.linSchedules.schedules.size(), 3);
    QVERIFY(file.linSchedules.find("Normal"));
    QCOMPARE(file.linSchedules.find("Normal")->entries.size(), 3);
    const DBCFile copy(file);
    QCOMPARE(copy.linSchedules.schedules.size(), 3);

    // interpreted rows in the frame list use these: the palette's colours, as for DBC files (invalid shows black)
    for (DBC_MESSAGE *msg : file.messageHandler->getMsgsAsList()) {
        QVERIFY2(msg->bgColor.isValid() && msg->fgColor.isValid(), qPrintable(msg->name));
        QCOMPARE(msg->bgColor, QApplication::palette().color(QPalette::Base));
        QCOMPARE(msg->fgColor, QApplication::palette().color(QPalette::WindowText));
    }
    QVERIFY(file.findAttributeByName("GenMsgBackgroundColor", ATTR_TYPE_MESSAGE));
    QVERIFY(file.findAttributeByName("GenMsgForegroundColor", ATTR_TYPE_MESSAGE));
}

void TestLdf::testDecode()
{
    DBCFile file;
    QVERIFY(file.loadFile(writeFile("decode.ldf", SAMPLE_LDF)));
    DBC_MESSAGE *status = file.messageHandler->findMsgByID(0x10);
    // position 0x50 = 40 %, temp 0x41 = 25 degC, serial 01 02 03, fault set
    CommFrame frame = linFrame(0x10, QByteArray::fromHex("504101020301"));
    double value = 0;
    QVERIFY(status->sigHandler->findSignalByName("SeatPosition")->processAsDouble(frame, value));
    QCOMPARE(value, 40.0);
    QVERIFY(status->sigHandler->findSignalByName("SeatTemp")->processAsDouble(frame, value));
    QCOMPARE(value, 25.0);
    QVERIFY(status->sigHandler->findSignalByName("SeatSerial")->processAsDouble(frame, value));
    QCOMPARE(value, double(0x030201)); // LSB first
    QVERIFY(status->sigHandler->findSignalByName("SeatFault")->processAsDouble(frame, value));
    QCOMPARE(value, 1.0);

    // the value list holds raw values (255 = SNA), and other values show scaled, with their decimals
    QString text;
    DBC_SIGNAL *pos = status->sigHandler->findSignalByName("SeatPosition");
    QVERIFY(pos->processAsText(linFrame(0x10, QByteArray::fromHex("FF0000000000")), text));
    QCOMPARE(text, QString("SeatPosition: SNA %"));
    QVERIFY(pos->processAsText(linFrame(0x10, QByteArray::fromHex("8F0000000000")), text));
    QCOMPARE(text, QString("SeatPosition: 71.5 %"));
    QVERIFY(pos->processAsText(linFrame(0x10, QByteArray::fromHex("500000000000")), text));
    QCOMPARE(text, QString("SeatPosition: 40 %"));
}

void TestLdf::testEventTriggeredDecode()
{
    DBCFile file;
    QVERIFY(file.loadFile(writeFile("events.ldf", SAMPLE_LDF)));
    DBC_MESSAGE *events = file.messageHandler->findMsgByID(0x3A);
    QVERIFY(events);
    QCOMPARE(events->len, 3u); // the longest frame it carries
    QVERIFY(events->multiplexorSignal);
    QCOMPARE(events->multiplexorSignal->name, QString("BodyEvents_PID"));
    QCOMPARE(events->multiplexorSignal->multiplexedChildren.size(), 3);
    QCOMPARE(events->multiplexorSignal->valList.size(), 2);

    const quint8 mirrorPid = CommFrame::linPidFromId(0x21);
    QCOMPARE(int(mirrorPid), 0x61);
    // the mirror answered: byte 0 is MirrorEvent's protected ID, then fault = 1 (bit 8), code = 0x2A (bits 9-16)
    CommFrame frame = linFrame(0x3A, QByteArray(1, char(mirrorPid)) + QByteArray::fromHex("5500"));
    DBC_SIGNAL *mirrorFault = events->sigHandler->findSignalByName("MirrorFault");
    DBC_SIGNAL *seatFault = events->sigHandler->findSignalByName("SeatFault");
    QVERIFY(mirrorFault && seatFault);
    QVERIFY(mirrorFault->isSignalInMessage(frame));
    QVERIFY(!seatFault->isSignalInMessage(frame));
    double value = 0;
    QVERIFY(mirrorFault->processAsDouble(frame, value));
    QCOMPARE(value, 1.0);
    QVERIFY(events->sigHandler->findSignalByName("MirrorCode")->processAsDouble(frame, value));
    QCOMPARE(value, 42.0);

    QString text;
    QVERIFY(events->multiplexorSignal->processAsText(frame, text));
    QVERIFY2(text.contains("MirrorEvent"), qPrintable(text));
}

void TestLdf::testMatchingOnFrameType()
{
    DBCHandler *handler = DBCHandler::getReference();
    QVERIFY(handler->loadDBCFile(writeFile("can.dbc", CAN_DBC)));
    QVERIFY(handler->loadDBCFile(writeFile("match.ldf", SAMPLE_LDF)));
    QCOMPARE(handler->getFileCount(), 2);

    CommFrame can(0x10, QByteArray::fromHex("01"));
    can.setBus(0);
    CommFrame lin = linFrame(0x10, QByteArray::fromHex("504101020301"));
    lin.setBus(0);
    QVERIFY(handler->findMessage(can));
    QCOMPARE(handler->findMessage(can)->name, QString("CanMsg"));
    QVERIFY(handler->findMessage(lin));
    QCOMPARE(handler->findMessage(lin)->name, QString("SeatStatus"));

    CommFrame linOnly = linFrame(0x3C, QByteArray(8, '\0'));
    QVERIFY(handler->findMessage(linOnly));
    CommFrame canOnly(0x3C, QByteArray(8, '\0'));
    QVERIFY(!handler->findMessage(canOnly)); // the CAN file has no 0x3C and the LDF doesn't match CAN

    // where only an ID is known (graphs, the signal viewer, filter labels): by kind of file, or by signal
    DBC_MESSAGE *canMsg = handler->findMessage(0x10, false);
    DBC_MESSAGE *linMsg = handler->findMessage(0x10, true);
    QVERIFY(canMsg && linMsg);
    QCOMPARE(canMsg->name, QString("CanMsg"));
    QCOMPARE(linMsg->name, QString("SeatStatus"));
    QVERIFY(!handler->findMessage(0x3C, false));
    QVERIFY(handler->isLinMessage(linMsg));
    QVERIFY(!handler->isLinMessage(canMsg));
    QVERIFY(!handler->isLinMessage(nullptr));
    QCOMPARE(handler->findMessageWithSignal(0x10, "SeatPosition"), linMsg);
    QCOMPARE(handler->findMessageWithSignal(0x10, "CanSig"), canMsg);
    QVERIFY(!handler->findMessageWithSignal(0x10, "NoSuchSignal"));

    // filter labels name both, from the files with filter labeling on
    handler->getFileByIdx(0)->messageHandler->setFilterLabeling(true);
    handler->getFileByIdx(1)->messageHandler->setFilterLabeling(true);
    QCOMPARE(handler->messageNamesForFilter(0x10), QString("CanMsg / SeatStatus"));
    QCOMPARE(handler->messageNamesForFilter(0x3C), QString("MasterReq"));
    QCOMPARE(handler->findMessageForFilter(0x10, nullptr, true), linMsg);
    handler->getFileByIdx(1)->messageHandler->setFilterLabeling(false);
    QCOMPARE(handler->messageNamesForFilter(0x10), QString("CanMsg"));
    QVERIFY(handler->messageNamesForFilter(0x3C).isEmpty());

    // a file that fails to load isn't returned (it used to give back the last file loaded)
    QVERIFY(!handler->loadDBCFile(dir.filePath("missing.dbc")));
    QCOMPARE(handler->getFileCount(), 2);
}

void TestLdf::testDbcBusType()
{
    DBCFile plain;
    QVERIFY(plain.loadFile(writeFile("plain.dbc", CAN_DBC)));
    QVERIFY(!plain.isLin());

    const QString withString = QString(CAN_DBC) +
        "BA_DEF_ \"BusType\" STRING ;\nBA_DEF_DEF_ \"BusType\" \"CAN\";\nBA_ \"BusType\" \"LIN\";\n";
    DBCFile linString;
    QVERIFY(linString.loadFile(writeFile("linstring.dbc", withString)));
    QVERIFY(linString.isLin());

    const QString withEnum = QString(CAN_DBC) +
        "BA_DEF_ \"BusType\" ENUM \"CAN\",\"LIN\";\nBA_DEF_DEF_ \"BusType\" \"CAN\";\nBA_ \"BusType\" 1;\n";
    DBCFile linEnum;
    QVERIFY(linEnum.loadFile(writeFile("linenum.dbc", withEnum)));
    QVERIFY(linEnum.isLin());

    const QString canDefault = QString(CAN_DBC) + "BA_DEF_ \"BusType\" STRING ;\nBA_DEF_DEF_ \"BusType\" \"CAN\";\n";
    DBCFile can;
    QVERIFY(can.loadFile(writeFile("candefault.dbc", canDefault)));
    QVERIFY(!can.isLin());
    can.setLin(true);
    QVERIFY(can.isLin());
    QCOMPARE(can.findAttributeByName("BusType", ATTR_TYPE_GENERAL)->defaultValue.toString(), QString("LIN"));
}

void TestLdf::testSaveAsDbc()
{
    const QString ldfPath = writeFile("saved.ldf", SAMPLE_LDF);
    DBCFile file;
    QVERIFY(file.loadFile(ldfPath));
    QVERIFY(file.saveFile(ldfPath)); // asked to save over the LDF: writes saved.dbc instead
    QCOMPARE(file.getFilename(), QString("saved.dbc"));
    QFile ldf(ldfPath);
    QVERIFY(ldf.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(ldf.readAll()), QString(SAMPLE_LDF)); // untouched

    DBCFile reloaded;
    QVERIFY(reloaded.loadFile(dir.filePath("saved.dbc")));
    QVERIFY(reloaded.isLin());
    QVERIFY(reloaded.linSchedules.isEmpty()); // a DBC file has no schedule tables
    QCOMPARE(reloaded.messageHandler->getCount(), file.messageHandler->getCount());
    for (DBC_MESSAGE *msg : file.messageHandler->getMsgsAsList()) {
        DBC_MESSAGE *other = reloaded.messageHandler->findMsgByID(msg->ID);
        QVERIFY2(other, qPrintable(msg->name));
        QCOMPARE(other->name, msg->name);
        QCOMPARE(other->len, msg->len);
        QCOMPARE(other->sigHandler->getCount(), msg->sigHandler->getCount());
        for (DBC_SIGNAL *sig : msg->sigHandler->getSignalsAsList()) {
            DBC_SIGNAL *o = other->sigHandler->findSignalByName(sig->name);
            QVERIFY2(o, qPrintable(sig->name));
            QCOMPARE(o->startBit, sig->startBit);
            QCOMPARE(o->signalSize, sig->signalSize);
            QCOMPARE(o->intelByteOrder, sig->intelByteOrder);
            QCOMPARE(o->factor, sig->factor);
            QCOMPARE(o->bias, sig->bias);
            QCOMPARE(o->isMultiplexor, sig->isMultiplexor);
            QCOMPARE(o->isMultiplexed, sig->isMultiplexed);
        }
        QCOMPARE(other->bgColor, msg->bgColor);
        QCOMPARE(other->fgColor, msg->fgColor);
    }
}

void TestLdf::testRealLdf()
{
    // a real LDF, if there is one in the git-ignored test/lin-data/ldf folder
    QDir ldfDir(QStringLiteral(SAVVYCAN_SOURCE_DIR "/test/lin-data/ldf"));
    const QStringList names = ldfDir.entryList({"*.ldf"}, QDir::Files);
    if (names.isEmpty()) QSKIP("no LDF files in test/lin-data/ldf");
    for (const QString &name : names) {
        QFile f(ldfDir.filePath(name));
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        LdfDocument doc;
        QString error;
        QVERIFY2(doc.parse(QString::fromUtf8(f.readAll()), &error), qPrintable(name + ": " + error));
        QVERIFY2(!doc.frames.isEmpty(), qPrintable(name));

        DBCFile file;
        QStringList warnings;
        LdfToDbc::fill(doc, &file, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(name + ": " + warnings.join("; ")));
        QCOMPARE(file.messageHandler->getCount(), int(doc.frames.size() + doc.eventTriggeredFrames.size()));
        for (const LdfFrame &frame : doc.frames) {
            DBC_MESSAGE *msg = file.messageHandler->findMsgByID(quint32(frame.id));
            QVERIFY2(msg, qPrintable(frame.name));
            QCOMPARE(msg->sigHandler->getCount(), int(frame.frameSignals.size()));
        }
        qInfo() << name << ":" << doc.frames.size() << "frames," << doc.allSignals.size() << "signals,"
                << doc.schedules.size() << "schedule tables";
        dumpLayout(file, name);
    }
}

// real LIN text logs in the git-ignored test/lin-data/captures, decoded with each real LDF there: every frame of a
// log that matches an LDF must have a message of its ID and length, and decode every signal
void TestLdf::testRealCaptures()
{
    QDir captures(QStringLiteral(SAVVYCAN_SOURCE_DIR "/test/lin-data/captures"));
    QDir ldfDir(QStringLiteral(SAVVYCAN_SOURCE_DIR "/test/lin-data/ldf"));
    QStringList logs;
    for (const QString &n : captures.entryList({"*.txt"}, QDir::Files))
        if (FrameFileIO::isLinTextLog(captures.filePath(n))) logs << n;
    const QStringList ldfs = ldfDir.entryList({"*.ldf"}, QDir::Files);
    if (logs.isEmpty() || ldfs.isEmpty()) QSKIP("no LIN text logs and LDFs in test/lin-data");

    for (const QString &ldfName : ldfs) {
        DBCFile ldf;
        QVERIFY(ldf.loadFile(ldfDir.filePath(ldfName)));
        for (const QString &logName : logs) {
            QVector<CommFrame> frames;
            QVERIFY2(FrameFileIO::loadLinTextLog(captures.filePath(logName), &frames), qPrintable(logName));
            int matching = 0, decoded = 0;
            QMap<quint32, int> unknownIds;
            QSet<quint32> shown;
            for (const CommFrame &f : frames) {
                DBC_MESSAGE *msg = ldf.messageHandler->findMsgByID(f.frameId());
                if (!msg || msg->len != unsigned(f.payload().size())) {
                    unknownIds[f.frameId()]++;
                    continue;
                }
                matching++;
                QStringList values;
                for (DBC_SIGNAL *sig : msg->sigHandler->getSignalsAsList()) {
                    QString text;
                    if (sig->isSignalInMessage(f) && sig->processAsText(f, text)) {
                        decoded++;
                        values << text;
                    }
                }
                if (!shown.contains(f.frameId())) { // one example per message
                    shown.insert(f.frameId());
                    qInfo().noquote() << "   " << msg->name << f.payload().toHex(' ') << "->" << values.join(", ");
                }
            }
            QStringList unknown;
            for (auto it = unknownIds.constBegin(); it != unknownIds.constEnd(); ++it)
                unknown << QString("0x%1 x%2").arg(it.key(), 2, 16, QChar('0')).arg(it.value());
            qInfo().noquote() << logName << "with" << ldfName << ":" << frames.size() << "frames," << matching
                              << "match the LDF," << decoded << "signal values decoded; other IDs/lengths:"
                              << (unknown.isEmpty() ? QString("none") : unknown.join(", "));
            QVERIFY(!frames.isEmpty());
        }
    }
}

// with LDF_DUMP_DIR set, writes each message's layout as SavvyCAN decodes it, one line per signal, for comparing
// with an independent LDF parser (python ldfparser)
void TestLdf::dumpLayout(DBCFile &file, const QString &name)
{
    const QString outDir = qEnvironmentVariable("LDF_DUMP_DIR");
    if (outDir.isEmpty()) return;
    QStringList lines;
    for (DBC_MESSAGE *msg : file.messageHandler->getMsgsAsList())
        for (DBC_SIGNAL *sig : msg->sigHandler->getSignalsAsList())
            lines << QString("%1 0x%2 %3 %4 %5 %6 %7 %8 %9").arg(msg->name).arg(msg->ID, 2, 16, QChar('0')).arg(msg->len)
                         .arg(sig->name).arg(sig->startBit).arg(sig->signalSize).arg(sig->factor).arg(sig->bias)
                         .arg(sig->unitName.isEmpty() ? "-" : sig->unitName);
    lines.sort();
    QFile out(outDir + "/" + name + ".savvycan.txt");
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) out.write((lines.join('\n') + '\n').toUtf8());
}

QTEST_MAIN(TestLdf)
#include "test_ldf.moc"
