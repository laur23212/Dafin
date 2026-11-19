#pragma once

#include "app/assistant/LlmProtocol.h"

#include <QByteArray>
#include <QString>
#include <QVariantList>

// Dafin AI Assist: what the model dropdowns list. Each option is a QVariantMap
// {value: model id, label: what the dropdown shows, hint: one short line, recommended: bool}.
// "Other…" (type a name) is added by the QML, not here. Pure functions, no network.
namespace dafin::assistant {

// Shown before the provider's own list has loaded, or when it cannot be loaded. Empty for a
// local OpenAI-compatible server (`local`), whose models only it knows.
QVariantList builtInModels(Provider provider, bool local);
// The provider's /models reply (Anthropic: {data:[{id, display_name}]}; OpenAI-compatible,
// Ollama, LM Studio: {data:[{id, created}]}) as options. Models that cannot chat (embeddings,
// speech, images, moderation) are left out. Empty when the reply is not such a list.
QVariantList parseModelList(Provider provider, const QByteArray &json);
// `options` with `current` added at the top when it is not in them, so a saved model that the
// list does not have still shows as selected.
QVariantList withCurrent(const QVariantList &options, const QString &current);
// For the Codex launch command ("" = Codex's own default).
QVariantList codexModels();

} // namespace dafin::assistant
