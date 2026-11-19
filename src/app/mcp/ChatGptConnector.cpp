#include "app/mcp/ChatGptConnector.h"

#include "app/mcp/McpController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#if QT_CONFIG(process)
#include <QProcess>
#endif

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace dafin::chatgpt {

namespace {
constexpr auto kReleaseApi = "https://api.github.com/repos/cloudflare/cloudflared/releases/latest";
constexpr int kStartTimeoutMs = 45000;
constexpr int kSettleMs = 8000; // after the address appears, if "Registered" never shows
constexpr int kMaxLogLines = 200;

QString tr(const char *text)
{
    return QCoreApplication::translate("ChatGptConnector", text);
}
} // namespace

QString parseTunnelAddress(const QString &line)
{
    static const QRegularExpression re(QStringLiteral("https://[a-z0-9-]+\\.trycloudflare\\.com"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(line);
    // "api.trycloudflare.com" is the service cloudflared talks to, not a tunnel address.
    if (!m.hasMatch() || m.captured(0).compare(QLatin1String("https://api.trycloudflare.com"),
                                               Qt::CaseInsensitive) == 0)
        return {};
    return m.captured(0).toLower();
}

QString chatGptPrompt(const QString &chatGptAddress)
{
    return tr("I want to connect my video editor, Dafin, to ChatGPT so you can edit my video "
              "project. Dafin is a remote MCP server (Streamable HTTP). Depending on the ChatGPT "
              "version, this may be called a custom MCP server, an app, a connector or a plugin.\n"
              "\n"
              "Server address:\n"
              "%1\n"
              "\n"
              "Name: Dafin\n"
              "Authentication: none (choose \"No authentication\"; the secret is already inside "
              "the address, so I must keep it private).\n"
              "\n"
              "Please:\n"
              "1. For the ChatGPT I am using right now, tell me exactly where to click to add "
              "this server and paste the address. Give me one step at a time and wait until I "
              "say \"done\" before the next one. If my plan cannot add custom MCP servers, or "
              "can only add read-only ones, tell me plainly.\n"
              "2. When it is added, use one of Dafin's tools that reads the project, and tell me "
              "in one sentence what is in my project, so we know the connection works.\n"
              "\n"
              "I am not technical, so keep every step short and simple. You can only guide me; "
              "the clicks in ChatGPT are mine.")
        .arg(chatGptAddress);
}

QString currentPlatform()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_WIN)
    if (arch == QLatin1String("i386"))
        return QStringLiteral("windows-386");
    // x86_64, and arm64 Windows runs the x64 build through its emulation.
    return QStringLiteral("windows-amd64");
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (arch == QLatin1String("x86_64"))
        return QStringLiteral("linux-amd64");
    if (arch == QLatin1String("arm64"))
        return QStringLiteral("linux-arm64");
    return {};
#else
    Q_UNUSED(arch)
    return {};
#endif
}

QJsonObject pickReleaseAsset(const QJsonObject &release, const QString &platform)
{
    if (platform.isEmpty())
        return {};
    QString wanted = QStringLiteral("cloudflared-") + platform;
    if (platform.startsWith(QLatin1String("windows-")))
        wanted += QStringLiteral(".exe");
    const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString() == wanted
            && asset.value(QStringLiteral("browser_download_url")).toString()
                   .startsWith(QLatin1String("https://")))
            return asset;
    }
    return {};
}

QByteArray checksumFromRelease(const QJsonObject &release, const QJsonObject &asset)
{
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-fA-F]{64}$"));
    const QString digest = asset.value(QStringLiteral("digest")).toString();
    if (digest.startsWith(QLatin1String("sha256:"))) {
        const QString value = digest.mid(7);
        if (hex64.match(value).hasMatch())
            return value.toLower().toLatin1();
    }
    const QString name = asset.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        return {};
    // The release notes list "cloudflared-linux-amd64: <hex>" lines.
    const QRegularExpression line(QStringLiteral("^\\s*[-*]?\\s*%1\\s*[:=]?\\s*([0-9a-fA-F]{64})\\s*$")
                                      .arg(QRegularExpression::escape(name)),
                                  QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = line.match(release.value(QStringLiteral("body")).toString());
    return m.hasMatch() ? m.captured(1).toLower().toLatin1() : QByteArray();
}

QString executableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("cloudflared.exe");
#else
    return QStringLiteral("cloudflared");
#endif
}

} // namespace dafin::chatgpt

using namespace dafin::chatgpt;

ChatGptConnector::ChatGptConnector(McpController *mcp, QObject *parent)
    : QObject(parent)
    , m_mcp(mcp)
    , m_nam(new QNetworkAccessManager(this))
    , m_startTimeout(new QTimer(this))
{
    m_startTimeout->setSingleShot(true);
    connect(m_startTimeout, &QTimer::timeout, this, [this] {
        if (m_state == QLatin1String("starting"))
            fail(tr("The helper did not give an address in time. Check that this computer is "
                    "online, then press Connect ChatGPT again."));
    });
    // Access turned off elsewhere (the switch, quitting): close the tunnel too. A new key
    // changes the secret part of the address, so the ChatGPT address is rebuilt.
    connect(m_mcp, &McpController::webAccessChanged, this, [this] {
        if (m_stopping || m_state == QLatin1String("idle"))
            return;
        if (!m_mcp->webAccess()) {
            if (m_state != QLatin1String("failed"))
                stop();
            return;
        }
        if (!m_publicAddress.isEmpty()) {
            const QString rebuilt = m_mcp->chatGptUrl(m_publicAddress);
            if (rebuilt != m_chatGptAddress) {
                m_chatGptAddress = rebuilt;
                m_testState.clear();
                emit changed();
            }
        }
    });
}

ChatGptConnector::~ChatGptConnector()
{
    m_stopping = true;
    stopProcess();
}

QString ChatGptConnector::toolsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/tools");
}

void ChatGptConnector::setState(const QString &state, const QString &message)
{
    m_state = state;
    m_message = message;
    emit changed();
}

void ChatGptConnector::fail(const QString &message)
{
    m_startTimeout->stop();
    stopProcess();
    if (m_reply)
        m_reply->abort();
    m_publicAddress.clear();
    m_chatGptAddress.clear();
    setState(QStringLiteral("failed"), message);
}

QString ChatGptConnector::findHelper() const
{
    const QString own = toolsDir() + QLatin1Char('/') + executableName();
    if (QFileInfo(own).isExecutable())
        return own;
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("cloudflared"));
    if (!onPath.isEmpty())
        return onPath;
#ifdef Q_OS_WIN
    // Where Cloudflare's installer and winget put it.
    for (const QString &dir : {qEnvironmentVariable("ProgramFiles(x86)"), qEnvironmentVariable("ProgramFiles")}) {
        const QString candidate = dir + QStringLiteral("/cloudflared/cloudflared.exe");
        if (!dir.isEmpty() && QFileInfo::exists(candidate))
            return candidate;
    }
#elif defined(Q_OS_MACOS)
    for (const char *candidate : {"/opt/homebrew/bin/cloudflared", "/usr/local/bin/cloudflared"}) {
        if (QFileInfo(QString::fromLatin1(candidate)).isExecutable())
            return QString::fromLatin1(candidate);
    }
#endif
    return {};
}

void ChatGptConnector::start()
{
    if (m_state == QLatin1String("needsDownload") || m_state == QLatin1String("downloading")
        || m_state == QLatin1String("starting") || m_state == QLatin1String("connected"))
        return;
    m_stopping = false;
    m_testState.clear();
    m_testMessage.clear();
#if !QT_CONFIG(process)
    fail(tr("ChatGPT cannot be connected from this version of Dafin."));
    return;
#else
    if (!m_mcp->running())
        m_mcp->setEnabled(true);
    if (!m_mcp->running()) {
        fail(tr("Could not turn agent access on. %1").arg(m_mcp->error()));
        return;
    }
    if (!m_mcp->webAccess())
        m_mcp->setWebAccess(true);
    if (!m_mcp->webAccess()) {
        fail(tr("Could not open the web address for ChatGPT."));
        return;
    }
    const QString helper = findHelper();
    if (!helper.isEmpty()) {
        m_helperChecksum.clear();
        m_helperOrigin = helper.startsWith(toolsDir()) ? tr("Downloaded earlier by Dafin")
                                                       : tr("Already installed on this computer");
        launch(helper);
        return;
    }
    if (!canDownload()) {
        fail(tr("Dafin cannot download the helper on this computer. Install cloudflared "
                "(on a Mac: brew install cloudflared), then press Connect ChatGPT again, or use "
                "the manual setup under Details & security."));
        return;
    }
    setState(QStringLiteral("needsDownload"),
             tr("To reach ChatGPT, Dafin needs a small free helper from Cloudflare (cloudflared, "
                "about 40 MB). It is downloaded once and checked before it runs."));
#endif
}

void ChatGptConnector::confirmDownload()
{
    if (m_state != QLatin1String("needsDownload"))
        return;
    m_progress = 0;
    setState(QStringLiteral("downloading"), tr("Looking up the latest helper…"));
    QNetworkRequest request{QUrl(QString::fromLatin1(kReleaseApi))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Dafin"));
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_nam->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_stopping || m_state != QLatin1String("downloading"))
            return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Could not reach GitHub to download the helper (%1).").arg(reply->errorString()));
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject asset = pickReleaseAsset(release, currentPlatform());
        const QByteArray sum = checksumFromRelease(release, asset);
        if (asset.isEmpty() || sum.isEmpty()) {
            fail(tr("The helper's download page did not list a checked file for this computer, "
                    "so nothing was downloaded. Try again later or use the manual setup."));
            return;
        }
        downloadAsset(asset, sum);
    });
}

void ChatGptConnector::downloadAsset(const QJsonObject &asset, const QByteArray &sha256)
{
    QDir().mkpath(toolsDir());
    const QString finalPath = toolsDir() + QLatin1Char('/') + executableName();
    const QString partPath = finalPath + QStringLiteral(".part");
    delete m_partFile;
    m_partFile = new QFile(partPath, this);
    if (!m_partFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("Could not save the helper in %1.").arg(QDir::toNativeSeparators(toolsDir())));
        return;
    }
    m_hash.reset();
    const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
    setState(QStringLiteral("downloading"), tr("Downloading the helper…"));

    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Dafin"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray chunk = reply->readAll();
        m_hash.addData(chunk);
        if (m_partFile)
            m_partFile->write(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) {
            m_progress = double(got) / double(total);
            emit changed();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, sha256, finalPath, partPath] {
        reply->deleteLater();
        const QByteArray rest = reply->readAll();
        m_hash.addData(rest);
        if (m_partFile) {
            m_partFile->write(rest);
            m_partFile->close();
            delete m_partFile;
            m_partFile = nullptr;
        }
        if (m_stopping || m_state != QLatin1String("downloading")) {
            QFile::remove(partPath);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(partPath);
            fail(tr("The helper download stopped (%1). Press Connect ChatGPT to try again.")
                     .arg(reply->errorString()));
            return;
        }
        const QByteArray got = m_hash.result().toHex();
        if (got != sha256) {
            QFile::remove(partPath);
            fail(tr("The downloaded helper did not match its published checksum, so it was "
                    "deleted and not run."));
            return;
        }
        QFile::remove(finalPath);
        if (!QFile::rename(partPath, finalPath)) {
            QFile::remove(partPath);
            fail(tr("Could not save the helper in %1.").arg(QDir::toNativeSeparators(toolsDir())));
            return;
        }
        QFile::setPermissions(finalPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                             | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                             | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                             | QFileDevice::ExeOther);
        m_helperOrigin = tr("Downloaded by Dafin from %1").arg(url.toString());
        m_helperChecksum = QString::fromLatin1(got);
        launch(finalPath);
    });
}

void ChatGptConnector::launch(const QString &path)
{
#if QT_CONFIG(process)
    stopProcess();
    m_helperPath = QDir::toNativeSeparators(path);
    if (m_helperChecksum.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&file);
            m_helperChecksum = QString::fromLatin1(hash.result().toHex());
        }
    }
    m_publicAddress.clear();
    m_chatGptAddress.clear();
    m_pendingOutput.clear();
    m_log.clear();
    m_registered = false;
    setState(QStringLiteral("starting"), tr("Starting the connection…"));

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    // No console window flashing up behind the editor.
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ChatGptConnector::onOutput);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_stopping)
            fail(tr("The helper could not start. Your security software may have blocked it."));
    });
    connect(m_process, &QProcess::finished, this, [this] {
        if (m_stopping)
            return;
        if (m_state == QLatin1String("starting") || m_state == QLatin1String("connected"))
            fail(tr("The helper stopped. %1").arg(m_log.isEmpty() ? QString() : m_log.constLast()));
    });
    m_process->start(path, {QStringLiteral("tunnel"), QStringLiteral("--no-autoupdate"),
                            QStringLiteral("--url"),
                            QStringLiteral("http://127.0.0.1:%1").arg(m_mcp->port())});
    m_startTimeout->start(kStartTimeoutMs);
#else
    Q_UNUSED(path)
#endif
}

void ChatGptConnector::appendLog(const QString &line)
{
    m_log.append(line);
    while (m_log.size() > kMaxLogLines)
        m_log.removeFirst();
}

void ChatGptConnector::onOutput()
{
#if QT_CONFIG(process)
    if (!m_process)
        return;
    m_pendingOutput += m_process->readAllStandardOutput();
    int newline;
    while ((newline = m_pendingOutput.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_pendingOutput.left(newline)).trimmed();
        m_pendingOutput.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        appendLog(line);
        if (m_publicAddress.isEmpty()) {
            const QString address = parseTunnelAddress(line);
            if (!address.isEmpty()) {
                m_publicAddress = address;
                m_chatGptAddress = m_mcp->chatGptUrl(address);
                m_message = tr("Almost ready…");
                const QString seen = address;
                QTimer::singleShot(kSettleMs, this, [this, seen] {
                    if (m_publicAddress == seen)
                        markConnected();
                });
            }
        }
        if (line.contains(QLatin1String("Registered tunnel connection")))
            m_registered = true;
        if (m_registered && !m_publicAddress.isEmpty())
            markConnected();
    }
    emit changed();
#endif
}

void ChatGptConnector::markConnected()
{
    if (m_state != QLatin1String("starting") || m_chatGptAddress.isEmpty())
        return;
    m_startTimeout->stop();
    setState(QStringLiteral("connected"),
             tr("Connected. Copy the prompt, paste it into ChatGPT and follow its steps."));
}

void ChatGptConnector::stopProcess()
{
#if QT_CONFIG(process)
    if (!m_process)
        return;
    QProcess *process = m_process;
    m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    process->kill();
    process->waitForFinished(2000);
    delete process;
#endif
}

void ChatGptConnector::stop()
{
    m_stopping = true;
    m_startTimeout->stop();
    if (m_reply)
        m_reply->abort();
    if (m_testReply)
        m_testReply->abort();
    stopProcess();
    if (m_mcp->webAccess())
        m_mcp->setWebAccess(false);
    m_publicAddress.clear();
    m_chatGptAddress.clear();
    m_testState.clear();
    m_testMessage.clear();
    m_progress = 0;
    setState(QStringLiteral("idle"), QString());
}

void ChatGptConnector::test()
{
    if (m_chatGptAddress.isEmpty() || m_testState == QLatin1String("testing"))
        return;
    m_testState = QStringLiteral("testing");
    m_testMessage = tr("Testing from the internet side…");
    emit changed();

    QNetworkRequest request{QUrl(m_chatGptAddress)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json, text/event-stream");
    request.setTransferTimeout(20000);
    const QByteArray body =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"Dafin connection test","version":"1"}}})";
    QNetworkReply *reply = m_nam->post(request, body);
    m_testReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_testState != QLatin1String("testing"))
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray answer = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && status == 200
            && answer.contains("\"result\"")) {
            m_testState = QStringLiteral("ok");
            m_testMessage = tr("Works: Dafin can be reached from the internet. If ChatGPT still "
                               "cannot connect, the problem is in ChatGPT (your plan or its "
                               "settings), not in Dafin.");
        } else {
            m_testState = QStringLiteral("failed");
            m_testMessage = tr("Could not reach Dafin through the helper (%1). Wait a few "
                               "seconds and test again; if it keeps failing, press Stop and "
                               "Connect ChatGPT again.")
                                .arg(status > 0 ? QStringLiteral("HTTP %1").arg(status)
                                                : reply->errorString());
        }
        emit changed();
    });
}

QString ChatGptConnector::prompt() const
{
    return m_chatGptAddress.isEmpty() ? QString() : chatGptPrompt(m_chatGptAddress);
}

bool ChatGptConnector::copyPrompt()
{
    return !m_chatGptAddress.isEmpty() && m_mcp->copyText(prompt());
}

bool ChatGptConnector::copyAddress()
{
    return !m_chatGptAddress.isEmpty() && m_mcp->copyText(m_chatGptAddress);
}
