#include "linscheduleanalysiswindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include "dbc/dbchandler.h"
#include "helpwindow.h"

namespace {

QTableWidgetItem *cell(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString ms(double value)
{
    return QString::number(value, 'f', value < 10 ? 2 : 1);
}

QString hexId(quint8 id)
{
    return "0x" + QString("%1").arg(id, 2, 16, QChar('0')).toUpper();
}

}

LinScheduleAnalysisWindow::LinScheduleAnalysisWindow(const QVector<CommFrame> *frames, QWidget *parent)
    : QDialog(parent, Qt::Window), mFrames(frames)
{
    setWindowTitle(tr("LIN Schedule Analysis"));
    resize(900, 600);

    mAllBuses = new QCheckBox(tr("All buses"));
    mAllBuses->setChecked(true);
    mBus = new QSpinBox;
    mBus->setRange(0, 255);
    mBus->setEnabled(false);
    mAnalyse = new QPushButton(tr("Analyse"));
    mCopy = new QPushButton(tr("Copy as LDF schedule"));
    mCopy->setEnabled(false);
    mSummary = new QLabel;
    mSummary->setWordWrap(true);

    mIds = new QTableWidget(0, 9);
    mIds->setHorizontalHeaderLabels({tr("ID"), tr("Frame"), tr("Headers"), tr("Answered"), tr("Period (ms)"),
                                     tr("Min-max (ms)"), tr("Jitter (ms)"), tr("Lengths"), tr("Checksum, errors")});
    mIds->verticalHeader()->setVisible(false);
    mIds->horizontalHeader()->setStretchLastSection(true);
    mIds->setSortingEnabled(false);

    mCycle = new QTableWidget(0, 5);
    mCycle->setHorizontalHeaderLabels({tr("Slot"), tr("ID"), tr("Frame"), tr("Delay (ms)"), tr("Answered")});
    mCycle->verticalHeader()->setVisible(false);
    mCycle->horizontalHeader()->setStretchLastSection(true);

    auto *top = new QHBoxLayout;
    top->addWidget(mAllBuses);
    top->addWidget(new QLabel(tr("Bus:")));
    top->addWidget(mBus);
    top->addStretch();
    top->addWidget(mAnalyse);
    top->addWidget(mCopy);

    auto *splitter = new QSplitter(Qt::Vertical);
    auto *idsBox = new QWidget;
    auto *idsLayout = new QVBoxLayout(idsBox);
    idsLayout->setContentsMargins(0, 0, 0, 0);
    idsLayout->addWidget(new QLabel(tr("Frame IDs")));
    idsLayout->addWidget(mIds);
    auto *cycleBox = new QWidget;
    auto *cycleLayout = new QVBoxLayout(cycleBox);
    cycleLayout->setContentsMargins(0, 0, 0, 0);
    cycleLayout->addWidget(new QLabel(tr("Schedule table (the order the headers repeat in)")));
    cycleLayout->addWidget(mCycle);
    splitter->addWidget(idsBox);
    splitter->addWidget(cycleBox);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(mSummary);
    layout->addWidget(splitter, 1);

    connect(mAllBuses, &QCheckBox::toggled, this, [this](bool all) { mBus->setEnabled(!all); });
    connect(mAnalyse, &QPushButton::clicked, this, &LinScheduleAnalysisWindow::analyse);
    connect(mCopy, &QPushButton::clicked, this, &LinScheduleAnalysisWindow::copyLdf);
    installEventFilter(this);
}

bool LinScheduleAnalysisWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent *>(event)->key() == Qt::Key_F1)
    {
        HelpWindow::getRef()->showHelp("linscheduleanalysis.md");
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void LinScheduleAnalysisWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    analyse();
}

QString LinScheduleAnalysisWindow::frameName(quint8 id) const
{
    CommFrame frame(CommFrame::LINDataFrame);
    frame.setFrameId(id);
    frame.setBus(mAllBuses->isChecked() ? 0 : mBus->value());
    DBC_MESSAGE *msg = DBCHandler::getReference()->findMessage(frame);
    return msg ? msg->name : QString();
}

void LinScheduleAnalysisWindow::analyse()
{
    mResult = LinScheduleAnalysis::analyse(*mFrames, mAllBuses->isChecked() ? -1 : mBus->value());

    mIds->setRowCount(int(mResult.ids.size()));
    for (int row = 0; row < mResult.ids.size(); row++)
    {
        const LinScheduleAnalysis::IdStats &s = mResult.ids.at(row);
        QStringList lengths;
        for (int len : s.lengths) lengths << QString::number(len);
        QStringList checks;
        if (s.classic) checks << tr("classic");
        if (s.enhanced) checks << tr("enhanced");
        if (s.errors) checks << tr("%1 errors").arg(s.errors);
        if (s.sent) checks << tr("%1 sent").arg(s.sent);
        const bool periodic = s.headers > 1;
        const QStringList texts = {hexId(s.id), frameName(s.id), QString::number(s.headers),
                                   QString("%1 (%2%)").arg(s.answered).arg(qRound(s.responseRate() * 100)),
                                   periodic ? ms(s.meanPeriodMs) : QString(),
                                   periodic ? ms(s.minPeriodMs) + " - " + ms(s.maxPeriodMs) : QString(),
                                   periodic ? ms(s.jitterMs) : QString(), lengths.join(", "), checks.join(", ")};
        for (int col = 0; col < texts.size(); col++) mIds->setItem(row, col, cell(texts.at(col)));
    }
    mIds->resizeColumnsToContents();

    mCycle->setRowCount(int(mResult.cycle.size()));
    for (int row = 0; row < mResult.cycle.size(); row++)
    {
        const LinScheduleAnalysis::Slot &slot = mResult.cycle.at(row);
        const QStringList texts = {QString::number(row + 1), hexId(slot.id), frameName(slot.id), ms(slot.delayMs),
                                   QString("%1%").arg(qRound(slot.responseRate * 100))};
        for (int col = 0; col < texts.size(); col++) mCycle->setItem(row, col, cell(texts.at(col)));
    }
    mCycle->resizeColumnsToContents();

    if (mResult.frames == 0)
        mSummary->setText(tr("No LIN frames on this bus. Capture or load some (File > Treat Frames as LIN turns LIN "
                             "frames loaded as CAN into LIN frames), then press Analyse."));
    else if (mResult.cycle.isEmpty())
        mSummary->setText(tr("%1 LIN frames over %2 s, %3 IDs. No repeating order of headers found: the master may "
                             "switch schedule tables, or the capture is too short.")
                              .arg(mResult.frames).arg(mResult.durationMs / 1000.0, 0, 'f', 1).arg(mResult.ids.size()));
    else
        mSummary->setText(tr("%1 LIN frames over %2 s, %3 IDs. The headers repeat in a cycle of %4 slots, %5 ms; "
                             "%6% of the capture follows it (the rest are frames slipped in, or another schedule "
                             "table). The median time between headers is %7 ms.")
                              .arg(mResult.frames).arg(mResult.durationMs / 1000.0, 0, 'f', 1).arg(mResult.ids.size())
                              .arg(mResult.cycle.size()).arg(ms(mResult.cycleMs)).arg(qRound(mResult.followRate * 100))
                              .arg(ms(mResult.medianGapMs)));
    mCopy->setEnabled(!mResult.cycle.isEmpty());
}

void LinScheduleAnalysisWindow::copyLdf()
{
    QApplication::clipboard()->setText(
        mResult.ldfSchedule("Reconstructed", [this](quint8 id) { return frameName(id); }));
}
