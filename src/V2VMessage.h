#pragma once

#include <QString>
#include <QtGlobal>
#include <QDateTime>

enum class V2VMessageType {
    CAM,    // Cooperative Awareness Message (périodique)
    ALERT   // Message d'alerte (événementiel)
};

struct V2VMessage {
    V2VMessageType type = V2VMessageType::CAM;
    int senderId = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double speedKmh = 0.0;
    qint64 timestamp = 0;  // Timestamp en millisecondes
    int ttl = 1;          // Time To Live (nombre de sauts restants)
    QString messageId;    // Identifiant unique pour éviter les boucles
    
    V2VMessage() = default;
    
    V2VMessage(V2VMessageType msgType, int id, double lat, double lon, double speed, int hops = 1)
        : type(msgType)
        , senderId(id)
        , latitude(lat)
        , longitude(lon)
        , speedKmh(speed)
        , timestamp(QDateTime::currentMSecsSinceEpoch())
        , ttl(hops)
    {
        // Générer un ID unique pour le message
        messageId = QString("%1_%2_%3").arg(senderId).arg(timestamp).arg(static_cast<int>(type));
    }
    
    bool isValid() const {
        return senderId > 0 && ttl > 0;
    }
    
    // Créer une copie pour retransmission avec ttl décrémenté
    V2VMessage createRelayCopy() const {
        V2VMessage relayed = *this;
        relayed.ttl = ttl - 1;
        return relayed;
    }
};

