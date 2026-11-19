#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class McpController;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

// Dafin: connect ChatGPT (the website) to this editor with one button.
//
// ChatGPT only reaches public https addresses, so the editor's local MCP server needs a tunnel.
// Before this, the user had to install cloudflared, run it in a terminal and paste its address
// back. Now Dafin does it: it turns agent access and web access on, finds cloudflared (Dafin's
// own copy, then PATH) or downloads it once after the user says OK, starts it in the background,
// reads the https address it prints and builds the ChatGPT address. The last step, adding the
// server inside ChatGPT, has to be the user's own click; copyPrompt() gives them a prompt that
// asks ChatGPT to explain its own current screens, so Dafin never describes ChatGPT's menus.
//
// A downloaded helper runs only after its SHA-256 equals the one GitHub publishes for that
// release asset; anything else is deleted. No network call happens before start().
namespace dafin::chatgpt {

// The https://<name>.trycloudflare.com address in one line of cloudflared's output, or "".
QString parseTunnelAddress(const QString &line);
// The text to paste into ChatGPT: the address, what kind of server it is, and a request for
// step-by-step instructions for the ChatGPT version the user is looking at.
QString chatGptPrompt(const QString &chatGptAddress);
// "windows-amd64", "windows-386", "linux-amd64", "linux-arm64", or "" where Dafin cannot
// download the helper (macOS ships it as an archive; Homebrew's copy on PATH is used there).
QString currentPlatform();
// The release asset for `platform` from GitHub's "latest release" JSON ({} when none).
QJsonObject pickReleaseAsset(const QJsonObject &release, const QString &platform);
// The SHA-256 GitHub publishes for `asset`: its "digest" ("sha256:<hex>"), else the line naming
// the asset in the release notes' checksum list. Lower-case hex, or "" when there is none.
QByteArray checksumFromRelease(const QJsonObject &release, const QJsonObject &asset);
// cloudflared or cloudflared.exe
QString executableName();

} // namespace dafin::chatgpt

class ChatGptConnector : public QObject
{
    Q_OBJECT
    // idle | needsDownload | downloading | starting | connected | failed
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // One plain sentence about the state, for the window.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString publicAddress READ publicAddress NOTIFY changed)
    Q_PROPERTY(QString chatGptAddress READ chatGptAddress NOTIFY changed)
    // "" | testing | ok | failed, and what the test found.
    Q_PROPERTY(QString testState READ testState NOTIFY changed)
    Q_PROPERTY(QString testMessage READ testMessage NOTIFY changed)
    // For Details & security: which helper runs, where it came from, its checksum, its output.
    Q_PROPERTY(QString helperPath READ helperPath NOTIFY changed)
    Q_PROPERTY(QString helperOrigin READ helperOrigin NOTIFY changed)
    Q_PROPERTY(QString helperChecksum READ helperChecksum NOTIFY changed)
    Q_PROPERTY(QString helperLog READ helperLog NOTIFY changed)
    Q_PROPERTY(bool canDownload READ canDownload CONSTANT)

public:
    explicit ChatGptConnector(McpController *mcp, QObject *parent = nullptr);
    ~ChatGptConnector() override;

    QString state() const { return m_state; }
    QString message() const { return m_message; }
    double progress() const { return m_progress; }
    QString publicAddress() const { return m_publicAddress; }
    QString chatGptAddress() const { return m_chatGptAddress; }
    QString testState() const { return m_testState; }
    QString testMessage() const { return m_testMessage; }
    QString helperPath() const { return m_helperPath; }
    QString helperOrigin() const { return m_helperOrigin; }
    QString helperChecksum() const { return m_helperChecksum; }
    QString helperLog() const { return m_log.join(QLatin1Char('\n')); }
    bool canDownload() const { return !dafin::chatgpt::currentPlatform().isEmpty(); }

    // Connect ChatGPT: access on, web access on, helper found (or needsDownload), tunnel started.
    Q_INVOKABLE void start();
    // The user said OK to downloading the helper.
    Q_INVOKABLE void confirmDownload();
    // Close the tunnel and web access; back to idle. Agent access itself stays as it was.
    Q_INVOKABLE void stop();
    // Calls the ChatGPT address from outside (through the tunnel), as ChatGPT would.
    Q_INVOKABLE void test();
    Q_INVOKABLE QString prompt() const;
    // True only when the clipboard read back the copied text.
    Q_INVOKABLE bool copyPrompt();
    Q_INVOKABLE bool copyAddress();
    // Where Dafin keeps its own copy of the helper.
    static QString toolsDir();

signals:
    void changed();

private:
    void setState(const QString &state, const QString &message);
    void fail(const QString &message);
    QString findHelper() const;
    void launch(const QString &path);
    void onOutput();
    void markConnected();
    void downloadAsset(const QJsonObject &asset, const QByteArray &sha256);
    void stopProcess();
    void appendLog(const QString &line);

    McpController *m_mcp = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_testReply;
    QProcess *m_process = nullptr;
    QTimer *m_startTimeout = nullptr;
    QFile *m_partFile = nullptr;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QByteArray m_pendingOutput;

    QString m_state = QStringLiteral("idle");
    QString m_message;
    double m_progress = 0;
    QString m_publicAddress;
    QString m_chatGptAddress;
    QString m_testState;
    QString m_testMessage;
    QString m_helperPath;
    QString m_helperOrigin;
    QString m_helperChecksum;
    QStringList m_log;
    bool m_stopping = false;
    bool m_registered = false; // cloudflared said "Registered tunnel connection"
};
