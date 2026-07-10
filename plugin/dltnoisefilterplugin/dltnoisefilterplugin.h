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

#define DLT_NOISE_FILTER_PLUGIN_VERSION "1.0.0"

// Token prepended to the payload of suppressed messages.
// Use a negative DLT filter on this token to hide them from the view.
#define DLT_NOISE_FILTER_MARKER "NOISE_SUPPRESSED"

// -----------------------------------------------------------------------
//  Configuration is loaded from a JSON file at startup via loadConfig().
//  If no file is provided, compile-time defaults defined in the .cpp are used.
//
//  JSON format:
//  {
//    "marker":           "NOISE_SUPPRESSED",
//    "defaultThreshold": 0.05,
//    "signals": [
//      { "apid": "ADF", "ctid": "ADF_",
//        "name": "MySignal_Value", "threshold": 1.0 }
//    ]
//  }
// -----------------------------------------------------------------------

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

private:
    static QString makeKey(const QString &apid, const QString &ctid, const QString &name);
    static QByteArray buildDltStringPayload(const QString &text, bool littleEndian);
    bool parseSignal(QDltMsg &msg, QString &keyOut, double &valueOut) const;
    void evaluate(int index, QDltMsg &msg);

    QHash<QString, double> thresholds;      // key -> threshold
    QHash<QString, double> lastShown;       // key -> last value actually shown
    QSet<int>              suppressIndices;

    double  defaultThreshold;
    QString marker;

    QDltFile *dltFile;
    QString   errorText;
};

#endif // DLTNOISEFILTERPLUGIN_H
