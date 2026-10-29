#include "windowfit.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMainWindow>
#include <QScreen>
#include <QScrollArea>

namespace
{
    //set on a window by addScrollArea until its first show has sized it to its contents
    const char *growProperty = "windowFitGrowToContents";
    const char *scrollAreaName = "windowFitScrollArea";

    //whether a layout item belongs to the bottom rows kept below the scroll area: buttons, and labels or spacers
    //next to them
    bool isActionItem(QLayoutItem *item, bool *hasButton)
    {
        if (item->spacerItem()) return true;
        if (QWidget *w = item->widget())
        {
            if (qobject_cast<QDialogButtonBox *>(w) || qobject_cast<QAbstractButton *>(w))
            {
                *hasButton = true;
                return true;
            }
            return qobject_cast<QLabel *>(w) != nullptr;
        }
        if (QBoxLayout *row = qobject_cast<QBoxLayout *>(item->layout()))
        {
            if (row->direction() != QBoxLayout::LeftToRight && row->direction() != QBoxLayout::RightToLeft) return false;
            for (int i = 0; i < row->count(); i++)
                if (!isActionItem(row->itemAt(i), hasButton)) return false;
            return true;
        }
        return false;
    }

    //Size hints are only right once the style has been applied, and while a window is hidden its layouts (nested
    //ones included) keep the hints they cached before that. Drop them all so the window is measured as it will show.
    void refreshSizeHints(QWidget *window)
    {
        window->ensurePolished();
        for (QWidget *w : window->findChildren<QWidget *>()) w->updateGeometry();
        for (QLayout *l : window->findChildren<QLayout *>()) l->invalidate();
    }
}

void WindowFit::eventDelivered(QObject *receiver, QEvent *event)
{
    if (event->type() != QEvent::Show || !receiver->isWidgetType()) return;
    QWidget *widget = static_cast<QWidget *>(receiver);
    if (widget->isWindow()) fitToScreen(widget);
}

void WindowFit::fitToScreen(QWidget *window)
{
    if (window->windowType() != Qt::Window && window->windowType() != Qt::Dialog) return; //not menus, tooltips, ...
    if (window->isMaximized() || window->isFullScreen()) return;

    QScreen *scr = QGuiApplication::screenAt(window->frameGeometry().center());
    if (!scr) scr = window->screen();
    if (!scr) return;
    const QRect avail = scr->availableGeometry();

    //the title bar and frame have to fit as well; their size is only known once the window has been shown
    QSize frame = window->frameGeometry().size() - window->size();
    if (frame.width() <= 0 || frame.height() <= 0) frame = QSize(16, 40);
    const QSize maxSize = avail.size() - frame;

    if (window->minimumWidth() > maxSize.width() || window->minimumHeight() > maxSize.height())
        window->setMinimumSize(window->minimumSize().boundedTo(maxSize));

    QSize wanted = window->size();
    if (window->property(growProperty).toBool())
    {
        //as big as the contents need, as the window was before they went into a scroll area
        window->setProperty(growProperty, false);
        QScrollArea *area = window->findChild<QScrollArea *>(scrollAreaName);
        if (area && area->widget())
        {
            refreshSizeHints(window);
            const QSize aroundArea = window->sizeHint() - area->sizeHint();
            wanted = wanted.expandedTo(area->widget()->minimumSizeHint() + aroundArea);
        }
    }
    wanted = wanted.boundedTo(maxSize);

    QRect geom(window->pos(), wanted + frame); //for a window, pos() is where its frame starts
    if (wanted != window->size())
    {
        //a dialog Qt has just centered stays centered
        if (!window->testAttribute(Qt::WA_Moved)) geom.moveCenter(window->frameGeometry().center());
        window->resize(wanted);
    }

    if (geom.right() > avail.right()) geom.moveRight(avail.right());
    if (geom.bottom() > avail.bottom()) geom.moveBottom(avail.bottom());
    if (geom.left() < avail.left()) geom.moveLeft(avail.left());
    if (geom.top() < avail.top()) geom.moveTop(avail.top());
    if (geom.topLeft() != window->pos()) window->move(geom.topLeft());
}

void WindowFit::addScrollArea(QWidget *window)
{
    QScrollArea *area = new QScrollArea();
    area->setObjectName(scrollAreaName);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidgetResizable(true);

    if (QMainWindow *mainWindow = qobject_cast<QMainWindow *>(window))
    {
        area->setWidget(mainWindow->takeCentralWidget());
        mainWindow->setCentralWidget(area);
    }
    else
    {
        QLayout *layout = window->layout();
        if (!layout)
        {
            delete area;
            return;
        }

        //rows of buttons at the bottom of a vertical layout stay outside the scroll area, always visible
        QList<QLayoutItem *> bottomRows;
        QBoxLayout *box = qobject_cast<QBoxLayout *>(layout);
        if (box && box->direction() == QBoxLayout::TopToBottom)
        {
            int first = box->count();
            bool hasButton = false;
            while (first > 0 && isActionItem(box->itemAt(first - 1), &hasButton)) first--;
            if (hasButton)
                while (box->count() > first) bottomRows.prepend(box->takeAt(box->count() - 1));
        }

        const QMargins margins = layout->contentsMargins();
        QWidget *contents = new QWidget();
        contents->setLayout(layout); //takes the layout, and the widgets in it, off the window
        area->setWidget(contents);

        QVBoxLayout *outer = new QVBoxLayout(window);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(area);
        if (!bottomRows.isEmpty())
        {
            QVBoxLayout *bottom = new QVBoxLayout();
            bottom->setContentsMargins(margins.left(), 0, margins.right(), margins.bottom());
            for (QLayoutItem *item : bottomRows)
            {
                if (QLayout *row = item->layout()) bottom->addLayout(row);
                else bottom->addItem(item);
            }
            outer->addLayout(bottom);
        }
    }
    window->setProperty(growProperty, true);
}
