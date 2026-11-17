#pragma once

#include "app/assistant/LlmProtocol.h"

#include <QNetworkAccessManager>
#include <QPointer>

#include <functional>

class QNetworkReply;

namespace dafin::assistant {

// Sends one request. Swappable so tests run the whole assistant loop with no network.
class LlmTransport
{
public:
    using Done = std::function<void(int httpStatus, const QByteArray &body, const QString &networkError)>;
    virtual ~LlmTransport() = default;
    virtual void post(const LlmRequest &request, Done done) = 0;
    virtual void abort() = 0;
};

class NetworkTransport : public LlmTransport
{
public:
    void post(const LlmRequest &request, Done done) override;
    void abort() override;

private:
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
};

} // namespace dafin::assistant
