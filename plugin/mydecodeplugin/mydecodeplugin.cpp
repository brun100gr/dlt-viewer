#include <QtGui>
#include "mydecodeplugin.h"

MyDecodePlugin::MyDecodePlugin() {
    dltFile = nullptr;
    counterMessages = 0;
    counterNonVerboseMessages = 0;
    counterVerboseMessages = 0;
}

MyDecodePlugin::~MyDecodePlugin() {
    // anche vuoto va bene, ma deve essere definito nel .cpp
}

bool MyDecodePlugin::isActiveDltDecoder() {
    return true;
}

bool MyDecodePlugin::isMsg(QDltMsg &msg, int /*triggeredByUser*/) {
    QString text = msg.toStringPayload();
    QRegularExpression re(R"(ActivationCondition:\s*(\d+))");
    return re.match(text).hasMatch();
}

bool MyDecodePlugin::isAcceptMsg(int index, QDltMsg &msg) {
    // Chiamato per ogni messaggio: accetta solo quelli con il nostro prefisso
    QString text = msg.toStringPayload();
    QRegularExpression re(R"(ActivationCondition:\s*(\d+))");
    return re.match(text).hasMatch();
}

bool MyDecodePlugin::decodeMsg(QDltMsg &msg, int /*triggeredByUser*/) {
    QString text = msg.toStringPayload();
    QRegularExpression re(R"(ActivationCondition:\s*(\d+))");
    QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return false;  // ← ora ritorna bool

    bool ok = false;
    uint32_t mask = match.captured(1).toUInt(&ok);
    if (!ok) {
        QByteArray error("Activation Condition parse error");
        msg.setPayload(error);
        return false;
    }

    QStringList conditions;
    for (int i = 0; i < 32; i++) {
        if (mask & (1u << i))
            conditions << QString("condizione%1").arg(i + 1);
    }

    QString decoded = "Activation Condition are: " + conditions.join(", ");
    QByteArray decodedPayload = decoded.toUtf8();
    msg.setPayload(decodedPayload);

    fprintf(stderr, "DLT decoded: %s\n", decoded.toUtf8().constData());
    return true;  // ← decodifica riuscita
}

QString MyDecodePlugin::name() {
    return QString("My Decode Plugin");
}

QString MyDecodePlugin::pluginVersion() {
    return MY_DECODE_PLUGIN_VERSION;
}

QString MyDecodePlugin::pluginInterfaceVersion() {
    return PLUGIN_INTERFACE_VERSION;
}

QString MyDecodePlugin::description() {
    return QString();
}

QString MyDecodePlugin::error() {
    return QString();
}

bool MyDecodePlugin::loadConfig(QString /*filename*/) {
    return true;
}

bool MyDecodePlugin::saveConfig(QString /*filename*/) {
    return true;
}

QStringList MyDecodePlugin::infoConfig() {
    return QStringList();
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(MyDecodePlugin, MyDecodePlugin);
#endif


