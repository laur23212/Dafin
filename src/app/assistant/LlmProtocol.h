#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

// Dafin AI Assist: the provider-neutral conversation and the two wire formats it is sent in
// (Anthropic Messages API, OpenAI-compatible Chat Completions — OpenAI itself, or a local
// server such as Ollama or LM Studio). Pure functions, no network: the controller sends what
// buildRequest() returns and hands the reply bytes to parseReply().
namespace dafin::assistant {

enum class Provider { Anthropic, OpenAiCompat };

Provider providerFromId(const QString &id);
QString providerId(Provider provider);

struct ToolCall
{
    QString id;
    QString name;
    QJsonObject args;
};

struct ImagePart
{
    QString mime;
    QString base64;
};

struct ChatMessage
{
    enum class Role { User, Assistant, Tool };
    Role role = Role::User;
    QString text;
    QList<ToolCall> toolCalls; // Assistant: tools it asked for
    QString toolCallId;        // Tool: which call this answers
    QString toolName;
    QList<ImagePart> images;   // Tool: pictures the tool returned
    bool isError = false;      // Tool
};

struct ToolSpec
{
    QString name;
    QString description;
    QJsonObject schema; // JSON Schema of the arguments
};

// Provider-reported token counts. -1 = the provider did not report it (unknown, not zero).
struct Usage
{
    qint64 input = -1;
    qint64 output = -1;
    qint64 cacheRead = -1;
    qint64 cacheWrite = -1;
};

struct LlmRequest
{
    QUrl url;
    QList<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
};

struct LlmReply
{
    bool ok = false;
    QString error;
    QString text;
    QList<ToolCall> toolCalls;
    Usage usage;
    QString stopReason;
};

struct RequestOptions
{
    Provider provider = Provider::Anthropic;
    QString model;
    QString baseUrl; // OpenAI-compatible only; empty = https://api.openai.com/v1
    QString apiKey;  // may be empty for a local OpenAI-compatible server
    QString system;
    int maxTokens = 4096;
};

LlmRequest buildRequest(const RequestOptions &options, const QList<ToolSpec> &tools,
                        const QList<ChatMessage> &history);
LlmReply parseReply(Provider provider, int httpStatus, const QByteArray &body);

} // namespace dafin::assistant
