#include "app/assistant/ModelCatalog.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <algorithm>

namespace dafin::assistant {

namespace {

constexpr int kMaxListed = 60;

QString tr(const char *text)
{
    return QCoreApplication::translate("ModelCatalog", text);
}

QVariantMap option(const QString &value, const QString &label, const QString &hint,
                   bool recommended = false)
{
    return QVariantMap{{QStringLiteral("value"), value},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("hint"), hint},
                       {QStringLiteral("recommended"), recommended}};
}

QString claudeHint(const QString &id)
{
    if (id.contains(QLatin1String("sonnet")))
        return tr("recommended for most edits");
    if (id.contains(QLatin1String("opus")))
        return tr("harder jobs, uses more of your limit");
    if (id.contains(QLatin1String("haiku")))
        return tr("fastest and cheapest");
    return {};
}

QString openAiHint(const QString &id)
{
    if (id.contains(QLatin1String("nano")))
        return tr("smallest, fastest");
    if (id.contains(QLatin1String("mini")))
        return tr("faster and cheaper");
    return {};
}

bool canChat(const QString &id)
{
    static const char *const notChat[] = {"embed", "tts", "whisper", "dall-e", "moderation",
                                          "transcribe", "realtime", "image", "audio", "search",
                                          "davinci", "babbage", "rerank", "sora"};
    const QString lower = id.toLower();
    for (const char *word : notChat) {
        if (lower.contains(QLatin1String(word)))
            return false;
    }
    return true;
}

} // namespace

QVariantList builtInModels(Provider provider, bool local)
{
    if (provider == Provider::Anthropic) {
        return {option(QStringLiteral("claude-sonnet-5-5"), QStringLiteral("Claude Sonnet 5.5"),
                       claudeHint(QStringLiteral("sonnet")), true),
                option(QStringLiteral("claude-opus-5-5"), QStringLiteral("Claude Opus 5.5"),
                       claudeHint(QStringLiteral("opus"))),
                option(QStringLiteral("claude-haiku-5-5"), QStringLiteral("Claude Haiku 5.5"),
                       claudeHint(QStringLiteral("haiku")))};
    }
    if (local)
        return {};
    return {option(QStringLiteral("gpt-5-mini"), QStringLiteral("gpt-5-mini"),
                   openAiHint(QStringLiteral("mini")), true),
            option(QStringLiteral("gpt-5"), QStringLiteral("gpt-5"), tr("larger, uses more of your credit"))};
}

QVariantList parseModelList(Provider provider, const QByteArray &json)
{
    const QJsonArray data = QJsonDocument::fromJson(json).object().value(QStringLiteral("data")).toArray();
    struct Entry
    {
        QString id;
        QString name;
        qint64 created = 0;
        int order = 0;
    };
    QList<Entry> entries;
    int order = 0;
    for (const QJsonValue &value : data) {
        const QJsonObject model = value.toObject();
        const QString id = model.value(QStringLiteral("id")).toString().trimmed();
        if (id.isEmpty() || !canChat(id))
            continue;
        entries.append({id, model.value(QStringLiteral("display_name")).toString(),
                        static_cast<qint64>(model.value(QStringLiteral("created")).toDouble()), order++});
    }
    // Anthropic already lists newest first; OpenAI-compatible servers list in no useful order.
    if (provider == Provider::OpenAiCompat) {
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return a.created > b.created;
        });
    }
    QVariantList options;
    bool recommended = false;
    for (const Entry &entry : std::as_const(entries)) {
        if (options.size() >= kMaxListed)
            break;
        if (provider == Provider::Anthropic) {
            const bool pick = !recommended && entry.id.contains(QLatin1String("sonnet"));
            recommended = recommended || pick;
            options.append(option(entry.id, entry.name.isEmpty() ? entry.id : entry.name,
                                  claudeHint(entry.id), pick));
        } else {
            options.append(option(entry.id, entry.id, openAiHint(entry.id)));
        }
    }
    return options;
}

QVariantList withCurrent(const QVariantList &options, const QString &current)
{
    if (current.isEmpty())
        return options;
    for (const QVariant &value : options) {
        if (value.toMap().value(QStringLiteral("value")).toString() == current)
            return options;
    }
    QVariantList out{option(current, current, tr("your choice"))};
    out.append(options);
    return out;
}

QVariantList codexModels()
{
    return {option(QString(), tr("Codex's own default"), tr("recommended"), true),
            option(QStringLiteral("gpt-5-codex"), QStringLiteral("gpt-5-codex"), tr("made for coding agents")),
            option(QStringLiteral("gpt-5"), QStringLiteral("gpt-5"), tr("larger, uses more of your limit")),
            option(QStringLiteral("gpt-5-mini"), QStringLiteral("gpt-5-mini"), openAiHint(QStringLiteral("mini")))};
}

} // namespace dafin::assistant
