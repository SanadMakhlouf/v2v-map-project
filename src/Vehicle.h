#pragma once

#include <QString>
#include <QtGlobal>

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

    void setId(int id) { m_id = id; }
    void setLatLon(double latitude, double longitude) { m_lat = latitude; m_lon = longitude; }
    void setSpeedKmh(double value) { m_speedKmh = value; }
    void setTransmissionRadiusMeters(double value) { m_transmissionRadius = value; }
    void setEdgeId(qint64 value) { m_edgeId = value; }
    void setHighwayType(const QString& type) { m_highwayType = type; }

private:
    int m_id = 0;
    double m_lat = 0.0;
    double m_lon = 0.0;
    double m_speedKmh = 0.0;
    double m_transmissionRadius = 0.0;
    qint64 m_edgeId = 0;
    QString m_highwayType;
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
      m_highwayType(highwayType) {}

