#ifndef myDecodePlugin_H
#define myDecodePlugin_H

#include <QObject>
#include "plugininterface.h"

#define MY_DECODE_PLUGIN_VERSION "1.0.0"

class MyDecodePlugin : public QObject,
                       public QDLTPluginInterface,
                       public QDLTPluginDecoderInterface  // ← QDLT tutto maiuscolo
{
    Q_OBJECT
    Q_INTERFACES(QDLTPluginInterface)
    Q_INTERFACES(QDLTPluginDecoderInterface)  // ← QDLT tutto maiuscolo

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    Q_PLUGIN_METADATA(IID "org.genivi.DLT.MyDecodePlugin")
#endif

public:
    MyDecodePlugin();
    ~MyDecodePlugin();

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
    bool isActiveDltDecoder();
    bool isMsg(QDltMsg &msg, int triggeredByUser);      // ← mancava
    bool isAcceptMsg(int index, QDltMsg &msg);
    bool decodeMsg(QDltMsg &msg, int triggeredByUser);  // ← bool, non void

    /* internal */
    void updateCounters(int index, QDltMsg &msg);
    int counterMessages;
    int counterNonVerboseMessages;
    int counterVerboseMessages;

private:
    QDltFile *dltFile;
    QString errorText;
};

#endif // myDecodePlugin_H