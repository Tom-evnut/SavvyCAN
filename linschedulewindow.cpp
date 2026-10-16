#include "linschedulewindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include "connections/canconmanager.h"
#include "dbc/dbchandler.h"
#include "helpwindow.h"

namespace {

QString hexBytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ').toUpper());
}

//"01 02 A3", "0102a3" or "1 2 a3"; false if it isn't 1-8 hex bytes
bool parseHexBytes(const QString &text, QByteArray &out)
{
    QByteArray bytes;
    const QStringList parts = text.simplified().split(' ', Qt::SkipEmptyParts);
    for (const QString &part : parts)
    {
        if (parts.size() == 1 && part.size() > 2) //no spaces: two digits a byte
        {
            if (part.size() % 2) return false;
            bytes = QByteArray::fromHex(part.toLatin1());
            if (bytes.toHex().size() != part.size()) return false;
            break;
        }
        bool ok = false;
        const uint v = part.toUInt(&ok, 16);
        if (!ok || v > 0xFF) return false;
        bytes.append(char(v));
    }
    if (bytes.isEmpty() || bytes.size() > CommFrame::LIN_MAX_PAYLOAD) return false;
    out = bytes;
    return true;
}

}

LinScheduleWindow::LinScheduleWindow(QWidget *parent) : QDialog(parent, Qt::Window)
{
    setWindowTitle(tr("LIN Schedule Runner"));
    resize(760, 480);

    mFile = new QComboBox;
    mSchedule = new QComboBox;
    mBus = new QSpinBox;
    mBus->setRange(0, 0);
    mCycles = new QSpinBox;
    mCycles->setRange(0, 1000000);
    mCycles->setSpecialValueText(tr("Until stopped"));
    mStartStop = new QPushButton(tr("Start"));
    mStatus = new QLabel;
    mTable = new QTableWidget(0, COL_COUNT);
    mTable->setHorizontalHeaderLabels({tr("Slot"), tr("Frame"), tr("ID"), tr("Sends"), tr("Delay (ms)"), tr("Data")});
    mTable->verticalHeader()->setVisible(false);
    mTable->horizontalHeader()->setSectionResizeMode(COL_SENDS, QHeaderView::Stretch);
    mTable->setColumnWidth(COL_SLOT, 40);
    mTable->setColumnWidth(COL_FRAME, 180);
    mTable->setColumnWidth(COL_ID, 50);
    mTable->setColumnWidth(COL_DATA, 190);
    mTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    auto *form = new QFormLayout;
    form->addRow(tr("LDF:"), mFile);
    form->addRow(tr("Schedule table:"), mSchedule);
    auto *busRow = new QHBoxLayout;
    busRow->addWidget(mBus);
    busRow->addWidget(new QLabel(tr("Cycles:")));
    busRow->addWidget(mCycles);
    busRow->addStretch();
    busRow->addWidget(mStartStop);
    form->addRow(tr("Bus:"), busRow);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(mTable);
    layout->addWidget(mStatus);

    mSendTimer.setSingleShot(true);
    mSendTimer.setTimerType(Qt::PreciseTimer);
    mStatusTimer.setInterval(250);
    mClock.start();

    connect(mFile, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LinScheduleWindow::fileChanged);
    connect(mSchedule, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LinScheduleWindow::scheduleChanged);
    connect(mStartStop, &QPushButton::clicked, this, &LinScheduleWindow::startStop);
    connect(mTable, &QTableWidget::cellChanged, this, &LinScheduleWindow::cellChanged);
    connect(&mSendTimer, &QTimer::timeout, this, &LinScheduleWindow::sendDue);
    connect(&mStatusTimer, &QTimer::timeout, this, &LinScheduleWindow::showStatus);

    installEventFilter(this);
    showStatus();
}

LinScheduleWindow::~LinScheduleWindow()
{
    removeEventFilter(this);
}

bool LinScheduleWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent *>(event)->key() == Qt::Key_F1)
    {
        HelpWindow::getRef()->showHelp("linschedulerunner.md");
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void LinScheduleWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshFiles();
}

void LinScheduleWindow::closeEvent(QCloseEvent *event)
{
    stopRunning();
    QDialog::closeEvent(event);
}

void LinScheduleWindow::refreshFiles()
{
    const int buses = CANConManager::getInstance()->getNumBuses();
    mBus->setRange(0, qMax(0, buses - 1));

    if (mRunner.isRunning()) return; //keep what's running on screen
    const QString current = mFile->currentText();
    mFile->blockSignals(true);
    mFile->clear();
    DBCHandler *handler = DBCHandler::getReference();
    for (int i = 0; i < handler->getFileCount(); i++)
    {
        DBCFile *file = handler->getFileByIdx(i);
        if (file && !file->linSchedules.isEmpty())
            mFile->addItem(file->getFilename(), QVariant::fromValue(quintptr(file)));
    }
    const int index = mFile->findText(current);
    mFile->setCurrentIndex(index >= 0 ? index : 0);
    mFile->blockSignals(false);
    fileChanged();
}

DBCFile *LinScheduleWindow::currentFile() const
{
    const quintptr wanted = mFile->currentData().value<quintptr>();
    DBCHandler *handler = DBCHandler::getReference();
    for (int i = 0; i < handler->getFileCount(); i++) //it may have been unloaded since
        if (quintptr(handler->getFileByIdx(i)) == wanted) return handler->getFileByIdx(i);
    return nullptr;
}

void LinScheduleWindow::fileChanged()
{
    const QString current = mSchedule->currentText();
    mSchedule->blockSignals(true);
    mSchedule->clear();
    if (DBCFile *file = currentFile())
        for (const LinSchedule &schedule : file->linSchedules.schedules)
            mSchedule->addItem(QString("%1 (%2 ms)").arg(schedule.name).arg(schedule.cycleMs()), schedule.name);
    const int index = mSchedule->findText(current);
    mSchedule->setCurrentIndex(index >= 0 ? index : 0);
    mSchedule->blockSignals(false);
    scheduleChanged();
}

void LinScheduleWindow::scheduleChanged()
{
    stopRunning();
    DBCFile *file = currentFile();
    const LinSchedule *schedule = file ? file->linSchedules.find(mSchedule->currentData().toString()) : nullptr;
    if (file && schedule) mRunner.setSchedule(*schedule, file->linSchedules.checksumModel);
    else mRunner.setSchedule(LinSchedule(), CommFrame::LinChecksumEnhanced);
    fillTable();
    showStatus();
}

void LinScheduleWindow::fillTable()
{
    mFilling = true;
    const LinSchedule &schedule = mRunner.schedule();
    mTable->setRowCount(int(schedule.entries.size()));
    for (int row = 0; row < schedule.entries.size(); row++)
    {
        const LinScheduleSlot &slot = schedule.entries.at(row);
        QString sends;
        switch (slot.kind)
        {
        case LinScheduleSlot::MasterFrame: sends = tr("Header and data (master)"); break;
        case LinScheduleSlot::Header: sends = tr("Header (a slave answers)"); break;
        case LinScheduleSlot::Silent: sends = tr("Nothing: %1").arg(slot.note); break;
        }
        const QStringList texts = {QString::number(row + 1), slot.name,
                                   slot.kind == LinScheduleSlot::Silent ? QString()
                                                                        : "0x" + QString("%1").arg(slot.id, 2, 16, QChar('0')).toUpper(),
                                   sends, QString::number(slot.delayMs),
                                   slot.kind == LinScheduleSlot::MasterFrame ? hexBytes(mRunner.dataFor(slot)) : QString()};
        for (int col = 0; col < COL_COUNT; col++)
        {
            auto *item = new QTableWidgetItem(texts.at(col));
            if (col != COL_DATA || slot.kind != LinScheduleSlot::MasterFrame)
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            else
                item->setToolTip(tr("The data the master sends in this frame: 1-8 hex bytes. Changes apply from the "
                                    "next time the frame is sent."));
            mTable->setItem(row, col, item);
        }
    }
    mFilling = false;
}

void LinScheduleWindow::cellChanged(int row, int column)
{
    if (mFilling || column != COL_DATA || row < 0 || row >= mRunner.schedule().entries.size()) return;
    const LinScheduleSlot &slot = mRunner.schedule().entries.at(row);
    QByteArray data;
    if (parseHexBytes(mTable->item(row, column)->text(), data)) mRunner.setData(slot.name, data);
    //show what's sent (normalised, or the old data if the text wasn't valid), in every row of the same frame
    mFilling = true;
    for (int r = 0; r < mRunner.schedule().entries.size(); r++)
        if (mRunner.schedule().entries.at(r).name == slot.name && mTable->item(r, COL_DATA))
            mTable->item(r, COL_DATA)->setText(hexBytes(mRunner.dataFor(slot)));
    mFilling = false;
}

void LinScheduleWindow::startStop()
{
    if (mRunner.isRunning())
    {
        stopRunning();
        return;
    }
    if (mRunner.schedule().entries.isEmpty()) return;
    mRunner.setBus(mBus->value());
    mRunner.start(nowUs(), quint64(mCycles->value()));
    mFile->setEnabled(false);
    mSchedule->setEnabled(false);
    mBus->setEnabled(false);
    mCycles->setEnabled(false);
    mStartStop->setText(tr("Stop"));
    mStatusTimer.start();
    sendDue();
}

void LinScheduleWindow::stopRunning()
{
    mRunner.stop();
    mSendTimer.stop();
    mStatusTimer.stop();
    mFile->setEnabled(true);
    mSchedule->setEnabled(true);
    mBus->setEnabled(true);
    mCycles->setEnabled(true);
    mStartStop->setText(tr("Start"));
    markSlot(-1);
    showStatus();
}

//an arrow in the Slot column of the next slot to be sent (-1: none); a mark rather than a selection, so editing the data
//isn't interrupted
void LinScheduleWindow::markSlot(int row)
{
    mFilling = true;
    for (int r = 0; r < mTable->rowCount(); r++)
        if (QTableWidgetItem *item = mTable->item(r, COL_SLOT))
            item->setText(r == row ? QString("%1 %2").arg(QChar(0x25B6)).arg(r + 1) : QString::number(r + 1));
    mFilling = false;
}

void LinScheduleWindow::sendDue()
{
    const QList<CommFrame> frames = mRunner.due(nowUs());
    for (const CommFrame &frame : frames) CANConManager::getInstance()->sendFrame(frame);
    if (!mRunner.isRunning())
    {
        stopRunning();
        return;
    }
    const qint64 waitUs = mRunner.nextDueUs() - nowUs();
    mSendTimer.start(int(qMax<qint64>(0, waitUs / 1000)));
}

void LinScheduleWindow::showStatus()
{
    if (mRunner.schedule().entries.isEmpty())
    {
        mStatus->setText(mFile->count() ? tr("This schedule table is empty.")
                                        : tr("Load an LDF with schedule tables in the DBC manager to run one."));
        return;
    }
    const QString counts = tr("%1 cycles, %2 frames sent").arg(mRunner.completedCycles()).arg(mRunner.framesSent());
    if (mRunner.isRunning())
    {
        mStatus->setText(tr("Running on bus %1: %2").arg(mRunner.bus()).arg(counts));
        markSlot(mRunner.nextSlot());
    }
    else
        mStatus->setText(mRunner.framesSent() ? tr("Stopped: %1").arg(counts)
                                              : tr("The bus's connection must be in LIN master mode (Connection window, "
                                                   "bus settings)."));
}
