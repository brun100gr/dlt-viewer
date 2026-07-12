#ifndef DLTPLOTPLUGIN_H
#define DLTPLOTPLUGIN_H

/**
 * DLT Plot Plugin
 *
 * Parses DLT messages using the same regex patterns as dlt_plot.py and serves
 * an interactive Plotly dashboard via a minimal embedded HTTP server on
 * http://localhost:808x.  Open the URL in any browser to see signals and
 * counters update every 2 s.
 *
 * Signal patterns supported:
 *   - ADAS v130  : [ts][COMP ADF_][level] COMP_IN/OUT_SIGNAL : value
 *   - ADAS v120  : [ts][COMP FCT_][level] COMP dir SIGNAL: value
 *   - HMI  v130  : [ts][COMP CTX][level]  'SIGNAL (received|send) Value'value
 *
 * Counter patterns supported (same three variants).
 */

#include <QObject>
#include <QWidget>
#include <QMutex>
#include <QMap>
#include <QVector>
#include <QPair>
#include <QHash>
#include <QByteArray>
#include <QRegularExpression>

#include "plugininterface.h"

#define DLT_PLOT_PLUGIN_VERSION "1.0.0"

class QLabel;
class QPushButton;
class QTcpServer;
class QTcpSocket;

// ── data types ───────────────────────────────────────────────────────────────

using PointList = QVector<QPair<double, double>>;

struct SignalData {
    QMap<QString, PointList> inp;
    QMap<QString, PointList> out;
};

// ── plugin class ─────────────────────────────────────────────────────────────

class DltPlotPlugin : public QObject,
                      public QDLTPluginInterface,
                      public QDltPluginViewerInterface
{
    Q_OBJECT
    Q_INTERFACES(QDLTPluginInterface)
    Q_INTERFACES(QDltPluginViewerInterface)
    Q_PLUGIN_METADATA(IID "org.genivi.DLT.DltPlotPlugin")

public:
    DltPlotPlugin();
    ~DltPlotPlugin() override;

    /* QDLTPluginInterface */
    QString name() override;
    QString pluginVersion() override;
    QString pluginInterfaceVersion() override;
    QString description() override;
    QString error() override;
    bool loadConfig(QString filename) override;
    bool saveConfig(QString filename) override;
    QStringList infoConfig() override;

    /* QDltPluginViewerInterface */
    QWidget* initViewer() override;
    void initFileStart(QDltFile *file) override;
    void initFileFinish() override;
    void initMsg(int index, QDltMsg &msg) override;
    void initMsgDecoded(int index, QDltMsg &msg) override;
    void updateFileStart() override;
    void updateMsg(int index, QDltMsg &msg) override;
    void updateMsgDecoded(int index, QDltMsg &msg) override;
    void updateFileFinish() override;
    void selectedIdxMsg(int index, QDltMsg &msg) override;
    void selectedIdxMsgDecoded(int index, QDltMsg &msg) override;

private slots:
    void onNewConnection();
    void onOpenBrowser();
    void onClearData();
    void updateStatusLabel();

private:
    // message → line → parse
    void processMsg(QDltMsg &msg);
    void parseLine(const QString &line);

    // HTTP server helpers
    bool startServer();
    void handleHttpRequest(QTcpSocket *socket, const QByteArray &request);
    void sendHttpResponse(QTcpSocket *socket, int code,
                          const QByteArray &contentType, const QByteArray &body);

    // data serialisation
    QByteArray buildDataJson();

    // ── UI ──────────────────────────────────────────────────────────────
    QWidget      *widget{nullptr};
    QLabel       *labelUrl{nullptr};
    QLabel       *labelStatus{nullptr};
    QPushButton  *btnBrowser{nullptr};
    QPushButton  *btnClear{nullptr};

    // ── HTTP server ─────────────────────────────────────────────────────
    QTcpServer                     *tcpServer{nullptr};
    int                             serverPort{0};
    QHash<QTcpSocket*, QByteArray>  socketBuffers;

    // ── shared data (protected by dataMutex) ───────────────────────────
    mutable QMutex                 dataMutex;
    QMap<QString, SignalData>      dataSignals;
    QMap<QString, PointList>       dataCounters;
    qint64                         receivedCount{0};
    qint64                         matchedCount{0};

    // ── regex patterns ──────────────────────────────────────────────────
    struct Pattern {
        QRegularExpression re;
        QString            name;
    };
    QVector<Pattern> signalPatterns;
    QVector<Pattern> counterPatterns;

    QString errorText;
};

#endif // DLTPLOTPLUGIN_H
