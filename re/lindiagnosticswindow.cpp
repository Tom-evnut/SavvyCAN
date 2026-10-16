#include "lindiagnosticswindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include "connections/canconmanager.h"
#include "helpwindow.h"

namespace {

QLineEdit *hexEdit(const QString &text, int width = 70)
{
    auto *edit = new QLineEdit(text);
    edit->setMaximumWidth(width);
    return edit;
}

QTableWidgetItem *cell(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString hex2(quint8 v)
{
    return "0x" + QString("%1").arg(v, 2, 16, QChar('0')).toUpper();
}

}

LinDiagnosticsWindow::LinDiagnosticsWindow(const QVector<CommFrame> *frames, QWidget *parent)
    : QDialog(parent, Qt::Window), mFrames(frames)
{
    setWindowTitle(tr("LIN Diagnostics"));
    resize(980, 640);

    mBus = new QSpinBox;
    mBus->setRange(0, 255);
    mNad = hexEdit("7F", 50);
    mNad->setToolTip(tr("The slave's node address (NAD), hex: 01-7D, 7E functional, 7F all slaves"));
    mService = new QComboBox;
    mService->addItems({tr("Read by identifier (B2)"), tr("Assign NAD (B0)"), tr("Conditional change NAD (B3)"),
                        tr("Save configuration (B6)"), tr("UDS or other request (hex)"), tr("Go to sleep")});
    mSend = new QPushButton(tr("Send"));
    mIdentifier = new QSpinBox;
    mIdentifier->setRange(0, 255);
    mIdentifier->setToolTip(tr("Read by identifier: 0 product ID, 1 serial number, 32-63 user defined"));
    mSupplier = hexEdit("7FFF");
    mFunction = hexEdit("FFFF");
    mNewNad = hexEdit("01", 50);
    mData = new QLineEdit;
    mData->setPlaceholderText(tr("hex bytes"));

    mDidFrom = hexEdit("F180");
    mDidTo = hexEdit("F19F");
    auto *scanNadsButton = new QPushButton(tr("Scan NADs"));
    scanNadsButton->setToolTip(tr("Read by identifier 0 (product ID) to NADs 01-7D, any supplier and function"));
    auto *scanDidsButton = new QPushButton(tr("Scan data identifiers"));
    scanDidsButton->setToolTip(tr("UDS ReadDataByIdentifier (22) for each identifier in the range, to the NAD above"));
    mStop = new QPushButton(tr("Stop"));
    mStop->setEnabled(false);
    mPollMs = new QSpinBox;
    mPollMs->setRange(10, 1000);
    mPollMs->setValue(20);
    mPollMs->setSuffix(" ms");
    mPollMs->setToolTip(tr("Time between the frames sent: at least a frame's slot at the bus's bit rate"));
    mPolls = new QSpinBox;
    mPolls->setRange(1, 100);
    mPolls->setValue(10);
    mPolls->setToolTip(tr("Slave response headers to send before giving up on an answer"));

    mLog = new QTableWidget(0, 6);
    mLog->setHorizontalHeaderLabels({tr("Time (s)"), tr("Bus"), tr("Direction"), tr("NAD"), tr("Message"), tr("Meaning")});
    mLog->verticalHeader()->setVisible(false);
    mLog->horizontalHeader()->setStretchLastSection(true);
    mLog->setColumnWidth(0, 90);
    mLog->setColumnWidth(1, 40);
    mLog->setColumnWidth(2, 80);
    mLog->setColumnWidth(3, 50);
    mLog->setColumnWidth(4, 260);
    auto *decodeButton = new QPushButton(tr("Decode frame list"));
    decodeButton->setToolTip(tr("Decode the diagnostic frames in the main frame list"));
    auto *clearButton = new QPushButton(tr("Clear"));
    mStatus = new QLabel;
    mStatus->setWordWrap(true);

    auto *request = new QGridLayout;
    request->addWidget(new QLabel(tr("Bus:")), 0, 0);
    request->addWidget(mBus, 0, 1);
    request->addWidget(new QLabel(tr("NAD:")), 0, 2);
    request->addWidget(mNad, 0, 3);
    request->addWidget(new QLabel(tr("Request:")), 0, 4);
    request->addWidget(mService, 0, 5);
    request->addWidget(mSend, 0, 6);
    request->addWidget(new QLabel(tr("Identifier:")), 1, 0);
    request->addWidget(mIdentifier, 1, 1);
    request->addWidget(new QLabel(tr("Supplier:")), 1, 2);
    request->addWidget(mSupplier, 1, 3);
    request->addWidget(new QLabel(tr("Function:")), 1, 4);
    auto *ids = new QHBoxLayout;
    ids->addWidget(mFunction);
    ids->addWidget(new QLabel(tr("New NAD:")));
    ids->addWidget(mNewNad);
    ids->addStretch();
    request->addLayout(ids, 1, 5);
    request->addWidget(new QLabel(tr("Data:")), 2, 0);
    request->addWidget(mData, 2, 1, 1, 5);

    auto *scan = new QHBoxLayout;
    scan->addWidget(scanNadsButton);
    scan->addSpacing(12);
    scan->addWidget(new QLabel(tr("Data identifiers")));
    scan->addWidget(mDidFrom);
    scan->addWidget(new QLabel(tr("to")));
    scan->addWidget(mDidTo);
    scan->addWidget(scanDidsButton);
    scan->addSpacing(12);
    scan->addWidget(new QLabel(tr("Every")));
    scan->addWidget(mPollMs);
    scan->addWidget(new QLabel(tr("Headers to wait:")));
    scan->addWidget(mPolls);
    scan->addStretch();
    scan->addWidget(mStop);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(decodeButton);
    bottom->addWidget(clearButton);
    bottom->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(request);
    layout->addLayout(scan);
    layout->addWidget(mLog, 1);
    layout->addLayout(bottom);
    layout->addWidget(mStatus);

    mTimer.setInterval(5);
    mTimer.setTimerType(Qt::PreciseTimer);
    mClock.start();

    connect(mService, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LinDiagnosticsWindow::serviceChanged);
    connect(mSend, &QPushButton::clicked, this, &LinDiagnosticsWindow::send);
    connect(scanNadsButton, &QPushButton::clicked, this, &LinDiagnosticsWindow::scanNads);
    connect(scanDidsButton, &QPushButton::clicked, this, &LinDiagnosticsWindow::scanDids);
    connect(mStop, &QPushButton::clicked, this, &LinDiagnosticsWindow::stopRequests);
    connect(decodeButton, &QPushButton::clicked, this, &LinDiagnosticsWindow::decodeFrameList);
    connect(clearButton, &QPushButton::clicked, this, [this]() { mLog->setRowCount(0); });
    connect(&mTimer, &QTimer::timeout, this, &LinDiagnosticsWindow::tick);
    installEventFilter(this);
    serviceChanged();
    mStatus->setText(tr("Requests need the bus's LIN connection in master mode. Diagnostic frames on the bus show "
                        "here as they come."));
}

LinDiagnosticsWindow::~LinDiagnosticsWindow()
{
    listen(false);
    removeEventFilter(this);
}

bool LinDiagnosticsWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyRelease && static_cast<QKeyEvent *>(event)->key() == Qt::Key_F1)
    {
        HelpWindow::getRef()->showHelp("lindiagnostics.md");
        return true;
    }
    return QDialog::eventFilter(obj, event);
}

void LinDiagnosticsWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    listen(true);
}

void LinDiagnosticsWindow::closeEvent(QCloseEvent *event)
{
    stopRequests();
    listen(false);
    QDialog::closeEvent(event);
}

//frames 0x3C and 0x3D (ID & 0x3E == 0x3C) on every bus
void LinDiagnosticsWindow::listen(bool on)
{
    if (on == mListening) return;
    if (on) CANConManager::getInstance()->addTargettedFrame(-1, LinTp::MASTER_REQUEST, 0x3E, this);
    else CANConManager::getInstance()->removeAllTargettedFrames(this);
    mListening = on;
}

void LinDiagnosticsWindow::gotTargettedFrame(CommFrame frame)
{
    if (!frame.isLin()) return; //a CAN frame with ID 0x3C or 0x3D
    for (const LinTp::Message &m : mDecoder.feed(frame))
    {
        addMessage(m);
        mRequester.received(m);
    }
}

void LinDiagnosticsWindow::serviceChanged()
{
    const int s = mService->currentIndex();
    mIdentifier->setEnabled(s == ReadById || s == ConditionalChangeNad);
    mSupplier->setEnabled(s == ReadById || s == AssignNad);
    mFunction->setEnabled(s == ReadById || s == AssignNad);
    mNewNad->setEnabled(s == AssignNad || s == ConditionalChangeNad);
    mData->setEnabled(s == UdsRequest || s == ConditionalChangeNad);
    mNad->setEnabled(s != GoToSleep);
    mData->setPlaceholderText(s == ConditionalChangeNad ? tr("byte, mask, invert (hex), e.g. 01 FF 00")
                                                        : tr("the request, hex: service and data, e.g. 22 F1 90"));
}

bool LinDiagnosticsWindow::parseHex(const QLineEdit *edit, quint32 maxValue, quint32 &value, const QString &what)
{
    bool ok = false;
    value = edit->text().trimmed().remove("0x", Qt::CaseInsensitive).toUInt(&ok, 16);
    if (!ok || value > maxValue)
    {
        mStatus->setText(tr("%1: %2 isn't a hex value up to %3").arg(what, edit->text()).arg(maxValue, 0, 16));
        return false;
    }
    return true;
}

void LinDiagnosticsWindow::send()
{
    quint32 nad = 0, supplier = 0, function = 0, newNad = 0;
    if (!parseHex(mNad, 0x7F, nad, tr("NAD"))) return;
    QByteArray payload;
    const QByteArray data = QByteArray::fromHex(mData->text().toLatin1());
    switch (mService->currentIndex())
    {
    case ReadById:
        if (!parseHex(mSupplier, 0xFFFF, supplier, tr("Supplier")) || !parseHex(mFunction, 0xFFFF, function, tr("Function"))) return;
        payload = LinTp::readByIdentifier(quint8(mIdentifier->value()), quint16(supplier), quint16(function));
        break;
    case AssignNad:
        if (!parseHex(mSupplier, 0xFFFF, supplier, tr("Supplier")) || !parseHex(mFunction, 0xFFFF, function, tr("Function"))
            || !parseHex(mNewNad, 0x7D, newNad, tr("New NAD"))) return;
        payload = LinTp::assignNad(quint8(newNad), quint16(supplier), quint16(function));
        break;
    case ConditionalChangeNad:
        if (!parseHex(mNewNad, 0x7D, newNad, tr("New NAD"))) return;
        if (data.size() != 3)
        {
            mStatus->setText(tr("Conditional change NAD needs the byte, mask and invert as three hex bytes"));
            return;
        }
        payload = LinTp::conditionalChangeNad(quint8(mIdentifier->value()), quint8(data.at(0)), quint8(data.at(1)),
                                              quint8(data.at(2)), quint8(newNad));
        break;
    case SaveConfiguration:
        payload = LinTp::saveConfiguration();
        break;
    case UdsRequest:
        if (data.isEmpty())
        {
            mStatus->setText(tr("Type the request as hex bytes: the service, then its data"));
            return;
        }
        payload = data;
        break;
    case GoToSleep:
        //not a transport layer message: one master request frame, nobody answers
        CANConManager::getInstance()->sendFrame(LinTp::Requester::masterRequest(mBus->value(), LinTp::goToSleep()));
        mStatus->setText(tr("Go-to-sleep command sent"));
        return;
    }
    startRequests({{mBus->value(), quint8(nad), payload}}, tr("Request"));
}

void LinDiagnosticsWindow::scanNads()
{
    QList<LinTp::Requester::Request> requests;
    for (int nad = 1; nad <= 0x7D; nad++)
        requests << LinTp::Requester::Request{mBus->value(), quint8(nad), LinTp::readByIdentifier(0)};
    startRequests(requests, tr("NAD scan"));
}

void LinDiagnosticsWindow::scanDids()
{
    quint32 nad = 0, from = 0, to = 0;
    if (!parseHex(mNad, 0x7F, nad, tr("NAD")) || !parseHex(mDidFrom, 0xFFFF, from, tr("From"))
        || !parseHex(mDidTo, 0xFFFF, to, tr("To"))) return;
    if (to < from) std::swap(from, to);
    QList<LinTp::Requester::Request> requests;
    for (quint32 did = from; did <= to; did++)
    {
        QByteArray payload(1, char(0x22));
        payload.append(char(did >> 8));
        payload.append(char(did & 0xFF));
        requests << LinTp::Requester::Request{mBus->value(), quint8(nad), payload};
    }
    startRequests(requests, tr("Data identifier scan"));
}

void LinDiagnosticsWindow::startRequests(const QList<LinTp::Requester::Request> &requests, const QString &what)
{
    listen(false); //again, for connections made since the window opened
    listen(true);
    mRequester.slotUs = qint64(mPollMs->value()) * 1000;
    mRequester.maxPolls = mPolls->value();
    mAnswered = mUnanswered = 0;
    mRequester.start(requests, nowUs());
    mStop->setEnabled(true);
    mStatus->setText(tr("%1: %2 requests").arg(what).arg(requests.size()));
    mTimer.start();
    tick();
}

void LinDiagnosticsWindow::stopRequests()
{
    mRequester.stop();
    mTimer.stop();
    mStop->setEnabled(false);
}

void LinDiagnosticsWindow::tick()
{
    for (const CommFrame &frame : mRequester.due(nowUs())) CANConManager::getInstance()->sendFrame(frame);
    for (const LinTp::Requester::Result &r : mRequester.takeResults())
    {
        if (r.answered) mAnswered++;
        else
        {
            mUnanswered++;
            if (mRequester.remaining() == 0 && mAnswered + mUnanswered == 1) //a single request: say so
                addNote(tr("No answer from NAD %1").arg(hex2(r.request.nad)));
        }
    }
    if (mRequester.isRunning())
        mStatus->setText(tr("Waiting: %1 requests to go, %2 answered, %3 not").arg(mRequester.remaining())
                             .arg(mAnswered).arg(mUnanswered));
    else
    {
        mStatus->setText(tr("Done: %1 answered, %2 not").arg(mAnswered).arg(mUnanswered));
        stopRequests();
    }
}

void LinDiagnosticsWindow::decodeFrameList()
{
    LinTp::Decoder decoder;
    int found = 0;
    for (const CommFrame &frame : *mFrames)
        for (const LinTp::Message &m : decoder.feed(frame))
        {
            addMessage(m);
            found++;
        }
    mStatus->setText(tr("%1 diagnostic messages in the frame list, %2 frames that didn't fit together")
                         .arg(found).arg(decoder.errors()));
}

void LinDiagnosticsWindow::addMessage(const LinTp::Message &message)
{
    const int row = mLog->rowCount();
    mLog->insertRow(row);
    const QStringList texts = {QString::number(message.time.microSeconds() / 1000000.0, 'f', 4),
                               QString::number(message.bus),
                               message.sleep ? tr("Command") : (message.request ? tr("Request") : tr("Response")),
                               message.sleep ? QString() : hex2(message.nad),
                               QString::fromLatin1(message.payload.toHex(' ').toUpper()), LinTp::describe(message)};
    for (int col = 0; col < texts.size(); col++) mLog->setItem(row, col, cell(texts.at(col)));
    mLog->scrollToBottom();
}

void LinDiagnosticsWindow::addNote(const QString &text)
{
    const int row = mLog->rowCount();
    mLog->insertRow(row);
    mLog->setItem(row, 5, cell(text));
    mLog->scrollToBottom();
}
