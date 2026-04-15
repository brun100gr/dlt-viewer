#include <QtGui>
#include "mydecodeplugin.h"

// ==========================
// Dispatcher type
// ==========================
using DecoderFn = std::function<bool(QDltMsg&, const QString&)>;

// ==========================
// Dispatcher registry
// ==========================
static QHash<QString, DecoderFn> dispatch;

// ==========================
// Constructor
// ==========================
MyDecodePlugin::MyDecodePlugin() {
    dltFile = nullptr;
    counterMessages = 0;
    counterNonVerboseMessages = 0;
    counterVerboseMessages = 0;

    // =========================================================
    // Decoder: ConditionEvaluationDbg
    // =========================================================
    dispatch["ADF|ADF_|FCT_ManageLateralState_OUT_ConditionEvaluationDbg"] =
        [](QDltMsg &msg, const QString &text) -> bool {

        // --- extract number (NO regex) ---
        int idx = text.lastIndexOf(':');
        if (idx < 0) return false;

        bool ok = false;
        uint32_t mask = text.mid(idx + 1).trimmed().toUInt(&ok);
        if (!ok) return false;

        // --- decode bitmask ---
        static const QMap<uint32_t, QString> bitMap = {
            {1, "CarbodyLatAccelerationCorrected"},
            {2, "LPA_PerceptionState"},
            {4, "DstWidthEgoLine"},
            {8, "SteeringwheelRotationVelocityFrontValue"},
            {16, "MAL"},
            {32, "ESC_Status"},
            {64, "ESC_TCS_State"},
            {128, "VehicleLongSpeedComputedValue"},
            {256, "FailureDetected"},
            {512, "TurnSignalSts"},
            {1024, "HandwheelSteeringTorqueMeasured"},
            {2048, "ADAS_VehicleMotion"},
            {4096, "HOD_Flag"},
        };

        QStringList conditions;
        for (auto it = bitMap.begin(); it != bitMap.end(); ++it) {
            if (mask & it.key())
                conditions << it.value();
        }

        QString decoded = conditions.isEmpty()
            ? "none"
            : conditions.join(", ");

        QString final = text + " -> " + decoded;

        // --- Build DLT payload ---
        QByteArray newPayload;
        bool littleEndian = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);

        QByteArray strData = final.toUtf8();
        strData.append('\0');

        uint32_t typeInfo = 0x00000200;  // DLT_TYPE_INFO_STRG
        uint16_t strLen   = static_cast<uint16_t>(strData.size());

        if (littleEndian) {
            newPayload.append(reinterpret_cast<const char*>(&typeInfo), 4);
            newPayload.append(reinterpret_cast<const char*>(&strLen),   2);
        } else {
            uint32_t typeInfoBE = qToBigEndian(typeInfo);
            uint16_t strLenBE   = qToBigEndian(strLen);
            newPayload.append(reinterpret_cast<const char*>(&typeInfoBE), 4);
            newPayload.append(reinterpret_cast<const char*>(&strLenBE),   2);
        }

        newPayload.append(strData);

        msg.setPayload(newPayload);
        msg.parseArguments();

        return true;
    };

    // =========================================================
    // Decoder: Generic for ADF|ADF_|ALTRO_PREFIX
    // =========================================================
    dispatch["APID|CTID|OTHER_PREFIX"] =
        [](QDltMsg &msg, const QString &text) -> bool {

        // parsing
        // decoding
        // msg.setPayload(...)

        return true;
    };
}

// ==========================
MyDecodePlugin::~MyDecodePlugin() {}

// ==========================
bool MyDecodePlugin::isActiveDltDecoder() {
    return true;
}

// ==========================
// FAST FILTER (important)
// ==========================
bool MyDecodePlugin::isMsg(QDltMsg &msg, int) {
    return msg.getApid().trimmed() == "ADF" &&
           msg.getCtid().trimmed() == "ADF_";
}

// ==========================
bool MyDecodePlugin::isAcceptMsg(int, QDltMsg &) {
    return true;
}

// ==========================
// DISPATCH CORE
// ==========================
bool MyDecodePlugin::decodeMsg(QDltMsg &msg, int)
{
    QString apid = msg.getApid().trimmed();
    QString ctid = msg.getCtid().trimmed();

    QString text = msg.toStringPayload();

    // --- extract prefix ---
    int idx = text.indexOf(':');
    if (idx < 0)
        return false;

    QString prefix = text.left(idx);

    // --- build key ---
    QString key = apid + "|" + ctid + "|" + prefix;

    // --- dispatch ---
    if (dispatch.contains(key)) {
        return dispatch[key](msg, text);
    }

    return false;
}

// ==========================
QString MyDecodePlugin::name() {
    return QString("My Decode Plugin");
}

QString MyDecodePlugin::pluginVersion() {
    return MY_DECODE_PLUGIN_VERSION;
}

QString MyDecodePlugin::pluginInterfaceVersion() {
    return PLUGIN_INTERFACE_VERSION;
}

QString MyDecodePlugin::description() {
    return QString();
}

QString MyDecodePlugin::error() {
    return QString();
}

bool MyDecodePlugin::loadConfig(QString) {
    return true;
}

bool MyDecodePlugin::saveConfig(QString) {
    return true;
}

QStringList MyDecodePlugin::infoConfig() {
    return QStringList();
}

// ==========================
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(MyDecodePlugin, MyDecodePlugin);
#endif