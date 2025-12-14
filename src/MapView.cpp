
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
#include <QTimer>
#include <QFileDialog>
#include <QDateTime>
#include <QMenu>
#include <QPair>
#include <QGraphicsRectItem>
#include <QRectF>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QSlider>
#include <QDialog>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLineF>

#include "RoadGraphLoader.h"
#include "V2VMessage.h"

MapView::MapView(QWidget* parent) : QGraphicsView(parent), m_scene(new QGraphicsScene(this)) {
    setScene(m_scene);
    setDragMode(NoDrag);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setRenderHint(QPainter::Antialiasing, true);
    connect(&m_tileManager, &TileManager::tileReady, this, &MapView::onTileReady);
    createZoomControls();
    createControlPanel();
    initializeSimulation();
    // Ne pas charger les tuiles ici - laisser setCenterLatLon le faire après que le widget soit rendu
}

MapView::~MapView() {
    if (m_simulationTimer) {
        m_simulationTimer->stop();
        delete m_simulationTimer;
    }
    if (m_camMessageTimer) {
        m_camMessageTimer->stop();
        delete m_camMessageTimer;
    }
    clearRoadGraphics();
    clearVehicleGraphics();
    clearConnectionGraphics();
    clearDensityHeatmap();
    clearV2VExchangeGraphics();
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

    // Calculer le centre de la scène à partir des coordonnées mises à jour
    QPointF centerScene = lonLatToScene(m_centerLon, m_centerLat, m_zoom);
    loadVisibleTiles(centerScene);
    reloadRoadGraphics();
    reloadVehicleGraphics();
    updateZoomButtons();
    
    // Appeler centerOn immédiatement si le viewport est prêt
    if (viewport() && viewport()->width() > 0 && viewport()->height() > 0) {
        centerOn(centerScene);
    }
    
    // Utiliser un timer pour s'assurer que centerOn est appelé après le rendu complet
    // Utiliser un petit délai pour garantir que toutes les tuiles sont positionnées
    QTimer::singleShot(50, this, [this, centerScene]() {
        if (viewport() && viewport()->width() > 0 && viewport()->height() > 0) {
            centerOn(centerScene);
        }
    });
}

void MapView::zoomToLevel(int newZoom) {
    if (newZoom == m_zoom || newZoom < 0 || newZoom > 19) {
        return;
    }

    // Vérifier que le viewport est valide avant de calculer les coordonnées
    if (!viewport() || viewport()->width() <= 0 || viewport()->height() <= 0) {
        // Si le viewport n'est pas prêt, utiliser simplement les coordonnées géographiques actuelles
        m_zoom = newZoom;
        QPointF newCenterScene = lonLatToScene(m_centerLon, m_centerLat, m_zoom);
        loadVisibleTiles(newCenterScene);
        reloadRoadGraphics();
        reloadVehicleGraphics();
        updateZoomButtons();
        return;
    }

    // Obtenir le centre actuel de la vue en coordonnées scène (avec l'ancien zoom)
    QPointF currentCenterScene = mapToScene(viewport()->rect().center());

    // Convertir en coordonnées géographiques avec l'ancien zoom
    QPointF centerLatLon = sceneToLonLat(currentCenterScene, m_zoom);
    double lat = clampLatitude(centerLatLon.y());
    double lon = normalizeLongitude(centerLatLon.x());

    // Mettre à jour le zoom AVANT de recalculer
    m_zoom = newZoom;
    m_centerLat = lat;
    m_centerLon = lon;

    clearRoadGraphics();
    clearVehicleGraphics();
    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        it->stillNeeded = false;
    }

    // Recalculer le centre de la scène avec le NOUVEAU zoom
    // Cette conversion doit être faite avec le nouveau zoom
    QPointF newCenterScene = lonLatToScene(lon, lat, m_zoom);

    // Charger les tuiles (loadVisibleTiles va appeler centerOn, mais on va le refaire après pour être sûr)
    loadVisibleTiles(newCenterScene);

    // Forcer le centrage après que tout soit chargé
    // Utiliser QTimer::singleShot pour s'assurer que c'est fait après le rendu
    QTimer::singleShot(0, this, [this, newCenterScene]() {
        centerOn(newCenterScene);
    });

    reloadRoadGraphics();
    reloadVehicleGraphics();
    updateZoomButtons();
}


void MapView::wheelEvent(QWheelEvent* event) {
    // Vérifier que le viewport est valide
    if (!viewport() || viewport()->width() <= 0 || viewport()->height() <= 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    int newZoom = m_zoom;
    if (event->angleDelta().y() > 0)
        newZoom = std::min(19, m_zoom + 1);
    else
        newZoom = std::max(0, m_zoom - 1);

    if (newZoom == m_zoom) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    // Pour le zoom molette, on veut zoomer sur le point sous le curseur
    QPointF cursorPos = mapToScene(event->position().toPoint());
    QPointF cursorLatLon = sceneToLonLat(cursorPos, m_zoom);
    
    // Normaliser les coordonnées
    double lat = clampLatitude(cursorLatLon.y());
    double lon = normalizeLongitude(cursorLatLon.x());

    // Utiliser setCenterLatLon pour la synchronisation complète
    setCenterLatLon(lat, lon, newZoom);

    event->accept();
}


void MapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Vérifier d'abord si on clique sur un véhicule
        QPointF scenePos = mapToScene(event->pos());
        int vehicleIndex = findVehicleAtPosition(scenePos);
        
        if (vehicleIndex >= 0) {
            // Stocker le véhicule sélectionné pour déclencher alerte
            m_selectedVehicleId = m_vehicles.at(vehicleIndex).id();
            // Afficher les informations du véhicule
            showVehicleInfoDialog(vehicleIndex);
            event->accept();
            return;
        }
        
        // Sinon, commencer le pan
        m_panning = true;
        m_lastPan = event->pos();
        m_panStartPos = event->pos();
        // Stocker le centre géographique au début du déplacement
        m_panStartCenterLat = m_centerLat;
        m_panStartCenterLon = m_centerLon;
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
        
        // Calculer le déplacement total en pixels depuis le début du drag
        QPoint currentPos = event->pos();
        QPoint delta = currentPos - m_panStartPos;
        
        // Convertir le déplacement en pixels en déplacement géographique
        // On calcule toujours à partir du déplacement en pixels pour plus de fiabilité
        if (viewport() && viewport()->width() > 0 && viewport()->height() > 0) {
            // Calculer le centre de la scène au début du drag
            QPointF startSceneCenter = lonLatToScene(m_panStartCenterLon, m_panStartCenterLat, m_zoom);
            
            // Calculer le nouveau centre de la scène après le déplacement (delta est inversé car on déplace la vue)
            QPointF newSceneCenter = startSceneCenter - QPointF(delta.x(), delta.y());
            
            // Convertir le nouveau centre de la scène en coordonnées géographiques
            QPointF newLonLat = sceneToLonLat(newSceneCenter, m_zoom);
            
            // Vérifier que les coordonnées sont valides
            if (newLonLat.x() >= -180 && newLonLat.x() <= 180 && 
                newLonLat.y() >= -85 && newLonLat.y() <= 85) {
                setCenterLatLon(newLonLat.y(), newLonLat.x(), m_zoom);
            }
        }
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void MapView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Vérifier que le viewport est valide avant de calculer les coordonnées
        if (!viewport() || viewport()->width() <= 0 || viewport()->height() <= 0) {
            QGraphicsView::mouseDoubleClickEvent(event);
            return;
        }
        QPointF scenePos = mapToScene(event->pos());
        QPointF lonLat = sceneToLonLat(scenePos, m_zoom);
        int targetZoom = (m_zoom < 19) ? m_zoom + 1 : m_zoom;
        setCenterLatLon(lonLat.y(), lonLat.x(), targetZoom, true);
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}


QPointF MapView::lonLatToScene(double lon, double lat, int z) const {
    double clampedLat = clampLatitude(lat);
    double normalizedLon = normalizeLongitude(lon);
    double n = std::pow(2.0, z);
    double xtile = (normalizedLon + 180.0) / 360.0 * n;
    double latrad = qDegreesToRadians(clampedLat);
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

void MapView::loadVisibleTiles(const QPointF& centerScene) {
    // Vérifier que le viewport est valide
    if (!viewport() || viewport()->width() <= 0 || viewport()->height() <= 0) {
        return;
    }

    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        it->stillNeeded = false;
    }
    QPointF actualCenterScene = centerScene.isNull() ? lonLatToScene(m_centerLon, m_centerLat, m_zoom) : centerScene;
    double tilesAcross = viewport()->width() / static_cast<double>(TILE_SIZE);
    double tilesDown = viewport()->height() / static_cast<double>(TILE_SIZE);
    int rangeX = static_cast<int>(std::ceil(tilesAcross / 2.0)) + 1;
    int rangeY = static_cast<int>(std::ceil(tilesDown / 2.0)) + 1;
    int range = std::max(rangeX, rangeY);
    range = std::clamp(range, 2, 6);
    double cxTile = actualCenterScene.x() / TILE_SIZE;
    double cyTile = actualCenterScene.y() / TILE_SIZE;
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
    // Centrer seulement si un centre n'a pas été fourni explicitement
    // (si centerScene est null, c'est qu'on appelle depuis le constructeur ou autre)
    // Si un centerScene est fourni, laisser l'appelant (setCenterLatLon ou zoomToLevel) gérer le centrage
    if (centerScene.isNull()) {
        centerOn(actualCenterScene);
    }
    // Sinon, ne pas appeler centerOn ici - l'appelant le fera avec un timer pour garantir le bon timing
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
    clearConnectionGraphics();
    for (auto it = m_tileItems.begin(); it != m_tileItems.end(); ++it) {
        it->stillNeeded = false;
    }
    m_roadGraph = std::move(parsedGraph);
    m_roadGraphLoaded = true;
    
    // Générer les véhicules seulement si le graphe contient des données
    if (!m_roadGraph.nodes().isEmpty() && !m_roadGraph.edges().isEmpty()) {
        generateVehicles(30); // Réduit à 30 véhicules pour tester (peut être augmenté plus tard)
    } else {
        qWarning() << "Graphe routier vide, aucun véhicule généré";
    }

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
    
    // Mettre à jour aussi les connexions V2V et la heatmap si activées
    if (m_showV2VConnections) {
        updateConnectionGraphics();
    }
    if (m_showDensityHeatmap) {
        updateDensityHeatmap();
    }
}

void MapView::generateVehicles(int count) {
    m_vehicles.clear();
    if (!m_roadGraphLoaded) return;

    const auto& edges = m_roadGraph.edges();
    const auto& nodes = m_roadGraph.nodes();
    if (edges.isEmpty() || nodes.isEmpty()) return;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> tDist(0.0, 1.0);
    std::uniform_real_distribution<double> radiusDist(100.0, 500.0);
    std::uniform_int_distribution<int> directionDist(0, 1);

    // Créer une liste d'arêtes valides
    QVector<int> validEdgeIndices;
    for (int i = 0; i < edges.size(); ++i) {
        const RoadEdge& edge = edges.at(i);
        if (edge.fromNode >= 0 && edge.toNode >= 0 &&
            edge.fromNode < nodes.size() && edge.toNode < nodes.size()) {
            validEdgeIndices.append(i);
        }
    }
    
    if (validEdgeIndices.isEmpty()) {
        qWarning() << "Aucune arête valide trouvée pour générer des véhicules";
        return;
    }

    std::uniform_int_distribution<int> edgeDist(0, validEdgeIndices.size() - 1);
    
    // Pour éviter de mettre plusieurs véhicules trop proches sur la même arête,
    // on garde une trace des positions déjà utilisées par arête
    QHash<int, QVector<double>> edgePositions; // edgeIndex -> positions déjà utilisées
    
    int vehicleId = 1;
    int attempts = 0;
    const int maxAttempts = count * 50; // Limite pour éviter boucle infinie
    const double minDistanceOnEdge = 0.05; // Distance minimale entre véhicules sur la même arête (5%)
    
    while (vehicleId <= count && attempts < maxAttempts) {
        attempts++;
        
        // Sélectionner une arête aléatoire
        int randomEdgeIdx = validEdgeIndices.at(edgeDist(rng));
        const RoadEdge& edge = edges.at(randomEdgeIdx);
        
        const RoadNode& fromNode = nodes.at(edge.fromNode);
        const RoadNode& toNode = nodes.at(edge.toNode);
        
        // Générer une position aléatoire sur l'arête
        double t = tDist(rng);
        
        // Vérifier que cette position n'est pas trop proche d'un autre véhicule sur la même arête
        bool tooClose = false;
        if (edgePositions.contains(randomEdgeIdx)) {
            for (double existingT : edgePositions[randomEdgeIdx]) {
                if (std::abs(t - existingT) < minDistanceOnEdge) {
                    tooClose = true;
                    break;
                }
            }
        }
        
        if (tooClose) {
            continue; // Essayer une autre position
        }
        
        // Calculer les coordonnées géographiques
        double lat = fromNode.lat + (toNode.lat - fromNode.lat) * t;
        double lon = fromNode.lon + (toNode.lon - fromNode.lon) * t;

        Vehicle vehicle;
        vehicle.setId(vehicleId);
        vehicle.setLatLon(lat, lon);
        vehicle.setSpeedKmh(edge.maxSpeedKmh);
        vehicle.setTransmissionRadiusMeters(radiusDist(rng));
        vehicle.setEdgeId(edge.id);
        vehicle.setHighwayType(edge.highwayType);
        vehicle.setEdgeIndex(randomEdgeIdx);
        vehicle.setPositionOnEdge(t);
        vehicle.setMovingForward(directionDist(rng) == 1 || edge.oneway);
        
        m_vehicles.append(vehicle);
        
        // Enregistrer cette position pour cette arête
        edgePositions[randomEdgeIdx].append(t);
        
        ++vehicleId;
    }
    
    qInfo() << "Véhicules générés:" << m_vehicles.size() << "sur" << count << "demandés";
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

    constexpr double radiusPixels = 5.0;

    for (const Vehicle& vehicle : std::as_const(m_vehicles)) {
        QPointF pos = lonLatToScene(vehicle.longitude(), vehicle.latitude(), m_zoom);
        QRectF rect(pos.x() - radiusPixels, pos.y() - radiusPixels, radiusPixels * 2, radiusPixels * 2);
        
        // Obtenir la couleur selon l'état du véhicule (alerte, etc.)
        QColor vehicleColor = getVehicleColor(vehicle);
        QBrush brush(vehicleColor);
        
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
    
    // Mettre à jour les connexions V2V après le rechargement des véhicules
    // Toujours mettre à jour si on a des véhicules chargés
    if (m_roadGraphLoaded && !m_vehicles.isEmpty()) {
        if (m_showV2VConnections) {
            updateConnectionGraphics();
        }
        if (m_showDensityHeatmap) {
            updateDensityHeatmap();
        }
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
    // Repositionner le panneau de contrôle si nécessaire
    if (m_controlPanel) {
        m_controlPanel->move(10, 10);
    }
}

void MapView::createZoomControls() {
    if (m_zoomInButton || m_zoomOutButton) return;

    m_zoomInButton = new QToolButton(this);
    m_zoomInButton->setText("+");
    m_zoomInButton->setAutoRaise(true);
    m_zoomInButton->setToolTip(tr("Zoom in"));
    connect(m_zoomInButton, &QToolButton::clicked, [this]() {
        zoomToLevel(std::min(19, m_zoom + 1));
    });

    m_zoomOutButton = new QToolButton(this);
    m_zoomOutButton->setText("-");
    m_zoomOutButton->setAutoRaise(true);
    m_zoomOutButton->setToolTip(tr("Zoom out"));
    connect(m_zoomOutButton, &QToolButton::clicked, [this]() {
        zoomToLevel(std::max(0, m_zoom - 1));
    });

    // Bouton pour charger un fichier OSM
    m_loadOsmButton = new QToolButton(this);
    m_loadOsmButton->setText("📁");
    m_loadOsmButton->setAutoRaise(true);
    m_loadOsmButton->setToolTip(tr("Charger un fichier OSM"));
    connect(m_loadOsmButton, &QToolButton::clicked, this, &MapView::onLoadOsmClicked);

    // Bouton Play/Pause
    m_playPauseButton = new QToolButton(this);
    m_playPauseButton->setText("▶");
    m_playPauseButton->setAutoRaise(true);
    m_playPauseButton->setToolTip(tr("Play/Pause"));
    connect(m_playPauseButton, &QToolButton::clicked, this, &MapView::onPlayPauseClicked);

    // Bouton de vitesse
    m_speedButton = new QToolButton(this);
    m_speedButton->setText(m_speedLabels.at(m_currentSpeedIndex));
    m_speedButton->setAutoRaise(true);
    m_speedButton->setToolTip(tr("Vitesse de simulation"));
    
    QMenu* speedMenu = new QMenu(this);
    for (int i = 0; i < m_speedLabels.size(); ++i) {
        QAction* action = speedMenu->addAction(m_speedLabels.at(i));
        action->setData(i);
        connect(action, &QAction::triggered, [this, i]() {
            onSpeedChanged(i);
        });
    }
    m_speedButton->setMenu(speedMenu);
    m_speedButton->setPopupMode(QToolButton::InstantPopup);

    // Bouton pour activer/désactiver la heatmap de densité
    m_densityHeatmapButton = new QToolButton(this);
    m_densityHeatmapButton->setText("📊");
    m_densityHeatmapButton->setAutoRaise(true);
    m_densityHeatmapButton->setCheckable(true);
    m_densityHeatmapButton->setChecked(m_showDensityHeatmap);
    m_densityHeatmapButton->setToolTip(tr("Afficher/Masquer la heatmap de densité"));
    connect(m_densityHeatmapButton, &QToolButton::toggled, this, &MapView::onDensityHeatmapToggled);

    // Bouton pour activer/désactiver les connexions V2V
    m_v2vConnectionsButton = new QToolButton(this);
    m_v2vConnectionsButton->setText("🔗");
    m_v2vConnectionsButton->setAutoRaise(true);
    m_v2vConnectionsButton->setCheckable(true);
    m_v2vConnectionsButton->setChecked(m_showV2VConnections);
    m_v2vConnectionsButton->setToolTip(tr("Afficher/Masquer les connexions V2V"));
    connect(m_v2vConnectionsButton, &QToolButton::toggled, this, &MapView::onV2VConnectionsToggled);

    // Bouton pour déclencher une alerte sur le véhicule sélectionné
    m_triggerAlertButton = new QToolButton(this);
    m_triggerAlertButton->setText("🚨");
    m_triggerAlertButton->setAutoRaise(true);
    m_triggerAlertButton->setToolTip(tr("Déclencher une alerte pour le véhicule sélectionné"));
    connect(m_triggerAlertButton, &QToolButton::clicked, this, &MapView::onTriggerAlertClicked);

    // Bouton pour afficher/masquer les échanges V2V
    m_showV2VExchangesButton = new QToolButton(this);
    m_showV2VExchangesButton->setText("📡");
    m_showV2VExchangesButton->setAutoRaise(true);
    m_showV2VExchangesButton->setCheckable(true);
    m_showV2VExchangesButton->setChecked(m_showV2VExchanges);
    m_showV2VExchangesButton->setToolTip(tr("Afficher/Masquer les échanges V2V"));
    connect(m_showV2VExchangesButton, &QToolButton::toggled, this, &MapView::onShowV2VExchangesToggled);

    positionZoomControls();
    updateZoomButtons();
}

void MapView::positionZoomControls() {
    if (!m_zoomInButton || !m_zoomOutButton || !m_loadOsmButton) return;

    const int margin = 12;
    QSize buttonSize = QSize(28, 28);

    QPoint basePos = QPoint(width() - buttonSize.width() - margin, margin);
    m_zoomInButton->move(basePos);
    m_zoomInButton->resize(buttonSize);

    QPoint outPos = basePos + QPoint(0, buttonSize.height() + 6);
    m_zoomOutButton->move(outPos);
    m_zoomOutButton->resize(buttonSize);

    // Positionner le bouton OSM en dessous du bouton zoom out
    QPoint osmPos = outPos + QPoint(0, buttonSize.height() + 6);
    m_loadOsmButton->move(osmPos);
    m_loadOsmButton->resize(buttonSize);

    // Positionner le bouton Play/Pause
    QPoint playPos = osmPos + QPoint(0, buttonSize.height() + 6);
    if (m_playPauseButton) {
        m_playPauseButton->move(playPos);
        m_playPauseButton->resize(buttonSize);
    }

    // Positionner le bouton de vitesse
    QPoint speedPos = playPos + QPoint(0, buttonSize.height() + 6);
    if (m_speedButton) {
        m_speedButton->move(speedPos);
        m_speedButton->resize(buttonSize);
    }

    // Positionner le bouton heatmap de densité
    QPoint densityPos = speedPos + QPoint(0, buttonSize.height() + 6);
    if (m_densityHeatmapButton) {
        m_densityHeatmapButton->move(densityPos);
        m_densityHeatmapButton->resize(buttonSize);
    }

    // Positionner le bouton connexions V2V
    QPoint v2vPos = densityPos + QPoint(0, buttonSize.height() + 6);
    if (m_v2vConnectionsButton) {
        m_v2vConnectionsButton->move(v2vPos);
        m_v2vConnectionsButton->resize(buttonSize);
    }

    // Positionner le bouton déclencher alerte
    QPoint alertPos = v2vPos + QPoint(0, buttonSize.height() + 6);
    if (m_triggerAlertButton) {
        m_triggerAlertButton->move(alertPos);
        m_triggerAlertButton->resize(buttonSize);
    }

    // Positionner le bouton afficher échanges V2V
    QPoint exchangesPos = alertPos + QPoint(0, buttonSize.height() + 6);
    if (m_showV2VExchangesButton) {
        m_showV2VExchangesButton->move(exchangesPos);
        m_showV2VExchangesButton->resize(buttonSize);
    }
}

void MapView::updateZoomButtons() {
    if (!m_zoomInButton || !m_zoomOutButton) return;
    m_zoomInButton->setEnabled(m_zoom < 19);
    m_zoomOutButton->setEnabled(m_zoom > 0);
}

void MapView::onLoadOsmClicked() {
    QString osmPath = QFileDialog::getOpenFileName(
        this,
        tr("Ouvrir un fichier OSM"),
        QString(),
        tr("Fichiers OSM (*.osm *.osm.pbf);;Tous les fichiers (*.*)"));
    
    if (!osmPath.isEmpty()) {
        loadRoadGraphFromFile(osmPath);
    }
}

void MapView::initializeSimulation() {
    m_simulationTimer = new QTimer(this);
    m_simulationTimer->setInterval(16); // ~60 FPS
    connect(m_simulationTimer, &QTimer::timeout, this, &MapView::onSimulationTick);
    m_lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
    
    // Timer pour messages CAM périodiques (500ms)
    m_camMessageTimer = new QTimer(this);
    m_camMessageTimer->setInterval(CAM_INTERVAL_MS);
    connect(m_camMessageTimer, &QTimer::timeout, this, &MapView::onCAMTimerTimeout);
    m_lastCamSendTime = QDateTime::currentMSecsSinceEpoch();
}

void MapView::onSimulationTick() {
    if (!m_simulationRunning || !m_roadGraphLoaded) return;
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 deltaTimeMs = currentTime - m_lastUpdateTime;
    if (deltaTimeMs <= 0) {
        m_lastUpdateTime = currentTime;
        return;
    }
    
    double deltaTimeSeconds = (deltaTimeMs / 1000.0) * m_simulationSpeed;
    m_lastUpdateTime = currentTime;
    
    updateVehiclePositions(deltaTimeSeconds);
    
    // Détecter les arrêts brutaux pour déclencher des alertes
    detectEmergencyStop();
    
    // Traiter les messages V2V reçus
    processV2VMessages();
    
    detectV2VConnections();
    updateVehicleVisualization(); // Mettre à jour les couleurs selon les alertes
    reloadVehicleGraphics();
    
    // Mettre à jour les connexions V2V si activées
    if (m_showV2VConnections) {
        updateConnectionGraphics();
    } else {
        clearConnectionGraphics();
    }
    
    // Mettre à jour la heatmap de densité si activée
    if (m_showDensityHeatmap) {
        updateDensityHeatmap();
    } else {
        clearDensityHeatmap();
    }
}

void MapView::updateVehiclePositions(double deltaTimeSeconds) {
    const auto& edges = m_roadGraph.edges();
    const auto& nodes = m_roadGraph.nodes();
    
    for (Vehicle& vehicle : m_vehicles) {
        int edgeIdx = vehicle.edgeIndex();
        if (edgeIdx < 0 || edgeIdx >= edges.size()) continue;
        
        const RoadEdge& edge = edges.at(edgeIdx);
        if (edge.fromNode < 0 || edge.toNode < 0 ||
            edge.fromNode >= nodes.size() || edge.toNode >= nodes.size()) {
            continue;
        }
        
        // Mettre à jour la position sur l'arête
        vehicle.updatePosition(deltaTimeSeconds, edge.lengthMeters);
        
        // Vérifier si le véhicule atteint une extrémité de l'arête
        if ((vehicle.isMovingForward() && vehicle.positionOnEdge() >= 1.0) ||
            (!vehicle.isMovingForward() && vehicle.positionOnEdge() <= 0.0)) {
            updateVehicleOnEdge(vehicle, deltaTimeSeconds);
        } else {
            // Mettre à jour les coordonnées géographiques
            const RoadNode& fromNode = nodes.at(edge.fromNode);
            const RoadNode& toNode = nodes.at(edge.toNode);
            double t = vehicle.positionOnEdge();
            
            if (!vehicle.isMovingForward()) {
                t = 1.0 - t; // Inverser pour le sens inverse
            }
            
            double lat = fromNode.lat + (toNode.lat - fromNode.lat) * t;
            double lon = fromNode.lon + (toNode.lon - fromNode.lon) * t;
            vehicle.setLatLon(lat, lon);
        }
    }
}

void MapView::updateVehicleOnEdge(Vehicle& vehicle, double deltaTimeSeconds) {
    const auto& edges = m_roadGraph.edges();
    const auto& nodes = m_roadGraph.nodes();
    
    int currentEdgeIdx = vehicle.edgeIndex();
    if (currentEdgeIdx < 0 || currentEdgeIdx >= edges.size()) return;
    
    const RoadEdge& currentEdge = edges.at(currentEdgeIdx);
    int currentNodeIdx = vehicle.isMovingForward() ? currentEdge.toNode : currentEdge.fromNode;
    
    if (currentNodeIdx < 0 || currentNodeIdx >= nodes.size()) return;
    
    // Trouver la prochaine arête
    int nextEdgeIdx = selectNextEdge(currentNodeIdx, currentEdgeIdx, vehicle.isMovingForward());
    
    if (nextEdgeIdx >= 0 && nextEdgeIdx < edges.size()) {
        const RoadEdge& nextEdge = edges.at(nextEdgeIdx);
        
        // Déterminer la direction sur la nouvelle arête
        bool movingForward = (nextEdge.fromNode == currentNodeIdx);
        
        // Calculer la position restante après avoir quitté l'ancienne arête
        double remainingProgress = 0.0;
        if (vehicle.isMovingForward() && vehicle.positionOnEdge() >= 1.0) {
            remainingProgress = (vehicle.positionOnEdge() - 1.0) * currentEdge.lengthMeters;
        } else if (!vehicle.isMovingForward() && vehicle.positionOnEdge() <= 0.0) {
            remainingProgress = (-vehicle.positionOnEdge()) * currentEdge.lengthMeters;
        }
        
        // Positionner le véhicule sur la nouvelle arête
        if (nextEdge.lengthMeters > 0) {
            double newPosition = remainingProgress / nextEdge.lengthMeters;
            if (!movingForward) {
                newPosition = 1.0 - newPosition;
            }
            vehicle.setPositionOnEdge(std::clamp(newPosition, 0.0, 1.0));
        } else {
            vehicle.setPositionOnEdge(movingForward ? 0.0 : 1.0);
        }
        
        vehicle.setEdgeIndex(nextEdgeIdx);
        vehicle.setEdgeId(nextEdge.id);
        vehicle.setMovingForward(movingForward);
        vehicle.setSpeedKmh(nextEdge.maxSpeedKmh);
        vehicle.setHighwayType(nextEdge.highwayType);
        
        // Mettre à jour les coordonnées
        const RoadNode& fromNode = nodes.at(nextEdge.fromNode);
        const RoadNode& toNode = nodes.at(nextEdge.toNode);
        double t = vehicle.positionOnEdge();
        if (!movingForward) {
            t = 1.0 - t;
        }
        double lat = fromNode.lat + (toNode.lat - fromNode.lat) * t;
        double lon = fromNode.lon + (toNode.lon - fromNode.lon) * t;
        vehicle.setLatLon(lat, lon);
    } else {
        // Pas de prochaine arête trouvée, inverser la direction ou rester sur place
        vehicle.setMovingForward(!vehicle.isMovingForward());
        if (vehicle.isMovingForward()) {
            vehicle.setPositionOnEdge(0.0);
        } else {
            vehicle.setPositionOnEdge(1.0);
        }
        
        // Mettre à jour les coordonnées pour la position actuelle
        const RoadNode& fromNode = nodes.at(currentEdge.fromNode);
        const RoadNode& toNode = nodes.at(currentEdge.toNode);
        double t = vehicle.positionOnEdge();
        if (!vehicle.isMovingForward()) {
            t = 1.0 - t;
        }
        double lat = fromNode.lat + (toNode.lat - fromNode.lat) * t;
        double lon = fromNode.lon + (toNode.lon - fromNode.lon) * t;
        vehicle.setLatLon(lat, lon);
    }
}

int MapView::selectNextEdge(int currentNodeIndex, int currentEdgeIndex, bool movingForward) {
    const auto& nodes = m_roadGraph.nodes();
    if (currentNodeIndex < 0 || currentNodeIndex >= nodes.size()) return -1;
    
    const RoadNode& node = nodes.at(currentNodeIndex);
    const auto& edges = m_roadGraph.edges();
    
    // Trouver toutes les arêtes sortantes de ce nœud
    QVector<int> candidateEdges;
    for (int i = 0; i < edges.size(); ++i) {
        const RoadEdge& edge = edges.at(i);
        if (edge.fromNode == currentNodeIndex && i != currentEdgeIndex) {
            candidateEdges.append(i);
        } else if (!edge.oneway && edge.toNode == currentNodeIndex && i != currentEdgeIndex) {
            candidateEdges.append(i);
        }
    }
    
    if (candidateEdges.isEmpty()) {
        // Chercher une arête bidirectionnelle en sens inverse
        for (int i = 0; i < edges.size(); ++i) {
            const RoadEdge& edge = edges.at(i);
            if (edge.toNode == currentNodeIndex && !edge.oneway && i != currentEdgeIndex) {
                candidateEdges.append(i);
            }
        }
    }
    
    if (candidateEdges.isEmpty()) return -1;
    
    // Sélectionner aléatoirement une arête candidate
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, candidateEdges.size() - 1);
    return candidateEdges.at(dist(rng));
}

void MapView::detectV2VConnections() {
    // Cette fonction sera appelée à chaque tick pour mettre à jour les connexions
    // Les connexions sont calculées dans updateConnectionGraphics pour éviter de stocker l'état
}

void MapView::onDensityHeatmapToggled() {
    m_showDensityHeatmap = m_densityHeatmapButton->isChecked();
    if (m_showDensityHeatmap) {
        updateDensityHeatmap();
    } else {
        clearDensityHeatmap();
    }
}

void MapView::onV2VConnectionsToggled() {
    m_showV2VConnections = m_v2vConnectionsButton->isChecked();
    if (m_showV2VConnections) {
        updateConnectionGraphics();
    } else {
        clearConnectionGraphics();
    }
}

void MapView::updateConnectionGraphics() {
    clearConnectionGraphics();
    
    if (m_vehicles.isEmpty() || !m_showV2VConnections) return;
    
    QPen connectionPen(QColor(0, 255, 0, 150)); // Vert semi-transparent
    connectionPen.setWidthF(1.0);
    connectionPen.setCosmetic(true);
    connectionPen.setStyle(Qt::DashLine);
    
    // Optimisation : utiliser une grille spatiale pour réduire les comparaisons
    // Taille de cellule : environ 1 km (0.009 degrés de latitude ≈ 1 km)
    const double cellSize = 0.009; // ~1 km
    QHash<QPair<int, int>, SpatialGridCell> grid;
    buildSpatialGrid(grid, cellSize);
    
    // Parcourir les cellules de la grille et leurs voisines
    QSet<QPair<int, int>> processedPairs;
    
    for (auto gridIt = grid.constBegin(); gridIt != grid.constEnd(); ++gridIt) {
        const QPair<int, int>& cellKey = gridIt.key();
        const SpatialGridCell& cell = gridIt.value();
        
        // Vérifier les véhicules dans la même cellule
        for (int i = 0; i < cell.vehicleIndices.size(); ++i) {
            int idx1 = cell.vehicleIndices.at(i);
            for (int j = i + 1; j < cell.vehicleIndices.size(); ++j) {
                int idx2 = cell.vehicleIndices.at(j);
                QPair<int, int> pairKey = qMakePair(qMin(idx1, idx2), qMax(idx1, idx2));
                if (processedPairs.contains(pairKey)) continue;
                processedPairs.insert(pairKey);
                
                const Vehicle& v1 = m_vehicles.at(idx1);
                const Vehicle& v2 = m_vehicles.at(idx2);
                
                double distance = calculateDistance(v1.latitude(), v1.longitude(),
                                                    v2.latitude(), v2.longitude());
                if (distance <= (v1.transmissionRadiusMeters() + v2.transmissionRadiusMeters())) {
                    QPointF p1 = lonLatToScene(v1.longitude(), v1.latitude(), m_zoom);
                    QPointF p2 = lonLatToScene(v2.longitude(), v2.latitude(), m_zoom);
                    auto* line = m_scene->addLine(QLineF(p1, p2), connectionPen);
                    line->setZValue(20);
                    m_connectionGraphics.append(line);
                }
            }
        }
        
        // Vérifier avec les cellules voisines (8 voisines)
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0) continue;
                QPair<int, int> neighborKey = qMakePair(cellKey.first + dx, cellKey.second + dy);
                auto neighborIt = grid.find(neighborKey);
                if (neighborIt == grid.end()) continue;
                
                const SpatialGridCell& neighborCell = neighborIt.value();
                for (int idx1 : cell.vehicleIndices) {
                    for (int idx2 : neighborCell.vehicleIndices) {
                        QPair<int, int> pairKey = qMakePair(qMin(idx1, idx2), qMax(idx1, idx2));
                        if (processedPairs.contains(pairKey)) continue;
                        processedPairs.insert(pairKey);
                        
                        const Vehicle& v1 = m_vehicles.at(idx1);
                        const Vehicle& v2 = m_vehicles.at(idx2);
                        
                        double distance = calculateDistance(v1.latitude(), v1.longitude(),
                                                            v2.latitude(), v2.longitude());
                        if (distance <= (v1.transmissionRadiusMeters() + v2.transmissionRadiusMeters())) {
                            QPointF p1 = lonLatToScene(v1.longitude(), v1.latitude(), m_zoom);
                            QPointF p2 = lonLatToScene(v2.longitude(), v2.latitude(), m_zoom);
                            auto* line = m_scene->addLine(QLineF(p1, p2), connectionPen);
                            line->setZValue(20);
                            m_connectionGraphics.append(line);
                        }
                    }
                }
            }
        }
    }
}

void MapView::buildSpatialGrid(QHash<QPair<int, int>, SpatialGridCell>& grid, double cellSize) const {
    grid.clear();
    for (int i = 0; i < m_vehicles.size(); ++i) {
        const Vehicle& v = m_vehicles.at(i);
        QPair<int, int> cellKey = getGridCell(v.latitude(), v.longitude(), cellSize);
        grid[cellKey].vehicleIndices.append(i);
    }
}

QPair<int, int> MapView::getGridCell(double lat, double lon, double cellSize) const {
    int cellX = static_cast<int>(std::floor(lon / cellSize));
    int cellY = static_cast<int>(std::floor(lat / cellSize));
    return qMakePair(cellX, cellY);
}

void MapView::clearConnectionGraphics() {
    for (QGraphicsLineItem* item : std::as_const(m_connectionGraphics)) {
        if (item) {
            m_scene->removeItem(item);
            delete item;
        }
    }
    m_connectionGraphics.clear();
}

double MapView::calculateDistance(double lat1, double lon1, double lat2, double lon2) const {
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

void MapView::onPlayPauseClicked() {
    m_simulationRunning = !m_simulationRunning;
    
    if (m_simulationRunning) {
        m_simulationTimer->start();
        m_camMessageTimer->start(); // Démarrer aussi le timer CAM
        m_lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
        m_lastCamSendTime = QDateTime::currentMSecsSinceEpoch();
        if (m_playPauseButton) {
            m_playPauseButton->setText("⏸");
            m_playPauseButton->setToolTip(tr("Pause"));
        }
    } else {
        m_simulationTimer->stop();
        m_camMessageTimer->stop(); // Arrêter aussi le timer CAM
        if (m_playPauseButton) {
            m_playPauseButton->setText("▶");
            m_playPauseButton->setToolTip(tr("Play"));
        }
    }
    
    // Mettre à jour le panneau de contrôle
    updateControlPanel();
}

void MapView::onSpeedChanged(int speedIndex) {
    if (speedIndex >= 0 && speedIndex < m_speedLabels.size()) {
        m_currentSpeedIndex = speedIndex;
        QString speedStr = m_speedLabels.at(speedIndex);
        speedStr.chop(1); // Enlever le 'x'
        m_simulationSpeed = speedStr.toDouble();
        if (m_speedButton) {
            m_speedButton->setText(m_speedLabels.at(speedIndex));
        }
        if (m_speedSlider) {
            m_speedSlider->setValue(speedIndex);
        }
        updateControlPanel();
    }
}

void MapView::updateDensityHeatmap() {
    clearDensityHeatmap();
    
    if (m_vehicles.isEmpty() || !m_roadGraphLoaded) return;
    
    // Taille de cellule : 100 mètres
    const double cellSizeMeters = 100.0;
    
    // Construire la grille de densité
    QHash<QPair<int, int>, DensityCell> densityGrid;
    buildDensityGrid(densityGrid, cellSizeMeters);
    
    if (densityGrid.isEmpty()) return;
    
    // Trouver le maximum de véhicules par cellule pour la normalisation
    int maxCount = 1;
    for (const auto& cell : densityGrid) {
        maxCount = std::max(maxCount, cell.vehicleCount);
    }
    
    // Créer les rectangles colorés
    for (const auto& cell : densityGrid) {
        if (cell.vehicleCount == 0) continue;
        
        QColor color = getDensityColor(cell.vehicleCount, maxCount);
        QBrush brush(color);
        QPen pen(Qt::NoPen);
        
        // Convertir les bounds en coordonnées scène
        QPointF topLeft = lonLatToScene(cell.bounds.left(), cell.bounds.top(), m_zoom);
        QPointF bottomRight = lonLatToScene(cell.bounds.right(), cell.bounds.bottom(), m_zoom);
        
        QRectF rect(topLeft, bottomRight);
        auto* rectItem = m_scene->addRect(rect, pen, brush);
        rectItem->setZValue(5); // En dessous des routes mais visible
        m_densityGridGraphics.append(rectItem);
    }
}

void MapView::clearDensityHeatmap() {
    for (QGraphicsRectItem* item : std::as_const(m_densityGridGraphics)) {
        if (item) {
            m_scene->removeItem(item);
            delete item;
        }
    }
    m_densityGridGraphics.clear();
}

QColor MapView::getDensityColor(int vehicleCount, int maxCount) const {
    if (maxCount == 0) return QColor(255, 255, 255, 0); // Transparent
    
    // Normaliser entre 0 et 1
    double normalized = static_cast<double>(vehicleCount) / static_cast<double>(maxCount);
    
    // Créer un gradient de couleur : jaune clair -> orange -> rouge foncé
    // Alpha varie de 50 (clair) à 200 (foncé)
    int alpha = 50 + static_cast<int>(normalized * 150);
    alpha = std::clamp(alpha, 50, 200);
    
    int red, green, blue;
    if (normalized < 0.5) {
        // Jaune clair -> orange
        double t = normalized * 2.0;
        red = 255;
        green = 255 - static_cast<int>(t * 100);
        blue = 0;
    } else {
        // Orange -> rouge foncé
        double t = (normalized - 0.5) * 2.0;
        red = 255;
        green = 155 - static_cast<int>(t * 155);
        blue = 0;
    }
    
    return QColor(red, green, blue, alpha);
}

void MapView::buildDensityGrid(QHash<QPair<int, int>, DensityCell>& densityGrid, double cellSizeMeters) const {
    densityGrid.clear();
    
    if (m_vehicles.isEmpty()) return;
    
    // Convertir la taille de cellule en degrés approximatifs
    // 1 degré de latitude ≈ 111 km, donc 100m ≈ 0.0009 degrés
    // Pour la longitude, ça dépend de la latitude, mais on utilise une approximation
    const double cellSizeDegrees = cellSizeMeters / 111000.0;
    
    // Trouver les bornes de la zone visible ou de tous les véhicules
    double minLat = std::numeric_limits<double>::max();
    double maxLat = std::numeric_limits<double>::lowest();
    double minLon = std::numeric_limits<double>::max();
    double maxLon = std::numeric_limits<double>::lowest();
    
    for (const Vehicle& vehicle : m_vehicles) {
        minLat = std::min(minLat, vehicle.latitude());
        maxLat = std::max(maxLat, vehicle.latitude());
        minLon = std::min(minLon, vehicle.longitude());
        maxLon = std::max(maxLon, vehicle.longitude());
    }
    
    if (minLat > maxLat || minLon > maxLon) return;
    
    // Créer les cellules de la grille
    int minCellX = static_cast<int>(std::floor(minLon / cellSizeDegrees));
    int maxCellX = static_cast<int>(std::ceil(maxLon / cellSizeDegrees));
    int minCellY = static_cast<int>(std::floor(minLat / cellSizeDegrees));
    int maxCellY = static_cast<int>(std::ceil(maxLat / cellSizeDegrees));
    
    // Initialiser toutes les cellules dans la zone
    for (int x = minCellX; x <= maxCellX; ++x) {
        for (int y = minCellY; y <= maxCellY; ++y) {
            QPair<int, int> key = qMakePair(x, y);
            DensityCell& cell = densityGrid[key];
            cell.gridKey = key;
            cell.vehicleCount = 0;
            cell.bounds = QRectF(
                x * cellSizeDegrees,
                y * cellSizeDegrees,
                cellSizeDegrees,
                cellSizeDegrees
            );
        }
    }
    
    // Compter les véhicules dans chaque cellule
    for (const Vehicle& vehicle : m_vehicles) {
        int cellX = static_cast<int>(std::floor(vehicle.longitude() / cellSizeDegrees));
        int cellY = static_cast<int>(std::floor(vehicle.latitude() / cellSizeDegrees));
        QPair<int, int> key = qMakePair(cellX, cellY);
        
        auto it = densityGrid.find(key);
        if (it != densityGrid.end()) {
            it->vehicleCount++;
        }
    }
}

void MapView::createControlPanel() {
    m_controlPanel = new QWidget(this);
    m_controlPanel->setObjectName("ControlPanel");
    m_controlPanel->setStyleSheet(
        "QWidget#ControlPanel {"
        "background-color: rgba(240, 240, 240, 240);"
        "border: 2px solid #888;"
        "border-radius: 5px;"
        "}"
        "QLabel { font-weight: bold; }"
        "QPushButton { min-width: 80px; }"
    );
    
    m_controlLayout = new QVBoxLayout(m_controlPanel);
    m_controlLayout->setSpacing(10);
    m_controlLayout->setContentsMargins(10, 10, 10, 10);
    
    // Titre
    QLabel* titleLabel = new QLabel(tr("Contrôles de Simulation"), m_controlPanel);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    m_controlLayout->addWidget(titleLabel);
    
    // Séparateur
    QFrame* separator1 = new QFrame();
    separator1->setFrameShape(QFrame::HLine);
    separator1->setFrameShadow(QFrame::Sunken);
    m_controlLayout->addWidget(separator1);
    
    // Contrôle Play/Pause
    m_playPausePanelButton = new QPushButton(tr("▶ Play"), m_controlPanel);
    m_playPausePanelButton->setCheckable(false);
    connect(m_playPausePanelButton, &QPushButton::clicked, this, &MapView::onPlayPauseClicked);
    m_controlLayout->addWidget(m_playPausePanelButton);
    
    // Contrôle de vitesse
    m_speedLabel = new QLabel(tr("Vitesse: 1.0x"), m_controlPanel);
    m_controlLayout->addWidget(m_speedLabel);
    
    m_speedSlider = new QSlider(Qt::Horizontal, m_controlPanel);
    m_speedSlider->setMinimum(0); // 0.5x
    m_speedSlider->setMaximum(3); // 5x
    m_speedSlider->setValue(1); // 1x par défaut
    m_speedSlider->setTickPosition(QSlider::TicksBelow);
    m_speedSlider->setTickInterval(1);
    connect(m_speedSlider, &QSlider::valueChanged, this, &MapView::onSpeedSliderChanged);
    m_controlLayout->addWidget(m_speedSlider);
    
    m_speedValueLabel = new QLabel(tr("1.0x"), m_controlPanel);
    m_speedValueLabel->setAlignment(Qt::AlignCenter);
    m_controlLayout->addWidget(m_speedValueLabel);
    
    // Séparateur
    QFrame* separator2 = new QFrame();
    separator2->setFrameShape(QFrame::HLine);
    separator2->setFrameShadow(QFrame::Sunken);
    m_controlLayout->addWidget(separator2);
    
    // Contrôle du nombre de véhicules
    m_vehicleCountLabel = new QLabel(tr("Nombre de véhicules:"), m_controlPanel);
    m_controlLayout->addWidget(m_vehicleCountLabel);
    
    QHBoxLayout* vehicleCountLayout = new QHBoxLayout();
    m_vehicleCountSpinBox = new QSpinBox(m_controlPanel);
    m_vehicleCountSpinBox->setMinimum(1);
    m_vehicleCountSpinBox->setMaximum(5000);
    m_vehicleCountSpinBox->setValue(30);
    m_vehicleCountSpinBox->setSuffix(tr(" véhicules"));
    vehicleCountLayout->addWidget(m_vehicleCountSpinBox);
    
    m_applyVehicleCountButton = new QPushButton(tr("Appliquer"), m_controlPanel);
    connect(m_applyVehicleCountButton, &QPushButton::clicked, [this]() {
        onVehicleCountChanged(m_vehicleCountSpinBox->value());
    });
    vehicleCountLayout->addWidget(m_applyVehicleCountButton);
    m_controlLayout->addLayout(vehicleCountLayout);
    
    m_controlPanel->setLayout(m_controlLayout);
    m_controlPanel->resize(280, 350);
    m_controlPanel->move(10, 10);
    m_controlPanel->show();
    
    // Mettre à jour l'état initial
    updateControlPanel();
}

void MapView::updateControlPanel() {
    if (!m_controlPanel) return;
    
    // Mettre à jour le bouton play/pause
    if (m_playPausePanelButton) {
        if (m_simulationRunning) {
            m_playPausePanelButton->setText(tr("⏸ Pause"));
        } else {
            m_playPausePanelButton->setText(tr("▶ Play"));
        }
    }
    
    // Mettre à jour le label de vitesse
    if (m_speedValueLabel) {
        m_speedValueLabel->setText(m_speedLabels.at(m_currentSpeedIndex));
    }
    if (m_speedLabel) {
        m_speedLabel->setText(tr("Vitesse: %1").arg(m_speedLabels.at(m_currentSpeedIndex)));
    }
}

void MapView::onSpeedSliderChanged(int value) {
    if (value >= 0 && value < m_speedLabels.size()) {
        onSpeedChanged(value);
        updateControlPanel();
    }
}

void MapView::onVehicleCountChanged(int count) {
    if (!m_roadGraphLoaded) {
        qWarning() << "Aucun graphe routier chargé. Veuillez charger un fichier OSM d'abord.";
        return;
    }
    
    // Arrêter la simulation si elle tourne
    bool wasRunning = m_simulationRunning;
    if (wasRunning) {
        m_simulationTimer->stop();
        m_simulationRunning = false;
    }
    
    // Générer les nouveaux véhicules
    generateVehicles(count);
    
    // Recharger les graphiques
    reloadVehicleGraphics();
    
    // Reprendre la simulation si elle était en cours
    if (wasRunning) {
        m_simulationRunning = true;
        m_simulationTimer->start();
        m_lastUpdateTime = QDateTime::currentMSecsSinceEpoch();
    }
    
    qInfo() << "Nombre de véhicules changé à:" << m_vehicles.size();
}

int MapView::findVehicleAtPosition(const QPointF& scenePos) const {
    constexpr double clickRadius = 10.0; // Rayon de détection en pixels
    
    for (int i = 0; i < m_vehicles.size(); ++i) {
        const Vehicle& vehicle = m_vehicles.at(i);
        QPointF vehiclePos = lonLatToScene(vehicle.longitude(), vehicle.latitude(), m_zoom);
        
        double distance = QLineF(scenePos, vehiclePos).length();
        if (distance <= clickRadius) {
            return i;
        }
    }
    
    return -1;
}

void MapView::showVehicleInfoDialog(int vehicleIndex) {
    if (vehicleIndex < 0 || vehicleIndex >= m_vehicles.size()) return;
    
    const Vehicle& vehicle = m_vehicles.at(vehicleIndex);
    
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Informations du Véhicule #%1").arg(vehicle.id()));
    dialog->setModal(true);
    dialog->resize(400, 300);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(dialog);
    
    QFormLayout* formLayout = new QFormLayout();
    
    // Informations de base
    formLayout->addRow(tr("ID:"), new QLabel(QString::number(vehicle.id()), dialog));
    formLayout->addRow(tr("Latitude:"), new QLabel(QString::number(vehicle.latitude(), 'f', 8), dialog));
    formLayout->addRow(tr("Longitude:"), new QLabel(QString::number(vehicle.longitude(), 'f', 8), dialog));
    
    // Informations de mouvement
    formLayout->addRow(tr("Vitesse:"), new QLabel(QString::number(vehicle.speedKmh(), 'f', 1) + tr(" km/h"), dialog));
    formLayout->addRow(tr("Position sur route:"), new QLabel(QString::number(vehicle.positionOnEdge() * 100, 'f', 1) + tr("%"), dialog));
    formLayout->addRow(tr("Direction:"), new QLabel(vehicle.isMovingForward() ? tr("Avant") : tr("Arrière"), dialog));
    
    // Informations de communication
    formLayout->addRow(tr("Rayon de transmission:"), new QLabel(QString::number(vehicle.transmissionRadiusMeters(), 'f', 1) + tr(" m"), dialog));
    
    // Informations de route
    formLayout->addRow(tr("Type de route:"), new QLabel(vehicle.highwayType(), dialog));
    formLayout->addRow(tr("ID de l'arête:"), new QLabel(QString::number(vehicle.edgeId()), dialog));
    
    // Compter les connexions V2V
    int connectionCount = 0;
    if (m_showV2VConnections) {
        for (int i = 0; i < m_vehicles.size(); ++i) {
            if (i == vehicleIndex) continue;
            const Vehicle& other = m_vehicles.at(i);
            double distance = calculateDistance(vehicle.latitude(), vehicle.longitude(),
                                                other.latitude(), other.longitude());
            if (distance <= (vehicle.transmissionRadiusMeters() + other.transmissionRadiusMeters())) {
                connectionCount++;
            }
        }
    }
    formLayout->addRow(tr("Connexions V2V actives:"), new QLabel(QString::number(connectionCount), dialog));
    
    // Statistiques des messages V2V
    formLayout->addRow(tr("Messages envoyés:"), new QLabel(QString::number(vehicle.messagesSent()), dialog));
    formLayout->addRow(tr("Messages reçus:"), new QLabel(QString::number(vehicle.messagesReceived()), dialog));
    formLayout->addRow(tr("Alertes relayées:"), new QLabel(QString::number(vehicle.alertsRelayed()), dialog));
    
    // État des alertes
    QString alertStatus;
    if (vehicle.hasActiveAlert()) {
        alertStatus = tr("🚨 Alerte active");
    } else if (vehicle.hasReceivedAlert()) {
        alertStatus = tr("⚠️ Alerte reçue");
    } else {
        alertStatus = tr("✅ Normal");
    }
    formLayout->addRow(tr("État:"), new QLabel(alertStatus, dialog));
    
    mainLayout->addLayout(formLayout);
    
    // Bouton de fermeture
    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    mainLayout->addWidget(buttonBox);
    
    dialog->setLayout(mainLayout);
    dialog->exec();
    delete dialog;
}

// ========== Système de messages V2V ==========

void MapView::onCAMTimerTimeout() {
    if (!m_simulationRunning || !m_roadGraphLoaded) return;
    sendCAMMessages();
}

void MapView::sendCAMMessages() {
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    
    for (Vehicle& vehicle : m_vehicles) {
        // Créer un message CAM avec position et vitesse actuelles
        V2VMessage camMessage(V2VMessageType::CAM, 
                              vehicle.id(),
                              vehicle.latitude(),
                              vehicle.longitude(),
                              vehicle.speedKmh(),
                              1); // TTL = 1 pour CAM (pas de relais)
        
        // Trouver tous les véhicules à portée
        for (int i = 0; i < m_vehicles.size(); ++i) {
            if (i == vehicle.id() - 1) continue; // Ne pas s'envoyer à soi-même
            
            Vehicle& receiver = m_vehicles[i];
            double distance = calculateDistance(vehicle.latitude(), vehicle.longitude(),
                                              receiver.latitude(), receiver.longitude());
            
            if (distance <= (vehicle.transmissionRadiusMeters() + receiver.transmissionRadiusMeters())) {
                receiver.addMessageToInbox(camMessage);
            }
        }
        
        vehicle.incrementMessagesSent();
    }
    
    m_lastCamSendTime = currentTime;
}

void MapView::processV2VMessages() {
    for (Vehicle& vehicle : m_vehicles) {
        processVehicleInbox(vehicle);
    }
}

void MapView::processVehicleInbox(Vehicle& vehicle) {
    for (const V2VMessage& message : vehicle.getInbox()) {
        // Vérifier si le message a déjà été traité (éviter boucles)
        if (vehicle.getProcessedMessageIds().contains(message.messageId)) {
            continue;
        }
        
        vehicle.addProcessedMessageId(message.messageId);
        vehicle.incrementMessagesReceived();
        
        if (message.type == V2VMessageType::ALERT) {
            // Marquer le véhicule comme ayant reçu une alerte
            vehicle.setReceivedAlert(true);
            
            // Relayer l'alerte si TTL > 0
            if (message.ttl > 0) {
                int vehicleIndex = vehicle.id() - 1;
                if (vehicleIndex >= 0 && vehicleIndex < m_vehicles.size()) {
                    relayAlertMessage(message, vehicleIndex);
                    vehicle.incrementAlertsRelayed();
                }
            }
        }
    }
    
    // Vider l'inbox après traitement
    vehicle.clearInbox();
    
    // Note: Le nettoyage des IDs traités n'est pas nécessaire car QSet gère efficacement
    // la mémoire. Si nécessaire, on pourrait limiter la taille avec une approche différente
    // (par exemple utiliser un QQueue avec timestamp pour garder l'ordre temporel)
}

void MapView::relayAlertMessage(const V2VMessage& alert, int receiverIndex) {
    if (receiverIndex < 0 || receiverIndex >= m_vehicles.size()) return;
    
    Vehicle& receiver = m_vehicles[receiverIndex];
    
    // Créer une copie pour relais avec TTL décrémenté
    V2VMessage relayedAlert = alert.createRelayCopy();
    
    // Trouver tous les véhicules à portée du relais
    for (int i = 0; i < m_vehicles.size(); ++i) {
        if (i == receiverIndex) continue;
        
        Vehicle& neighbor = m_vehicles[i];
        double distance = calculateDistance(receiver.latitude(), receiver.longitude(),
                                          neighbor.latitude(), neighbor.longitude());
        
        if (distance <= (receiver.transmissionRadiusMeters() + neighbor.transmissionRadiusMeters())) {
            neighbor.addMessageToInbox(relayedAlert);
        }
    }
}

void MapView::detectEmergencyStop() {
    const double speedDropThreshold = 30.0; // Réduction de vitesse de 30 km/h ou plus
    
    for (Vehicle& vehicle : m_vehicles) {
        double currentSpeed = vehicle.speedKmh();
        double previousSpeed = vehicle.previousSpeedKmh();
        
        // Initialiser la vitesse précédente si c'est la première fois
        if (previousSpeed == 0.0 && vehicle.messagesSent() == 0) {
            vehicle.setPreviousSpeedKmh(currentSpeed);
            continue;
        }
        
        // Détecter arrêt brutal : vitesse < seuil ET réduction importante
        if (currentSpeed < EMERGENCY_STOP_THRESHOLD && 
            previousSpeed > EMERGENCY_STOP_THRESHOLD &&
            (previousSpeed - currentSpeed) >= speedDropThreshold) {
            
            // Déclencher une alerte
            triggerAlertForVehicle(vehicle.id());
        }
        
        // Stocker la vitesse actuelle pour la prochaine itération
        vehicle.setPreviousSpeedKmh(currentSpeed);
    }
}

void MapView::triggerAlertForVehicle(int vehicleId) {
    int vehicleIndex = vehicleId - 1;
    if (vehicleIndex < 0 || vehicleIndex >= m_vehicles.size()) return;
    
    Vehicle& vehicle = m_vehicles[vehicleIndex];
    
    // Ne pas déclencher plusieurs alertes pour le même véhicule
    if (vehicle.hasActiveAlert()) return;
    
    vehicle.setActiveAlert(true);
    
    // Créer un message ALERT avec TTL = 3
    V2VMessage alertMessage(V2VMessageType::ALERT,
                            vehicle.id(),
                            vehicle.latitude(),
                            vehicle.longitude(),
                            vehicle.speedKmh(),
                            3); // TTL = 3 sauts
    
    // Envoyer l'alerte à tous les voisins à portée
    for (int i = 0; i < m_vehicles.size(); ++i) {
        if (i == vehicleIndex) continue;
        
        Vehicle& neighbor = m_vehicles[i];
        double distance = calculateDistance(vehicle.latitude(), vehicle.longitude(),
                                          neighbor.latitude(), neighbor.longitude());
        
        if (distance <= (vehicle.transmissionRadiusMeters() + neighbor.transmissionRadiusMeters())) {
            neighbor.addMessageToInbox(alertMessage);
        }
    }
    
    vehicle.incrementMessagesSent();
}

void MapView::updateVehicleVisualization() {
    // Cette fonction sera appelée depuis reloadVehicleGraphics
    // Les couleurs seront mises à jour dans reloadVehicleGraphics
}

QColor MapView::getVehicleColor(const Vehicle& vehicle) const {
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    const qint64 alertBlinkInterval = 500; // 500ms pour le clignotement
    const qint64 receivedAlertDuration = 3000; // 3 secondes pour l'orange
    
    // Véhicule avec alerte active -> rouge clignotant
    if (vehicle.hasActiveAlert()) {
        qint64 timeSinceAlert = currentTime - vehicle.alertTimestamp();
        bool blinkOn = (timeSinceAlert / alertBlinkInterval) % 2 == 0;
        if (blinkOn) {
            return QColor(255, 0, 0, 255); // Rouge vif
        } else {
            return QColor(200, 0, 0, 200); // Rouge foncé
        }
    }
    
    // Véhicule ayant reçu une alerte -> orange pendant quelques secondes
    if (vehicle.hasReceivedAlert()) {
        qint64 timeSinceReceived = currentTime - vehicle.receivedAlertTimestamp();
        if (timeSinceReceived < receivedAlertDuration) {
            return QColor(255, 165, 0, 220); // Orange
        } else {
            // Réinitialiser après la durée
            const_cast<Vehicle&>(vehicle).setReceivedAlert(false);
        }
    }
    
    // Couleur par défaut : bleu
    return QColor(0, 100, 255, 220);
}

void MapView::onTriggerAlertClicked() {
    if (m_selectedVehicleId > 0) {
        triggerAlertForVehicle(m_selectedVehicleId);
    } else {
        qWarning() << "Aucun véhicule sélectionné. Cliquez sur un véhicule d'abord.";
    }
}

void MapView::onShowV2VExchangesToggled() {
    m_showV2VExchanges = m_showV2VExchangesButton->isChecked();
    if (m_showV2VExchanges) {
        updateV2VExchangeVisualization();
    } else {
        clearV2VExchangeGraphics();
    }
}

void MapView::updateV2VExchangeVisualization() {
    clearV2VExchangeGraphics();
    
    if (m_vehicles.isEmpty() || !m_showV2VExchanges) return;
    
    // Afficher les lignes pour les messages en cours d'échange
    // On visualise les messages dans les inbox des véhicules
    QPen exchangePen(QColor(100, 200, 255, 120)); // Bleu clair semi-transparent pour les messages CAM
    exchangePen.setWidthF(1.5);
    exchangePen.setCosmetic(true);
    exchangePen.setStyle(Qt::DashLine);
    
    QPen alertPen(QColor(255, 100, 100, 180)); // Rouge clair pour les alertes
    alertPen.setWidthF(2.0);
    alertPen.setCosmetic(true);
    alertPen.setStyle(Qt::SolidLine);
    
    // Parcourir tous les véhicules et afficher les messages dans leur inbox
    for (int i = 0; i < m_vehicles.size(); ++i) {
        const Vehicle& receiver = m_vehicles.at(i);
        const QVector<V2VMessage>& inbox = receiver.getInbox();
        
        for (const V2VMessage& message : inbox) {
            // Trouver le véhicule émetteur
            int senderIndex = message.senderId - 1;
            if (senderIndex < 0 || senderIndex >= m_vehicles.size() || senderIndex == i) continue;
            
            const Vehicle& sender = m_vehicles.at(senderIndex);
            
            // Calculer la distance pour vérifier si le message est toujours valide
            double distance = calculateDistance(sender.latitude(), sender.longitude(),
                                              receiver.latitude(), receiver.longitude());
            
            // Afficher la ligne seulement si les véhicules sont toujours à portée
            if (distance <= (sender.transmissionRadiusMeters() + receiver.transmissionRadiusMeters())) {
                QPointF senderPos = lonLatToScene(sender.longitude(), sender.latitude(), m_zoom);
                QPointF receiverPos = lonLatToScene(receiver.longitude(), receiver.latitude(), m_zoom);
                
                // Utiliser une couleur différente pour les alertes
                QPen* penToUse = (message.type == V2VMessageType::ALERT) ? &alertPen : &exchangePen;
                
                auto* line = m_scene->addLine(QLineF(senderPos, receiverPos), *penToUse);
                line->setZValue(25); // Entre les connexions V2V (20) et les véhicules (30)
                m_v2vExchangeGraphics.append(line);
            }
        }
    }
}

void MapView::clearV2VExchangeGraphics() {
    for (QGraphicsLineItem* item : std::as_const(m_v2vExchangeGraphics)) {
        if (item) {
            m_scene->removeItem(item);
            delete item;
        }
    }
    m_v2vExchangeGraphics.clear();
}
