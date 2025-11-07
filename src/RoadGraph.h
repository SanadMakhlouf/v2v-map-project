#pragma once

#include <QHash>
#include <QVector>
#include <QString>

struct RoadEdge;

struct RoadNode {
    qint64 id = 0;
    double lat = 0.0;
    double lon = 0.0;
    QVector<int> outgoingEdges;
};

struct RoadEdge {
    qint64 id = 0;
    int fromNode = -1;
    int toNode = -1;
    double lengthMeters = 0.0;
    bool oneway = false;
    double maxSpeedKmh = 50.0;
    QString highwayType;
};

class RoadGraph {
public:
    int addNode(const RoadNode& node);
    int addEdge(const RoadEdge& edge);

    const RoadNode* nodeById(qint64 osmId) const;
    const RoadEdge* edgeById(qint64 osmId) const;
    int nodeIndex(qint64 osmId) const;

    const QVector<RoadNode>& nodes() const { return m_nodes; }
    const QVector<RoadEdge>& edges() const { return m_edges; }

    void clear();

private:
    QVector<RoadNode> m_nodes;
    QVector<RoadEdge> m_edges;
    QHash<qint64, int> m_nodeIndexById;
    QHash<qint64, int> m_edgeIndexById;
};

