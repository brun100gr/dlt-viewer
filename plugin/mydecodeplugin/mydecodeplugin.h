
#ifndef myDecodePlugin_H
#define myDecodePlugin_H

#include <QObject>
#include "plugininterface.h"

#define MY_DECODE_PLUGIN_VERSION "1.0.0"

class MyDecodePlugin : public QObject, QDLTPluginInterface, QDltPluginViewerInterface
{
    Q_OBJECT
    Q_INTERFACES(QDLTPluginInterface)
    Q_INTERFACES(QDltPluginViewerInterface)
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    Q_PLUGIN_METADATA(IID "org.genivi.DLT.MyDecodePlugin")
#endif

public:
    MyDecodePlugin();
    ~MyDecodePlugin();

    /* QDLTPluginInterface interface */
    QString name();
    QString pluginVersion();
    QString pluginInterfaceVersion();
    QString description();
    QString error();
    bool loadConfig(QString filename);
    bool saveConfig(QString filename);
    QStringList infoConfig();

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


    /* internal variables */
    int counterMessages;
    int counterNonVerboseMessages;
    int counterVerboseMessages;

    void show(bool value);
    void updateCounters(int index, QDltMsg &msg);

private:
    QDltFile *dltFile;
    QString errorText;
};

#endif // myDecodePlugin_H
