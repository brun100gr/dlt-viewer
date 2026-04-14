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

void MyDecodePlugin::updateMsg(int index, QDltMsg &msg){
        if(!dltFile)
            return;

        updateCounters(index,msg);

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
