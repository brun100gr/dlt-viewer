/**
 * @licence app begin@
 * Copyright (C) 2011-2012  BMW AG
 *
 * This file is part of COVESA Project Dlt Viewer.
 *
 * Contributions are licensed to the COVESA Alliance under one or more
 * Contribution License Agreements.
 *
 * \copyright
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0. If a  copy of the MPL was not distributed with
 * this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * \file MyDecodePlugin.cpp
 * For further information see http://www.covesa.global/.
 * @licence end@
 */

#include <QtGui>

#include "mydecodeplugin.h"

MyDecodePlugin::MyDecodePlugin()
{
    dltFile = NULL;
    counterMessages = 0;
    counterNonVerboseMessages = 0;
    counterVerboseMessages = 0;

    fprintf(stderr, "PLUGIN COSTRUTTORE\n");
}

MyDecodePlugin::~MyDecodePlugin()
{

}

QString MyDecodePlugin::name()
{
    return QString("My Decode Plugin");
}

QString MyDecodePlugin::pluginVersion(){
    return MY_DECODE_PLUGIN_VERSION;
}

QString MyDecodePlugin::pluginInterfaceVersion(){
    return PLUGIN_INTERFACE_VERSION;
}

QString MyDecodePlugin::description()
{
    return QString();
}

QString MyDecodePlugin::error()
{
    return QString();
}

bool MyDecodePlugin::loadConfig(QString /* filename */)
{
    return true;
}

bool MyDecodePlugin::saveConfig(QString /* filename */)
{
    return true;
}

QStringList MyDecodePlugin::infoConfig()
{
    return QStringList();
}

QWidget* MyDecodePlugin::initViewer()
{
    return nullptr;
}


void MyDecodePlugin::updateCounters(int , QDltMsg &msg)
{
    if(!dltFile)
        return;



            if(msg.getMode() == QDltMsg::DltModeVerbose)
            {
                counterVerboseMessages++;
            }
            if(msg.getMode() == QDltMsg::DltModeNonVerbose)
            {
                counterNonVerboseMessages++;
            }
}


void MyDecodePlugin::selectedIdxMsg(int index, QDltMsg &/*msg*/) {
    if(!dltFile)
        return;

    //qDebug() << "undecoded: " << msg.toStringPayload();
}

void MyDecodePlugin::selectedIdxMsgDecoded(int , QDltMsg &/*msg*/){

    //qDebug() << "decoded: " << msg.toStringPayload();

}

void MyDecodePlugin::initFileStart(QDltFile *file){
if(nullptr == file)
        return;

    dltFile = file;

    counterMessages = dltFile->size();

    counterNonVerboseMessages = 0;
    counterVerboseMessages = 0;
}

void MyDecodePlugin::initMsg(int index, QDltMsg &msg){

    updateCounters(index, msg);
}

void MyDecodePlugin::initMsgDecoded(int , QDltMsg &){
//empty. Implemented because derived plugin interface functions are virtual.
}

void MyDecodePlugin::initFileFinish(){
   if(nullptr == dltFile)
        return;
}

void MyDecodePlugin::updateFileStart(){

}

void MyDecodePlugin::updateMsg(int index, QDltMsg &msg) {
    if (!dltFile) {
        return;
    }

    // ✅ Modo corretto: usa toStringPayload() che decodifica gli argomenti DLT
    QString text = msg.toStringPayload();

    // --- cerca e valida il numero in modo robusto ---
    QRegularExpression re(R"(ActivationCondition:\s*(\d+))");
    QRegularExpressionMatch match = re.match(text);

    if (!match.hasMatch()) {
        fprintf(stderr, "Prefix NOT found. Text was: %s\n",
                text.toUtf8().constData());
        return;
    }

    fprintf(stderr, "++++++++++>02\n");

    bool ok = false;
    uint32_t mask = match.captured(1).toUInt(&ok);
    if (!ok) {
        QByteArray error = QByteArray("Activation Condition parse error");
        msg.setPayload(error);
        return;
    }

    // --- decode bitmask ---
    QStringList conditions;
    for (int i = 0; i < 32; i++) {
        uint32_t bit = (1u << i);
        if (mask & bit) {
            conditions << QString("condizione%1").arg(i + 1);
        }
    }

    // --- output finale ---
    QString decoded = "Activation Condition are: " + conditions.join(", ");
    QByteArray decodedPayload = decoded.toUtf8();
    msg.setPayload(decodedPayload);

    fprintf(stderr, "DLT payload: %s\n", decoded.toUtf8().constData());
    updateCounters(index, msg);
    counterMessages = dltFile->size();
}

void MyDecodePlugin::updateMsgDecoded(int , QDltMsg &){
//empty. Implemented because derived plugin interface functions are virtual.
}

void MyDecodePlugin::updateFileFinish(){
   if(nullptr == dltFile)
        return;
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(MyDecodePlugin, MyDecodePlugin);
#endif


