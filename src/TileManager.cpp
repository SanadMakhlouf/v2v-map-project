
#include "TileManager.h"
#include <QNetworkReply>
#include <QImage>
#include <QDebug>
#include <QNetworkDiskCache>
#include <QStandardPaths>
#include <QDir>

TileManager::TileManager(QObject* parent) : QObject(parent) {
    auto cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheDir.isEmpty()) {
        cacheDir = QDir::tempPath() + QStringLiteral("/v2v_map_cache");
    } else {
        cacheDir += QStringLiteral("/v2v_map_tiles");
    }
    QDir().mkpath(cacheDir);
    m_diskCache = std::make_unique<QNetworkDiskCache>();
    m_diskCache->setCacheDirectory(cacheDir);
    m_diskCache->setMaximumCacheSize(50 * 1024 * 1024); // 50 MB
    m_nam.setCache(m_diskCache.get());
}

QString TileManager::key(int z, int x, int y) const {
    return QString("%1/%2/%3").arg(z).arg(x).arg(y);
}

QString TileManager::tileUrl(int z, int x, int y) const {
    return QString("https://tile.openstreetmap.org/%1/%2/%3.png").arg(z).arg(x).arg(y);
}

void TileManager::requestTile(int z, int x, int y) {
    QString k = key(z,x,y);
    if (m_cache.contains(k)) {
        QPixmap* p = m_cache.object(k);
        if (p) emit tileReady(z,x,y,*p);
        return;
    }
    QUrl url(tileUrl(z,x,y));
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", QByteArrayLiteral("v2v-map-simulator/0.1 (contact: user@example.com)"));
    req.setRawHeader("Referer", QByteArrayLiteral("https://example.com/v2v-map-simulator"));
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, &TileManager::onReplyFinished);
}

void TileManager::onReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    QUrl url = reply->url();
    auto parts = url.path().split('/');
    if (parts.size() >= 4) {
        int z = parts.at(parts.size()-3).toInt();
        int x = parts.at(parts.size()-2).toInt();
        int y = parts.at(parts.size()-1).split('.').first().toInt();
        QByteArray data = reply->readAll();
        QPixmap pix;
        pix.loadFromData(data);
        QString k = key(z,x,y);
        m_cache.insert(k, new QPixmap(pix));
        emit tileReady(z,x,y,pix);
    }
    reply->deleteLater();
}

QPixmap TileManager::cachedTile(int z, int x, int y) {
    QString k = key(z,x,y);
    if (m_cache.contains(k)) {
        QPixmap* p = m_cache.object(k);
        if (p) return *p;
    }
    return QPixmap();
}
