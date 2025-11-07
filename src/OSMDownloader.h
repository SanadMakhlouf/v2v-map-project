#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QUrl>

class OSMDownloader : public QObject {
    Q_OBJECT
public:
    explicit OSMDownloader(QObject* parent = nullptr);

    void fetchBoundingBox(double minLat, double minLon, double maxLat, double maxLon);

signals:
    void downloadFinished(const QByteArray& data);
    void downloadFailed(const QString& errorString);

private slots:
    void onReplyFinished();

private:
    QNetworkAccessManager m_network;
};

