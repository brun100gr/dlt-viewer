#include <QtGui>
#include "mydecodeplugin.h"

MyDecodePlugin::MyDecodePlugin() {
    dltFile = nullptr;
    counterMessages = 0;
    counterNonVerboseMessages = 0;
    counterVerboseMessages = 0;
}

MyDecodePlugin::~MyDecodePlugin() {
    // anche vuoto va bene, ma deve essere definito nel .cpp
}

bool MyDecodePlugin::isActiveDltDecoder() {
    return true;
}

bool MyDecodePlugin::isMsg(QDltMsg &msg, int /*triggeredByUser*/) {
    QString text = msg.toStringPayload();
    QRegularExpression re(R"(FCT_ManageLateralState_OUT_ConditionEvaluationDbg:\s*(\d+))");
    return re.match(text).hasMatch();
}

bool MyDecodePlugin::isAcceptMsg(int index, QDltMsg &msg) {
    // Chiamato per ogni messaggio: accetta solo quelli con il nostro prefisso
    QString text = msg.toStringPayload();
    QRegularExpression re(R"(FCT_ManageLateralState_OUT_ConditionEvaluationDbg:\s*(\d+))");
    return re.match(text).hasMatch();
}

bool MyDecodePlugin::decodeMsg(QDltMsg &msg, int /*triggeredByUser*/) {
    QString text = msg.toStringPayload();

    QRegularExpression re(R"(FCT_ManageLateralState_OUT_ConditionEvaluationDbg:\s*(\d+))");
    QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return false;

    bool ok = false;
    uint32_t mask = match.captured(1).toUInt(&ok);
    if (!ok)
        return false;

    // --- decode bitmask ---
    static const QMap<uint32_t, QString> bitMap = {
        {1, "dbgCarbodyLatAccelerationCorrected"},
        {2, "dbgLPA_PerceptionState"},
        {4, "dbgDstWidthEgoLine"},
        {8, "dbgSteeringwheelRotationVelocityFrontValue"},
        {16, "dbgMAL"},
        {32, "dbgESC_Status"},
        {64, "dbgESC_TCS_State"},
        {128, "dbgVehicleLongSpeedComputedValue"},
        {256, "dbgFailureDetected"},
        {512, "dbgTurnSignalSts"},
        {1024, "dbgHandwheelSteeringTorqueMeasured"},
        {2048, "dbgADAS_VehicleMotion"},
        {4096, "dbgHOD_Flag"},
    };

    QStringList conditions;
    for (auto it = bitMap.constBegin(); it != bitMap.constEnd(); ++it) {
        if (mask & it.key())
            conditions << it.value();
    }

    QString decoded = conditions.isEmpty()
        ? "none"
        : conditions.join(", ");

    // payload finale: originale + " -> " + decodifica
    QString final = text + " -> " + decoded;

    // Costruisci payload DLT binario
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

    fprintf(stderr, "DLT decoded: %s\n", final.toUtf8().constData());
    return true;
}

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

bool MyDecodePlugin::loadConfig(QString /*filename*/) {
    return true;
}

bool MyDecodePlugin::saveConfig(QString /*filename*/) {
    return true;
}

QStringList MyDecodePlugin::infoConfig() {
    return QStringList();
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(MyDecodePlugin, MyDecodePlugin);
#endif


