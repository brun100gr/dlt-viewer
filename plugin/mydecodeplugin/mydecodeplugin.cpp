#include <QtGui>
#include <QtEndian>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
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
//  Decoder registry loaded from JSON
//  JSON format (mydecodeplugin.json):
//  {
//    "decoders": [
//      {
//        "apid": "ADF", "ctid": "ADF_",
//        "prefix": "FCT_ManageLateralState_OUT_ConditionEvaluationDbg",
//        "outputPrefix": "",          // optional: if set, replaces "{text} -> "
//        "bits": [
//          { "value": 1, "name": "CarbodyLatAccelerationCorrected" },
//          ...
//        ]
//      }
//    ]
//  }
// ============================================================

// ============================================================
//  Plugin lifecycle
// ============================================================
MyDecodePlugin::MyDecodePlugin() {}

MyDecodePlugin::~MyDecodePlugin() {}

// ============================================================
//  QDLTPluginInterface
// ============================================================
QString MyDecodePlugin::name()                  { return "My Decode Plugin"; }
QString MyDecodePlugin::pluginVersion()         { return MY_DECODE_PLUGIN_VERSION; }
QString MyDecodePlugin::pluginInterfaceVersion(){ return PLUGIN_INTERFACE_VERSION; }
QString MyDecodePlugin::description()           { return QString(); }
QString MyDecodePlugin::error()                 { return errorText_; }

bool MyDecodePlugin::loadConfig(QString filename)
{
    errorText_.clear();
    dispatch_.clear();

    // Se nessun file è configurato in DLT Viewer, cerca nei percorsi standard.
    if (filename.isEmpty()) {
        const QStringList candidates = {
            QCoreApplication::applicationDirPath() + "/../../plugin/mydecodeplugin/mydecodeplugin.json",
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                + "/mydecodeplugin.json",
        };
        for (const QString &candidate : candidates) {
            qDebug() << "[MyDecodePlugin] Trying config path:" << candidate
                     << "- exists:" << QFile::exists(candidate);
            if (QFile::exists(candidate)) {
                filename = candidate;
                break;
            }
        }
        if (filename.isEmpty())
            return true; // nessun file trovato, dispatch rimane vuoto
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorText_ = QString("Cannot open config file: %1").arg(filename);
        return false;
    }

    QJsonParseError jsonErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonErr);
    if (doc.isNull()) {
        errorText_ = QString("JSON parse error in '%1': %2").arg(filename, jsonErr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    if (!root.contains("decoders")) {
        errorText_ = QString("Missing 'decoders' array in '%1'").arg(filename);
        return false;
    }

    const QJsonArray decoders = root.value("decoders").toArray();
    for (const QJsonValue &v : decoders) {
        const QJsonObject d = v.toObject();

        const QString apid   = d.value("apid").toString().trimmed();
        const QString ctid   = d.value("ctid").toString().trimmed();
        const QString prefix = d.value("prefix").toString().trimmed();
        if (apid.isEmpty() || ctid.isEmpty() || prefix.isEmpty())
            continue;

        const QString outputPrefix = d.value("outputPrefix").toString(); // "" se assente

        QVector<QPair<uint32_t, QString>> bitMap;
        const QJsonArray bits = d.value("bits").toArray();
        for (const QJsonValue &bv : bits) {
            const QJsonObject b = bv.toObject();
            // "value" può essere stringa (es. "0x10") o numero intero
            uint32_t bitVal = 0;
            if (b.value("value").isString()) {
                bool ok = false;
                bitVal = b.value("value").toString().toUInt(&ok, 0);
                if (!ok) continue;
            } else {
                bitVal = static_cast<uint32_t>(b.value("value").toInt(0));
            }
            const QString bitName = b.value("name").toString().trimmed();
            if (bitName.isEmpty()) continue;
            bitMap.append({bitVal, bitName});
        }

        const QString key = apid + "|" + ctid + "|" + prefix;
        dispatch_[key] = [bitMap, outputPrefix](QDltMsg &msg, const QString &text) -> bool
        {
            const int colonIdx = text.lastIndexOf(':');
            if (colonIdx < 0) return false;

            bool ok = false;
            const uint32_t mask = text.mid(colonIdx + 1).trimmed().toUInt(&ok);
            if (!ok) return false;

            QStringList active;
            for (const auto &pair : bitMap)
                if (mask & pair.first)
                    active << pair.second;

            const QString activeStr = active.isEmpty() ? "none" : active.join(", ");
            const QString decoded   = outputPrefix.isEmpty()
                                      ? text + " -> " + activeStr
                                      : outputPrefix + activeStr;

            const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
            QByteArray newPayload = MyDecodePlugin::buildDltStringPayload(decoded, le);
            msg.setPayload(newPayload);
            msg.parseArguments();
            return true;
        };
    }

    return true;
}

bool MyDecodePlugin::saveConfig(QString) { return true; }

QStringList MyDecodePlugin::infoConfig()
{
    QStringList info;
    info << QString("Loaded decoders: %1").arg(dispatch_.size());
    for (auto it = dispatch_.constBegin(); it != dispatch_.constEnd(); ++it) {
        const QStringList parts = it.key().split('|');
        info << (parts.size() == 3
                 ? QString("  %1 | %2 | %3").arg(parts[0], parts[1], parts[2])
                 : QString("  ") + it.key());
    }
    return info;
}

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