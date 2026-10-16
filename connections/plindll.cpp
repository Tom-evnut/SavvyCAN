#include <cstddef>
#include "plindll.h"

namespace {

//The PLIN-API's structures as the documentation lays them out (8-byte alignment)
#pragma pack(push, 8)
struct RawRcvMsg           //TLINRcvMsg, 40 bytes
{
    quint8 type;
    quint8 frameId;
    quint8 length;
    quint8 direction;
    quint8 checksumType;
    quint8 data[8];
    quint8 checksum;
    qint32 errorFlags;
    quint64 timestamp;
    quint16 hardware;
};
struct RawFrameEntry       //TLINFrameEntry, 14 bytes
{
    quint8 frameId;
    quint8 length;
    quint8 direction;
    quint8 checksumType;
    quint16 flags;
    quint8 initialData[8];
};
#pragma pack(pop)
static_assert(sizeof(RawRcvMsg) == 40, "TLINRcvMsg is 40 bytes");
static_assert(offsetof(RawRcvMsg, errorFlags) == 16, "TLINRcvMsg.ErrorFlags at +16");
static_assert(offsetof(RawRcvMsg, timestamp) == 24, "TLINRcvMsg.TimeStamp at +24");
static_assert(offsetof(RawRcvMsg, hardware) == 32, "TLINRcvMsg.hHw at +32");
static_assert(sizeof(RawFrameEntry) == 14, "TLINFrameEntry is 14 bytes");

//__stdcall on 32-bit Windows; 64-bit Windows has one calling convention
#if defined(_WIN32) && !defined(_WIN64)
#define PLIN_CALL __stdcall
#else
#define PLIN_CALL
#endif

typedef quint32 (PLIN_CALL *RegisterClientFn)(char *name, quint32 hWnd, quint8 *client);
typedef quint32 (PLIN_CALL *ClientFn)(quint8 client);
typedef quint32 (PLIN_CALL *ClientHwFn)(quint8 client, quint16 hw);
typedef quint32 (PLIN_CALL *InitializeHardwareFn)(quint8 client, quint16 hw, quint8 mode, quint16 baudrate);
typedef quint32 (PLIN_CALL *GetAvailableHardwareFn)(quint16 *buffer, quint16 bufferBytes, int *count);
typedef quint32 (PLIN_CALL *GetHardwareParamFn)(quint16 hw, quint16 param, void *buffer, quint16 bufferBytes);
typedef quint32 (PLIN_CALL *SetFrameEntryFn)(quint8 client, quint16 hw, RawFrameEntry *entry);
typedef quint32 (PLIN_CALL *SetClientFilterFn)(quint8 client, quint16 hw, quint64 mask);
typedef quint32 (PLIN_CALL *ReadFn)(quint8 client, RawRcvMsg *msg);
typedef quint32 (PLIN_CALL *GetErrorTextFn)(quint32 error, quint8 language, char *buffer, quint16 bufferBytes);

RegisterClientFn pRegisterClient = nullptr;
ClientFn pRemoveClient = nullptr;
ClientHwFn pConnectClient = nullptr;
ClientHwFn pDisconnectClient = nullptr;
InitializeHardwareFn pInitializeHardware = nullptr;
GetAvailableHardwareFn pGetAvailableHardware = nullptr;
GetHardwareParamFn pGetHardwareParam = nullptr;
SetFrameEntryFn pSetFrameEntry = nullptr;
SetClientFilterFn pSetClientFilter = nullptr;
ReadFn pRead = nullptr;
GetErrorTextFn pGetErrorText = nullptr;

//TLINHardwareParam
constexpr quint16 hwpName = 1;
constexpr quint16 hwpDeviceNumber = 2;
constexpr quint16 hwpChannelNumber = 3;
constexpr quint16 hwpType = 13;
constexpr int NAME_LENGTH = 48;    //LIN_MAX_NAME_LENGTH
constexpr quint8 LANG_ENGLISH = 0x09;
constexpr quint32 NOT_LOADED = 0xFFFF; //errUnknown, for calls made without the DLL

}

PlinDll::PlinDll()
{
#ifdef Q_OS_WIN
    mLib.setFileName("PLinApi");
    if (!mLib.load())
    {
        mLoadError = "PEAK's PLinApi.dll isn't installed (it comes with the PLIN driver from peak-system.com)";
        return;
    }
    pRegisterClient = reinterpret_cast<RegisterClientFn>(mLib.resolve("LIN_RegisterClient"));
    pRemoveClient = reinterpret_cast<ClientFn>(mLib.resolve("LIN_RemoveClient"));
    pConnectClient = reinterpret_cast<ClientHwFn>(mLib.resolve("LIN_ConnectClient"));
    pDisconnectClient = reinterpret_cast<ClientHwFn>(mLib.resolve("LIN_DisconnectClient"));
    pInitializeHardware = reinterpret_cast<InitializeHardwareFn>(mLib.resolve("LIN_InitializeHardware"));
    pGetAvailableHardware = reinterpret_cast<GetAvailableHardwareFn>(mLib.resolve("LIN_GetAvailableHardware"));
    pGetHardwareParam = reinterpret_cast<GetHardwareParamFn>(mLib.resolve("LIN_GetHardwareParam"));
    pSetFrameEntry = reinterpret_cast<SetFrameEntryFn>(mLib.resolve("LIN_SetFrameEntry"));
    pSetClientFilter = reinterpret_cast<SetClientFilterFn>(mLib.resolve("LIN_SetClientFilter"));
    pRead = reinterpret_cast<ReadFn>(mLib.resolve("LIN_Read"));
    pGetErrorText = reinterpret_cast<GetErrorTextFn>(mLib.resolve("LIN_GetErrorText"));
    if (!pRegisterClient || !pRemoveClient || !pConnectClient || !pDisconnectClient || !pInitializeHardware ||
        !pGetAvailableHardware || !pGetHardwareParam || !pSetFrameEntry || !pSetClientFilter || !pRead || !pGetErrorText)
    {
        mLoadError = "PLinApi.dll is missing functions SavvyCAN needs (an old PLIN driver?)";
        mLib.unload();
        return;
    }
    mLoaded = true;
#else
    mLoadError = "PEAK LIN interfaces are supported on Windows only so far (PEAK's PLIN-API)";
#endif
}

PlinDll &PlinDll::instance()
{
    static PlinDll dll;
    return dll;
}

QList<PlinHardware> PlinDll::hardware()
{
    QList<PlinHardware> result;
    if (!mLoaded) return result;
    int count = 0;
    if (pGetAvailableHardware(nullptr, 0, &count) != Plin::errOK || count <= 0) return result;
    QList<quint16> handles(count);
    if (pGetAvailableHardware(handles.data(), quint16(count * sizeof(quint16)), &count) != Plin::errOK) return result;
    for (int i = 0; i < count && i < handles.size(); i++)
    {
        PlinHardware hw;
        hw.handle = handles.at(i);
        char name[NAME_LENGTH + 1] = {};
        if (pGetHardwareParam(hw.handle, hwpName, name, NAME_LENGTH) == Plin::errOK) hw.name = QString::fromLatin1(name);
        pGetHardwareParam(hw.handle, hwpDeviceNumber, &hw.device, sizeof(int));
        pGetHardwareParam(hw.handle, hwpChannelNumber, &hw.channel, sizeof(int));
        pGetHardwareParam(hw.handle, hwpType, &hw.type, sizeof(int));
        result.append(hw);
    }
    return result;
}

quint32 PlinDll::registerClient(const QString &name, quint8 *client)
{
    if (!mLoaded) return NOT_LOADED;
    QByteArray n = name.toLatin1().left(NAME_LENGTH - 1);
    return pRegisterClient(n.data(), 0, client);
}

quint32 PlinDll::removeClient(quint8 client)
{
    return mLoaded ? pRemoveClient(client) : NOT_LOADED;
}

quint32 PlinDll::connectClient(quint8 client, quint16 hw)
{
    return mLoaded ? pConnectClient(client, hw) : NOT_LOADED;
}

quint32 PlinDll::disconnectClient(quint8 client, quint16 hw)
{
    return mLoaded ? pDisconnectClient(client, hw) : NOT_LOADED;
}

quint32 PlinDll::initializeHardware(quint8 client, quint16 hw, quint8 mode, quint16 baudrate)
{
    return mLoaded ? pInitializeHardware(client, hw, mode, baudrate) : NOT_LOADED;
}

quint32 PlinDll::setFrameEntry(quint8 client, quint16 hw, quint8 id, quint8 length, quint8 direction,
                               quint8 checksumType)
{
    if (!mLoaded) return NOT_LOADED;
    RawFrameEntry entry = {};
    entry.frameId = id;
    entry.length = length;
    entry.direction = direction;
    entry.checksumType = checksumType;
    return pSetFrameEntry(client, hw, &entry);
}

quint32 PlinDll::setClientFilter(quint8 client, quint16 hw, quint64 mask)
{
    return mLoaded ? pSetClientFilter(client, hw, mask) : NOT_LOADED;
}

quint32 PlinDll::read(quint8 client, PlinRcvMsg *msg)
{
    if (!mLoaded) return NOT_LOADED;
    RawRcvMsg raw = {};
    const quint32 rc = pRead(client, &raw);
    if (rc != Plin::errOK) return rc;
    msg->type = raw.type;
    msg->frameId = raw.frameId;
    msg->length = raw.length;
    msg->direction = raw.direction;
    msg->checksumType = raw.checksumType;
    for (int i = 0; i < 8; i++) msg->data[i] = raw.data[i];
    msg->checksum = raw.checksum;
    msg->errorFlags = quint32(raw.errorFlags);
    msg->timestampUs = raw.timestamp;
    msg->hardware = raw.hardware;
    return rc;
}

QString PlinDll::errorText(quint32 error)
{
    if (!mLoaded) return mLoadError;
    char text[256] = {};
    if (pGetErrorText(error, LANG_ENGLISH, text, sizeof(text) - 1) != Plin::errOK)
        return QString("PLIN-API error %1").arg(error);
    return QString::fromLatin1(text);
}
