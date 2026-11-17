#include "app/assistant/LlmProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace dafin::assistant {

namespace {

constexpr auto kAnthropicUrl = "https://api.anthropic.com/v1/messages";
constexpr auto kAnthropicVersion = "2023-06-01";
constexpr auto kOpenAiBase = "https://api.openai.com/v1";

QString argsJson(const QJsonObject &args)
{
    return QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
}

QJsonObject parseArgs(const QJsonValue &value)
{
    if (value.isObject())
        return value.toObject();
    // OpenAI sends arguments as a JSON string.
    const QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

qint64 count(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? static_cast<qint64>(v.toDouble()) : -1;
}

QString errorText(int status, const QJsonObject &root, const QByteArray &body)
{
    const QJsonValue e = root.value(QStringLiteral("error"));
    QString message = e.isObject() ? e.toObject().value(QStringLiteral("message")).toString() : e.toString();
    if (message.isEmpty())
        message = QString::fromUtf8(body.left(300));
    return QStringLiteral("HTTP %1: %2").arg(status).arg(message);
}

// ---- Anthropic ----

QJsonArray anthropicMessages(const QList<ChatMessage> &history)
{
    QJsonArray out;
    QJsonArray pendingResults; // tool results go back together in one user turn
    auto flushResults = [&] {
        if (pendingResults.isEmpty())
            return;
        out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                               {QStringLiteral("content"), pendingResults}});
        pendingResults = {};
    };
    for (const ChatMessage &m : history) {
        if (m.role == ChatMessage::Role::Tool) {
            QJsonArray content;
            if (!m.text.isEmpty())
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                           {QStringLiteral("text"), m.text}});
            for (const ImagePart &img : m.images)
                content.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("source"), QJsonObject{{QStringLiteral("type"), QStringLiteral("base64")},
                                                           {QStringLiteral("media_type"), img.mime},
                                                           {QStringLiteral("data"), img.base64}}}});
            QJsonObject block{{QStringLiteral("type"), QStringLiteral("tool_result")},
                              {QStringLiteral("tool_use_id"), m.toolCallId},
                              {QStringLiteral("content"), content}};
            if (m.isError)
                block.insert(QStringLiteral("is_error"), true);
            pendingResults.append(block);
            continue;
        }
        flushResults();
        if (m.role == ChatMessage::Role::User) {
            const QJsonObject textBlock{{QStringLiteral("type"), QStringLiteral("text")},
                                        {QStringLiteral("text"), m.text}};
            // After tool results (e.g. a stopped turn) the next user text joins that same user
            // turn: roles must alternate.
            if (!out.isEmpty() && out.last().toObject().value(QStringLiteral("role")).toString() == QLatin1String("user")) {
                QJsonObject last = out.last().toObject();
                QJsonArray content = last.value(QStringLiteral("content")).toArray();
                content.append(textBlock);
                last.insert(QStringLiteral("content"), content);
                out.replace(out.size() - 1, last);
                continue;
            }
            out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                   {QStringLiteral("content"), QJsonArray{textBlock}}});
            continue;
        }
        QJsonArray content;
        if (!m.text.isEmpty())
            content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                       {QStringLiteral("text"), m.text}});
        for (const ToolCall &c : m.toolCalls)
            content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                       {QStringLiteral("id"), c.id},
                                       {QStringLiteral("name"), c.name},
                                       {QStringLiteral("input"), c.args}});
        out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                               {QStringLiteral("content"), content}});
    }
    flushResults();
    return out;
}

LlmRequest anthropicRequest(const RequestOptions &o, const QList<ToolSpec> &tools,
                            const QList<ChatMessage> &history)
{
    QJsonArray toolArray;
    for (const ToolSpec &t : tools)
        toolArray.append(QJsonObject{{QStringLiteral("name"), t.name},
                                     {QStringLiteral("description"), t.description},
                                     {QStringLiteral("input_schema"), t.schema}});
    // The system block is marked for prompt caching: tools + system are the same on every
    // round of a turn, so later rounds read them from the cache (reported as cacheRead).
    const QJsonArray system{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                        {QStringLiteral("text"), o.system},
                                        {QStringLiteral("cache_control"),
                                         QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}}}}};
    QJsonObject body{{QStringLiteral("model"), o.model},
                     {QStringLiteral("max_tokens"), o.maxTokens},
                     {QStringLiteral("system"), system},
                     {QStringLiteral("messages"), anthropicMessages(history)}};
    if (!toolArray.isEmpty())
        body.insert(QStringLiteral("tools"), toolArray);
    LlmRequest r;
    r.url = QUrl(QString::fromLatin1(kAnthropicUrl));
    r.headers = {{"content-type", "application/json"},
                 {"anthropic-version", kAnthropicVersion},
                 {"x-api-key", o.apiKey.toUtf8()}};
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return r;
}

LlmReply anthropicReply(int status, const QJsonObject &root, const QByteArray &body)
{
    LlmReply reply;
    if (status < 200 || status >= 300 || root.value(QStringLiteral("type")).toString() == QLatin1String("error")) {
        reply.error = errorText(status, root, body);
        return reply;
    }
    QStringList texts;
    for (const QJsonValue &v : root.value(QStringLiteral("content")).toArray()) {
        const QJsonObject block = v.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text"))
            texts.append(block.value(QStringLiteral("text")).toString());
        else if (type == QLatin1String("tool_use"))
            reply.toolCalls.append({block.value(QStringLiteral("id")).toString(),
                                    block.value(QStringLiteral("name")).toString(),
                                    block.value(QStringLiteral("input")).toObject()});
    }
    reply.text = texts.join(QStringLiteral("\n\n"));
    const QJsonObject u = root.value(QStringLiteral("usage")).toObject();
    reply.usage.input = count(u, "input_tokens");
    reply.usage.output = count(u, "output_tokens");
    reply.usage.cacheRead = count(u, "cache_read_input_tokens");
    reply.usage.cacheWrite = count(u, "cache_creation_input_tokens");
    reply.stopReason = root.value(QStringLiteral("stop_reason")).toString();
    reply.ok = true;
    return reply;
}

// ---- OpenAI-compatible ----

QJsonArray openAiMessages(const QString &system, const QList<ChatMessage> &history)
{
    QJsonArray out{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), system}}};
    QList<ImagePart> pendingImages; // tool messages are text-only here; pictures follow as a user turn
    auto flushImages = [&] {
        if (pendingImages.isEmpty())
            return;
        QJsonArray parts{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                     {QStringLiteral("text"), QStringLiteral("Images returned by the tools above.")}}};
        for (const ImagePart &img : std::as_const(pendingImages))
            parts.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("image_url")},
                {QStringLiteral("image_url"),
                 QJsonObject{{QStringLiteral("url"), QStringLiteral("data:%1;base64,%2").arg(img.mime, img.base64)}}}});
        out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), parts}});
        pendingImages.clear();
    };
    for (const ChatMessage &m : history) {
        if (m.role == ChatMessage::Role::Tool) {
            QString text = m.text;
            if (m.isError && !text.startsWith(QLatin1String("Error")))
                text.prepend(QStringLiteral("Error: "));
            out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                   {QStringLiteral("tool_call_id"), m.toolCallId},
                                   {QStringLiteral("content"), text}});
            pendingImages += m.images;
            continue;
        }
        flushImages();
        if (m.role == ChatMessage::Role::User) {
            out.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), m.text}});
            continue;
        }
        QJsonObject msg{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), m.text.isEmpty() ? QJsonValue() : QJsonValue(m.text)}};
        if (!m.toolCalls.isEmpty()) {
            QJsonArray calls;
            for (const ToolCall &c : m.toolCalls)
                calls.append(QJsonObject{{QStringLiteral("id"), c.id},
                                         {QStringLiteral("type"), QStringLiteral("function")},
                                         {QStringLiteral("function"),
                                          QJsonObject{{QStringLiteral("name"), c.name},
                                                      {QStringLiteral("arguments"), argsJson(c.args)}}}});
            msg.insert(QStringLiteral("tool_calls"), calls);
        }
        out.append(msg);
    }
    flushImages();
    return out;
}

LlmRequest openAiRequest(const RequestOptions &o, const QList<ToolSpec> &tools, const QList<ChatMessage> &history)
{
    QJsonArray toolArray;
    for (const ToolSpec &t : tools)
        toolArray.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                                     {QStringLiteral("function"),
                                      QJsonObject{{QStringLiteral("name"), t.name},
                                                  {QStringLiteral("description"), t.description},
                                                  {QStringLiteral("parameters"), t.schema}}}});
    QJsonObject body{{QStringLiteral("model"), o.model},
                     {QStringLiteral("messages"), openAiMessages(o.system, history)},
                     {QStringLiteral("max_tokens"), o.maxTokens}};
    if (!toolArray.isEmpty())
        body.insert(QStringLiteral("tools"), toolArray);
    QString base = o.baseUrl.trimmed().isEmpty() ? QString::fromLatin1(kOpenAiBase) : o.baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    LlmRequest r;
    r.url = QUrl(base + QStringLiteral("/chat/completions"));
    r.headers = {{"content-type", "application/json"}};
    if (!o.apiKey.isEmpty())
        r.headers.append({"authorization", "Bearer " + o.apiKey.toUtf8()});
    r.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return r;
}

LlmReply openAiReply(int status, const QJsonObject &root, const QByteArray &body)
{
    LlmReply reply;
    if (status < 200 || status >= 300 || root.contains(QStringLiteral("error"))) {
        reply.error = errorText(status, root, body);
        return reply;
    }
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        reply.error = QStringLiteral("The server returned no answer");
        return reply;
    }
    const QJsonObject choice = choices.first().toObject();
    const QJsonObject msg = choice.value(QStringLiteral("message")).toObject();
    reply.text = msg.value(QStringLiteral("content")).toString();
    for (const QJsonValue &v : msg.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject call = v.toObject();
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        reply.toolCalls.append({call.value(QStringLiteral("id")).toString(),
                                fn.value(QStringLiteral("name")).toString(),
                                parseArgs(fn.value(QStringLiteral("arguments")))});
    }
    const QJsonObject u = root.value(QStringLiteral("usage")).toObject();
    reply.usage.input = count(u, "prompt_tokens");
    reply.usage.output = count(u, "completion_tokens");
    reply.usage.cacheRead = count(u.value(QStringLiteral("prompt_tokens_details")).toObject(), "cached_tokens");
    reply.stopReason = choice.value(QStringLiteral("finish_reason")).toString();
    reply.ok = true;
    return reply;
}

} // namespace

Provider providerFromId(const QString &id)
{
    return id == QLatin1String("openai") ? Provider::OpenAiCompat : Provider::Anthropic;
}

QString providerId(Provider provider)
{
    return provider == Provider::OpenAiCompat ? QStringLiteral("openai") : QStringLiteral("anthropic");
}

LlmRequest buildRequest(const RequestOptions &options, const QList<ToolSpec> &tools,
                        const QList<ChatMessage> &history)
{
    return options.provider == Provider::Anthropic ? anthropicRequest(options, tools, history)
                                                   : openAiRequest(options, tools, history);
}

LlmReply parseReply(Provider provider, int httpStatus, const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        LlmReply reply;
        reply.error = httpStatus == 0 ? QStringLiteral("No response from the server")
                                      : QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(QString::fromUtf8(body.left(300)));
        return reply;
    }
    return provider == Provider::Anthropic ? anthropicReply(httpStatus, doc.object(), body)
                                           : openAiReply(httpStatus, doc.object(), body);
}

} // namespace dafin::assistant
