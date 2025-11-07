
#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QCache>
#include <QUrl>
#include <QNetworkDiskCache>
#include <memory>

class TileManager : public QObject {
    Q_OBJECT
public:
    TileManager(QObject* parent = nullptr);
    void requestTile(int z, int x, int y);
    QPixmap cachedTile(int z, int x, int y);

signals:
    void tileReady(int z, int x, int y, const QPixmap& pix);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager m_nam;
    QString tileUrl(int z, int x, int y) const;
    QString key(int z, int x, int y) const;
    QCache<QString, QPixmap> m_cache{100};
    std::unique_ptr<QNetworkDiskCache> m_diskCache;
};
