#pragma once

#include "app/assistant/LlmProtocol.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

class AppController;
class QSettings;

namespace dafin::assets {
class SecretStore;
}
namespace drift::mcp {
class McpDispatcher;
}

namespace dafin::assistant {

class LlmTransport;

// Dafin AI Assist: the chat panel's brain. The user types a request; a model (Anthropic, or any
// OpenAI-compatible endpoint, including a local server) answers and edits the open project
// through Dafin's own MCP tools, run in-process. Off until the user sets up a provider and gives
// consent; until then no network call is made at all. A model can never confirm an
// irreversible op (that dialog needs a click) and can never answer its own question (ask_user
// is answered only by the panel's buttons).
class AssistantController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY settingsChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY settingsChanged)
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY settingsChanged)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY settingsChanged)
    Q_PROPERTY(bool keyFromEnvironment READ keyFromEnvironment NOTIFY settingsChanged)
    Q_PROPERTY(bool keyStorageEncrypted READ keyStorageEncrypted NOTIFY settingsChanged)
    Q_PROPERTY(bool consent READ consent WRITE setConsent NOTIFY settingsChanged)
    Q_PROPERTY(bool allowFrames READ allowFrames WRITE setAllowFrames NOTIFY settingsChanged)
    Q_PROPERTY(QString knowledgeFolder READ knowledgeFolder WRITE setKnowledgeFolder NOTIFY settingsChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY settingsChanged)
    Q_PROPERTY(QString notReadyReason READ notReadyReason NOTIFY settingsChanged)
    Q_PROPERTY(QString consentText READ consentText NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap question READ question NOTIFY questionChanged)
    Q_PROPERTY(QString lastUsage READ lastUsage NOTIFY usageChanged)
    Q_PROPERTY(QString usageLogPath READ usageLogPath CONSTANT)
    // Dafin AI Fill: the request the timeline's AI Fill tool started, as the card shows it.
    // {state: "" | working | done | error | stopped, status, error, setup, clipId (the AI Fill
    //  clip in the space, "" when the request was about other places), version, versions, text,
    //  changed (the project changed), start, end, track}
    Q_PROPERTY(QVariantMap fill READ fill NOTIFY fillChanged)

public:
    static constexpr int kMaxToolCallsPerTurn = 40;
    static constexpr int kMaxToolResultChars = 60000;
    static constexpr int kMaxFillDocumentChars = 100000; // current document sent for "Change it"

    // `settingsFile` empty = the app's normal settings; tests pass a temporary INI file.
    explicit AssistantController(AppController *app, const QString &settingsFile = {},
                                 QObject *parent = nullptr);
    ~AssistantController() override;

    // Where API keys are kept (main.cpp passes SecretStore::createBest). Without one, only keys
    // from the environment (ANTHROPIC_API_KEY / OPENAI_API_KEY) are available.
    void setSecretStore(std::unique_ptr<dafin::assets::SecretStore> store);
    // Tests replace the network with a fake.
    void setTransport(std::unique_ptr<LlmTransport> transport);
    void setUsageLogPath(const QString &path) { m_usageLogPath = path; }

    QVariantList messages() const { return m_messages; }
    bool busy() const { return m_busy; }
    QString provider() const;
    void setProvider(const QString &id);
    QString model() const;
    void setModel(const QString &model);
    QString baseUrl() const;
    void setBaseUrl(const QString &url);
    bool hasKey() const;
    bool keyFromEnvironment() const;
    bool keyStorageEncrypted() const;
    bool consent() const;
    void setConsent(bool given);
    bool allowFrames() const;
    void setAllowFrames(bool allow);
    QString knowledgeFolder() const;
    void setKnowledgeFolder(const QString &folder);
    bool ready() const { return notReadyReason().isEmpty(); }
    QString notReadyReason() const;
    QString consentText() const;
    QVariantMap question() const { return m_question; }
    QString lastUsage() const { return m_lastUsage; }
    QString usageLogPath() const { return m_usageLogPath; }
    QVariantMap fill() const { return m_fillState; }

    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newChat();
    // Stores the key for the current provider. The key never appears in messages, logs or MCP.
    Q_INVOKABLE bool setApiKey(const QString &key);
    Q_INVOKABLE void clearApiKey();
    // Only the panel's buttons call this: choice = option index, -1 = Skip, -2 = Not sure.
    // Ignored unless `questionId` is the one open question and the project is unchanged.
    Q_INVOKABLE void answerQuestion(const QString &questionId, int choice);

    // Dafin AI Fill. `request`: prompt, start, end (seconds), track (top row drawn over, -1 any),
    // clipId (an AI Fill clip to give a new version, "Change it" / "Try again"), retry (bool: a
    // different take on the same prompt). Runs in a conversation of its own, so the chat is left
    // as it was, with every tool the chat has (the request may reach beyond the space) plus
    // fill_region / check_fill; protected ranges and the irreversible-action dialog apply as
    // always, and an ask_user question shows on the AI Fill card. False when it could not start
    // (the reason is in fill.error; fill.setup is true when AI Assist still needs setting up).
    Q_INVOKABLE bool generateFill(const QVariantMap &request);
    // Forget a finished fill (the card closed). Ignored while one is running; Stop ends that.
    Q_INVOKABLE void dismissFill();

    // For tests and diagnostics.
    const QList<ChatMessage> &history() const { return m_history; }
    QList<ToolSpec> toolSpecs() const;
    QString systemPrompt() const;

signals:
    void messagesChanged();
    void busyChanged();
    void settingsChanged();
    void questionChanged();
    void usageChanged();
    void turnFinished(const QString &outcome);
    void fillChanged();

private:
    std::unique_ptr<QSettings> settings() const;
    QString settingKey(const QString &name) const;
    QString apiKey() const;
    QString envKeyName() const;

    void addMessage(const QString &kind, const QString &text, const QVariantMap &extra = {});
    void setBusy(bool busy);
    void requestNext();
    void onReply(quint64 generation, int status, const QByteArray &body, const QString &netError);
    void processQueue();
    void runToolCall(const ToolCall &call);
    // Editing knowledge: the user's own folder if it holds a SKILL.md, else the skill built into Dafin
    // (assistant-skill/, a Qt resource). readSkillFile only ever reads inside that root.
    QString skillRoot() const;
    QString readSkillFile(const QString &relPath, QString *error) const;
    void addToolResult(const ToolCall &call, const QString &text, bool isError,
                       const QList<ImagePart> &images = {});
    void closeOpenCalls(const QString &why);
    void finishTurn(const QString &outcome);
    void writeUsageRow(const QString &outcome);
    QString projectHash() const;
    QString fillSystemPrompt() const;
    QList<ToolSpec> fillToolSpecs() const;
    QString fillUserMessage(const QVariantMap &request) const;
    void runFillTool(const ToolCall &call);
    void updateFill(const QVariantMap &changes);
    void finishFill(const QString &outcome);
    void beginTurn();

    AppController *m_app = nullptr;
    QString m_settingsFile;
    std::unique_ptr<drift::mcp::McpDispatcher> m_dispatcher;
    std::unique_ptr<LlmTransport> m_transport;
    std::unique_ptr<dafin::assets::SecretStore> m_secrets;
    mutable QHash<QString, std::optional<QString>> m_keyCache; // per provider, memory only

    QList<ChatMessage> m_history;
    QVariantList m_messages;
    QList<ToolCall> m_queue;
    bool m_busy = false;
    quint64 m_generation = 0; // bumps on Stop / New chat; late replies of an old one are dropped
    quint64 m_chatGeneration = 0; // bumps on New chat only
    bool m_toolRunning = false;   // a tool is running (it may be showing a confirm dialog)

    QVariantMap m_question;  // the one open ask_user question, shown as buttons
    ToolCall m_questionCall;
    QString m_questionHash;  // project content when it was asked

    int m_turnToolCalls = 0;
    int m_turnFailedCalls = 0;
    int m_turnRounds = 0;
    Usage m_turnUsage;
    QElapsedTimer m_turnTimer;
    QString m_turnHashBefore;
    QString m_lastUsage;
    QString m_usageLogPath;
    int m_imageCallId = 0;

    // AI Fill: the request in flight, and the chat put aside while it runs.
    struct FillJob
    {
        bool active = false;
        QVariantMap request;
        QString requestId;
        QString clipId;     // the clip this request placed or changed, once fill_region worked
        QString lastText;   // the model's latest words, shown when it ends without an animation
        QString lastError;  // the latest failure, for the card
    };
    FillJob m_fill;
    QVariantMap m_fillState;
    QList<ChatMessage> m_chatAside;
};

} // namespace dafin::assistant
