#ifndef MAINSETTINGSDIALOG_H
#define MAINSETTINGSDIALOG_H

#include <QDialog>
#include <QSettings>

namespace Ui {
class MainSettingsDialog;
}

class MainSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MainSettingsDialog(QWidget *parent = 0);
    ~MainSettingsDialog();
    void setVisible(bool visible) override;

signals:
    void updatedSettings();

public slots:
    void updateSettings();

private:
    Ui::MainSettingsDialog *ui;
    bool sizedToContents = false;

    void populateLanguageCombo();
    void fitToScreen();
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
};

#endif // MAINSETTINGSDIALOG_H
