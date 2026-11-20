#include "flexrayschedulewindow.h"
#include "ui_flexrayschedulewindow.h"

#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayoutItem>
#include <QSettings>
#include "utility.h"

FlexRayScheduleWindow::FlexRayScheduleWindow(const QVector<CommFrame> *frames, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::FlexRayScheduleWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    modelFrames = frames;

    gridModel = new FlexRaySlotCycleModel(this);
    ui->tableView->setModel(gridModel);

    //64 cycle columns: keep them narrow and uniform; the slot-ID column is the (frozen) vertical header
    ui->tableView->horizontalHeader()->setDefaultSectionSize(26);
    ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->tableView->horizontalHeader()->setHighlightSections(false);
    //the summary columns (Count / Repeat / Mux) carry text, so size them to their contents
    for (int c = FR_COL_COUNT; c < FR_TOTAL_COLS; ++c)
        ui->tableView->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    ui->tableView->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(ui->btnRefresh, &QAbstractButton::clicked, this, &FlexRayScheduleWindow::rebuild);
    connect(ui->cbChannel, SIGNAL(currentIndexChanged(int)), this, SLOT(channelChanged()));
    connect(ui->chkHideStatic, &QCheckBox::toggled, this, &FlexRayScheduleWindow::filtersChanged);
    connect(ui->chkOnlyMux,    &QCheckBox::toggled, this, &FlexRayScheduleWindow::filtersChanged);
    connect(ui->chkHideNull,   &QCheckBox::toggled, this, &FlexRayScheduleWindow::filtersChanged);
    connect(ui->spinMinCount, SIGNAL(valueChanged(int)), this, SLOT(filtersChanged()));

    buildLegend();
    installEventFilter(this);

    rebuild();
}

FlexRayScheduleWindow::~FlexRayScheduleWindow()
{
    removeEventFilter(this);
    delete ui;
}

bool FlexRayScheduleWindow::eventFilter(QObject *obj, QEvent *event)
{
    //standard event processing (no modal help doc for P1)
    return QObject::eventFilter(obj, event);
}

void FlexRayScheduleWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    readSettings();
    //P1 is a snapshot: refresh the grid each time the window is (re)opened
    rebuild();
}

void FlexRayScheduleWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    writeSettings();
}

void FlexRayScheduleWindow::rebuild()
{
    if (!modelFrames) return;

    //remember the channel the user was looking at so a Refresh doesn't jump the selection
    quint8 prevBus = 0;
    bool hadSelection = (ui->cbChannel->count() > 0);
    if (hadSelection) prevBus = static_cast<quint8>(ui->cbChannel->currentData().toUInt());

    schedules = FrScheduleAnalysis::aggregate(*modelFrames);

    ui->cbChannel->blockSignals(true);
    ui->cbChannel->clear();
    int restoreIdx = 0, fr = 1;
    for (auto it = schedules.constBegin(); it != schedules.constEnd(); ++it, ++fr)
    {
        const quint8 bus = it.key();
        ui->cbChannel->addItem(QStringLiteral("FR%1 (bus %2)").arg(fr).arg(bus), bus);
        if (hadSelection && bus == prevBus) restoreIdx = ui->cbChannel->count() - 1;
    }
    ui->cbChannel->setCurrentIndex(restoreIdx);
    ui->cbChannel->blockSignals(false);

    if (ui->cbChannel->count() > 0)
        showChannel(static_cast<quint8>(ui->cbChannel->currentData().toUInt()));
    else
        showChannel(0);
}

void FlexRayScheduleWindow::channelChanged()
{
    if (ui->cbChannel->count() == 0) return;
    showChannel(static_cast<quint8>(ui->cbChannel->currentData().toUInt()));
}

void FlexRayScheduleWindow::showChannel(quint8 bus)
{
    if (!schedules.contains(bus))
    {
        gridModel->clear();
        ui->lblStatus->setText(tr("No FlexRay frames in the current capture."));
        return;
    }

    const FrChannelSchedule &sched = schedules[bus];
    gridModel->setSchedule(sched);
    updateStatusLabel(bus);
}

void FlexRayScheduleWindow::filtersChanged()
{
    FrFilterSettings f;
    f.hideStatic      = ui->chkHideStatic->isChecked();
    f.onlyMultiplexed = ui->chkOnlyMux->isChecked();
    f.hideNull        = ui->chkHideNull->isChecked();
    f.minCount        = static_cast<quint32>(ui->spinMinCount->value());
    gridModel->setFilters(f);
    if (ui->cbChannel->count() > 0)
        updateStatusLabel(static_cast<quint8>(ui->cbChannel->currentData().toUInt()));
}

void FlexRayScheduleWindow::updateStatusLabel(quint8 bus)
{
    if (!schedules.contains(bus))
    {
        ui->lblStatus->setText(tr("No FlexRay frames in the current capture."));
        return;
    }
    const int total = schedules[bus].slotRows.size();
    const int shown = gridModel->rowCount();
    if (shown == total)
        ui->lblStatus->setText(tr("%1 slot(s) on bus %2").arg(total).arg(bus));
    else
        ui->lblStatus->setText(tr("%1 of %2 slot(s) on bus %3 (filtered)").arg(shown).arg(total).arg(bus));
}

void FlexRayScheduleWindow::buildLegend()
{
    QLayout *lay = ui->legendBar->layout();
    if (!lay) return;

    //clear any previous swatches (e.g. if rebuilt on a theme change)
    QLayoutItem *item;
    while ((item = lay->takeAt(0)) != nullptr)
    {
        if (item->widget()) delete item->widget();
        delete item;
    }

    const bool dark = FlexRaySlotCycleModel::isDarkTheme();
    struct LegEntry { FrCellState state; const char *label; };
    const LegEntry entries[] = {
        { FrCellState::Static,   "static" },
        { FrCellState::Changing, "changing" },
        { FrCellState::Error,    "error / null" },
    };

    for (const LegEntry &e : entries)
    {
        QLabel *swatch = new QLabel(ui->legendBar);
        swatch->setFixedSize(14, 14);
        const QColor c = FlexRaySlotCycleModel::colorForState(e.state, dark);
        swatch->setStyleSheet(QStringLiteral("background:%1; border:1px solid palette(mid);").arg(c.name()));
        lay->addWidget(swatch);
        lay->addWidget(new QLabel(QString::fromLatin1(e.label), ui->legendBar));
    }
}

void FlexRayScheduleWindow::readSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("FlexRaySchedule/WindowSize", QSize(900, 500)).toSize());
        move(Utility::constrainedWindowPos(settings.value("FlexRaySchedule/WindowPos", QPoint(50, 50)).toPoint()));
    }
}

void FlexRayScheduleWindow::writeSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("FlexRaySchedule/WindowSize", size());
        settings.setValue("FlexRaySchedule/WindowPos", pos());
    }
}
