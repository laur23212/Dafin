#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <memory>

class AppController;
class QThread;

namespace drift::mcp {
class McpHttp;
class McpDispatcher;

// Runs one MCP tool (homepage tool or toolbox op) and returns the MCP tool result
// ({content:[…], isError}). What both the HTTP/stdio server and the AI Assist panel use.
QJsonObject runTool(McpDispatcher &dispatcher, const QString &name, const QJsonObject &args);

class McpServer : public QObject
{
    Q_OBJECT

public:
    explicit McpServer(AppController *controller, QObject *parent = nullptr);
    ~McpServer() override;

    bool running() const { return m_running; }
    quint16 port() const { return m_port; }
    QString token() const { return m_token; }
    QString url() const;
    QString error() const { return m_error; }

    QString cursorSnippet() const;
    QString claudeCommand() const;
    // A block for ~/.codex/config.toml: Codex reads MCP servers from there (HTTP with a header).
    QString codexSnippet() const;

    // Web access, off at every launch. While on (and the server is running) POST
    // /w/<secret>/mcp reaches the homepage endpoint without the bearer token, so a website
    // assistant (ChatGPT) can use it through a tunnel the user starts. The secret persists
    // like the token and is replaced with it.
    bool webAccess() const { return m_webAccess; }
    void setWebAccess(bool enabled);
    // "/w/<secret>/mcp" while web access is on and the server runs, else empty.
    QString webPath() const;

    // Headless overrides, applied by the next start(). The editor sets neither and keeps
    // the defaults: the token persisted in QSettings (generated on first start) on port 4731.
    void setToken(const QString &token) { m_fixedToken = token; }
    void setPort(quint16 port) { m_requestedPort = port; }
    // Recorded in the session registry so a bridge can tell a headless worker from an editor.
    void setHeadless(bool headless) { m_headless = headless; }

    // Replaces the persisted token, live if the server is running. Every assistant set up
    // with the old one has to be re-pointed; that is the point.
    void rotateToken();

public slots:
    bool start();
    void stop();
    QJsonValue handleRpc(const QString &toolbox, const QJsonValue &body);

signals:
    void runningChanged();
    void tokenChanged();
    void errorChanged();
    void toolCallStarted(int id, const QString &tool, const QString &summary);
    void toolCallFinished(int id, bool ok, const QString &error, qint64 ms);
    void toolCallResult(int id, const QString &tool, const QJsonObject &args, const QJsonObject &result);
    // The name an assistant gave in its initialize request (empty when it gave none).
    void clientSeen(const QString &name, const QString &version);
    void webAccessChanged();

private:
    QString makeToken() const;
    QString persistedToken() const;
    QString persistedWebSecret() const;
    void applyWebSecret();
    bool publishSession();
    int m_callSeq = 0;
    QJsonObject dispatchTool(const QString &name, const QJsonObject &args);

    AppController *m_controller = nullptr;
    std::unique_ptr<McpDispatcher> m_dispatcher;
    QThread *m_thread = nullptr;
    McpHttp *m_http = nullptr;
    QString m_token;
    QString m_fixedToken;
    QString m_webSecret;
    bool m_webAccess = false;
    QString m_error;
    quint16 m_port = 0;
    quint16 m_requestedPort = 4731;
    qint64 m_startedAt = 0;
    bool m_headless = false;
    // The last project load to finish, for the session record (launch_instance waits on it).
    bool m_hasLoaded = false;
    bool m_loadOk = false;
    QString m_loadMessage;
    // stop() must not delete a session file this server never wrote: a headless instance
    // serving only stdio would otherwise unregister a GUI editor running alongside it.
    bool m_wroteSessionFile = false;
    bool m_running = false;
};

} // namespace drift::mcp
