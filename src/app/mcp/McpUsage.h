#pragma once

// What Dafin itself can measure about an assistant that is connected over MCP: how many tool
// calls, how long they took, which failed and were then repeated, and how big Dafin's replies
// were. It never has the assistant's own token counts, cached tokens, price or model (the
// client does not send them), so none of those are here and the UI says so instead of
// showing a made-up number. The only token figure is an estimate of Dafin's text replies,
// and it is named as one.

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

namespace drift::mcp {

class McpUsage
{
public:
    // Rough rule of thumb for English/JSON text: four bytes per token.
    static qint64 approxTokens(qint64 textBytes) { return (textBytes + 3) / 4; }

    // Splits one tool result into the bytes of its text parts and of its images (base64).
    static void measure(const QJsonObject &result, qint64 *textBytes, qint64 *imageBytes)
    {
        qint64 text = 0;
        qint64 image = 0;
        const QJsonArray content = result.value(QStringLiteral("content")).toArray();
        for (const QJsonValue &v : content) {
            const QJsonObject part = v.toObject();
            const QString type = part.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("image"))
                image += part.value(QStringLiteral("data")).toString().size();
            else if (type == QLatin1String("text"))
                text += part.value(QStringLiteral("text")).toString().toUtf8().size();
            else
                text += QJsonDocument(part).toJson(QJsonDocument::Compact).size();
        }
        if (textBytes)
            *textBytes = text;
        if (imageBytes)
            *imageBytes = image;
    }

    void setClient(const QString &name, const QString &version)
    {
        m_client = name.trimmed().left(80);
        m_clientVersion = version.trimmed().left(40);
    }
    QString client() const { return m_client; }
    QString clientVersion() const { return m_clientVersion; }

    // A call counts as a retry when the same tool was already called with the same arguments
    // and that earlier call failed: the assistant is trying again, and each try costs it a turn.
    void record(const QString &tool, const QJsonObject &args, const QJsonObject &result, bool ok,
                qint64 ms)
    {
        Row &row = m_rows[tool];
        row.tool = tool;
        ++row.calls;
        if (!ok)
            ++row.errors;
        row.totalMs += ms;
        row.maxMs = std::max(row.maxMs, ms);

        const QString key = tool + QLatin1Char('\n')
                            + QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        if (m_failedKeys.contains(key)) {
            ++row.retries;
            if (ok)
                m_failedKeys.remove(key);
        } else if (!ok) {
            if (m_failedKeys.size() >= kMaxFailedKeys)
                m_failedKeys.clear();
            m_failedKeys.insert(key);
        }

        qint64 text = 0;
        qint64 image = 0;
        measure(result, &text, &image);
        row.textBytes += text;
        row.imageBytes += image;
    }

    void reset()
    {
        m_rows.clear();
        m_failedKeys.clear();
    }

    bool isEmpty() const { return m_rows.isEmpty(); }

    // One entry per tool, the biggest repliers first, since those are the ones worth trimming.
    QVariantList rows() const
    {
        QList<Row> sorted = m_rows.values();
        std::sort(sorted.begin(), sorted.end(), [](const Row &a, const Row &b) {
            const qint64 sa = a.textBytes + a.imageBytes;
            const qint64 sb = b.textBytes + b.imageBytes;
            return sa != sb ? sa > sb : a.tool < b.tool;
        });
        QVariantList out;
        out.reserve(sorted.size());
        for (const Row &r : sorted) {
            out.append(QVariantMap{
                {QStringLiteral("tool"), r.tool},
                {QStringLiteral("calls"), r.calls},
                {QStringLiteral("errors"), r.errors},
                {QStringLiteral("retries"), r.retries},
                {QStringLiteral("totalMs"), r.totalMs},
                {QStringLiteral("avgMs"), r.calls > 0 ? r.totalMs / r.calls : 0},
                {QStringLiteral("maxMs"), r.maxMs},
                {QStringLiteral("textBytes"), r.textBytes},
                {QStringLiteral("imageBytes"), r.imageBytes},
                {QStringLiteral("approxTokens"), approxTokens(r.textBytes)},
            });
        }
        return out;
    }

    QVariantMap totals() const
    {
        qint64 calls = 0, errors = 0, retries = 0, ms = 0, text = 0, image = 0;
        for (const Row &r : m_rows) {
            calls += r.calls;
            errors += r.errors;
            retries += r.retries;
            ms += r.totalMs;
            text += r.textBytes;
            image += r.imageBytes;
        }
        return {
            {QStringLiteral("calls"), calls},
            {QStringLiteral("errors"), errors},
            {QStringLiteral("retries"), retries},
            {QStringLiteral("totalMs"), ms},
            {QStringLiteral("textBytes"), text},
            {QStringLiteral("imageBytes"), image},
            {QStringLiteral("approxTokens"), approxTokens(text)},
        };
    }

    // Plain text for pasting into a chat or a bug report.
    QString report() const
    {
        const QVariantMap t = totals();
        QStringList lines;
        lines << QStringLiteral("Dafin AI usage (measured by Dafin)");
        lines << QStringLiteral("Client: %1")
                     .arg(m_client.isEmpty()
                              ? QStringLiteral("not reported")
                              : (m_clientVersion.isEmpty() ? m_client
                                                           : m_client + QLatin1Char(' ') + m_clientVersion));
        lines << QStringLiteral("Tool calls: %1, failed: %2, repeated after a failure: %3, "
                                "time spent in Dafin: %4 ms")
                     .arg(t.value(QStringLiteral("calls")).toLongLong())
                     .arg(t.value(QStringLiteral("errors")).toLongLong())
                     .arg(t.value(QStringLiteral("retries")).toLongLong())
                     .arg(t.value(QStringLiteral("totalMs")).toLongLong());
        lines << QStringLiteral("Dafin's replies: %1 bytes of text (about %2 tokens, estimated), "
                                "%3 bytes of images")
                     .arg(t.value(QStringLiteral("textBytes")).toLongLong())
                     .arg(t.value(QStringLiteral("approxTokens")).toLongLong())
                     .arg(t.value(QStringLiteral("imageBytes")).toLongLong());
        lines << QStringLiteral("The assistant's own input/output/cached tokens and cost are not "
                                "sent to Dafin.");
        lines << QString();
        lines << QStringLiteral("tool | calls | failed | repeated | avg ms | max ms | text bytes | image bytes");
        for (const QVariant &v : rows()) {
            const QVariantMap r = v.toMap();
            lines << QStringLiteral("%1 | %2 | %3 | %4 | %5 | %6 | %7 | %8")
                         .arg(r.value(QStringLiteral("tool")).toString())
                         .arg(r.value(QStringLiteral("calls")).toLongLong())
                         .arg(r.value(QStringLiteral("errors")).toLongLong())
                         .arg(r.value(QStringLiteral("retries")).toLongLong())
                         .arg(r.value(QStringLiteral("avgMs")).toLongLong())
                         .arg(r.value(QStringLiteral("maxMs")).toLongLong())
                         .arg(r.value(QStringLiteral("textBytes")).toLongLong())
                         .arg(r.value(QStringLiteral("imageBytes")).toLongLong());
        }
        return lines.join(QLatin1Char('\n'));
    }

private:
    struct Row
    {
        QString tool;
        qint64 calls = 0;
        qint64 errors = 0;
        qint64 retries = 0;
        qint64 totalMs = 0;
        qint64 maxMs = 0;
        qint64 textBytes = 0;
        qint64 imageBytes = 0;
    };

    static constexpr int kMaxFailedKeys = 200;

    QHash<QString, Row> m_rows;
    QSet<QString> m_failedKeys;
    QString m_client;
    QString m_clientVersion;
};

} // namespace drift::mcp
