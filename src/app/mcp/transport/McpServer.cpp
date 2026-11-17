#include "app/mcp/transport/McpServer.h"
#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/mcp/tools/McpCatalog.h"
#include "app/mcp/tools/McpDispatcher.h"
#include "app/mcp/transport/McpHttp.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/mcp/protocol/McpProtocol.h"

#include "app/mcp/protocol/McpSession.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QThread>
#include <QTimer>

namespace drift::mcp {

McpServer::McpServer(AppController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_dispatcher(std::make_unique<McpDispatcher>(controller))
{
    // The registry names each instance's project so an agent can pick the right editor;
    // that is only useful while it tracks what is actually open.
    if (ProjectFileController *project = controller ? controller->projectFile() : nullptr) {
        const auto republish = [this]() {
            if (m_wroteSessionFile)
                publishSession();
        };
        connect(project, &ProjectFileController::currentProjectPathChanged, this, republish);
        connect(project, &ProjectFileController::projectNameChanged, this, republish);
        connect(project, &ProjectFileController::projectLoadPendingChanged, this, republish);
        connect(project, &ProjectFileController::projectLoadFinished, this,
                [this, republish](bool ok, const QString &message) {
                    m_hasLoaded = true;
                    m_loadOk = ok;
                    m_loadMessage = message;
                    republish();
                });
    }
}

McpServer::~McpServer()
{
    stop();
}

QString McpServer::url() const
{
    if (!m_running || m_port == 0)
        return {};
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_port);
}

QString McpServer::cursorSnippet() const
{
    if (!m_running)
        return {};
    return QStringLiteral(
               "{\n"
               "  \"mcpServers\": {\n"
               "    \"drift\": {\n"
               "      \"url\": \"%1\",\n"
               "      \"headers\": {\n"
               "        \"Authorization\": \"Bearer %2\"\n"
               "      }\n"
               "    }\n"
               "  }\n"
               "}\n")
        .arg(url(), m_token);
}

QString McpServer::claudeCommand() const
{
    if (!m_running)
        return {};
    return QStringLiteral(
               "claude mcp add --transport http drift %1 --header \"Authorization: Bearer %2\"")
        .arg(url(), m_token);
}

QString McpServer::codexSnippet() const
{
    if (!m_running)
        return {};
    return QStringLiteral(
               "# Add this to ~/.codex/config.toml, then restart Codex.\n"
               "[mcp_servers.dafin]\n"
               "url = \"%1\"\n"
               "http_headers = { Authorization = \"Bearer %2\" }\n")
        .arg(url(), m_token);
}

QString McpServer::webPath() const
{
    if (!m_running || !m_webAccess || m_webSecret.isEmpty())
        return {};
    return QStringLiteral("/w/%1/mcp").arg(m_webSecret);
}

QString McpServer::persistedWebSecret() const
{
    QSettings settings;
    const QString key = QStringLiteral("mcp/webSecret");
    QString secret = settings.value(key).toString();
    if (secret.isEmpty()) {
        secret = makeToken();
        settings.setValue(key, secret);
    }
    return secret;
}

// m_http reads the secret on its own thread; an empty secret closes /w/ again.
void McpServer::applyWebSecret()
{
    if (!m_http)
        return;
    const QString secret = m_webAccess ? m_webSecret : QString();
    QMetaObject::invokeMethod(
        m_http, [this, secret]() { m_http->setWebSecret(secret); }, Qt::BlockingQueuedConnection);
}

void McpServer::setWebAccess(bool enabled)
{
    if (m_webAccess == enabled)
        return;
    m_webAccess = enabled;
    if (enabled && m_webSecret.isEmpty())
        m_webSecret = persistedWebSecret();
    applyWebSecret();
    emit webAccessChanged();
}

QString McpServer::makeToken() const
{
    QString token;
    token.reserve(64);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < 32; ++i)
        token += QString::number(rng->bounded(256), 16).rightJustified(2, QLatin1Char('0'));
    return token;
}

// A token that changed on every launch meant re-pasting the Cursor/Claude setup each
// session, so the first generated one is kept until rotateToken().
QString McpServer::persistedToken() const
{
    QSettings settings;
    const QString key = QStringLiteral("mcp/token");
    QString token = settings.value(key).toString();
    if (token.isEmpty()) {
        token = makeToken();
        settings.setValue(key, token);
    }
    return token;
}

void McpServer::rotateToken()
{
    const QString token = makeToken();
    const QString webSecret = makeToken();
    QSettings().setValue(QStringLiteral("mcp/token"), token);
    QSettings().setValue(QStringLiteral("mcp/webSecret"), webSecret);
    if (!m_running)
        return;
    m_token = token;
    m_webSecret = webSecret;
    applyWebSecret();
    // m_http reads its token on its own thread.
    QMetaObject::invokeMethod(
        m_http, [this, token]() { m_http->setToken(token); }, Qt::BlockingQueuedConnection);
    if (m_wroteSessionFile)
        publishSession();
    emit tokenChanged();
}

bool McpServer::start()
{
    if (m_running)
        return true;

    m_error.clear();
    m_token = m_fixedToken.isEmpty() ? persistedToken() : m_fixedToken;

    m_thread = new QThread(this);
    m_http = new McpHttp;
    m_http->setToken(m_token);
    m_webSecret.clear();
    if (m_webAccess) {
        m_webSecret = persistedWebSecret();
        m_http->setWebSecret(m_webSecret);
    }
    m_http->setRpcHandler([this](const QString &toolbox, const QJsonValue &body) {
        QJsonValue result;
        QMetaObject::invokeMethod(
            this,
            [this, toolbox, body, &result]() { result = handleRpc(toolbox, body); },
            Qt::BlockingQueuedConnection);
        return result;
    });
    m_http->moveToThread(m_thread);

    QObject::connect(
        m_http, &McpHttp::failed, this,
        [this](const QString &error) {
            m_error = error;
            emit errorChanged();
            stop();
        },
        Qt::QueuedConnection);

    m_thread->start();

    bool ok = false;
    quint16 port = 0;
    QEventLoop loop;
    QMetaObject::Connection listeningConn = QObject::connect(
        m_http, &McpHttp::listening, this, [&](quint16 p) {
            ok = true;
            port = p;
            loop.quit();
        });
    QMetaObject::Connection failedConn = QObject::connect(
        m_http, &McpHttp::failed, this, [&](const QString &error) {
            m_error = error;
            loop.quit();
        });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    QMetaObject::invokeMethod(
        m_http, [this]() { m_http->listen(m_requestedPort); }, Qt::QueuedConnection);
    loop.exec();
    QObject::disconnect(listeningConn);
    QObject::disconnect(failedConn);

    if (!ok) {
        if (m_error.isEmpty())
            m_error = QStringLiteral("Could not bind 127.0.0.1");
        emit errorChanged();
        stop();
        return false;
    }
    m_port = port;
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_wroteSessionFile = publishSession();
    m_running = true;
    emit runningChanged();
    return true;
}

void McpServer::stop()
{
    if (m_wroteSessionFile) {
        removeSessionFile();
        m_wroteSessionFile = false;
    }
    if (m_http) {
        McpHttp *http = m_http;
        m_http = nullptr;
        QMetaObject::invokeMethod(
            http,
            [http]() {
                http->close();
                delete http;
            },
            Qt::BlockingQueuedConnection);
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
        delete m_thread;
        m_thread = nullptr;
    }
    const bool wasRunning = m_running;
    m_running = false;
    m_port = 0;
    m_token.clear();
    m_webSecret.clear();
    if (m_webAccess) {
        m_webAccess = false;
        emit webAccessChanged();
    }
    if (wasRunning)
        emit runningChanged();
}

bool McpServer::publishSession()
{
    SessionInfo info;
    info.port = m_port;
    info.token = m_token;
    info.startedAt = m_startedAt;
    info.headless = m_headless;
    if (ProjectFileController *project = m_controller ? m_controller->projectFile() : nullptr) {
        info.project = project->currentProjectPath();
        info.projectName = project->projectName();
        info.loading = project->projectLoadPending() || project->startupProjectQueued();
        info.isolated = project->isolatedSession();
    }
    info.hasLoaded = m_hasLoaded;
    info.loadOk = m_loadOk;
    info.loadMessage = m_loadMessage;
    return writeSessionFile(info);
}

QJsonValue McpServer::handleRpc(const QString &toolbox, const QJsonValue &body)
{
    // The assistant introduces itself once, in initialize; keep the name for the usage monitor.
    const auto noteClient = [this](const QJsonObject &message) {
        if (message.value(QStringLiteral("method")).toString() != QLatin1String("initialize"))
            return;
        const QJsonObject info = message.value(QStringLiteral("params")).toObject()
                                     .value(QStringLiteral("clientInfo")).toObject();
        emit clientSeen(info.value(QStringLiteral("name")).toString(),
                        info.value(QStringLiteral("version")).toString());
    };
    if (body.isObject()) {
        noteClient(body.toObject());
    } else if (body.isArray()) {
        for (const QJsonValue &message : body.toArray())
            noteClient(message.toObject());
    }
    return handleJsonRpc(body, toolbox, [this](const QString &name, const QJsonObject &args) {
        // Both transports reach here on this object's thread, so the log never needs a lock.
        const int id = ++m_callSeq;
        QString summary = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        if (summary.size() > 120)
            summary = summary.left(119) + QChar(0x2026);
        emit toolCallStarted(id, name, summary);
        QElapsedTimer timer;
        timer.start();
        const QJsonObject result = dispatchTool(name, args);
        const bool failed = result.value(QStringLiteral("isError")).toBool();
        QString error;
        if (failed) {
            const QJsonArray content = result.value(QStringLiteral("content")).toArray();
            if (!content.isEmpty())
                error = content.first().toObject().value(QStringLiteral("text")).toString().left(600);
        }
        emit toolCallFinished(id, !failed, error, timer.elapsed());
        emit toolCallResult(id, name, args, result);
        return result;
    });
}

QJsonObject McpServer::dispatchTool(const QString &name, const QJsonObject &args)
{
    return runTool(*m_dispatcher, name, args);
}

// Dafin: shared with the in-app AI Assist panel, which calls tools in-process.
QJsonObject runTool(McpDispatcher &dispatcher, const QString &name, const QJsonObject &args)
{
    McpDispatcher *m_dispatcher = &dispatcher;
    if (name == QLatin1String("catalog"))
        return textResult(catalogPayload(args));
    if (name == QLatin1String("toolbox")) {
        QStringList only;
        for (const QJsonValue &v : args.value(QStringLiteral("ops")).toArray())
            only.append(v.toString());
        return textResult(toolboxPayload(args.value(QStringLiteral("name")).toString(), only));
    }
    if (name == QLatin1String("search")) {
        return textResult(searchOps(args.value(QStringLiteral("q")).toString(),
                                    args.value(QStringLiteral("limit")).toInt(8),
                                    args.value(QStringLiteral("schema")).toBool()));
    }
    if (name == QLatin1String("inspect"))
        return textResult(m_dispatcher->inspect(args));
    if (name == QLatin1String("apply"))
        return textResult(m_dispatcher->apply(args));
    if (name == QLatin1String("capture"))
        return m_dispatcher->capture(args);
    if (name == QLatin1String("frames"))
        return m_dispatcher->frames(args);
    if (name == QLatin1String("activity"))
        return textResult(m_dispatcher->activity(args));
    const QJsonObject result = m_dispatcher->applyOne(name, args);
    return isRawResult(result) ? result : textResult(result);
}

} // namespace drift::mcp

#include "moc_McpServer.cpp"
