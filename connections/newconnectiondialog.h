#ifndef NEWCONNECTIONDIALOG_H
#define NEWCONNECTIONDIALOG_H

#include <QDialog>
#include <QCanBusDeviceInfo>
#include <QSerialPortInfo>
#include <QDebug>
#include <QUdpSocket>
#include "canconnectionmodel.h"
#include "connections/canconnection.h"
#include "connections/devicescanner.h"
#include "connections/gs_usb_driver/gs_usb_definitions.h" //candle_handle (platform-neutral void*)

namespace Ui {
class NewConnectionDialog;
}

class QListWidget;
class QListWidgetItem;
class QPushButton;

class NewConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewConnectionDialog(QVector<QString>* gvretips, QVector<QString>* kayakips, QVector<candle_handle>* gsusbIDs = nullptr, QWidget *parent = nullptr);
    ~NewConnectionDialog();

    CANCon::type getConnectionType();
    QString getPortName();
    QString getDriverName();
    int getSerialSpeed();
    int getBusSpeed();
    int getSamplePoint();
    bool isCanFd();
    int getDataRate();

public slots:
    void handleConnTypeChanged();
    void handleDeviceTypeChanged();
    void handlePortChanged();
    void handleCreateButton();

private:
    Ui::NewConnectionDialog *ui;
    QList<QSerialPortInfo> ports;
    QList<QCanBusDeviceInfo> canDevices;
    QVector<QString>* remoteDeviceIPGVRET;
    QVector<QString>* remoteBusKayak;
    QVector<candle_handle>* remoteDeviceGSUSB = nullptr;

    //Device auto-detect: the detected list is the primary way to connect; manual setup stays
    //available in a collapsible section below it.
    QListWidget *mDetectedList = nullptr;
    QPushButton *mManualToggle = nullptr;
    QList<QWidget*> mManualWidgets;      //the original manual controls, collapsed by default
    QList<DetectedDevice> mDetected;     //backing data for the list rows
    void buildDetectPanel();             //adds the detected-devices group + manual toggle
    void rescanDevices();                //(re)populate the detected list
    void applyDetected(const DetectedDevice &d); //drive the manual controls from a detected device
    void setManualVisible(bool show);    //show/hide the manual section
    void onDetectedClicked(QListWidgetItem *item);
    void onDetectedActivated(QListWidgetItem *item); //double-click: apply + connect
    void onIdentifyClicked();            //opt-in active GVRET probe of the selected serial device

    void selectSerial();
    void selectKvaser();
    void selectSocketCan();
    void selectRemote();
    void selectKayak();
    void selectMQTT();
    void selectLawicel();
    void selectCANserver();
    void selectPicoFlexRay();
    void selectCANlogserver();
    void selectGSUSB();
    bool isSerialBusAvailable();
    void setPortName(CANCon::type pType, QString pPortName, QString pDriver);
    void fillBusSpeedLists();
    bool hasBusSettings();
};

#endif // NEWCONNECTIONDIALOG_H
