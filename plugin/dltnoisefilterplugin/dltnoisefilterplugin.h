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
 * \file dltnoisefilterplugin.h
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#ifndef DLTNOISEFILTERPLUGIN_H
#define DLTNOISEFILTERPLUGIN_H

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include "plugininterface.h"
#include "form.h"

#define DLT_NOISE_FILTER_PLUGIN_VERSION "1.0.0"

//! Default token prepended to the payload of suppressed (noisy) messages.
//! A negative DLT filter on this token can be used to hide the messages.
#define DLT_NOISE_FILTER_DEFAULT_MARKER "NOISE_SUPPRESSED"

//! Configuration of a single noisy signal.
struct NoiseSignalConfig
{
    QString apid;       //!< Application id (empty = match any)
    QString ctid;       //!< Context id (empty = match any)
    QString name;       //!< Signal name = payload text before the ':' separator
    double  threshold;  //!< Dead-band: the signal is shown again only when it
                        //!< changed by at least this amount from the last shown value
};

//! DLT Viewer plugin which suppresses "noisy" physical signals.
/*!
  Many signals (speed, acceleration, ...) are logged every time they change,
  even when the change is just sensor noise / normal jitter of a real system.
  This plugin applies a per-signal dead-band (hysteresis): a message is kept
  only when the signal value moved by at least the configured threshold from
  the last value that was actually shown. All the messages in between are
  marked with a configurable token so that a negative filter can hide them.

  The plugin is at the same time:
   - a Decoder plugin: it rewrites the payload of the noisy messages, adding
     the marker token (this runs *before* the filters, so a negative filter on
     the token removes them from the view);
   - a Viewer plugin: it receives every message sequentially during indexing
     (initMsg), which is required to compute the dead-band deterministically,
     and it provides a small configuration UI.
*/
class DltNoiseFilterPlugin : public QObject,
                             public QDLTPluginInterface,
                             public QDLTPluginDecoderInterface,
                             public QDltPluginViewerInterface
{
    Q_OBJECT
    Q_INTERFACES(QDLTPluginInterface)
    Q_INTERFACES(QDLTPluginDecoderInterface)
    Q_INTERFACES(QDltPluginViewerInterface)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    Q_PLUGIN_METADATA(IID "org.genivi.DLT.DltNoiseFilterPlugin")
#endif

public:
    DltNoiseFilterPlugin();
    ~DltNoiseFilterPlugin();

    /* QDLTPluginInterface */
    QString name();
    QString pluginVersion();
    QString pluginInterfaceVersion();
    QString description();
    QString error();
    bool loadConfig(QString filename);
    bool saveConfig(QString filename);
    QStringList infoConfig();

    /* QDLTPluginDecoderInterface */
    bool isMsg(QDltMsg &msg, int triggeredByUser);
    bool decodeMsg(QDltMsg &msg, int triggeredByUser);

    /* QDltPluginViewerInterface */
    QWidget* initViewer();
    void initFileStart(QDltFile *file);
    void initFileFinish();
    void initMsg(int index, QDltMsg &msg);
    void initMsgDecoded(int index, QDltMsg &msg);
    void updateFileStart();
    void updateMsg(int index, QDltMsg &msg);
    void updateMsgDecoded(int index, QDltMsg &msg);
    void updateFileFinish();
    void selectedIdxMsg(int index, QDltMsg &msg);
    void selectedIdxMsgDecoded(int index, QDltMsg &msg);

    /* API used by the configuration Form */

    //! Current list of configured noisy signals.
    QList<NoiseSignalConfig> signalConfigs() const;
    //! Replace the whole configuration (signals + marker token + default).
    void setConfiguration(const QList<NoiseSignalConfig> &configs,
                          const QString &markerToken,
                          double defaultThreshold);
    //! Marker token currently used for suppressed messages.
    QString markerToken() const { return marker; }
    //! Global default dead-band applied to every numeric signal that is not
    //! explicitly configured. A value <= 0 disables the default (such signals
    //! are never suppressed).
    double globalDefaultThreshold() const { return defaultThreshold; }
    //! Number of messages suppressed in the current file.
    quint64 suppressedCount() const { return suppressed; }
    //! Recompute the suppression decisions for the whole loaded file.
    //! Call after changing the configuration to refresh the markers without
    //! reloading the file (a filter re-apply / reload is still needed to hide).
    void reanalyze();

private:
    using ThresholdMap = QHash<QString, double>;

    //! Build the dispatch key "APID|CTID|SignalName".
    static QString makeKey(const QString &apid, const QString &ctid, const QString &name);
    //! Build a well-formed DLT verbose string payload.
    static QByteArray buildDltStringPayload(const QString &text, bool littleEndian);
    //! Extract signal key and numeric value from a message. Returns false if
    //! the message is not a configured noisy signal or has no numeric value.
    bool parseSignal(QDltMsg &msg, QString &keyOut, double &valueOut) const;
    //! Apply the dead-band logic for one message at the given absolute index.
    void evaluate(int index, QDltMsg &msg);
    //! Load the built-in default configuration (the example noisy signals).
    void loadDefaults();

    //! key "APID|CTID|Name" -> threshold (dead-band)
    ThresholdMap thresholds;
    //! Display names, kept to be able to rebuild the configuration list.
    QHash<QString, NoiseSignalConfig> configByKey;
    //! key -> last value that was actually shown (sequential state)
    ThresholdMap lastShown;
    //! Absolute file indices of the messages that must be suppressed.
    QSet<int> suppressIndices;

    //! Dead-band used for numeric signals not present in "thresholds".
    //! <= 0 means "no global default" (only explicitly configured signals
    //! are ever suppressed).
    double defaultThreshold;

    QString marker;
    quint64 suppressed;

    QDltFile *dltFile;
    QString errorText;

    DltNoiseFilterPluginNs::Form *form;
};

#endif // DLTNOISEFILTERPLUGIN_H
