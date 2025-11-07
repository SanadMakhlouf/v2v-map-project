#pragma once

#include <QObject>
#include <QString>

class RoadGraph;

class RoadGraphLoader {
public:
    // Supporte les fichiers .osm (XML) et .osm.pbf (binaire) si libosmium est disponible.
    static bool loadFromOsmFile(const QString& filePath, RoadGraph& graph, QString* errorMessage = nullptr);
    static bool loadFromOsmData(const QByteArray& data, RoadGraph& graph, QString* errorMessage = nullptr);
};

