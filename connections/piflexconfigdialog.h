#ifndef PIFLEXCONFIGDIALOG_H
#define PIFLEXCONFIGDIALOG_H

#include <QDialog>
#include <QPointer>
#include "connections/picoflexray.h"

class QTableWidget;
class QSpinBox;
class QCheckBox;
class QLabel;
class QComboBox;
class QGroupBox;

/*
 * Editor for a PicoFlexRay device's host-configurable injection rules and generator (source) schedule.
 *
 * It edits two tables plus the generator timing, and pushes them to the device over the new USB ops (0x92-0x96)
 * via PicoFlexRay's apply/clear slots; those also persist the config to QSettings and re-push it on reconnect.
 * The device's injector status (op 0xa3) is shown live: rule / source-slot counts, injector enabled, source
 * running, and the match/drop counters, so the user can see a rule take and whether overrides are matching.
 *
 * The editor doesn't require the device to be connected: edits are applied when connected and restored on the
 * next connect otherwise. It loads its initial contents from the connection's QSettings group (not its live
 * members) so it never races the worker thread.
 */
class PiFlexConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PiFlexConfigDialog(PicoFlexRay *conn, QWidget *parent = nullptr);

private slots:
    void onAddRule();
    void onDeleteRule();
    void onApplyRules();
    void onClearRules();
    void onAddSource();
    void onDeleteSource();
    void onApplySchedule();
    void onClearSchedule();
    void onSendOverride(); //fire a 0x90 override for the selected injection rule
    void onInjectStatus(QByteArray reply);
    void onSetMode();            //send the selected run mode to the device (0xa4)
    void onDeviceMode(int mode); //device reported its current mode (0xa5); update the UI, don't send

protected:
    bool eventFilter(QObject *obj, QEvent *event) override; //F1 -> context help

private:
    void buildUi();
    void loadFromSettings();
    void addRuleRow(const PicoFlexRay::InjectRule &rule);
    void addSourceRow(const PicoFlexRay::SourceSlot &slot);
    bool gatherRules(QList<PicoFlexRay::InjectRule> &out);      //false + message box on a bad cell
    bool gatherSources(QList<PicoFlexRay::SourceSlot> &out);    //false + message box on a bad cell

    void applyModeGating(int mode); //enable/grey the rules + schedule boxes for the device's current mode

    QPointer<PicoFlexRay> mConn;

    QComboBox *mModeCombo = nullptr;    //Sniff / Bridge / Inject / Source
    QLabel *mModeDeviceLabel = nullptr; //shows the device's actual reported mode
    QGroupBox *mRuleBox = nullptr;      //"Injection rules" group (relevant in INJECT mode)
    QGroupBox *mSrcBox = nullptr;       //"Generator schedule" group (relevant in SOURCE mode)
    QTableWidget *mRuleTable = nullptr;
    QTableWidget *mSourceTable = nullptr;
    QSpinBox *mPeriodUs = nullptr;
    QSpinBox *mCycleGapUs = nullptr;
    QCheckBox *mRun = nullptr;

    QLabel *mStatusLine = nullptr;
};

#endif // PIFLEXCONFIGDIALOG_H
