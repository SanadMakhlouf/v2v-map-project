#pragma once

#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <QVector>
#include <QSet>
#include <QDateTime>

#include "V2VMessage.h"

class Vehicle {
public:
    Vehicle() = default;
    Vehicle(int id,
            double latitude,
            double longitude,
            double speedKmh,
            double transmissionRadiusMeters,
            qint64 edgeId,
            const QString& highwayType);

    int id() const { return m_id; }
    double latitude() const { return m_lat; }
    double longitude() const { return m_lon; }
    double speedKmh() const { return m_speedKmh; }
    double transmissionRadiusMeters() const { return m_transmissionRadius; }
    qint64 edgeId() const { return m_edgeId; }
    const QString& highwayType() const { return m_highwayType; }
    
    // Propriétés pour le déplacement
    double positionOnEdge() const { return m_positionOnEdge; } // 0.0 à 1.0
    int edgeIndex() const { return m_edgeIndex; }
    bool isMovingForward() const { return m_movingForward; }
    
    // Propriétés pour les messages V2V
    int messagesSent() const { return m_messagesSent; }
    int messagesReceived() const { return m_messagesReceived; }
    int alertsRelayed() const { return m_alertsRelayed; }
    bool hasActiveAlert() const { return m_hasActiveAlert; }
    bool hasReceivedAlert() const { return m_hasReceivedAlert; }
    qint64 alertTimestamp() const { return m_alertTimestamp; }
    qint64 receivedAlertTimestamp() const { return m_receivedAlertTimestamp; }
    
    // Méthodes pour les messages V2V
    void addMessageToInbox(const V2VMessage& message);
    QVector<V2VMessage> getInbox() const { return m_inbox; }
    void clearInbox() { m_inbox.clear(); }
    void incrementMessagesSent() { m_messagesSent++; }
    void incrementMessagesReceived() { m_messagesReceived++; }
    void incrementAlertsRelayed() { m_alertsRelayed++; }
    void setActiveAlert(bool active) { 
        m_hasActiveAlert = active; 
        if (active) {
            m_alertTimestamp = QDateTime::currentMSecsSinceEpoch();
        }
    }
    void setReceivedAlert(bool received) {
        m_hasReceivedAlert = received;
        if (received) {
            m_receivedAlertTimestamp = QDateTime::currentMSecsSinceEpoch();
        }
    }
    QSet<QString> getProcessedMessageIds() const { return m_processedMessageIds; }
    void addProcessedMessageId(const QString& messageId) { m_processedMessageIds.insert(messageId); }
    double previousSpeedKmh() const { return m_previousSpeedKmh; }
    void setPreviousSpeedKmh(double speed) { m_previousSpeedKmh = speed; }

    void setId(int id) { m_id = id; }
    void setLatLon(double latitude, double longitude) { m_lat = latitude; m_lon = longitude; }
    void setSpeedKmh(double value) { m_speedKmh = value; }
    void setTransmissionRadiusMeters(double value) { m_transmissionRadius = value; }
    void setEdgeId(qint64 value) { m_edgeId = value; }
    void setHighwayType(const QString& type) { m_highwayType = type; }
    
    // Méthodes pour le déplacement
    void setEdgeIndex(int index) { m_edgeIndex = index; }
    void setPositionOnEdge(double t) { m_positionOnEdge = std::clamp(t, 0.0, 1.0); }
    void setMovingForward(bool forward) { m_movingForward = forward; }
    void updatePosition(double deltaTimeSeconds, double edgeLengthMeters);

private:
    int m_id = 0;
    double m_lat = 0.0;
    double m_lon = 0.0;
    double m_speedKmh = 0.0;
    double m_transmissionRadius = 0.0;
    qint64 m_edgeId = 0;
    QString m_highwayType;
    
    // Propriétés pour le déplacement
    int m_edgeIndex = -1;           // Index de l'arête dans le graphe
    double m_positionOnEdge = 0.5;   // Position sur l'arête (0.0 = début, 1.0 = fin)
    bool m_movingForward = true;     // Direction de déplacement
    
    // Propriétés pour les messages V2V
    QVector<V2VMessage> m_inbox;                    // Boîte de réception des messages
    QSet<QString> m_processedMessageIds;           // IDs des messages déjà traités (éviter boucles)
    int m_messagesSent = 0;                         // Compteur de messages envoyés
    int m_messagesReceived = 0;                    // Compteur de messages reçus
    int m_alertsRelayed = 0;                        // Compteur d'alertes relayées
    bool m_hasActiveAlert = false;                  // Le véhicule a-t-il déclenché une alerte ?
    bool m_hasReceivedAlert = false;                // Le véhicule a-t-il reçu une alerte ?
    qint64 m_alertTimestamp = 0;                    // Timestamp de l'alerte déclenchée
    qint64 m_receivedAlertTimestamp = 0;           // Timestamp de réception d'alerte
    double m_previousSpeedKmh = 0.0;               // Vitesse précédente pour détecter arrêt brutal
};

inline Vehicle::Vehicle(int id,
                        double latitude,
                        double longitude,
                        double speedKmh,
                        double transmissionRadiusMeters,
                        qint64 edgeId,
                        const QString& highwayType)
    : m_id(id),
      m_lat(latitude),
      m_lon(longitude),
      m_speedKmh(speedKmh),
      m_transmissionRadius(transmissionRadiusMeters),
      m_edgeId(edgeId),
      m_highwayType(highwayType),
      m_edgeIndex(-1),
      m_positionOnEdge(0.5),
      m_movingForward(true) {}

inline void Vehicle::updatePosition(double deltaTimeSeconds, double edgeLengthMeters) {
    if (edgeLengthMeters <= 0.0) return;
    
    // Calculer la distance parcourue en mètres
    double speedMs = m_speedKmh / 3.6; // Conversion km/h -> m/s
    double distanceMeters = speedMs * deltaTimeSeconds;
    
    // Calculer la nouvelle position sur l'arête
    double progress = distanceMeters / edgeLengthMeters;
    
    if (m_movingForward) {
        m_positionOnEdge += progress;
        if (m_positionOnEdge >= 1.0) {
            m_positionOnEdge = 1.0;
            // Le véhicule atteint la fin de l'arête, sera géré par le système de simulation
        }
    } else {
        m_positionOnEdge -= progress;
        if (m_positionOnEdge <= 0.0) {
            m_positionOnEdge = 0.0;
            // Le véhicule atteint le début de l'arête
        }
    }
    
    m_positionOnEdge = std::clamp(m_positionOnEdge, 0.0, 1.0);
}

inline void Vehicle::addMessageToInbox(const V2VMessage& message) {
    m_inbox.append(message);
}

