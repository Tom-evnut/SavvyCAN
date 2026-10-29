#ifndef WINDOWFIT_H
#define WINDOWFIT_H

#include <QWidget>

class QEvent;

//Keeps windows within the screen they open on, so a small or scaled laptop screen never ends up with the bottom
//of a window (and its buttons) out of reach.
namespace WindowFit
{
    //Call from QApplication::notify once an event has been delivered. When a top-level window has just been
    //shown, it is capped to the available area of its screen and moved inside it. This runs after the window's
    //own showEvent, where many windows restore a size and position saved on another (bigger) monitor, and
    //before the window appears.
    void eventDelivered(QObject *receiver, QEvent *event);

    //Caps one window now. eventDelivered() does this every time a window is shown.
    void fitToScreen(QWidget *window);

    //Moves the window's contents into a scroll area, so the window can be smaller than its layout needs.
    //Rows of buttons (and labels) at the bottom of a dialog stay below the scroll area, always visible.
    //The first time the window is shown it still opens as big as its contents need, if the screen allows it.
    void addScrollArea(QWidget *window);
}

#endif // WINDOWFIT_H
