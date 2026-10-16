#include <QApplication>
#include <QPalette>
#include <QtMath>
#include "ldftodbc.h"
#include "dbchandler.h"

namespace {

void addGeneralAttribute(DBCFile *file, const QString &name, DBC_ATTRIBUTE_VAL_TYPE type, const QVariant &value)
{
    DBC_ATTRIBUTE attr;
    attr.name = name;
    attr.attrType = ATTR_TYPE_GENERAL;
    attr.valType = type;
    attr.lower = 0;
    attr.upper = type == ATTR_INT ? 1000000 : 0;
    attr.defaultValue = value;
    file->dbc_attributes.append(attr);
}

//the message colour attributes DBCFile::loadFile() adds to every DBC file, with the same defaults
void addColorAttribute(DBCFile *file, const QString &name, const QColor &color)
{
    DBC_ATTRIBUTE attr;
    attr.name = name;
    attr.attrType = ATTR_TYPE_MESSAGE;
    attr.valType = ATTR_STRING;
    attr.lower = 0;
    attr.upper = 0;
    attr.defaultValue = color.name();
    file->dbc_attributes.append(attr);
}

DBC_NODE *addNode(DBCFile *file, const QString &name, const QString &comment)
{
    DBC_NODE *node = new DBC_NODE;
    node->name = name;
    node->comment = comment;
    file->dbc_nodes.append(node);
    return node;
}

DBC_NODE *nodeOrDefault(DBCFile *file, const QString &name)
{
    DBC_NODE *node = name.isEmpty() ? nullptr : file->findNodeByName(name);
    return node ? node : file->findNodeByIdx(0);
}

//a DBC signal for an LDF signal placed at offset in a frame
DBC_SIGNAL *makeSignal(const LdfDocument &doc, DBCFile *file, const LdfSignal &ls, int offset, DBC_MESSAGE *msg,
                       QStringList *warnings)
{
    DBC_SIGNAL *sig = new DBC_SIGNAL;
    sig->name = ls.name;
    sig->startBit = offset;
    sig->signalSize = ls.size;
    sig->intelByteOrder = true; //LIN signals go LSB first
    sig->valType = UNSIGNED_INT;
    sig->factor = 1;
    sig->bias = 0;
    sig->min = 0;
    sig->max = qPow(2.0, ls.size) - 1;
    sig->receiver = nodeOrDefault(file, ls.subscribers.value(0));
    sig->parentMessage = msg;

    QStringList notes;
    const QString encName = doc.representation.value(ls.name);
    if (const LdfEncoding *enc = encName.isEmpty() ? nullptr : doc.findEncoding(encName))
    {
        bool physicalDone = false;
        for (const LdfEncodingValue &v : enc->values)
        {
            switch (v.kind) {
            case LdfEncodingValue::Logical:
            {
                DBC_VAL_ENUM_ENTRY entry;
                entry.value = int(v.value);
                entry.descript = v.text.isEmpty() ? QString::number(v.value) : v.text;
                sig->valList.append(entry);
                break;
            }
            case LdfEncodingValue::Physical:
                if (physicalDone)
                {
                    notes << QString("raw %1-%2: x %3 + %4 %5").arg(v.min).arg(v.max).arg(v.scale).arg(v.offset).arg(v.unit);
                    if (warnings) *warnings << QString("signal %1: only the first physical range of %2 is used")
                                                   .arg(ls.name, enc->name);
                    break;
                }
                physicalDone = true;
                sig->factor = v.scale;
                sig->bias = v.offset;
                sig->min = qMin(v.min * v.scale + v.offset, v.max * v.scale + v.offset);
                sig->max = qMax(v.min * v.scale + v.offset, v.max * v.scale + v.offset);
                sig->unitName = v.unit;
                break;
            case LdfEncodingValue::Bcd:
                notes << "BCD encoded";
                sig->bcdEncoded = true;
                break;
            case LdfEncodingValue::Ascii:
                notes << "ASCII encoded";
                //text, when the bytes line up (SavvyCAN's text signals are whole bytes)
                if (sig->startBit % 8 == 0 && sig->signalSize % 8 == 0) sig->valType = STRING;
                else if (warnings) *warnings << QString("signal %1: ASCII but not in whole bytes, shown as a number").arg(ls.name);
                break;
            }
        }
        notes.prepend("encoding " + enc->name);
    }
    else if (!encName.isEmpty() && warnings)
        *warnings << QString("signal %1: encoding %2 not defined").arg(ls.name, encName);

    QStringList initText;
    for (qint64 v : ls.initValues) initText << QString::number(v);
    if (!initText.isEmpty()) notes << "initial value " + initText.join(", ");
    if (!ls.publisher.isEmpty()) notes << "published by " + ls.publisher;
    if (ls.subscribers.size() > 1) notes << "subscribers " + ls.subscribers.join(", ");
    sig->comment = notes.join("; ");
    return sig;
}

void addFrameSignals(const LdfDocument &doc, DBCFile *file, const LdfFrame &frame, DBC_MESSAGE *msg,
                     QStringList *warnings)
{
    for (const LdfFrameSignal &fs : frame.frameSignals)
    {
        const LdfSignal *ls = doc.findSignal(fs.name);
        if (!ls)
        {
            if (warnings) *warnings << QString("frame %1: signal %2 not defined, skipped").arg(frame.name, fs.name);
            continue;
        }
        if (fs.offset + ls->size > frame.length * 8 && warnings)
            *warnings << QString("frame %1: signal %2 runs past the frame's %3 bytes").arg(frame.name, fs.name).arg(frame.length);
        msg->sigHandler->addSignal(makeSignal(doc, file, *ls, fs.offset, msg, warnings));
    }
}

}

void LdfToDbc::fill(const LdfDocument &doc, DBCFile *file, QStringList *warnings)
{
    file->messageHandler->removeAllMessages();
    file->messageHandler->setMatchingCriteria(EXACT);
    file->messageHandler->setFilterLabeling(false);
    qDeleteAll(file->dbc_nodes);
    file->dbc_nodes.clear();
    file->dbc_value_tables.clear();
    file->dbc_attributes.clear();

    addGeneralAttribute(file, "BusType", ATTR_STRING, QString("LIN"));
    if (!doc.protocolVersion.isEmpty()) addGeneralAttribute(file, "LIN_protocol_version", ATTR_STRING, doc.protocolVersion);
    if (doc.speedBps > 0) addGeneralAttribute(file, "LIN_speed", ATTR_INT, int(doc.speedBps + 0.5));
    //interpreted rows in the frame list take their message's colours: without these they'd be invalid (black)
    const QColor background = QApplication::palette().color(QPalette::Base);
    const QColor foreground = QApplication::palette().color(QPalette::WindowText);
    addColorAttribute(file, "GenMsgBackgroundColor", background);
    addColorAttribute(file, "GenMsgForegroundColor", foreground);

    //nodes: the default first, as DBC files get it
    addNode(file, "Vector__XXX", "Default node if none specified");
    if (!doc.master.isEmpty())
        addNode(file, doc.master, QString("LIN master, time base %1 ms, jitter %2 ms").arg(doc.timeBaseMs).arg(doc.jitterMs));
    for (const QString &slave : doc.slaves)
    {
        QStringList comment("LIN slave");
        for (const LdfNodeAttributes &na : doc.nodeAttributes)
        {
            if (na.node != slave) continue;
            for (auto it = na.values.constBegin(); it != na.values.constEnd(); ++it) comment << it.key() + " = " + it.value();
            if (!na.configurableFrames.isEmpty()) comment << "configurable frames " + na.configurableFrames.join(", ");
        }
        addNode(file, slave, comment.join("; "));
    }

    for (const LdfFrame &frame : doc.frames)
    {
        if (file->messageHandler->findMsgByID(quint32(frame.id)))
        {
            if (warnings) *warnings << QString("frame %1: ID 0x%2 is used twice, skipped").arg(frame.name).arg(frame.id, 2, 16, QChar('0'));
            continue;
        }
        DBC_MESSAGE *msg = new DBC_MESSAGE;
        msg->ID = quint32(frame.id);
        msg->extendedID = false;
        msg->name = frame.name;
        msg->len = unsigned(frame.length);
        if (frame.diagnostic)
        {
            //the master sends the master request; whichever slave is addressed answers
            msg->sender = nodeOrDefault(file, frame.id == 0x3C ? doc.master : QString());
            msg->comment = "LIN diagnostic frame (classic checksum)";
        }
        else
        {
            msg->sender = nodeOrDefault(file, frame.publisher);
            msg->comment = frame.lengthGiven ? "LIN unconditional frame"
                                             : QString("LIN unconditional frame, length %1 from its ID (LIN 1.x)").arg(frame.length);
        }
        for (const LdfSporadicFrame &sf : doc.sporadicFrames)
            if (sf.frames.contains(frame.name)) msg->comment += "; also sent in sporadic frame " + sf.name;
        file->messageHandler->addMessage(msg);
        addFrameSignals(doc, file, frame, msg, warnings);
    }

    for (const LdfEventTriggeredFrame &ef : doc.eventTriggeredFrames)
    {
        if (ef.id < 0 || ef.id > 63 || file->messageHandler->findMsgByID(quint32(ef.id)))
        {
            if (warnings) *warnings << QString("event-triggered frame %1: ID 0x%2 is used twice or out of range, skipped")
                                           .arg(ef.name).arg(ef.id, 2, 16, QChar('0'));
            continue;
        }
        DBC_MESSAGE *msg = new DBC_MESSAGE;
        msg->ID = quint32(ef.id);
        msg->extendedID = false;
        msg->name = ef.name;
        msg->sender = nodeOrDefault(file, QString());
        msg->comment = "LIN event-triggered frame: byte 0 is the protected ID of the frame carried (" + ef.frames.join(", ") + ")";
        if (!ef.collisionTable.isEmpty()) msg->comment += "; collisions resolved by schedule " + ef.collisionTable;

        DBC_SIGNAL *mux = new DBC_SIGNAL;
        mux->name = ef.name + "_PID";
        mux->startBit = 0;
        mux->signalSize = 8;
        mux->intelByteOrder = true;
        mux->valType = UNSIGNED_INT;
        mux->min = 0;
        mux->max = 255;
        mux->isMultiplexor = true;
        mux->receiver = nodeOrDefault(file, doc.master);
        mux->parentMessage = msg;
        mux->comment = "protected ID of the frame carried";
        msg->sigHandler->addSignal(mux);
        msg->multiplexorSignal = mux;

        unsigned len = 1;
        for (const QString &carriedName : ef.frames)
        {
            const LdfFrame *carried = doc.findFrame(carriedName);
            if (!carried)
            {
                if (warnings) *warnings << QString("event-triggered frame %1: frame %2 not defined").arg(ef.name, carriedName);
                continue;
            }
            const int pid = CommFrame::linPidFromId(quint8(carried->id));
            DBC_VAL_ENUM_ENTRY entry;
            entry.value = pid;
            entry.descript = carried->name;
            mux->valList.append(entry);
            len = qMax(len, unsigned(carried->length));

            for (const LdfFrameSignal &fs : carried->frameSignals)
            {
                const LdfSignal *ls = doc.findSignal(fs.name);
                if (!ls) continue; //already warned about with the frame itself
                if (fs.offset < 8 && warnings)
                    *warnings << QString("frame %1: signal %2 overlaps byte 0, which carries the protected ID in "
                                         "event-triggered frame %3").arg(carried->name, fs.name, ef.name);
                DBC_SIGNAL *sig = makeSignal(doc, file, *ls, fs.offset, msg, warnings);
                if (msg->sigHandler->findSignalByName(sig->name)) sig->name += "_" + carried->name; //names are per message
                sig->addMultiplexRange(pid, pid);
                sig->multiplexParent = mux;
                mux->multiplexedChildren.append(sig);
                msg->sigHandler->addSignal(sig);
            }
        }
        msg->len = len;
        file->messageHandler->addMessage(msg);
    }

    for (DBC_MESSAGE *msg : file->messageHandler->getMsgsAsList())
    {
        msg->bgColor = background;
        msg->fgColor = foreground;
    }
}
