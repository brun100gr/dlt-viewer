#include <QtGui>
#include <QtEndian>
#include <QRegularExpression>
#include "mydecodeplugin.h"

// ============================================================
//  Utility: build a well-formed DLT verbose string payload
//  (4-byte typeInfo + 2-byte length + UTF-8 data + NUL)
// ============================================================
QByteArray MyDecodePlugin::buildDltStringPayload(const QString &text,
                                                 bool littleEndian)
{
    QByteArray strData = text.toUtf8();
    strData.append('\0');

    const uint32_t TYPE_INFO_STRG = 0x00000200u;
    const uint16_t strLen = static_cast<uint16_t>(strData.size());

    QByteArray payload;
    payload.reserve(4 + 2 + strData.size());

    if (littleEndian) {
        payload.append(reinterpret_cast<const char *>(&TYPE_INFO_STRG), 4);
        payload.append(reinterpret_cast<const char *>(&strLen), 2);
    } else {
        const uint32_t typeInfoBE = qToBigEndian(TYPE_INFO_STRG);
        const uint16_t strLenBE   = qToBigEndian(strLen);
        payload.append(reinterpret_cast<const char *>(&typeInfoBE), 4);
        payload.append(reinterpret_cast<const char *>(&strLenBE),   2);
    }

    payload.append(strData);
    return payload;
}

// ============================================================
//  Decoder registry
//  Key format: "APID|CTID|payload_prefix"
//  Add one entry per signal you want to decode.
// ============================================================
void MyDecodePlugin::registerDecoders(QHash<QString, DecoderFn> &d)
{
    // ----------------------------------------------------------
    // Decoder 1: ADF | ADF_ | FCT_ManageLateralState_OUT_ConditionEvaluationDbg
    // ----------------------------------------------------------
    d["ADF|ADF_|FCT_ManageLateralState_OUT_ConditionEvaluationDbg"] =
        [](QDltMsg &msg, const QString &text) -> bool
    {
        const int colonIdx = text.lastIndexOf(':');
        if (colonIdx < 0) return false;

        bool ok = false;
        const uint32_t mask = text.mid(colonIdx + 1).trimmed().toUInt(&ok);
        if (!ok) return false;

        static const QVector<QPair<uint32_t, QString>> bitMap = {
            {    1u, "CarbodyLatAccelerationCorrected"},
            {    2u, "LPA_PerceptionState"},
            {    4u, "DstWidthEgoLine"},
            {    8u, "SteeringwheelRotationVelocityFrontValue"},
            {   16u, "MAL"},
            {   32u, "ESC_Status"},
            {   64u, "ESC_TCS_State"},
            {  128u, "VehicleLongSpeedComputedValue"},
            {  256u, "FailureDetected"},
            {  512u, "TurnSignalSts"},
            { 1024u, "HandwheelSteeringTorqueMeasured"},
            { 2048u, "ADAS_VehicleMotion"},
            { 4096u, "HOD_Flag"},
        };

        QStringList active;
        for (const auto &pair : bitMap)
            if (mask & pair.first)
                active << pair.second;

        const QString decoded = text + " -> " +
                                (active.isEmpty() ? "none" : active.join(", "));

        const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
        QByteArray newPayload = buildDltStringPayload(decoded, le);
        msg.setPayload(newPayload);
        msg.parseArguments();
        return true;
    };

    // ----------------------------------------------------------
    // Decoder 1: ADF | ADF_ | FCT_ManageLateralState_OUT_FailureEvaluationDbg
    // ----------------------------------------------------------
    d["ADF|ADF_|FCT_ManageLateralState_OUT_FailureEvaluationDbg"] =
        [](QDltMsg &msg, const QString &text) -> bool
    {
        const int colonIdx = text.lastIndexOf(':');
        if (colonIdx < 0) return false;

        bool ok = false;
        const uint32_t mask = text.mid(colonIdx + 1).trimmed().toUInt(&ok);
        if (!ok) return false;

        static const QVector<QPair<uint32_t, QString>> bitMap = {
            {    1u, "uno"},
            {    2u, "due"},
            {    4u, "tre"},
            {    8u, "quattro"},
            {   16u, "cinque"},
            {   32u, "sei"},
            {   64u, "sette"},
            {  128u, "otto"},
            {  256u, "nove"},
            {  512u, "dieci"},
            { 1024u, "undici"},
            { 2048u, "dodici"},
            { 4096u, "tredici"},
        };

        QStringList active;
        for (const auto &pair : bitMap)
            if (mask & pair.first)
                active << pair.second;

        const QString decoded = text + " -> " +
                                (active.isEmpty() ? "none" : active.join(", "));

        const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
        QByteArray newPayload = buildDltStringPayload(decoded, le);
        msg.setPayload(newPayload);
        msg.parseArguments();
        return true;
    };

    // ----------------------------------------------------------
    // Decoder 2: APP2 | CTX2 | ActivationCondition
    // Generic bitmask → condizione1..condizione32
    // Replace APP2/CTX2 with your real APID and CTID.
    // ----------------------------------------------------------
    d["APP2|CTX2|ActivationCondition"] =
        [](QDltMsg &msg, const QString &text) -> bool
    {
        const int colonIdx = text.lastIndexOf(':');
        if (colonIdx < 0) return false;

        bool ok = false;
        const uint32_t mask = text.mid(colonIdx + 1).trimmed().toUInt(&ok);
        if (!ok) return false;

        QStringList conditions;
        for (int i = 0; i < 32; ++i)
            if (mask & (1u << i))
                conditions << QString("condizione%1").arg(i + 1);

        const QString decoded = "Activation Condition are: " +
                                (conditions.isEmpty() ? "none" : conditions.join(", "));

        const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
        QByteArray newPayload = buildDltStringPayload(decoded, le);
        msg.setPayload(newPayload);
        msg.parseArguments();
        return true;
    };

    // ----------------------------------------------------------
    // Aggiungi altri decoder qui con lo stesso schema:
    //
    // d["APID|CTID|TuoPrefix"] = [](QDltMsg &msg, const QString &text) -> bool
    // {
    //     const int colonIdx = text.lastIndexOf(':');
    //     if (colonIdx < 0) return false;
    //
    //     bool ok = false;
    //     const uint32_t mask = text.mid(colonIdx + 1).trimmed().toUInt(&ok);
    //     if (!ok) return false;
    //
    //     // ... tua logica di decodifica ...
    //
    //     const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
    //     QByteArray newPayload = buildDltStringPayload(decoded, le);
    //     msg.setPayload(newPayload);
    //     msg.parseArguments();
    //     return true;
    // };
    // ----------------------------------------------------------
}

// ============================================================
//  Plugin lifecycle
// ============================================================
MyDecodePlugin::MyDecodePlugin()
{
    registerDecoders(dispatch_);
}

MyDecodePlugin::~MyDecodePlugin() {}

// ============================================================
//  QDLTPluginInterface
// ============================================================
QString MyDecodePlugin::name()                  { return "My Decode Plugin"; }
QString MyDecodePlugin::pluginVersion()         { return MY_DECODE_PLUGIN_VERSION; }
QString MyDecodePlugin::pluginInterfaceVersion(){ return PLUGIN_INTERFACE_VERSION; }
QString MyDecodePlugin::description()           { return QString(); }
QString MyDecodePlugin::error()                 { return QString(); }
bool    MyDecodePlugin::loadConfig(QString)     { return true; }
bool    MyDecodePlugin::saveConfig(QString)     { return true; }
QStringList MyDecodePlugin::infoConfig()        { return QStringList(); }

// ============================================================
//  QDLTPluginDecoderInterface
// ============================================================
bool MyDecodePlugin::isActiveDltDecoder()
{
    return true;
}

// Pre-filtro rapido per APID+CTID: evita di chiamare toStringPayload()
// su ogni messaggio del sistema.
bool MyDecodePlugin::isMsg(QDltMsg &msg, int /*triggeredByUser*/)
{
    const QString apid = msg.getApid().trimmed();
    const QString ctid = msg.getCtid().trimmed();

    // Controlla se esiste almeno una entry nel dispatch con questo APID+CTID
    const QString keyPrefix = apid + "|" + ctid + "|";
    for (auto it = dispatch_.constBegin(); it != dispatch_.constEnd(); ++it)
        if (it.key().startsWith(keyPrefix))
            return true;

    return false;
}

// Filtro completo: verifica APID + CTID + prefix del payload
bool MyDecodePlugin::isAcceptMsg(int /*index*/, QDltMsg &msg)
{
    const QString apid = msg.getApid().trimmed();
    const QString ctid = msg.getCtid().trimmed();
    const QString text = msg.toStringPayload();

    const int colonIdx = text.indexOf(':');
    if (colonIdx < 0) return false;

    const QString prefix = text.left(colonIdx).trimmed();
    const QString key    = apid + "|" + ctid + "|" + prefix;

    return dispatch_.contains(key);
}

// Decodifica: se nessun decoder matcha, il payload rimane invariato
bool MyDecodePlugin::decodeMsg(QDltMsg &msg, int /*triggeredByUser*/)
{
    const QString apid = msg.getApid().trimmed();
    const QString ctid = msg.getCtid().trimmed();
    const QString text = msg.toStringPayload();

    const int colonIdx = text.indexOf(':');
    if (colonIdx < 0) return false;

    const QString prefix = text.left(colonIdx).trimmed();
    const QString key    = apid + "|" + ctid + "|" + prefix;

    const auto it = dispatch_.constFind(key);
    if (it == dispatch_.constEnd())
        return false;  // nessun match → payload invariato

    return it.value()(msg, text);
}

// ============================================================
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(MyDecodePlugin, MyDecodePlugin);
#endif