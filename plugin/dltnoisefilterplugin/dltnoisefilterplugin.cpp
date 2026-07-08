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
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "dltnoisefilterplugin.h"

// ============================================================
//  Utility: build a well-formed DLT verbose string payload
//  (4-byte typeInfo + 2-byte length + UTF-8 data + NUL)
// ============================================================
QByteArray DltNoiseFilterPlugin::buildDltStringPayload(const QString &text,
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

QString DltNoiseFilterPlugin::makeKey(const QString &apid,
                                      const QString &ctid,
                                      const QString &name)
{
    return apid + "|" + ctid + "|" + name;
}

// ============================================================
//  Plugin lifecycle
// ============================================================
DltNoiseFilterPlugin::DltNoiseFilterPlugin()
    : defaultThreshold(0.0),
      marker(DLT_NOISE_FILTER_DEFAULT_MARKER),
      suppressed(0),
      dltFile(nullptr),
      form(nullptr)
{
    loadDefaults();
}

DltNoiseFilterPlugin::~DltNoiseFilterPlugin()
{
}

void DltNoiseFilterPlugin::loadDefaults()
{
    // Built-in defaults for the noisy signals from the example.
    // These can be overridden by a configuration file or via the UI.
    const QList<NoiseSignalConfig> defaults = {
        { "ADF", "ADF_", "AdasDrivingFunction_IN_VehicleLongSpeedComputedValue",   1.0 },
        { "ADF", "ADF_", "AdasDrivingFunction_IN_CarbodyLatAccelerationCorrected", 0.5 },
    };
    // Global default dead-band applied to every other numeric signal.
    setConfiguration(defaults, marker, 0.05);
}

// ============================================================
//  QDLTPluginInterface
// ============================================================
QString DltNoiseFilterPlugin::name()                  { return "DLT Noise Filter Plugin"; }
QString DltNoiseFilterPlugin::pluginVersion()         { return DLT_NOISE_FILTER_PLUGIN_VERSION; }
QString DltNoiseFilterPlugin::pluginInterfaceVersion(){ return PLUGIN_INTERFACE_VERSION; }
QString DltNoiseFilterPlugin::description()
{
    return QString("Suppresses noisy physical signals (speed, acceleration, ...) "
                   "using a per-signal dead-band. Noisy messages are marked with "
                   "the token '%1', so a negative filter on the token hides them.")
            .arg(marker);
}
QString DltNoiseFilterPlugin::error()                 { return errorText; }

bool DltNoiseFilterPlugin::loadConfig(QString filename)
{
    errorText.clear();

    if (filename.isEmpty())
        return true; // keep built-in defaults

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorText = QString("Cannot open configuration file: %1").arg(filename);
        return false;
    }

    QList<NoiseSignalConfig> configs;
    QString newMarker = marker;
    double newDefault = defaultThreshold;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        if (xml.name() == QLatin1String("noisefilter")) {
            const QString m = xml.attributes().value("marker").toString();
            if (!m.isEmpty())
                newMarker = m;
            const QString d = xml.attributes().value("defaultThreshold").toString();
            if (!d.isEmpty()) {
                bool dok = false;
                const double dv = d.toDouble(&dok);
                if (dok)
                    newDefault = dv;
            }
        } else if (xml.name() == QLatin1String("signal")) {
            const QXmlStreamAttributes attr = xml.attributes();
            NoiseSignalConfig cfg;
            cfg.apid      = attr.value("apid").toString().trimmed();
            cfg.ctid      = attr.value("ctid").toString().trimmed();
            cfg.name      = attr.value("name").toString().trimmed();
            bool ok = false;
            cfg.threshold = attr.value("threshold").toString().toDouble(&ok);
            if (!ok || cfg.name.isEmpty())
                continue;
            configs.append(cfg);
        }
    }

    if (xml.hasError()) {
        errorText = QString("Error parsing configuration file: %1").arg(xml.errorString());
        return false;
    }

    if (!configs.isEmpty())
        setConfiguration(configs, newMarker, newDefault);

    return true;
}

bool DltNoiseFilterPlugin::saveConfig(QString filename)
{
    errorText.clear();

    if (filename.isEmpty()) {
        errorText = "No configuration file name given.";
        return false;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorText = QString("Cannot write configuration file: %1").arg(filename);
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("noisefilter");
    xml.writeAttribute("marker", marker);
    xml.writeAttribute("defaultThreshold", QString::number(defaultThreshold));
    for (const NoiseSignalConfig &cfg : configByKey) {
        xml.writeStartElement("signal");
        xml.writeAttribute("apid", cfg.apid);
        xml.writeAttribute("ctid", cfg.ctid);
        xml.writeAttribute("name", cfg.name);
        xml.writeAttribute("threshold", QString::number(cfg.threshold));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();

    return true;
}

QStringList DltNoiseFilterPlugin::infoConfig()
{
    QStringList info;
    info << QString("Marker token: %1").arg(marker);
    info << QString("Default threshold (all other numeric signals): %1")
                .arg(defaultThreshold > 0.0 ? QString::number(defaultThreshold)
                                            : QString("disabled"));
    for (const NoiseSignalConfig &cfg : configByKey) {
        info << QString("%1 | %2 | %3  (threshold %4)")
                    .arg(cfg.apid, cfg.ctid, cfg.name)
                    .arg(cfg.threshold);
    }
    return info;
}

// ============================================================
//  Configuration API
// ============================================================
QList<NoiseSignalConfig> DltNoiseFilterPlugin::signalConfigs() const
{
    return configByKey.values();
}

void DltNoiseFilterPlugin::setConfiguration(const QList<NoiseSignalConfig> &configs,
                                            const QString &markerToken,
                                            double defaultThr)
{
    thresholds.clear();
    configByKey.clear();
    marker = markerToken.trimmed().isEmpty() ? QString(DLT_NOISE_FILTER_DEFAULT_MARKER)
                                             : markerToken.trimmed();
    defaultThreshold = (defaultThr > 0.0) ? defaultThr : 0.0;

    for (const NoiseSignalConfig &cfg : configs) {
        const QString key = makeKey(cfg.apid, cfg.ctid, cfg.name);
        thresholds.insert(key, cfg.threshold);
        configByKey.insert(key, cfg);
    }
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

    // Accept any signal that has an explicit configuration, or - when a global
    // default dead-band is active - any numeric "name : value" signal.
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
        return; // not a numeric signal subject to filtering -> always kept

    // Per-signal threshold if configured, otherwise the global default.
    const double threshold = thresholds.value(key, defaultThreshold);
    if (threshold <= 0.0)
        return; // 0 (or disabled) -> never suppress this signal

    auto it = lastShown.find(key);
    if (it == lastShown.end()) {
        // First occurrence of this signal -> always show it.
        lastShown.insert(key, value);
        return;
    }

    if (qAbs(value - it.value()) >= threshold) {
        // Significant change -> show it and update the reference value.
        it.value() = value;
        return;
    }

    // Within the dead-band -> this is noise, suppress it.
    suppressIndices.insert(index);
    ++suppressed;
}

void DltNoiseFilterPlugin::reanalyze()
{
    lastShown.clear();
    suppressIndices.clear();
    suppressed = 0;

    if (!dltFile)
        return;

    for (int i = 0; i < dltFile->size(); ++i) {
        QDltMsg msg;
        if (dltFile->getMsg(i, msg))
            evaluate(i, msg);
    }

    if (form)
        form->updateStatus();
}

// ============================================================
//  QDLTPluginDecoderInterface
// ============================================================
// Only the messages that were marked as noise during indexing are decoded.
bool DltNoiseFilterPlugin::isMsg(QDltMsg &msg, int /*triggeredByUser*/)
{
    return suppressIndices.contains(msg.getIndex());
}

// Prepend the marker token to the payload so that a negative filter can hide it.
bool DltNoiseFilterPlugin::decodeMsg(QDltMsg &msg, int /*triggeredByUser*/)
{
    const QString text    = msg.toStringPayload();
    const QString decoded = marker + " " + text;

    const bool le = (msg.getEndianness() == QDlt::DltEndiannessLittleEndian);
    QByteArray newPayload = buildDltStringPayload(decoded, le);
    msg.setPayload(newPayload);
    msg.parseArguments();
    return true;
}

// ============================================================
//  QDltPluginViewerInterface
// ============================================================
QWidget* DltNoiseFilterPlugin::initViewer()
{
    form = new DltNoiseFilterPluginNs::Form(this);
    return form;
}

void DltNoiseFilterPlugin::initFileStart(QDltFile *file)
{
    dltFile = file;

    // Reset all sequential state: it is rebuilt by initMsg() in file order.
    lastShown.clear();
    suppressIndices.clear();
    suppressed = 0;
}

void DltNoiseFilterPlugin::initMsg(int index, QDltMsg &msg)
{
    if (!dltFile)
        return;
    // initMsg() is guaranteed to be called sequentially in file order, which
    // is exactly what the dead-band needs. The decision is stored per absolute
    // index and consumed later by isMsg()/decodeMsg().
    evaluate(index, msg);
}

void DltNoiseFilterPlugin::initMsgDecoded(int /*index*/, QDltMsg &/*msg*/)
{
}

void DltNoiseFilterPlugin::initFileFinish()
{
    if (form)
        form->updateStatus();
}

void DltNoiseFilterPlugin::updateFileStart()
{
}

void DltNoiseFilterPlugin::updateMsg(int index, QDltMsg &msg)
{
    if (!dltFile)
        return;
    evaluate(index, msg);
}

void DltNoiseFilterPlugin::updateMsgDecoded(int /*index*/, QDltMsg &/*msg*/)
{
}

void DltNoiseFilterPlugin::updateFileFinish()
{
    if (form)
        form->updateStatus();
}

void DltNoiseFilterPlugin::selectedIdxMsg(int /*index*/, QDltMsg &/*msg*/)
{
}

void DltNoiseFilterPlugin::selectedIdxMsgDecoded(int /*index*/, QDltMsg &/*msg*/)
{
}

// ============================================================
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(DltNoiseFilterPlugin, DltNoiseFilterPlugin);
#endif
