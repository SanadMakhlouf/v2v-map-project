
#include "MapView.h"
#include <QtMath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QDebug>
#include <QPainter>
#include <limits>
#include <utility>
#include <random>
#include <cmath>
#include <algorithm>
#include <QToolButton>
#include <QStyle>

#include "RoadGraphLoader.h"

MapView::MapView(QWidget* parent) : QGraphicsView(parent), m_scene(new QGraphicsScene(this)) {
    setScene(m_scene);
    setDragMode(NoDrag);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing, true);
    connect(&m_tileManager, &TileManager::tileReady, this, &MapView::onTileReady);
    createZoomControls();
    loadVisibleTiles();
}

MapView::~MapView() {
    clearRoadGraphics();
    clearVehicleGraphics();
    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        if (it->item) {
            m_scene->removeItem(it->item);
            delete it->item;
        }
    }
    m_tileItems.clear();
}

void MapView::setCenterLatLon(double lat, double lon, int zoom, bool preserveIfOutOfBounds) {
    double clampedLat = clampLatitude(lat);
    double clampedLon = normalizeLongitude(lon);
    bool clamped = clampCenterToBounds(clampedLat, clampedLon);
    if (clamped && preserveIfOutOfBounds) {
        clampedLat = m_centerLat;
        clampedLon = m_centerLon;
    }

    bool zoomChanged = (zoom != m_zoom);
    bool centerChanged = !qFuzzyCompare(1.0 + clampedLat, 1.0 + m_centerLat) ||
                         !qFuzzyCompare(1.0 + clampedLon, 1.0 + m_centerLon);

    if (!zoomChanged && !centerChanged) {
        return;
    }

    m_centerLat = clampedLat;
    m_centerLon = clampedLon;
    m_zoom = zoom;

    clearRoadGraphics();
    clearVehicleGraphics();
    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        it->stillNeeded = false;
    }
    loadVisibleTiles();
    reloadRoadGraphics();
    reloadVehicleGraphics();
    updateZoomButtons();
}


void MapView::wheelEvent(QWheelEvent* event) {
    int newZoom = m_zoom;
    if (event->angleDelta().y() > 0)
        newZoom = std::min(19, m_zoom + 1);
    else
        newZoom = std::max(0, m_zoom - 1);

    if (newZoom == m_zoom) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    QPointF viewCenter = mapToScene(viewport()->rect().center());
    QPointF centerLatLon = sceneToLonLat(viewCenter, m_zoom);
    setCenterLatLon(centerLatLon.y(), centerLatLon.x(), newZoom);
    event->accept();
}


void MapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastPan = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QGraphicsView::mousePressEvent(event);
}



void MapView::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        QPoint delta = event->pos() - m_lastPan;
        m_lastPan = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    }
    QGraphicsView::mouseMoveEvent(event);
}

void MapView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        QPointF sceneCenter = mapToScene(viewport()->rect().center());
        QPointF lonLat = sceneToLonLat(sceneCenter, m_zoom);
        setCenterLatLon(lonLat.y(), lonLat.x(), m_zoom);
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void MapView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        QPointF lonLat = sceneToLonLat(scenePos, m_zoom);
        int targetZoom = (m_zoom < 19) ? m_zoom + 1 : m_zoom;
        setCenterLatLon(lonLat.y(), lonLat.x(), targetZoom, true);
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}


QPointF MapView::lonLatToScene(double lon, double lat, int z) {
    lat = clampLatitude(lat);
    lon = normalizeLongitude(lon);
    double n = std::pow(2.0, z);
    double xtile = (lon + 180.0) / 360.0 * n;
    double latrad = qDegreesToRadians(lat);
    double ytile = (1.0 - std::log(std::tan(latrad) + 1.0 / std::cos(latrad)) / M_PI) / 2.0 * n;
    return QPointF(xtile * TILE_SIZE, ytile * TILE_SIZE);
}

QPointF MapView::sceneToLonLat(const QPointF& scenePoint, int z) const {
    double n = std::pow(2.0, z);
    double lon = scenePoint.x() / (TILE_SIZE * n) * 360.0 - 180.0;
    double ytile = scenePoint.y() / TILE_SIZE;
    double mercator = M_PI * (1.0 - 2.0 * ytile / n);
    double latRad = std::atan(std::sinh(mercator));
    double lat = qRadiansToDegrees(latRad);
    lon = normalizeLongitude(lon);
    lat = clampLatitude(lat);
    return QPointF(lon, lat);
}

void MapView::loadVisibleTiles() {
    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        it->stillNeeded = false;
    }
    QPointF centerScene = lonLatToScene(m_centerLon, m_centerLat, m_zoom);
    double tilesAcross = viewport()->width() / static_cast<double>(TILE_SIZE);
    double tilesDown = viewport()->height() / static_cast<double>(TILE_SIZE);
    int rangeX = static_cast<int>(std::ceil(tilesAcross / 2.0)) + 1;
    int rangeY = static_cast<int>(std::ceil(tilesDown / 2.0)) + 1;
    int range = std::max(rangeX, rangeY);
    range = std::clamp(range, 2, 6);
    double cxTile = centerScene.x() / TILE_SIZE;
    double cyTile = centerScene.y() / TILE_SIZE;
    for (int dx = -range; dx <= range; ++dx) {
        for (int dy = -range; dy <= range; ++dy) {
            int tx = int(std::floor(cxTile)) + dx;
            int ty = int(std::floor(cyTile)) + dy;
            QString key = tileKey(m_zoom, tx, ty);
            auto it = m_tileItems.find(key);
            if (it != m_tileItems.end()) {
                if (it->item) {
                    it->item->setPos(tx * TILE_SIZE, ty * TILE_SIZE);
                }
                it->stillNeeded = true;
            } else {
                QPixmap placeholder(TILE_SIZE, TILE_SIZE);
                placeholder.fill(QColor(235, 235, 235));
                TileInfo info;
                info.item = m_scene->addPixmap(placeholder);
                info.item->setZValue(0);
                info.item->setPos(tx * TILE_SIZE, ty * TILE_SIZE);
                info.stillNeeded = true;
                info.loading = true;
                m_tileItems.insert(key, info);
                m_tileManager.requestTile(m_zoom, tx, ty);
            }
        }
    }
    for (auto it = m_tileItems.begin(); it != m_tileItems.end();) {
        if (!it->stillNeeded) {
            if (it->item) {
                m_scene->removeItem(it->item);
                delete it->item;
            }
            it = m_tileItems.erase(it);
        } else {
            ++it;
        }
    }
    centerOn(centerScene);
    updateZoomButtons();
}

bool MapView::loadRoadGraphFromFile(const QString& filePath) {
    RoadGraph parsedGraph;
    QString error;
    if (!RoadGraphLoader::loadFromOsmFile(filePath, parsedGraph, &error)) {
        qWarning() << "Échec du chargement du graphe routier:" << error;
        return false;
    }

    clearRoadGraphics();
    clearVehicleGraphics();
    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        it->stillNeeded = false;
    }
    m_roadGraph = std::move(parsedGraph);
    m_roadGraphLoaded = true;
    generateVehicles(60);

    if (!m_roadGraph.nodes().isEmpty()) {
        double minLat = std::numeric_limits<double>::max();
        double maxLat = std::numeric_limits<double>::lowest();
        double minLon = std::numeric_limits<double>::max();
        double maxLon = std::numeric_limits<double>::lowest();
        for (const auto& node : m_roadGraph.nodes()) {
            minLat = std::min(minLat, node.lat);
            maxLat = std::max(maxLat, node.lat);
            minLon = std::min(minLon, node.lon);
            maxLon = std::max(maxLon, node.lon);
        }
        double centerLat = (minLat + maxLat) / 2.0;
        double centerLon = (minLon + maxLon) / 2.0;
        setCenterLatLon(centerLat, centerLon, m_zoom);
    } else {
        reloadRoadGraphics();
        reloadVehicleGraphics();
    }

    qInfo() << "Graphe routier chargé:" << m_roadGraph.nodes().size()
            << "noeuds," << m_roadGraph.edges().size() << "arêtes.";
    return true;
}

void MapView::onTileReady(int z, int x, int y, const QPixmap& pix) {
    qreal px = x * TILE_SIZE;
    qreal py = y * TILE_SIZE;
    QString key = tileKey(z, x, y);
    auto it = m_tileItems.find(key);
    if (it == m_tileItems.end()) {
        TileInfo info;
        info.item = m_scene->addPixmap(pix);
        info.item->setZValue(0);
        info.item->setPos(px, py);
        info.stillNeeded = true;
        info.loading = false;
        m_tileItems.insert(key, info);
    } else {
        if (!it->item) {
            it->item = m_scene->addPixmap(pix);
            it->item->setZValue(0);
        } else {
            it->item->setPixmap(pix);
        }
        it->item->setPos(px, py);
        it->stillNeeded = true;
        it->loading = false;
    }
}

void MapView::clearRoadGraphics() {
    for (QGraphicsLineItem* item : std::as_const(m_roadGraphics)) {
        if (item) {
            m_scene->removeItem(item);
            delete item;
        }
    }
    m_roadGraphics.clear();
}

void MapView::reloadRoadGraphics() {
    clearRoadGraphics();
    if (!m_roadGraphLoaded) return;

    QPen pen(Qt::red);
    pen.setWidthF(1.5);
    pen.setCosmetic(true);

    const auto& nodes = m_roadGraph.nodes();
    for (const RoadEdge& edge : m_roadGraph.edges()) {
        if (edge.fromNode < 0 || edge.toNode < 0 ||
            edge.fromNode >= nodes.size() || edge.toNode >= nodes.size()) {
            continue;
        }
        const RoadNode& fromNode = nodes.at(edge.fromNode);
        const RoadNode& toNode = nodes.at(edge.toNode);
        QPointF p1 = lonLatToScene(fromNode.lon, fromNode.lat, m_zoom);
        QPointF p2 = lonLatToScene(toNode.lon, toNode.lat, m_zoom);
        auto* line = m_scene->addLine(QLineF(p1, p2), pen);
        line->setZValue(10);
        m_roadGraphics.append(line);
    }
}

void MapView::generateVehicles(int count) {
    m_vehicles.clear();
    if (!m_roadGraphLoaded) return;

    const auto& edges = m_roadGraph.edges();
    const auto& nodes = m_roadGraph.nodes();
    if (edges.isEmpty() || nodes.isEmpty()) return;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> tDist(0.1, 0.9);
    std::uniform_real_distribution<double> radiusDist(100.0, 500.0);

    int vehicleId = 1;
    int edgeIndex = 0;
    while (vehicleId <= count && edgeIndex < edges.size()) {
        const RoadEdge& edge = edges.at(edgeIndex);
        edgeIndex++;
        if (edge.fromNode < 0 || edge.toNode < 0 ||
            edge.fromNode >= nodes.size() || edge.toNode >= nodes.size()) {
            continue;
        }
        const RoadNode& fromNode = nodes.at(edge.fromNode);
        const RoadNode& toNode = nodes.at(edge.toNode);
        double t = tDist(rng);
        double lat = fromNode.lat + (toNode.lat - fromNode.lat) * t;
        double lon = fromNode.lon + (toNode.lon - fromNode.lon) * t;

        Vehicle vehicle;
        vehicle.setId(vehicleId);
        vehicle.setLatLon(lat, lon);
        vehicle.setSpeedKmh(edge.maxSpeedKmh);
        vehicle.setTransmissionRadiusMeters(radiusDist(rng));
        vehicle.setEdgeId(edge.id);
        vehicle.setHighwayType(edge.highwayType);
        m_vehicles.append(vehicle);
        ++vehicleId;
    }

    // If not enough edges, reuse existing ones in a circular manner.
    while (vehicleId <= count && !m_vehicles.isEmpty()) {
        Vehicle vehicle = m_vehicles.at((vehicleId - 1) % m_vehicles.size());
        vehicle.setId(vehicleId);
        m_vehicles.append(vehicle);
        ++vehicleId;
    }
}

void MapView::clearVehicleGraphics() {
    for (QGraphicsEllipseItem* item : std::as_const(m_vehicleGraphics)) {
        if (item) {
            m_scene->removeItem(item);
            delete item;
        }
    }
    m_vehicleGraphics.clear();
}

void MapView::reloadVehicleGraphics() {
    clearVehicleGraphics();
    if (m_vehicles.isEmpty()) return;

    QPen pen(Qt::black);
    pen.setWidthF(1.0);
    pen.setCosmetic(true);
    QBrush brush(QColor(255, 215, 0, 220));

    constexpr double radiusPixels = 5.0;

    for (const Vehicle& vehicle : std::as_const(m_vehicles)) {
        QPointF pos = lonLatToScene(vehicle.longitude(), vehicle.latitude(), m_zoom);
        QRectF rect(pos.x() - radiusPixels, pos.y() - radiusPixels, radiusPixels * 2, radiusPixels * 2);
        auto* ellipse = m_scene->addEllipse(rect, pen, brush);
        ellipse->setZValue(30);
        ellipse->setToolTip(QStringLiteral("Véhicule #%1\nLat: %2\nLon: %3\nVitesse: %4 km/h\nRayon: %5 m\nRoute: %6")
                                .arg(vehicle.id())
                                .arg(vehicle.latitude(), 0, 'f', 6)
                                .arg(vehicle.longitude(), 0, 'f', 6)
                                .arg(vehicle.speedKmh(), 0, 'f', 1)
                                .arg(vehicle.transmissionRadiusMeters(), 0, 'f', 1)
                                .arg(vehicle.highwayType()));
        m_vehicleGraphics.append(ellipse);
    }
}

QString MapView::tileKey(int z, int x, int y) const {
    return QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);
}

bool MapView::clampCenterToBounds(double& lat, double& lon) const {
    if (!m_limitRegion) return false;
    double clampedLat = std::clamp(lat, m_minLat, m_maxLat);
    double clampedLon = std::clamp(lon, m_minLon, m_maxLon);
    bool changed = !qFuzzyCompare(1.0 + lat, 1.0 + clampedLat) || !qFuzzyCompare(1.0 + lon, 1.0 + clampedLon);
    lat = clampedLat;
    lon = clampedLon;
    return changed;
}

double MapView::normalizeLongitude(double lon) const {
    return std::clamp(lon, -180.0, 180.0);
}

double MapView::clampLatitude(double lat) const {
    static constexpr double minLat = -85.05112878;
    static constexpr double maxLat = 85.05112878;
    return std::clamp(lat, minLat, maxLat);
}

void MapView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    positionZoomControls();
}

void MapView::createZoomControls() {
    if (m_zoomInButton || m_zoomOutButton) return;

    m_zoomInButton = new QToolButton(this);
    m_zoomInButton->setText("+");
    m_zoomInButton->setAutoRaise(true);
    m_zoomInButton->setToolTip(tr("Zoom in"));
    connect(m_zoomInButton, &QToolButton::clicked, [this]() {
        setCenterLatLon(m_centerLat, m_centerLon, std::min(19, m_zoom + 1));
    });

    m_zoomOutButton = new QToolButton(this);
    m_zoomOutButton->setText("-");
    m_zoomOutButton->setAutoRaise(true);
    m_zoomOutButton->setToolTip(tr("Zoom out"));
    connect(m_zoomOutButton, &QToolButton::clicked, [this]() {
        setCenterLatLon(m_centerLat, m_centerLon, std::max(0, m_zoom - 1));
    });

    positionZoomControls();
    updateZoomButtons();
}

void MapView::positionZoomControls() {
    if (!m_zoomInButton || !m_zoomOutButton) return;

    const int margin = 12;
    QSize buttonSize = QSize(28, 28);

    QPoint basePos = QPoint(width() - buttonSize.width() - margin, margin);
    m_zoomInButton->move(basePos);
    m_zoomInButton->resize(buttonSize);

    QPoint outPos = basePos + QPoint(0, buttonSize.height() + 6);
    m_zoomOutButton->move(outPos);
    m_zoomOutButton->resize(buttonSize);
}

void MapView::updateZoomButtons() {
    if (!m_zoomInButton || !m_zoomOutButton) return;
    m_zoomInButton->setEnabled(m_zoom < 19);
    m_zoomOutButton->setEnabled(m_zoom > 0);
}
