#include "app/assistant/LlmTransport.h"

#include <QNetworkReply>
#include <QNetworkRequest>

namespace dafin::assistant {

void NetworkTransport::post(const LlmRequest &request, Done done)
{
    abort();
    QNetworkRequest req(request.url);
    for (const auto &[name, value] : request.headers)
        req.setRawHeader(name, value);
    req.setTransferTimeout(180000); // a long tool-planning answer can take a while
    QNetworkReply *reply = m_nam.post(req, request.body);
    m_reply = reply;
    QObject::connect(reply, &QNetworkReply::finished, &m_nam, [this, reply, done] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool aborted = reply->error() == QNetworkReply::OperationCanceledError;
        const QString netError = reply->error() == QNetworkReply::NoError || status > 0 ? QString() : reply->errorString();
        reply->deleteLater();
        if (aborted)
            return;
        // Handed over on the next turn of the event loop: the tool loop that follows can run
        // for a long time (even open the confirm dialog's own loop), and must not run inside
        // this handler while the reply it belongs to is being deleted. Dropped if the
        // transport is destroyed first.
        QMetaObject::invokeMethod(
            &m_nam, [done, status, body, netError] { done(status, body, netError); }, Qt::QueuedConnection);
    });
}

void NetworkTransport::abort()
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply.clear();
        reply->abort();
    }
}

} // namespace dafin::assistant
