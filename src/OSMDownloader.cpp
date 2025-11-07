#include "OSMDownloader.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {
QString buildOverpassQuery(double minLat, double minLon, double maxLat, double maxLon) {
    return QString("[out:xml][timeout:25];"
                   "(way[\"highway\"](%1,%2,%3,%4);"
                   "node(w);"
                   ");out body;")
        .arg(minLat, 0, 'f', 7)
        .arg(minLon, 0, 'f', 7)
        .arg(maxLat, 0, 'f', 7)
        .arg(maxLon, 0, 'f', 7);
}
}

OSMDownloader::OSMDownloader(QObject* parent) : QObject(parent) {}

void OSMDownloader::fetchBoundingBox(double minLat, double minLon, double maxLat, double maxLon) {
    const QString interpreterUrl = QStringLiteral("https://overpass-api.de/api/interpreter");
    QUrl url(interpreterUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", QByteArrayLiteral("v2v-map-simulator/0.1 (contact: user@example.com)"));

    QUrlQuery body;
    body.addQueryItem("data", buildOverpassQuery(minLat, minLon, maxLat, maxLon));

    QNetworkReply* reply = m_network.post(request, body.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, &OSMDownloader::onReplyFinished);
}

void OSMDownloader::onReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(reply->errorString());
        reply->deleteLater();
        return;
    }

    QByteArray payload = reply->readAll();
    emit downloadFinished(payload);
    reply->deleteLater();
}

