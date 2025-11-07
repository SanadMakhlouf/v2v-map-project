#include "RoadGraph.h"

int RoadGraph::addNode(const RoadNode& node) {
    if (m_nodeIndexById.contains(node.id)) {
        return m_nodeIndexById.value(node.id);
    }
    int index = m_nodes.size();
    m_nodes.append(node);
    m_nodeIndexById.insert(node.id, index);
    return index;
}

int RoadGraph::addEdge(const RoadEdge& edge) {
    if (m_edgeIndexById.contains(edge.id)) {
        return m_edgeIndexById.value(edge.id);
    }
    if (edge.fromNode >= 0 && edge.fromNode < m_nodes.size()) {
        m_nodes[edge.fromNode].outgoingEdges.append(m_edges.size());
    }
    int index = m_edges.size();
    m_edges.append(edge);
    m_edgeIndexById.insert(edge.id, index);
    return index;
}

const RoadNode* RoadGraph::nodeById(qint64 osmId) const {
    auto it = m_nodeIndexById.constFind(osmId);
    if (it == m_nodeIndexById.constEnd()) return nullptr;
    return &m_nodes.at(it.value());
}

const RoadEdge* RoadGraph::edgeById(qint64 osmId) const {
    auto it = m_edgeIndexById.constFind(osmId);
    if (it == m_edgeIndexById.constEnd()) return nullptr;
    return &m_edges.at(it.value());
}

int RoadGraph::nodeIndex(qint64 osmId) const {
    auto it = m_nodeIndexById.constFind(osmId);
    if (it == m_nodeIndexById.constEnd()) return -1;
    return it.value();
}

void RoadGraph::clear() {
    m_nodes.clear();
    m_edges.clear();
    m_nodeIndexById.clear();
    m_edgeIndexById.clear();
}

