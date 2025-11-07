#include "RoadGraphLoader.h"

#include "RoadGraph.h"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <QtMath>
#include <QSet>
#include <QRegularExpression>
#include <exception>
#include <cstddef>

#ifdef HAVE_LIBOSMIUM
#include <osmium/io/any_input.hpp>
#include <osmium/visitor.hpp>
#include <osmium/handler/node_locations.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#endif

namespace {
double haversine(double lat1, double lon1, double lat2, double lon2) {
    static constexpr double earthRadiusMeters = 6371000.0;
    double lat1Rad = qDegreesToRadians(lat1);
    double lon1Rad = qDegreesToRadians(lon1);
    double lat2Rad = qDegreesToRadians(lat2);
    double lon2Rad = qDegreesToRadians(lon2);
    double dlat = lat2Rad - lat1Rad;
    double dlon = lon2Rad - lon1Rad;
    double a = qSin(dlat / 2) * qSin(dlat / 2) +
               qCos(lat1Rad) * qCos(lat2Rad) * qSin(dlon / 2) * qSin(dlon / 2);
    double c = 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
    return earthRadiusMeters * c;
}

bool isHighwayTypeSupported(const QString& value) {
    static const QSet<QString> supportedTypes = {
        "motorway", "motorway_link", "trunk", "trunk_link",
        "primary", "primary_link", "secondary", "secondary_link",
        "tertiary", "tertiary_link", "residential", "unclassified",
        "living_street", "service"
    };
    return supportedTypes.contains(value);
}

bool isOnewayValueTrue(const QString& value) {
    static const QSet<QString> trueVals = {"yes", "true", "1"};
    static const QSet<QString> falseVals = {"no", "false", "0"};
    if (trueVals.contains(value)) return true;
    if (falseVals.contains(value)) return false;
    if (value == "-1") return true; // backwards but handle later
    return false;
}

#ifdef HAVE_LIBOSMIUM
class RoadGraphBuilderHandler : public osmium::handler::Handler {
public:
    explicit RoadGraphBuilderHandler(RoadGraph& graph) : m_graph(graph) {}

    void way(const osmium::Way& way) {
        const char* highway = way.tags()["highway"];
        if (!highway) return;
        QString highwayType = QString::fromUtf8(highway);
        if (!isHighwayTypeSupported(highwayType)) return;

        QString onewayTag;
        if (way.tags().has_key("oneway")) {
            onewayTag = QString::fromUtf8(way.tags()["oneway"]);
        }
        bool oneway = isOnewayValueTrue(onewayTag) || onewayTag == QLatin1String("-1");
        bool reverseOneway = onewayTag == QLatin1String("-1");

        QString maxSpeedTag;
        if (way.tags().has_key("maxspeed")) {
            maxSpeedTag = QString::fromUtf8(way.tags()["maxspeed"]);
        }
        double maxSpeed = parseMaxSpeedKmh(maxSpeedTag);

        const auto& nodes = way.nodes();
        for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
            const auto& fromRef = nodes[i];
            const auto& toRef = nodes[i + 1];
            if (!fromRef.location().valid() || !toRef.location().valid()) {
                continue;
            }

            int fromIndex = ensureNode(fromRef);
            int toIndex = ensureNode(toRef);
            if (fromIndex < 0 || toIndex < 0) continue;

            double length = haversine(fromRef.location().lat(), fromRef.location().lon(),
                                      toRef.location().lat(), toRef.location().lon());

            qint64 wayId = static_cast<qint64>(way.id());

            RoadEdge forward;
            forward.id = (wayId << 16) + static_cast<qint64>(i);
            forward.fromNode = fromIndex;
            forward.toNode = toIndex;
            forward.lengthMeters = length;
            forward.oneway = oneway && !reverseOneway;
            forward.maxSpeedKmh = maxSpeed;
            forward.highwayType = highwayType;

            if (!reverseOneway) {
                m_graph.addEdge(forward);
            }

            if (!oneway || reverseOneway) {
                RoadEdge backward = forward;
                backward.id = (wayId << 16) + static_cast<qint64>(i) + static_cast<qint64>(nodes.size());
                backward.fromNode = toIndex;
                backward.toNode = fromIndex;
                backward.oneway = oneway && reverseOneway;
                if (reverseOneway) {
                    m_graph.addEdge(backward);
                } else if (!oneway) {
                    backward.oneway = false;
                    m_graph.addEdge(backward);
                }
            }
        }
    }

private:
    int ensureNode(const osmium::WayNodeRef& ref) {
        qint64 id = static_cast<qint64>(ref.ref());
        int idx = m_graph.nodeIndex(id);
        if (idx >= 0) {
            return idx;
        }
        if (!ref.location().valid()) {
            return -1;
        }
        RoadNode node;
        node.id = id;
        node.lat = ref.location().lat();
        node.lon = ref.location().lon();
        return m_graph.addNode(node);
    }

    RoadGraph& m_graph;
};

bool loadFromOsmPbf(const QString& filePath, RoadGraph& graph, QString* errorMessage) {
    try {
        graph.clear();

        osmium::io::Reader reader(filePath.toStdString(), osmium::osm_entity_bits::node | osmium::osm_entity_bits::way);

        using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
        using location_handler_type = osmium::handler::node_locations_for_ways<index_type>;

        index_type index;
        location_handler_type locationHandler(index);
        locationHandler.ignore_errors();

        RoadGraphBuilderHandler handler(graph);

        osmium::apply(reader, locationHandler, handler);
        reader.close();

        return true;
    } catch (const std::exception& ex) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Erreur libosmium: %1").arg(QString::fromUtf8(ex.what()));
        }
        return false;
    }
}
#endif

double parseMaxSpeedKmh(const QString& value) {
    if (value.isEmpty()) return 50.0;
    QString cleaned = value.trimmed().toLower();
    QRegularExpression regex("^(\\d+(?:\\.\\d+)?)(?:\\s*(km/h|kmh|mph|kph))?$");
    QRegularExpressionMatch match = regex.match(cleaned);
    if (!match.hasMatch()) return 50.0;
    double speed = match.captured(1).toDouble();
    QString unit = match.captured(2);
    if (unit == "mph") {
        speed *= 1.60934;
    }
    return speed;
}
} // namespace

bool RoadGraphLoader::loadFromOsmFile(const QString& filePath, RoadGraph& graph, QString* errorMessage) {
    QString lower = QFileInfo(filePath).suffix().toLower();
    if (lower == QLatin1String("pbf") || filePath.toLower().endsWith(".osm.pbf")) {
#ifdef HAVE_LIBOSMIUM
        return loadFromOsmPbf(filePath, graph, errorMessage);
#else
        if (errorMessage) {
            *errorMessage = QStringLiteral("Support des fichiers .pbf indisponible (libosmium non détecté).");
        }
        return false;
#endif
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Impossible d'ouvrir le fichier OSM: %1").arg(file.errorString());
        }
        return false;
    }
    QByteArray data = file.readAll();
    return loadFromOsmData(data, graph, errorMessage);
}

bool RoadGraphLoader::loadFromOsmData(const QByteArray& data, RoadGraph& graph, QString* errorMessage) {
    graph.clear();

    struct NodeInfo {
        double lat = 0.0;
        double lon = 0.0;
    };

    QHash<qint64, NodeInfo> nodesTmp;

    QXmlStreamReader xmlNodes(data);

    while (!xmlNodes.atEnd()) {
        xmlNodes.readNext();
        if (xmlNodes.isStartElement() && xmlNodes.name() == QLatin1String("node")) {
            auto attrs = xmlNodes.attributes();
            bool okId = false;
            qint64 id = attrs.value("id").toLongLong(&okId);
            if (!okId) continue;
            double lat = attrs.value("lat").toDouble();
            double lon = attrs.value("lon").toDouble();
            nodesTmp.insert(id, NodeInfo{lat, lon});
        }
    }

    if (xmlNodes.hasError()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Erreur lors de la lecture des nœuds OSM: %1").arg(xmlNodes.errorString());
        }
        return false;
    }

    for (auto it = nodesTmp.constBegin(); it != nodesTmp.constEnd(); ++it) {
        RoadNode node;
        node.id = it.key();
        node.lat = it.value().lat;
        node.lon = it.value().lon;
        graph.addNode(node);
    }

    QXmlStreamReader xmlWays(data);

    while (!xmlWays.atEnd()) {
        xmlWays.readNext();
        if (xmlWays.isStartElement() && xmlWays.name() == QLatin1String("way")) {
            QHash<QString, QString> tags;
            QVector<qint64> nodeRefs;
            qint64 wayId = xmlWays.attributes().value("id").toLongLong();

            while (!(xmlWays.isEndElement() && xmlWays.name() == QLatin1String("way"))) {
                xmlWays.readNext();
                if (xmlWays.isStartElement()) {
                    if (xmlWays.name() == QLatin1String("nd")) {
                        bool okRef = false;
                        qint64 ref = xmlWays.attributes().value("ref").toLongLong(&okRef);
                        if (okRef) nodeRefs.append(ref);
                    } else if (xmlWays.name() == QLatin1String("tag")) {
                        QString k = xmlWays.attributes().value("k").toString();
                        QString v = xmlWays.attributes().value("v").toString();
                        tags.insert(k, v);
                    }
                }
            }

            QString highwayType = tags.value("highway");
            if (!isHighwayTypeSupported(highwayType)) {
                continue;
            }

            QString onewayTag = tags.value("oneway");
            bool oneway = isOnewayValueTrue(onewayTag) || onewayTag == QLatin1String("-1");
            bool reverseOneway = onewayTag == QLatin1String("-1");
            double maxSpeed = parseMaxSpeedKmh(tags.value("maxspeed"));

            for (int i = 0; i < nodeRefs.size() - 1; ++i) {
                qint64 fromId = nodeRefs[i];
                qint64 toId = nodeRefs[i + 1];

                const RoadNode* fromNode = graph.nodeById(fromId);
                const RoadNode* toNode = graph.nodeById(toId);
                if (!fromNode || !toNode) continue;

                int fromIndex = graph.nodeIndex(fromId);
                int toIndex = graph.nodeIndex(toId);
                if (fromIndex < 0 || toIndex < 0) continue;

                double length = haversine(fromNode->lat, fromNode->lon, toNode->lat, toNode->lon);

                RoadEdge forward;
                forward.id = (wayId << 16) + i;
                forward.fromNode = fromIndex;
                forward.toNode = toIndex;
                forward.lengthMeters = length;
                forward.oneway = oneway && !reverseOneway;
                forward.maxSpeedKmh = maxSpeed;
                forward.highwayType = highwayType;

                if (!reverseOneway && forward.fromNode >= 0 && forward.toNode >= 0) {
                    graph.addEdge(forward);
                }

                if (!oneway || reverseOneway) {
                    RoadEdge backward = forward;
                    backward.id = (wayId << 16) + i + nodeRefs.size();
                    backward.fromNode = toIndex;
                    backward.toNode = fromIndex;
                    backward.oneway = oneway && reverseOneway;
                    if (reverseOneway) {
                        // oneway in reverse direction, so only keep backward edge
                        graph.addEdge(backward);
                    } else if (!oneway) {
                        backward.oneway = false;
                        graph.addEdge(backward);
                    }
                }
            }
        }
    }

    if (xmlWays.hasError()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Erreur lors de la lecture des routes OSM: %1").arg(xmlWays.errorString());
        }
        return false;
    }

    return true;
}

