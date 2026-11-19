#include "app/assistant/AssistantController.h"

#include "app/assistant/LlmTransport.h"
#include "app/editor/AppController.h"
#include "app/mcp/McpController.h"
#include "app/mcp/tools/McpCatalog.h"
#include "app/mcp/tools/McpDispatcher.h"
#include "app/mcp/transport/McpServer.h"
#include "app/online/library/SecretStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QUuid>

namespace dafin::assistant {

namespace {

constexpr auto kDefaultAnthropicModel = "claude-sonnet-5-5";
constexpr int kKnowledgeMaxChars = 24000;
constexpr int kSkillFileMaxChars = 12000;
const QString kBuiltInSkillRoot = QStringLiteral(":/assistant-skill");

QString compactArgs(const QJsonObject &args)
{
    QString s = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
    if (s.size() > 100)
        s = s.left(99) + QChar(0x2026);
    return s;
}

void addUsage(qint64 &total, qint64 part)
{
    if (part < 0)
        return;
    total = (total < 0 ? 0 : total) + part;
}

QString usageField(qint64 v)
{
    return v < 0 ? QStringLiteral("unknown") : QString::number(v);
}

QJsonObject askUserSchema()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("question"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("options"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                  {QStringLiteral("minItems"), 2},
                                  {QStringLiteral("maxItems"), 4}}},
                     {QStringLiteral("why"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("question"), QStringLiteral("options")}}};
}

} // namespace

AssistantController::AssistantController(AppController *app, const QString &settingsFile, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_settingsFile(settingsFile)
    , m_dispatcher(std::make_unique<drift::mcp::McpDispatcher>(app))
    , m_transport(std::make_unique<NetworkTransport>())
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_usageLogPath = dir.isEmpty() ? QString() : dir + QStringLiteral("/assistant-usage.csv");
}

AssistantController::~AssistantController()
{
    ++m_generation;
    if (m_transport)
        m_transport->abort();
}

void AssistantController::setSecretStore(std::unique_ptr<dafin::assets::SecretStore> store)
{
    m_secrets = std::move(store);
    m_keyCache.clear();
    emit settingsChanged();
}

void AssistantController::setTransport(std::unique_ptr<LlmTransport> transport)
{
    if (m_transport)
        m_transport->abort();
    m_transport = std::move(transport);
}

// ---- Settings ---------------------------------------------------------------------------------

std::unique_ptr<QSettings> AssistantController::settings() const
{
    if (m_settingsFile.isEmpty())
        return std::make_unique<QSettings>();
    return std::make_unique<QSettings>(m_settingsFile, QSettings::IniFormat);
}

QString AssistantController::settingKey(const QString &name) const
{
    return QStringLiteral("assistant/") + name;
}

QString AssistantController::provider() const
{
    return providerId(providerFromId(settings()->value(settingKey(QStringLiteral("provider"))).toString()));
}

void AssistantController::setProvider(const QString &id)
{
    const QString next = providerId(providerFromId(id));
    if (next == provider())
        return;
    settings()->setValue(settingKey(QStringLiteral("provider")), next);
    emit settingsChanged();
}

QString AssistantController::model() const
{
    const QString fallback = provider() == QLatin1String("anthropic") ? QString::fromLatin1(kDefaultAnthropicModel) : QString();
    return settings()->value(settingKey(QStringLiteral("model/") + provider()), fallback).toString();
}

void AssistantController::setModel(const QString &model)
{
    if (model.trimmed() == this->model())
        return;
    settings()->setValue(settingKey(QStringLiteral("model/") + provider()), model.trimmed());
    emit settingsChanged();
}

QString AssistantController::baseUrl() const
{
    return settings()->value(settingKey(QStringLiteral("baseUrl"))).toString();
}

void AssistantController::setBaseUrl(const QString &url)
{
    if (url.trimmed() == baseUrl())
        return;
    settings()->setValue(settingKey(QStringLiteral("baseUrl")), url.trimmed());
    emit settingsChanged();
}

bool AssistantController::consent() const
{
    return settings()->value(settingKey(QStringLiteral("consent/") + provider()), false).toBool();
}

void AssistantController::setConsent(bool given)
{
    if (given == consent())
        return;
    settings()->setValue(settingKey(QStringLiteral("consent/") + provider()), given);
    emit settingsChanged();
}

bool AssistantController::allowFrames() const
{
    return settings()->value(settingKey(QStringLiteral("allowFrames")), false).toBool();
}

void AssistantController::setAllowFrames(bool allow)
{
    if (allow == allowFrames())
        return;
    settings()->setValue(settingKey(QStringLiteral("allowFrames")), allow);
    emit settingsChanged();
}

QString AssistantController::knowledgeFolder() const
{
    return settings()->value(settingKey(QStringLiteral("knowledgeFolder"))).toString();
}

void AssistantController::setKnowledgeFolder(const QString &folder)
{
    QString path = folder.trimmed();
    if (path.startsWith(QLatin1String("file:")))
        path = QUrl(path).toLocalFile();
    if (path == knowledgeFolder())
        return;
    settings()->setValue(settingKey(QStringLiteral("knowledgeFolder")), path);
    emit settingsChanged();
}

// ---- Keys -------------------------------------------------------------------------------------

QString AssistantController::envKeyName() const
{
    return provider() == QLatin1String("anthropic") ? QStringLiteral("ANTHROPIC_API_KEY")
                                                    : QStringLiteral("OPENAI_API_KEY");
}

bool AssistantController::keyFromEnvironment() const
{
    return !qEnvironmentVariable(envKeyName().toLatin1().constData()).trimmed().isEmpty();
}

QString AssistantController::apiKey() const
{
    const QString env = qEnvironmentVariable(envKeyName().toLatin1().constData()).trimmed();
    if (!env.isEmpty())
        return env;
    const QString p = provider();
    if (!m_keyCache.contains(p)) {
        std::optional<QString> key;
        if (m_secrets) {
            if (const auto stored = m_secrets->read(QStringLiteral("assistant-") + p))
                key = QString::fromUtf8(*stored);
        }
        m_keyCache.insert(p, key);
    }
    return m_keyCache.value(p).value_or(QString());
}

bool AssistantController::hasKey() const
{
    return !apiKey().isEmpty();
}

bool AssistantController::keyStorageEncrypted() const
{
    return m_secrets && m_secrets->isEncrypted();
}

bool AssistantController::setApiKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty() || !m_secrets)
        return false;
    if (!m_secrets->write(QStringLiteral("assistant-") + provider(), trimmed.toUtf8()))
        return false;
    m_keyCache.insert(provider(), trimmed);
    emit settingsChanged();
    return true;
}

void AssistantController::clearApiKey()
{
    if (m_secrets)
        m_secrets->remove(QStringLiteral("assistant-") + provider());
    m_keyCache.insert(provider(), std::nullopt);
    emit settingsChanged();
}

QString AssistantController::notReadyReason() const
{
    const bool local = provider() == QLatin1String("openai") && !baseUrl().isEmpty();
    if (model().isEmpty())
        return tr("Enter the model name to use.");
    if (!hasKey() && !local)
        return tr("Add an API key (or, for a local server, its address).");
    if (!consent())
        return tr("Read what is sent and agree to it.");
    return {};
}

QString AssistantController::consentText() const
{
    const QString where = provider() == QLatin1String("anthropic")
                              ? tr("Anthropic (api.anthropic.com)")
                              : (baseUrl().isEmpty() ? tr("OpenAI (api.openai.com)")
                                                     : tr("the server at %1").arg(baseUrl()));
    QString text = tr("When you send a request, AI Assist sends it to %1, together with what the "
                      "model asks to read from your open project: clip and track details, file "
                      "names, transcript text and the results of edits.")
                       .arg(where);
    text += QLatin1Char(' ');
    text += allowFrames() ? tr("Frames and contact sheets from your footage are also sent, because "
                               "\"Allow the assistant to see frames\" is on.")
                          : tr("Frames from your footage are not sent while \"Allow the assistant to see "
                               "frames\" is off.");
    text += QLatin1Char(' ');
    text += tr("Nothing is sent until you press Send. Your API key is kept on this computer and is "
               "never shown to the model.");
    return text;
}

// ---- Conversation -----------------------------------------------------------------------------

QList<ToolSpec> AssistantController::toolSpecs() const
{
    QList<ToolSpec> out;
    const bool frames = allowFrames();
    for (const QJsonValue &v : drift::mcp::homepageTools()) {
        const QJsonObject t = v.toObject();
        const QString name = t.value(QStringLiteral("name")).toString();
        if (!frames && (name == QLatin1String("frames") || name == QLatin1String("capture")))
            continue; // not offered at all, and refused if asked for anyway
        out.append({name, t.value(QStringLiteral("description")).toString(),
                    t.value(QStringLiteral("inputSchema")).toObject()});
    }
    out.append({QStringLiteral("ask_user"),
                QStringLiteral("Ask the user ONE multiple-choice question when a real choice changes the "
                               "result (style, which take, how much to cut). 2-4 short options; the user "
                               "can also Skip or say Not sure. The answer is a preference, never "
                               "permission for anything irreversible. Do not ask what you can find out "
                               "with a tool."),
                askUserSchema()});
    out.append({QStringLiteral("read_skill_file"),
                QStringLiteral("Read one file of the editing knowledge (relative path such as "
                               "skills/sound-director/SKILL.md). Read-only, up to 12000 characters. Open only "
                               "the files the task needs."),
                QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                            {QStringLiteral("properties"),
                             QJsonObject{{QStringLiteral("path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
                            {QStringLiteral("required"), QJsonArray{QStringLiteral("path")}}}});
    return out;
}

QString AssistantController::systemPrompt() const
{
    QString s = QStringLiteral(
        "You are AI Assist inside Dafin, a desktop video editor. You change the user's open project only "
        "through the tools you are given; never claim an edit a tool result does not show.\n"
        "Workflow: catalog({brief:true}) once to see the toolboxes, then toolbox({name}) or search({q}) for "
        "an op's schema, then call the op, or apply({ops:[...]}) to make several edits one undo step. Read "
        "before you edit (inspect, get_transcript). Use dry_run where an op offers it before large cuts and "
        "say what you plan.\n"
        "Rules: do only what the user asked. Never cut protected footage (list_protected_ranges); if an op "
        "fails with protected_range, move the cut. Some ops make the editor ask the user first; if one fails "
        "with not_confirmed, do not retry it, tell the user. Use ask_user only for a choice that changes the "
        "result. Reply briefly, in the user's language, and end with what changed and how to undo it.\n");
    s += allowFrames()
             ? QStringLiteral("You can check visual results with capture or frames; keep them small.\n")
             : QStringLiteral("You cannot see frames from the footage; say what the user should check by eye.\n");
    const QString root = skillRoot();
    const bool builtIn = root == kBuiltInSkillRoot;
    QFile f(QDir(root).filePath(QStringLiteral("SKILL.md")));
    if (f.open(QIODevice::ReadOnly)) {
        QString knowledge = QString::fromUtf8(f.read(kKnowledgeMaxChars * 4)).left(kKnowledgeMaxChars);
        s += builtIn ? QStringLiteral("\nEditing knowledge built into Dafin (follow it where it applies, Dafin's tool "
                                      "results win over it; read more of it with read_skill_file):\n")
                     : QStringLiteral("\nEditing knowledge the user added (their own SKILL.md; follow it where it applies, "
                                      "Dafin's tool results win over it; read more of it with read_skill_file):\n");
        s += knowledge;
    }
    return s;
}

QString AssistantController::skillRoot() const
{
    const QString folder = knowledgeFolder();
    if (!folder.isEmpty() && QFileInfo::exists(QDir(folder).filePath(QStringLiteral("SKILL.md"))))
        return folder;
    return kBuiltInSkillRoot; // nothing set, or the chosen folder has no SKILL.md: the built-in skill
}

QString AssistantController::readSkillFile(const QString &relPath, QString *error) const
{
    const QString rel = QDir::cleanPath(relPath.trimmed());
    const bool okType = rel.endsWith(QLatin1String(".md")) || rel.endsWith(QLatin1String(".json"));
    if (rel.isEmpty() || rel.startsWith(QLatin1Char('/')) || rel.startsWith(QLatin1String("..")) || rel.contains(QLatin1Char(':'))
        || rel.contains(QLatin1Char('\\')) || !okType) {
        *error = QStringLiteral("read_skill_file takes a relative .md or .json path inside the skill, for example skills/sound-director/SKILL.md.");
        return {};
    }
    QFile f(QDir(skillRoot()).filePath(rel));
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("No such skill file: %1").arg(rel);
        return {};
    }
    const QString text = QString::fromUtf8(f.read(kSkillFileMaxChars * 4));
    return text.size() > kSkillFileMaxChars ? text.left(kSkillFileMaxChars) + QStringLiteral("\n[cut at 12000 characters]") : text;
}

void AssistantController::addMessage(const QString &kind, const QString &text, const QVariantMap &extra)
{
    if (m_fill.active) {
        // An AI Fill request is not part of the chat: its progress shows on the timeline card.
        if (kind == QLatin1String("assistant")) {
            m_fill.lastText = text.left(400);
        } else if (kind == QLatin1String("error")) {
            m_fill.lastError = text.left(400);
        } else if (kind == QLatin1String("question")) {
            updateFill({{QStringLiteral("status"), tr("Waiting for your answer…")}});
        } else if (kind == QLatin1String("step")) {
            const bool ok = extra.value(QStringLiteral("ok")).toBool();
            QString status;
            if (text == QLatin1String("fill_region"))
                status = ok ? tr("Placed it. Checking how it looks…") : tr("Fixing a problem in the animation…");
            else if (text == QLatin1String("check_fill"))
                status = tr("Checking the animation…");
            else if (text == QLatin1String("capture") || text == QLatin1String("frames"))
                status = tr("Looking at the picture…");
            else if (text == QLatin1String("inspect") || text == QLatin1String("catalog")
                     || text == QLatin1String("search") || text == QLatin1String("toolbox")
                     || text == QLatin1String("activity"))
                status = tr("Looking at the project…");
            else
                status = tr("Editing the project…");
            updateFill({{QStringLiteral("status"), status}});
        }
        return;
    }
    QVariantMap m = extra;
    m.insert(QStringLiteral("kind"), kind);
    m.insert(QStringLiteral("text"), text);
    m_messages.append(m);
    emit messagesChanged();
}

void AssistantController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

QString AssistantController::projectHash() const
{
    return m_app ? static_cast<const AppController *>(m_app)->project()->contentHash() : QString();
}

void AssistantController::send(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || m_busy)
        return;
    if (m_toolRunning) {
        addMessage(QStringLiteral("info"), tr("Wait for the current step to finish."));
        return;
    }
    if (!m_question.isEmpty()) {
        addMessage(QStringLiteral("info"), tr("Answer the question above with its buttons, or press Stop."));
        return;
    }
    if (!ready()) {
        // No network call at all until the user has set a provider up and agreed.
        addMessage(QStringLiteral("info"), tr("Set up AI Assist first: %1").arg(notReadyReason()));
        return;
    }
    ChatMessage user;
    user.role = ChatMessage::Role::User;
    user.text = prompt;
    m_history.append(user);
    addMessage(QStringLiteral("user"), prompt);
    beginTurn();
    requestNext();
}

void AssistantController::beginTurn()
{
    m_turnToolCalls = 0;
    m_turnFailedCalls = 0;
    m_turnRounds = 0;
    m_turnUsage = {};
    m_turnHashBefore = projectHash();
    m_turnTimer.start();
    setBusy(true);
}

void AssistantController::requestNext()
{
    RequestOptions options;
    options.provider = providerFromId(provider());
    options.model = model();
    options.baseUrl = baseUrl();
    options.apiKey = apiKey();
    options.system = m_fill.active ? fillSystemPrompt() : systemPrompt();
    const LlmRequest request = buildRequest(options, m_fill.active ? fillToolSpecs() : toolSpecs(), m_history);
    ++m_turnRounds;
    const quint64 generation = m_generation;
    m_transport->post(request, [this, generation](int status, const QByteArray &body, const QString &netError) {
        onReply(generation, status, body, netError);
    });
}

void AssistantController::onReply(quint64 generation, int status, const QByteArray &body, const QString &netError)
{
    if (generation != m_generation || !m_busy)
        return; // stopped, or a new chat started meanwhile
    if (status == 0 && !netError.isEmpty()) {
        addMessage(QStringLiteral("error"), tr("Couldn't reach the AI service: %1").arg(netError));
        finishTurn(QStringLiteral("error"));
        return;
    }
    const LlmReply reply = parseReply(providerFromId(provider()), status, body);
    addUsage(m_turnUsage.input, reply.usage.input);
    addUsage(m_turnUsage.output, reply.usage.output);
    addUsage(m_turnUsage.cacheRead, reply.usage.cacheRead);
    addUsage(m_turnUsage.cacheWrite, reply.usage.cacheWrite);
    if (!reply.ok) {
        addMessage(QStringLiteral("error"), reply.error);
        finishTurn(QStringLiteral("error"));
        return;
    }
    ChatMessage assistant;
    assistant.role = ChatMessage::Role::Assistant;
    assistant.text = reply.text;
    assistant.toolCalls = reply.toolCalls;
    m_history.append(assistant);
    if (!reply.text.trimmed().isEmpty())
        addMessage(QStringLiteral("assistant"), reply.text.trimmed());
    if (reply.toolCalls.isEmpty()) {
        finishTurn(QStringLiteral("ok"));
        return;
    }
    m_queue = reply.toolCalls;
    processQueue();
}

void AssistantController::addToolResult(const ToolCall &call, const QString &text, bool isError,
                                        const QList<ImagePart> &images)
{
    ChatMessage result;
    result.role = ChatMessage::Role::Tool;
    result.toolCallId = call.id;
    result.toolName = call.name;
    result.text = text;
    result.isError = isError;
    result.images = images;
    m_history.append(result);
}

void AssistantController::processQueue()
{
    const quint64 generation = m_generation;
    while (!m_queue.isEmpty()) {
        if (generation != m_generation || !m_busy)
            return;
        const ToolCall call = m_queue.takeFirst();
        if (m_turnToolCalls >= kMaxToolCallsPerTurn) {
            addToolResult(call, QStringLiteral("Tool call limit for one request reached."), true);
            continue;
        }
        if (m_fill.active
            && (call.name == QLatin1String("fill_region") || call.name == QLatin1String("check_fill"))) {
            // AI Fill's own two tools; every other tool runs exactly as in the chat.
            runFillTool(call);
            if (generation != m_generation)
                return;
            continue;
        }
        if (call.name == QLatin1String("read_skill_file")) {
            QString err;
            const QString text = readSkillFile(call.args.value(QStringLiteral("path")).toString(), &err);
            ++m_turnToolCalls;
            if (text.isEmpty()) {
                addToolResult(call, err.isEmpty() ? QStringLiteral("Empty file.") : err, true);
                ++m_turnFailedCalls;
            } else {
                addToolResult(call, text, false);
            }
            continue;
        }
        if (call.name == QLatin1String("ask_user")) {
            const QString text = call.args.value(QStringLiteral("question")).toString().trimmed().left(300);
            QStringList options;
            for (const QJsonValue &v : call.args.value(QStringLiteral("options")).toArray())
                if (!v.toString().trimmed().isEmpty())
                    options.append(v.toString().trimmed().left(80));
            if (text.isEmpty() || options.size() < 2 || options.size() > 4) {
                addToolResult(call, QStringLiteral("ask_user needs a question and 2-4 options."), true);
                ++m_turnFailedCalls;
                continue;
            }
            ++m_turnToolCalls;
            m_questionCall = call;
            m_questionHash = projectHash();
            m_question = {{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                          {QStringLiteral("text"), text},
                          {QStringLiteral("why"), call.args.value(QStringLiteral("why")).toString().left(200)},
                          {QStringLiteral("options"), options}};
            addMessage(QStringLiteral("question"), text, {{QStringLiteral("options"), options}});
            emit questionChanged();
            return; // waits for the panel's buttons
        }
        runToolCall(call);
        if (generation != m_generation)
            return; // Stop or New chat happened while the tool ran (e.g. a confirm dialog was open)
    }
    if (m_turnToolCalls >= kMaxToolCallsPerTurn) {
        addMessage(QStringLiteral("info"),
                   tr("Stopped after %1 tool calls for one request. Ask again to continue.").arg(kMaxToolCallsPerTurn));
        finishTurn(QStringLiteral("limit"));
        return;
    }
    requestNext();
}

void AssistantController::runToolCall(const ToolCall &call)
{
    ++m_turnToolCalls;
    if (!allowFrames() && (call.name == QLatin1String("frames") || call.name == QLatin1String("capture"))) {
        addToolResult(call,
                      QStringLiteral("The user has not allowed AI Assist to see frames. Work from inspect and "
                                     "transcripts, or ask the user to turn on \"Allow the assistant to see frames\"."),
                      true);
        ++m_turnFailedCalls;
        addMessage(QStringLiteral("step"), call.name, {{QStringLiteral("ok"), false}, {QStringLiteral("detail"), tr("not allowed to see frames")}});
        return;
    }
    const quint64 chat = m_chatGeneration;
    m_toolRunning = true;
    const QJsonObject result = drift::mcp::runTool(*m_dispatcher, call.name, call.args);
    m_toolRunning = false;
    if (chat != m_chatGeneration)
        return; // New chat while it ran: this result belongs to a conversation that is gone
    const bool isError = result.value(QStringLiteral("isError")).toBool();
    QStringList texts;
    QList<ImagePart> images;
    for (const QJsonValue &v : result.value(QStringLiteral("content")).toArray()) {
        const QJsonObject item = v.toObject();
        const QString type = item.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text"))
            texts.append(item.value(QStringLiteral("text")).toString());
        else if (type == QLatin1String("image") && allowFrames())
            images.append({item.value(QStringLiteral("mimeType")).toString(), item.value(QStringLiteral("data")).toString()});
    }
    QString text = texts.join(QLatin1Char('\n'));
    if (text.size() > kMaxToolResultChars)
        text = text.left(kMaxToolResultChars) + QStringLiteral("\n…(truncated; ask for less, e.g. a narrower range)");
    addToolResult(call, text, isError, images);
    if (!images.isEmpty() && m_app && m_app->mcp())
        m_app->mcp()->imageLog()->recordResult(--m_imageCallId, call.name, call.args, result); // shown in the Agent images log
    if (isError)
        ++m_turnFailedCalls;
    QString detail = compactArgs(call.args);
    if (isError && !texts.isEmpty()) {
        const QJsonObject payload = QJsonDocument::fromJson(texts.first().toUtf8()).object();
        const QString code = payload.value(QStringLiteral("error")).toString();
        if (!code.isEmpty())
            detail = code;
    }
    addMessage(QStringLiteral("step"), call.name, {{QStringLiteral("ok"), !isError}, {QStringLiteral("detail"), detail}});
}

void AssistantController::answerQuestion(const QString &questionId, int choice)
{
    if (m_question.isEmpty() || questionId.isEmpty() || questionId != m_question.value(QStringLiteral("id")).toString())
        return; // stale, replayed or forged
    const QStringList options = m_question.value(QStringLiteral("options")).toStringList();
    const ToolCall call = m_questionCall;
    QString text;
    QString shown;
    if (projectHash() != m_questionHash) {
        text = QStringLiteral("The question expired: the project changed before the user answered. Re-check the "
                              "project and ask again if it still matters.");
        shown = tr("Question expired: the project changed.");
    } else if (choice >= 0 && choice < options.size()) {
        text = QStringLiteral("The user chose: %1. This is a preference, not permission for anything irreversible.")
                   .arg(options.at(choice));
        shown = options.at(choice);
    } else if (choice == -1) {
        text = QStringLiteral("The user skipped the question. Use a sensible default and say which.");
        shown = tr("Skipped");
    } else {
        text = QStringLiteral("The user is not sure. Pick the safest reasonable option and say which.");
        shown = tr("Not sure");
    }
    m_question.clear();
    m_questionCall = {};
    emit questionChanged();
    addMessage(QStringLiteral("answer"), shown);
    addToolResult(call, text, false);
    processQueue();
}

void AssistantController::closeOpenCalls(const QString &why)
{
    if (!m_question.isEmpty()) {
        addToolResult(m_questionCall, why, true);
        m_question.clear();
        m_questionCall = {};
        emit questionChanged();
    }
    for (const ToolCall &call : std::as_const(m_queue))
        addToolResult(call, why, true);
    m_queue.clear();
}

void AssistantController::stop()
{
    if (!m_busy)
        return;
    ++m_generation;
    m_transport->abort();
    // Every tool call the model made needs a result, or the next request is invalid.
    closeOpenCalls(QStringLiteral("Stopped by the user before this ran."));
    addMessage(QStringLiteral("info"), tr("Stopped."));
    finishTurn(QStringLiteral("stopped"));
}

void AssistantController::newChat()
{
    if (m_busy)
        stop();
    ++m_generation;
    ++m_chatGeneration;
    m_history.clear();
    m_messages.clear();
    m_queue.clear();
    if (!m_question.isEmpty()) {
        m_question.clear();
        emit questionChanged();
    }
    m_lastUsage.clear();
    emit messagesChanged();
    emit usageChanged();
}

void AssistantController::finishTurn(const QString &outcome)
{
    if (m_turnUsage.input < 0 && m_turnUsage.output < 0)
        m_lastUsage = tr("Token use: not reported by this server");
    else
        m_lastUsage = tr("Tokens reported by the provider: %1 in, %2 out, %3 read from cache")
                          .arg(usageField(m_turnUsage.input), usageField(m_turnUsage.output),
                               usageField(m_turnUsage.cacheRead));
    emit usageChanged();
    writeUsageRow(outcome);
    if (m_fill.active)
        finishFill(outcome);
    setBusy(false);
    emit turnFinished(outcome);
}

void AssistantController::writeUsageRow(const QString &outcome)
{
    if (m_usageLogPath.isEmpty())
        return;
    QDir().mkpath(QFileInfo(m_usageLogPath).absolutePath());
    QFile file(m_usageLogPath);
    const bool fresh = !file.exists();
    if (!file.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&file);
    if (fresh)
        out << "time,provider,model,input_tokens,output_tokens,cache_read_tokens,cache_write_tokens,"
               "model_calls,tool_calls,failed_tool_calls,duration_ms,project_before,project_after,outcome\n";
    auto field = [](const QString &s) {
        QString v = s;
        v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(v);
    };
    out << QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << ',' << provider() << ',' << field(model()) << ','
        << usageField(m_turnUsage.input) << ',' << usageField(m_turnUsage.output) << ','
        << usageField(m_turnUsage.cacheRead) << ',' << usageField(m_turnUsage.cacheWrite) << ',' << m_turnRounds << ','
        << m_turnToolCalls << ',' << m_turnFailedCalls << ',' << m_turnTimer.elapsed() << ','
        << m_turnHashBefore.left(12) << ',' << projectHash().left(12) << ',' << outcome << '\n';
}

// ---- AI Fill ---------------------------------------------------------------------------------

namespace {

QJsonObject fillDocumentSchema(bool withTitle)
{
    QJsonObject props{
        {QStringLiteral("json"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("Lottie (Bodymovin 5.x) JSON document text")}}},
        {QStringLiteral("svg"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                     {QStringLiteral("description"), QStringLiteral("SVG document text, for a still")}}},
    };
    if (withTitle)
        props.insert(QStringLiteral("title"),
                     QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                 {QStringLiteral("description"), QStringLiteral("Short clip name, at most 40 characters")}});
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), props}};
}

QString seconds2(double v)
{
    return QString::number(v, 'f', 2);
}

} // namespace

QList<ToolSpec> AssistantController::fillToolSpecs() const
{
    // Everything the chat can do (the user may ask for more than the marked space), plus the two
    // tools that put an animation exactly in the space and keep its versions.
    QList<ToolSpec> out = toolSpecs();
    out.append({QStringLiteral("check_fill"),
                QStringLiteral("Parse a Lottie JSON or SVG document and report what this renderer draws and "
                               "skips (durationSec, width, height, fps, layers, expressions, unsupported, "
                               "hints) without placing it. Give json or svg."),
                fillDocumentSchema(false)});
    out.append({QStringLiteral("fill_region"),
                QStringLiteral("Hand over the animation for the marked space. Dafin places it exactly there "
                               "(or, for a change, replaces the animation that is there) and keeps every "
                               "earlier version. Give json (Lottie text) or svg (SVG text), and a short title. "
                               "Calling it again in this request replaces this request's version instead of "
                               "adding another. Returns the inspect summary of what was placed."),
                fillDocumentSchema(true)});
    return out;
}

QString AssistantController::fillSystemPrompt() const
{
    // The chat's own instructions (rules, frames, the user's editing knowledge), then AI Fill's.
    QString s = systemPrompt();
    s += QStringLiteral(
        "\nThis request comes from AI Fill on the timeline: the user marked a space and typed a request. Do "
        "everything the request asks, with all your tools, including things outside the space (another clip, "
        "the next video, cuts, captions, more animations). For the animation that belongs in the marked space, "
        "use fill_region: it places the document exactly there, or for a change gives the AI Fill clip there a "
        "new version, and Dafin keeps every earlier version. Other animations go where the user said, with the "
        "motion toolbox (add_lottie, add_svg) as usual. If the request is only about other places, you do not "
        "have to use fill_region.\n"
        "Lottie rules for this renderer (Skottie):\n"
        "- Top level: v \"5.7.4\", fr = the project fps, ip 0, op = the frames it should last, w/h = the canvas "
        "size, ddd 0, assets [], layers [...].\n"
        "- Shape layers (ty 4) with groups (ty gr) of rc/el/sh plus fl/st and a tr; text layers (ty 5) with a "
        "top-level fonts list such as {\"list\":[{\"fName\":\"Arial-BoldMT\",\"fFamily\":\"Arial\",\"fStyle\":\"Bold\","
        "\"ascent\":75}]}; Dafin draws them with the system fonts.\n"
        "- No expressions (Skottie renders them static). Every layer has ind, ip, op, st and ks (o, r, p, a, s).\n"
        "- Animate with eased keyframes (i/o tangents). Unless asked otherwise: an entrance in the first "
        "0.3-0.6 s, a hold, and an exit in the last 0.3-0.5 s.\n"
        "- Keep the background transparent so the footage shows through, stay inside 5% safe margins, and keep "
        "text at least 4% of the canvas height and readable against busy footage (a shadow, outline or plate).\n"
        "check_fill parses a document without placing it. After fill_region, if unsupported or expressions is not "
        "empty, fix the document and call fill_region again (that replaces this request's version). The user sees "
        "your last reply on a small card: finish with one or two short sentences saying what you did and where, "
        "no markdown.\n");
    return s;
}

QString AssistantController::fillUserMessage(const QVariantMap &request) const
{
    const auto *app = static_cast<const AppController *>(m_app);
    const drift::Project *project = app->project();
    const double start = request.value(QStringLiteral("start")).toDouble();
    const double end = request.value(QStringLiteral("end")).toDouble();
    const int fps = project->fps() > 0 ? project->fps() : 30;
    const int frames = qMax(1, qRound((end - start) * fps));
    QString m;
    QTextStream out(&m);
    out << "AI Fill request.\n";
    out << "Marked space: " << seconds2(start) << " s to " << seconds2(end) << " s (" << seconds2(end - start)
        << " s long, " << frames << " frames at " << fps << " fps), drawn over track row "
        << request.value(QStringLiteral("track"), -1).toInt() << " (0 = top).\n";
    out << "Canvas: " << project->width() << "x" << project->height() << ".\n";

    const QVariantList under = app->clipsInRange(start, end);
    if (under.isEmpty()) {
        out << "Under the space: nothing.\n";
    } else {
        out << "Under the space:\n";
        for (const QVariant &v : under) {
            const QVariantMap c = v.toMap();
            out << "- track " << c.value(QStringLiteral("track")).toInt() << ", " << c.value(QStringLiteral("kind")).toString()
                << " \"" << c.value(QStringLiteral("name")).toString() << "\" " << seconds2(c.value(QStringLiteral("start")).toDouble())
                << "-" << seconds2(c.value(QStringLiteral("end")).toDouble()) << " s\n";
        }
    }

    const QString clipId = request.value(QStringLiteral("clipId")).toString();
    if (!clipId.isEmpty()) {
        const QVariantMap info = app->aiFillVersions(clipId);
        const QVariantList versions = info.value(QStringLiteral("versions")).toList();
        const int current = info.value(QStringLiteral("current")).toInt();
        out << "There is already an AI Fill animation in this space (version " << current + 1 << " of "
            << versions.size() << "). Earlier requests for it:\n";
        for (int i = 0; i < versions.size(); ++i)
            out << "- v" << i + 1 << ": \"" << versions.at(i).toMap().value(QStringLiteral("prompt")).toString().left(300) << "\"\n";
        QString doc = app->vectorSourceText(info.value(QStringLiteral("track")).toInt(), info.value(QStringLiteral("index")).toInt());
        if (doc.size() > kMaxFillDocumentChars)
            doc = doc.left(kMaxFillDocumentChars) + QStringLiteral("\n…(cut off: the document is longer)");
        if (request.value(QStringLiteral("retry")).toBool()) {
            out << "The user wants a DIFFERENT take on the same request: new layout, motion and look, same message. "
                   "The current document, for reference only:\n";
        } else {
            out << "The user wants a CHANGE to the version showing now. Start from its document and change only what "
                   "they ask:\n";
        }
        out << doc << "\n";
    }
    out << "Request (it may also ask for things outside the space; do those too): \""
        << request.value(QStringLiteral("prompt")).toString() << "\"\n";
    return m;
}

bool AssistantController::generateFill(const QVariantMap &request)
{
    const QString prompt = request.value(QStringLiteral("prompt")).toString().trimmed().left(2000);
    const double start = request.value(QStringLiteral("start")).toDouble();
    const double end = request.value(QStringLiteral("end")).toDouble();
    const QString clipId = request.value(QStringLiteral("clipId")).toString();
    QVariantMap state{{QStringLiteral("state"), QStringLiteral("error")},
                      {QStringLiteral("status"), QString()},
                      {QStringLiteral("error"), QString()},
                      {QStringLiteral("setup"), false},
                      {QStringLiteral("clipId"), clipId},
                      {QStringLiteral("text"), QString()},
                      {QStringLiteral("prompt"), prompt},
                      {QStringLiteral("start"), start},
                      {QStringLiteral("end"), end},
                      {QStringLiteral("track"), request.value(QStringLiteral("track"), -1).toInt()}};
    auto refuse = [&](const QString &why, bool setup = false) {
        state.insert(QStringLiteral("error"), why);
        state.insert(QStringLiteral("setup"), setup);
        m_fillState = state;
        emit fillChanged();
        return false;
    };
    if (m_busy || m_toolRunning)
        return refuse(tr("AI Assist is busy with another request. Wait for it, or press Stop."));
    if (!ready())
        return refuse(tr("Set up AI Assist first: %1").arg(notReadyReason()), true);
    if (prompt.isEmpty())
        return refuse(tr("Describe what should go in the marked space."));
    if (!(start >= 0.0) || !(end - start >= 0.1))
        return refuse(tr("The marked space is too short. Draw it again."));
    if (!m_app || !m_app->vectorSupportAvailable())
        return refuse(tr("This build of Dafin cannot draw animations."));
    if (!clipId.isEmpty() && !m_app->aiFillVersions(clipId).value(QStringLiteral("ok")).toBool())
        return refuse(tr("The animation to change is gone. Mark the space again."));

    QVariantMap job = request;
    job.insert(QStringLiteral("prompt"), prompt);
    m_fill = {};
    m_fill.active = true;
    m_fill.request = job;
    m_fill.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // The chat waits aside; this request is a short conversation of its own.
    m_chatAside = m_history;
    m_history.clear();
    ChatMessage user;
    user.role = ChatMessage::Role::User;
    user.text = fillUserMessage(job);
    m_history.append(user);

    state.insert(QStringLiteral("state"), QStringLiteral("working"));
    state.insert(QStringLiteral("status"), tr("Designing the animation…"));
    m_fillState = state;
    emit fillChanged();
    beginTurn();
    requestNext();
    return true;
}

void AssistantController::runFillTool(const ToolCall &call)
{
    ++m_turnToolCalls;
    QString source;
    QString kind = QStringLiteral("lottie");
    const QJsonValue json = call.args.value(QStringLiteral("json"));
    if (json.isObject())
        source = QString::fromUtf8(QJsonDocument(json.toObject()).toJson(QJsonDocument::Compact));
    else
        source = json.toString();
    if (source.trimmed().isEmpty()) {
        source = call.args.value(QStringLiteral("svg")).toString();
        kind = QStringLiteral("svg");
    }
    const QString trimmed = source.trimmed();
    QString error;
    if (trimmed.isEmpty())
        error = QStringLiteral("json (Lottie text or a .json path) or svg (SVG text or a .svg path) required");

    QVariantMap reply;
    if (error.isEmpty()) {
        if (call.name == QLatin1String("check_fill")) {
            reply = m_app->inspectVector(trimmed, kind);
        } else {
            QVariantMap region = m_fill.request;
            region.insert(QStringLiteral("requestId"), m_fill.requestId);
            // A second hand-over in this request updates the clip the first one placed.
            if (!m_fill.clipId.isEmpty())
                region.insert(QStringLiteral("clipId"), m_fill.clipId);
            region.insert(QStringLiteral("title"), call.args.value(QStringLiteral("title")).toString());
            reply = m_app->commitAiFill(region, trimmed, kind);
        }
        if (!reply.value(QStringLiteral("ok")).toBool())
            error = reply.value(QStringLiteral("error")).toString();
    }
    if (!error.isEmpty()) {
        ++m_turnFailedCalls;
        m_fill.lastError = error.left(400);
        addToolResult(call, QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("ok"), false},
                                                                         {QStringLiteral("error"), error}})
                                                  .toJson(QJsonDocument::Compact)),
                      true);
        addMessage(QStringLiteral("step"), call.name, {{QStringLiteral("ok"), false}, {QStringLiteral("detail"), error}});
        return;
    }
    QString text = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(reply)).toJson(QJsonDocument::Compact));
    if (text.size() > kMaxToolResultChars)
        text = text.left(kMaxToolResultChars) + QStringLiteral("…(truncated)");
    if (call.name == QLatin1String("fill_region")) {
        m_fill.clipId = reply.value(QStringLiteral("id")).toString();
        m_fill.lastError.clear();
        updateFill({{QStringLiteral("clipId"), m_fill.clipId},
                    {QStringLiteral("version"), reply.value(QStringLiteral("version"))},
                    {QStringLiteral("versions"), reply.value(QStringLiteral("versions"))}});
        text += QStringLiteral("\nPlaced in the marked space as version %1. If unsupported or expressions is not "
                               "empty, fix the document and call fill_region again (that replaces this version). "
                               "Otherwise finish with one short sentence.")
                    .arg(reply.value(QStringLiteral("version")).toInt());
    }
    addToolResult(call, text, false);
    addMessage(QStringLiteral("step"), call.name, {{QStringLiteral("ok"), true}});
}

void AssistantController::updateFill(const QVariantMap &changes)
{
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        m_fillState.insert(it.key(), it.value());
    emit fillChanged();
}

void AssistantController::finishFill(const QString &outcome)
{
    const FillJob job = m_fill;
    m_fill = {};
    m_history = m_chatAside;
    m_chatAside.clear();

    const bool changed = projectHash() != m_turnHashBefore;
    const bool placed = !job.clipId.isEmpty() && m_app->aiFillVersions(job.clipId).value(QStringLiteral("ok")).toBool();
    QVariantMap changes{{QStringLiteral("status"), QString()},
                        {QStringLiteral("error"), QString()},
                        {QStringLiteral("changed"), changed},
                        {QStringLiteral("clipId"), placed ? job.clipId : QString()},
                        {QStringLiteral("text"), job.lastText}};
    if (placed) {
        const QVariantMap info = m_app->aiFillVersions(job.clipId);
        changes.insert(QStringLiteral("version"), info.value(QStringLiteral("current")).toInt() + 1);
        changes.insert(QStringLiteral("versions"), info.value(QStringLiteral("versions")).toList().size());
        changes.insert(QStringLiteral("start"), info.value(QStringLiteral("start")));
        changes.insert(QStringLiteral("end"), info.value(QStringLiteral("end")));
    }
    if (outcome == QLatin1String("ok")) {
        changes.insert(QStringLiteral("state"), QStringLiteral("done"));
    } else if (outcome == QLatin1String("stopped")) {
        // What was already done stays (and can be undone); say so rather than pretend nothing happened.
        changes.insert(QStringLiteral("state"), changed ? QStringLiteral("done") : QStringLiteral("stopped"));
        if (changed)
            changes.insert(QStringLiteral("text"), tr("Stopped. What was done so far is on the timeline."));
    } else if (changed && outcome == QLatin1String("limit")) {
        changes.insert(QStringLiteral("state"), QStringLiteral("done"));
        changes.insert(QStringLiteral("text"), tr("Stopped after %1 steps. What was done so far is on the timeline; "
                                                  "ask again to continue.").arg(kMaxToolCallsPerTurn));
    } else {
        QString why;
        if (!job.lastError.isEmpty())
            why = tr("It didn't work: %1").arg(job.lastError);
        else if (outcome == QLatin1String("limit"))
            why = tr("The AI took too many steps without finishing. Try again, perhaps with a simpler description.");
        else
            why = tr("It didn't work. Try again.");
        if (changed)
            why += QLatin1Char(' ') + tr("Some changes were already made; Ctrl+Z undoes them.");
        changes.insert(QStringLiteral("state"), QStringLiteral("error"));
        changes.insert(QStringLiteral("error"), why);
    }
    updateFill(changes);
}

void AssistantController::dismissFill()
{
    if (m_fill.active)
        return;
    m_fillState.clear();
    emit fillChanged();
}

} // namespace dafin::assistant
