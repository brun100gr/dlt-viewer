/**
 * @licence app begin@
 * Copyright (C) 2026  COVESA
 *
 * This file is part of COVESA Project Dlt Viewer.
 *
 * Contributions are licensed to the COVESA Alliance under one or more
 * Contribution License Agreements.
 *
 * \copyright
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with
 * this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * \file dltnoisefilterplugin.cpp
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#include <QtGui>
#include <QtEndian>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "dltnoisefilterplugin.h"

// Compile-time defaults used when no JSON config file is provided.
static const char   * const kDefaultMarker         = DLT_NOISE_FILTER_MARKER;
static const double         kDefaultGlobalThreshold = 0.05;

// ============================================================
//  Utility: build a well-formed DLT verbose string payload
//  (4-byte typeInfo LE/BE + 2-byte length + UTF-8 data + NUL)
// ============================================================
QByteArray DltNoiseFilterPlugin::buildDltStringPayload(const QString &text, bool littleEndian)
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

QString DltNoiseFilterPlugin::makeKey(const QString &apid, const QString &ctid, const QString &name)
{
    return apid + "|" + ctid + "|" + name;
}

// ============================================================
//  Plugin lifecycle
// ============================================================
DltNoiseFilterPlugin::DltNoiseFilterPlugin()
    : dltFile(nullptr),
      defaultThreshold(kDefaultGlobalThreshold),
      marker(kDefaultMarker)
{
    // Signals are loaded from the JSON config file via loadConfig().
    // If no file is configured the threshold table stays empty and only
    // the global defaultThreshold is applied to numeric signals.
}

DltNoiseFilterPlugin::~DltNoiseFilterPlugin()
{
}

// ============================================================
//  QDLTPluginInterface
// ============================================================
QString DltNoiseFilterPlugin::name()                   { return "DLT Noise Filter Plugin"; }
QString DltNoiseFilterPlugin::pluginVersion()          { return DLT_NOISE_FILTER_PLUGIN_VERSION; }
QString DltNoiseFilterPlugin::pluginInterfaceVersion() { return PLUGIN_INTERFACE_VERSION; }
QString DltNoiseFilterPlugin::description()
{
    return QString("Suppresses noisy physical signals using a per-signal dead-band. "
                   "Noisy messages are marked with the token '%1' so a negative DLT "
                   "filter on that token hides them from the view.")
            .arg(marker);
}
QString DltNoiseFilterPlugin::error() { return errorText; }

bool DltNoiseFilterPlugin::loadConfig(QString filename)
{
    errorText.clear();

    // If no file was configured in DLT Viewer, search in well-known locations.
    if (filename.isEmpty()) {
        const QStringList candidates = {
            // Next to the dlt-viewer executable (most common install layout)
            QCoreApplication::applicationDirPath() + "/../../plugin/dltnoisefilterplugin/dltnoisefilterplugin.json",
            // User config directory (~/.config/dlt-viewer/ on Linux)
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                + "/dltnoisefilterplugin.json",
        };
        for (const QString &candidate : candidates) {
            qDebug() << "[DltNoiseFilter] Trying config path:" << candidate
                     << "- exists:" << QFile::exists(candidate);
            if (QFile::exists(candidate)) {
                filename = candidate;
                break;
            }
        }
        if (filename.isEmpty())
            return true; // no file found anywhere, keep defaults
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorText = QString("Cannot open config file: %1").arg(filename);
        return false;
    }

    QJsonParseError jsonErr;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &jsonErr);
    if (doc.isNull()) {
        errorText = QString("JSON parse error in '%1': %2").arg(filename, jsonErr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();

    if (root.contains("marker"))
        marker = root.value("marker").toString(marker).trimmed();
    if (root.contains("defaultThreshold"))
        defaultThreshold = root.value("defaultThreshold").toDouble(defaultThreshold);

    if (root.contains("signals")) {
        thresholds.clear();
        const QJsonArray jsonSignals = root.value("signals").toArray();
        for (const QJsonValue &v : jsonSignals) {
            const QJsonObject s = v.toObject();
            const QString name = s.value("name").toString().trimmed();
            if (name.isEmpty())
                continue;
            const QString apid = s.value("apid").toString().trimmed();
            const QString ctid = s.value("ctid").toString().trimmed();
            const double  thr  = s.value("threshold").toDouble();
            thresholds.insert(makeKey(apid, ctid, name), thr);
        }
    }

    return true;
}

bool DltNoiseFilterPlugin::saveConfig(QString /*filename*/) { return true; }

QStringList DltNoiseFilterPlugin::infoConfig()
{
    QStringList info;
    info << QString("Marker: %1").arg(marker);
    info << QString("Default threshold: %1")
                .arg(defaultThreshold > 0.0 ? QString::number(defaultThreshold)
                                            : QString("disabled"));
    for (auto it = thresholds.constBegin(); it != thresholds.constEnd(); ++it) {
        const QStringList parts = it.key().split('|');
        const QString label = (parts.size() == 3)
            ? QString("%1 | %2 | %3").arg(parts[0], parts[1], parts[2])
            : it.key();
        info << QString("%1  (threshold %2)").arg(label).arg(it.value());
    }
    return info;
}

// ============================================================
//  Dead-band evaluation
// ============================================================
bool DltNoiseFilterPlugin::parseSignal(QDltMsg &msg, QString &keyOut, double &valueOut) const
{
    const QString apid = msg.getApid().trimmed();
    const QString ctid = msg.getCtid().trimmed();
    const QString text = msg.toStringPayload();

    const int colonIdx = text.lastIndexOf(':');
    if (colonIdx < 0)
        return false;

    const QString signalName = text.left(colonIdx).trimmed();
    const QString key = makeKey(apid, ctid, signalName);

    // Accept signals with an explicit entry, or any numeric signal when the
    // global default threshold is active.
    if (!thresholds.contains(key) && defaultThreshold <= 0.0)
        return false;

    bool ok = false;
    const double value = text.mid(colonIdx + 1).trimmed().toDouble(&ok);
    if (!ok)
        return false;

    keyOut   = key;
    valueOut = value;
    return true;
}

void DltNoiseFilterPlugin::evaluate(int index, QDltMsg &msg)
{
    QString key;
    double value = 0.0;
    if (!parseSignal(msg, key, value))
        return;

    const double threshold = thresholds.value(key, defaultThreshold);
    if (threshold <= 0.0)
        return;

    auto it = lastShown.find(key);
    if (it == lastShown.end()) {
        lastShown.insert(key, value);
        return; // first occurrence -> always show
    }

    if (qAbs(value - it.value()) >= threshold) {
        it.value() = value; // significant change -> show and update reference
        return;
    }

    // Within dead-band -> suppress.
    suppressIndices.insert(index);
}

// ============================================================
//  QDLTPluginDecoderInterface
// ============================================================
bool DltNoiseFilterPlugin::isMsg(QDltMsg &msg, int /*triggeredByUser*/)
{
    return suppressIndices.contains(msg.getIndex());
}

bool DltNoiseFilterPlugin::decodeMsg(QDltMsg &msg, int /*triggeredByUser*/)
{
    const QString newText = marker + " " + msg.toStringPayload();
    const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
    QByteArray newPayload = buildDltStringPayload(newText, le);
    msg.setPayload(newPayload);
    msg.parseArguments();
    return true;
}

// ============================================================
//  QDltPluginViewerInterface
// ============================================================
QWidget* DltNoiseFilterPlugin::initViewer()
{
    return nullptr; // no GUI panel
}

void DltNoiseFilterPlugin::initFileStart(QDltFile *file)
{
    dltFile = file;
    lastShown.clear();
    suppressIndices.clear();
}

void DltNoiseFilterPlugin::initMsg(int index, QDltMsg &msg)
{
    evaluate(index, msg);
}

void DltNoiseFilterPlugin::initMsgDecoded(int /*index*/, QDltMsg &/*msg*/) {}
void DltNoiseFilterPlugin::initFileFinish() {}
void DltNoiseFilterPlugin::updateFileStart() {}

void DltNoiseFilterPlugin::updateMsg(int index, QDltMsg &msg)
{
    evaluate(index, msg);
}

void DltNoiseFilterPlugin::updateMsgDecoded(int /*index*/, QDltMsg &/*msg*/) {}
void DltNoiseFilterPlugin::updateFileFinish() {}
void DltNoiseFilterPlugin::selectedIdxMsg(int /*index*/, QDltMsg &/*msg*/) {}
void DltNoiseFilterPlugin::selectedIdxMsgDecoded(int /*index*/, QDltMsg &/*msg*/) {}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(DltNoiseFilterPlugin, DltNoiseFilterPlugin);
#endif
