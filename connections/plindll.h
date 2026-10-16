#ifndef PLINDLL_H
#define PLINDLL_H

#include <QLibrary>
#include "plinbackend.h"

/*
 * PlinBackend on PEAK's PLinApi.dll, loaded at run time (it comes with PEAK's PLIN driver), so SavvyCAN builds and runs
 * without it. Windows only: on Linux PEAK's LIN driver has a different interface, not supported yet.
 */
class PlinDll : public PlinBackend
{
public:
    PlinDll();

    bool isLoaded() const override { return mLoaded; }
    QString loadError() const override { return mLoadError; }
    QList<PlinHardware> hardware() override;

    quint32 registerClient(const QString &name, quint8 *client) override;
    quint32 removeClient(quint8 client) override;
    quint32 connectClient(quint8 client, quint16 hw) override;
    quint32 disconnectClient(quint8 client, quint16 hw) override;
    quint32 initializeHardware(quint8 client, quint16 hw, quint8 mode, quint16 baudrate) override;
    quint32 setFrameEntry(quint8 client, quint16 hw, quint8 id, quint8 length, quint8 direction,
                          quint8 checksumType) override;
    quint32 setClientFilter(quint8 client, quint16 hw, quint64 mask) override;
    quint32 read(quint8 client, PlinRcvMsg *msg) override;
    QString errorText(quint32 error) override;

    static PlinDll &instance(); //one per process: the DLL keeps its own state

private:
    QLibrary mLib;
    bool mLoaded = false;
    QString mLoadError;
};

#endif // PLINDLL_H
