#ifndef SCRIPTCONTAINER_H
#define SCRIPTCONTAINER_H

#include "can_structs.h"
#include "canfilter.h"
#include "bus_protocols/isotp_message.h"
#include "bus_protocols/uds_handler.h"

#include <QElapsedTimer>
#include <QJSEngine>
#include <QTimer>
#include <qlistwidget.h>

class ScriptingWindow;
class QTableWidget;
class ISOTP_HANDLER;
class UDS_HANDLER;

class CANScriptHelper: public QObject
{
    Q_OBJECT
public:
    CANScriptHelper(QJSEngine *engine);

public slots:
    void setFilter(QJSValue id, QJSValue mask, QJSValue bus);
    void clearFilters();
    void sendFrame(QJSValue bus, QJSValue id, QJSValue length, QJSValue data);
    void sendFDFrame(QJSValue bus, QJSValue id, QJSValue length, QJSValue data, QJSValue brs);
    void setRxCallback(QJSValue cb);

private slots:
    void gotTargettedFrame(const CommFrame &frame);

private:
    QList<CANFilter> filters;
    QJSValue gotFrameFunction;
    QJSEngine *scriptEngine;
};

/*
 * lin.xxx in scripts: LIN frames, with SavvyCAN as the LIN master (the bus's connection must be in master mode).
 *   lin.sendFrame(bus, id, data[, checksum])  a header and response; checksum "classic" or "enhanced" (left out:
 *                                             the bus's checksum setting)
 *   lin.sendHeader(bus, id)                   a header alone, for a slave to answer
 *   lin.setFilter(id, mask, bus), lin.clearFilters()
 * Received LIN frames that pass a filter call gotLINFrame(bus, id, data, info): info.checksum, info.model ("classic",
 * "enhanced" or ""), info.headerOnly, info.errors (a list of error names) and info.received.
 */
class LINScriptHelper: public QObject
{
    Q_OBJECT
public:
    LINScriptHelper(QJSEngine *engine);

public slots:
    void setFilter(QJSValue id, QJSValue mask, QJSValue bus);
    void clearFilters();
    void sendFrame(QJSValue bus, QJSValue id, QJSValue data, QJSValue checksum = QJSValue());
    void sendHeader(QJSValue bus, QJSValue id);
    void setRxCallback(QJSValue cb);

private slots:
    void gotTargettedFrame(const CommFrame &frame);

private:
    QList<CANFilter> filters;
    QJSValue gotFrameFunction;
    QJSEngine *scriptEngine;
};

class ISOTPScriptHelper: public QObject
{
    Q_OBJECT
public:
    ISOTPScriptHelper(QJSEngine *engine);
public slots:
    void setFilter(QJSValue id, QJSValue mask, QJSValue bus);
    void clearFilters();
    void sendISOTP(QJSValue bus, QJSValue id, QJSValue length, QJSValue data);
    void setRxCallback(QJSValue cb);
    void setPaddingByte(QJSValue byt);
private slots:
    void newISOMessage(ISOTP_MESSAGE msg);
private:
    QJSValue gotFrameFunction;
    QJSEngine *scriptEngine;
    ISOTP_HANDLER *handler;
};

class UDSScriptHelper: public QObject
{
    Q_OBJECT
public:
    UDSScriptHelper(QJSEngine *engine);
public slots:
    void setFilter(QJSValue id, QJSValue mask, QJSValue bus);
    void clearFilters();
    void sendUDS(QJSValue bus, QJSValue id, QJSValue service, QJSValue sublen, QJSValue subFunc, QJSValue length, QJSValue data);
    void setRxCallback(QJSValue cb);
private slots:
    void newUDSMessage(UDS_MESSAGE msg);
private:
    QJSValue gotFrameFunction;
    QJSEngine *scriptEngine;
    UDS_HANDLER *handler;
};

class ScriptContainer : public QObject
{
    Q_OBJECT

public:
    ScriptContainer();
    virtual ~ScriptContainer();
    void setScriptWindow(ScriptingWindow *win);

    QString fileName;
    QString filePath;
    QString scriptText;

/*
  Anything registered as a public slot on this class and that takes either no arguments
  or only QJSValue arguments will be callable from scripts via  host.XXX calls. So, have at it, champ.
*/
public slots:
    void compileScript();
    void setTickInterval(QJSValue interval);
    void log(QJSValue logString);
    void addParameter(QJSValue name);
    void updateValuesTable(QTableWidget *widget);
    void updateParameter(QString name, QString value);

signals:
    void sendLog(QString text);

private slots:
    void tick();

private:
    QJSEngine *scriptEngine;
    QJSValue compiledScript;
    QJSValue setupFunction;
    QJSValue tickFunction;
    QTimer timer;
    ScriptingWindow *window;
    CANScriptHelper *canHelper;
    LINScriptHelper *linHelper;
    ISOTPScriptHelper *isoHelper;
    UDSScriptHelper *udsHelper;
    QVector<QString> scriptParams;
};

#endif // SCRIPTCONTAINER_H
