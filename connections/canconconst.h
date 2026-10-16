#ifndef CANCONCONST_H
#define CANCONCONST_H

namespace CANCon {

    /**
     * @brief The status enum
     */
    enum status
    {
        NOT_CONNECTED,  /*!< device is not connected */
        //IN_PROGRESS,    //GS
        CONNECTED       /*!< device is connected */
    };

    enum type
    {
        GVRET_SERIAL,
        KVASER,
        SERIALBUS,
        REMOTE,
        KAYAK,
        MQTT,
        LAWICEL,
        CANSERVER,
        CANLOGSERVER,
        //saved connections store these as numbers: a fixed value keeps this one's whatever other branches add above
        APGDT001 = 20,
        PEAKLIN = 21,
        NONE
    };
}

class CANConStatus
{
public:
    CANCon::status conStatus;
    int numHardwareBuses;
};

#endif // CANCONCONST_H
