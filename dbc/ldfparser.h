#ifndef LDFPARSER_H
#define LDFPARSER_H

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

/*
 * A LIN description file (LDF, LIN 1.3 to 2.2A and SAE J2602) as read from the text: what it says, without turning it
 * into anything else. LdfToDbc turns it into SavvyCAN's DBC classes.
 *
 * Parsed: the header (protocol and language version, speed, channel), Nodes, Signals, Diagnostic_signals, Frames,
 * Sporadic_frames, Event_triggered_frames, Diagnostic_frames, Node_attributes, Schedule_tables,
 * Signal_encoding_types and Signal_representation. Other sections (Node_composition, Signal_groups, Dynamic_frames)
 * are skipped. Line and block comments are allowed anywhere.
 */
struct LdfSignal
{
    QString name;
    int size = 0;                 //bits
    QList<qint64> initValues;     //one value, or one per byte for a byte array
    bool byteArray = false;
    QString publisher;
    QStringList subscribers;
    bool diagnostic = false;
};

struct LdfFrameSignal
{
    QString name;
    int offset = 0;               //bit offset in the frame
};

struct LdfFrame
{
    QString name;
    int id = 0;
    QString publisher;            //empty for diagnostic frames
    int length = 0;               //bytes; LIN 1.3 files may leave it out (lengthGiven false): see defaultLength()
    bool lengthGiven = false;
    bool diagnostic = false;
    QList<LdfFrameSignal> frameSignals;

    static int defaultLength(int id) { return id < 32 ? 2 : (id < 48 ? 4 : 8); } //LIN 1.x: from ID bits 4-5
};

struct LdfEventTriggeredFrame
{
    QString name;
    QString collisionTable;       //LIN 2.1 and later; empty otherwise
    int id = 0;
    QStringList frames;           //the unconditional frames it carries
};

struct LdfSporadicFrame
{
    QString name;
    QStringList frames;
};

struct LdfEncodingValue
{
    enum Kind { Logical, Physical, Bcd, Ascii };
    Kind kind = Logical;
    qint64 value = 0;             //logical
    QString text;                 //logical (may be empty)
    double min = 0, max = 0, scale = 1, offset = 0; //physical: raw min..max -> raw * scale + offset
    QString unit;                 //physical (may be empty)
};

struct LdfEncoding
{
    QString name;
    QList<LdfEncodingValue> values;
};

struct LdfScheduleEntry
{
    QString command;              //a frame name, or MasterReq/SlaveResp, AssignNAD, FreeFormat, ...
    QStringList arguments;        //what's between { } after the command
    double delayMs = 0;
};

struct LdfSchedule
{
    QString name;
    QList<LdfScheduleEntry> entries;
};

struct LdfNodeAttributes
{
    QString node;
    QMap<QString, QString> values; //attribute name -> value as written (lists joined with ", ")
    QStringList configurableFrames;
};

class LdfDocument
{
public:
    //false (with the reason in error) if the text isn't an LDF this parser can read; warnings are things skipped
    bool parse(const QString &text, QString *error = nullptr);

    QString protocolVersion;
    QString languageVersion;
    double speedBps = 0;
    QString channelName;
    QString master;
    double timeBaseMs = 0, jitterMs = 0;
    QStringList slaves;
    QList<LdfSignal> allSignals;  //Signals and Diagnostic_signals
    QList<LdfFrame> frames;       //Frames and Diagnostic_frames
    QList<LdfEventTriggeredFrame> eventTriggeredFrames;
    QList<LdfSporadicFrame> sporadicFrames;
    QList<LdfEncoding> encodings;
    QMap<QString, QString> representation; //signal name -> encoding name
    QList<LdfSchedule> schedules;
    QList<LdfNodeAttributes> nodeAttributes;
    QStringList warnings;

    const LdfSignal *findSignal(const QString &name) const;
    const LdfFrame *findFrame(const QString &name) const;
    const LdfEncoding *findEncoding(const QString &name) const;
};

#endif // LDFPARSER_H
