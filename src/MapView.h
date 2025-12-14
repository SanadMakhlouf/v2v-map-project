
#pragma once
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QVector>
#include <QString>
#include <QHash>
#include <QToolButton>
#include <QTimer>
#include <QSet>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QSlider>
#include <QDialog>
#include <QPushButton>

#include "TileManager.h"
#include "RoadGraph.h"
#include "Vehicle.h"
#include "V2VMessage.h"

class QGraphicsLineItem;
class QGraphicsEllipseItem;
class QGraphicsRectItem;

class MapView : public QGraphicsView {
    Q_OBJECT
public:
    MapView(QWidget* parent = nullptr);
    ~MapView() override;
    void setCenterLatLon(double lat, double lon, int zoom, bool preserveIfOutOfBounds = false);
    void zoomToLevel(int newZoom);
    bool loadRoadGraphFromFile(const QString& filePath);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTileReady(int z, int x, int y, const QPixmap& pix);
    void onLoadOsmClicked();
    void onSimulationTick();
    void onPlayPauseClicked();
    void onSpeedChanged(int speedIndex);
    void onDensityHeatmapToggled();
    void onV2VConnectionsToggled();
    void onVehicleCountChanged(int count);
    void onSpeedSliderChanged(int value);
    void onTriggerAlertClicked();
    void onShowV2VExchangesToggled();
    void onCAMTimerTimeout();

private:
    struct TileInfo {
        QGraphicsPixmapItem* item = nullptr;
        bool stillNeeded = false;
        bool loading = false;
    };

    TileManager m_tileManager;
    QGraphicsScene* m_scene;
    RoadGraph m_roadGraph;
    bool m_roadGraphLoaded = false;
    QVector<Vehicle> m_vehicles;
    int m_zoom = 12;
    double m_centerLat = 47.750839;
    double m_centerLon = 7.335888;
    QPoint m_lastPan;
    QPoint m_panStartPos;
    bool m_panning = false;
    double m_panStartCenterLat = 0.0;
    double m_panStartCenterLon = 0.0;

    QVector<QGraphicsLineItem*> m_roadGraphics;
    QVector<QGraphicsEllipseItem*> m_vehicleGraphics;
    QVector<QGraphicsLineItem*> m_connectionGraphics; // Lignes pour les connexions V2V
    QVector<QGraphicsRectItem*> m_densityGridGraphics; // Rectangles pour la heatmap de densité
    QVector<QGraphicsLineItem*> m_v2vExchangeGraphics; // Lignes temporaires pour visualiser les échanges de messages
    QHash<QString, TileInfo> m_tileItems;
    
    // Système de simulation
    QTimer* m_simulationTimer = nullptr;
    QTimer* m_camMessageTimer = nullptr; // Timer pour messages CAM périodiques (500ms)
    bool m_simulationRunning = false;
    double m_simulationSpeed = 1.0; // Multiplicateur de vitesse (0.5x, 1x, 2x, 5x)
    qint64 m_lastUpdateTime = 0;
    qint64 m_lastCamSendTime = 0; // Dernier envoi de messages CAM
    static constexpr qint64 CAM_INTERVAL_MS = 500; // Intervalle entre messages CAM (500ms)
    static constexpr double EMERGENCY_STOP_THRESHOLD = 5.0; // Seuil de vitesse pour arrêt brutal (km/h)
    
    // Contrôles UI
    QToolButton* m_playPauseButton = nullptr;
    QToolButton* m_speedButton = nullptr;
    QToolButton* m_densityHeatmapButton = nullptr;
    QToolButton* m_v2vConnectionsButton = nullptr;
    QToolButton* m_triggerAlertButton = nullptr;
    QToolButton* m_showV2VExchangesButton = nullptr;
    QStringList m_speedLabels = {"0.5x", "1x", "2x", "5x"};
    int m_currentSpeedIndex = 1; // Par défaut 1x
    bool m_showDensityHeatmap = false;
    bool m_showV2VConnections = true;
    bool m_showV2VExchanges = false;
    int m_selectedVehicleId = -1; // ID du véhicule sélectionné pour déclencher alerte

    QPointF lonLatToScene(double lon, double lat, int z) const;
    QPointF sceneToLonLat(const QPointF& scenePoint, int z) const;
    void loadVisibleTiles(const QPointF& centerScene = QPointF());
    void reloadRoadGraphics();
    void clearRoadGraphics();
    void generateVehicles(int count);
    void reloadVehicleGraphics();
    void clearVehicleGraphics();
    QString tileKey(int z, int x, int y) const;
    bool clampCenterToBounds(double& lat, double& lon) const;
    double normalizeLongitude(double lon) const;
    double clampLatitude(double lat) const;
    bool m_limitRegion = false;
    double m_minLat = -90.0;
    double m_maxLat = 90.0;
    double m_minLon = -180.0;
    double m_maxLon = 180.0;
    int TILE_SIZE = 256;

    QToolButton* m_zoomInButton = nullptr;
    QToolButton* m_zoomOutButton = nullptr;
    QToolButton* m_loadOsmButton = nullptr;
    void createZoomControls();
    void positionZoomControls();
    void updateZoomButtons();
    
    // Panneau de contrôle
    QWidget* m_controlPanel = nullptr;
    QVBoxLayout* m_controlLayout = nullptr;
    QLabel* m_speedLabel = nullptr;
    QSlider* m_speedSlider = nullptr;
    QLabel* m_speedValueLabel = nullptr;
    QPushButton* m_playPausePanelButton = nullptr;
    QLabel* m_vehicleCountLabel = nullptr;
    QSpinBox* m_vehicleCountSpinBox = nullptr;
    QPushButton* m_applyVehicleCountButton = nullptr;
    void createControlPanel();
    void updateControlPanel();
    
    // Détection de clic sur véhicules
    int findVehicleAtPosition(const QPointF& scenePos) const;
    void showVehicleInfoDialog(int vehicleIndex);
    
    // Système de messages V2V
    void processV2VMessages();
    void sendCAMMessages();
    void processVehicleInbox(Vehicle& vehicle);
    void relayAlertMessage(const V2VMessage& alert, int receiverIndex);
    void detectEmergencyStop();
    void triggerAlertForVehicle(int vehicleId);
    void updateVehicleVisualization();
    QColor getVehicleColor(const Vehicle& vehicle) const;
    void updateV2VExchangeVisualization();
    void clearV2VExchangeGraphics();
    
    // Simulation
    void initializeSimulation();
    void updateVehiclePositions(double deltaTimeSeconds);
    void updateVehicleOnEdge(Vehicle& vehicle, double deltaTimeSeconds);
    int selectNextEdge(int currentNodeIndex, int currentEdgeIndex, bool movingForward);
    void detectV2VConnections();
    void updateConnectionGraphics();
    void clearConnectionGraphics();
    double calculateDistance(double lat1, double lon1, double lat2, double lon2) const;
    
    // Optimisation pour la détection V2V avec beaucoup de véhicules
    struct SpatialGridCell {
        QVector<int> vehicleIndices;
    };
    void buildSpatialGrid(QHash<QPair<int, int>, SpatialGridCell>& grid, double cellSize) const;
    QPair<int, int> getGridCell(double lat, double lon, double cellSize) const;
    
    // Visualisation de densité (heatmap)
    void updateDensityHeatmap();
    void clearDensityHeatmap();
    QColor getDensityColor(int vehicleCount, int maxCount) const;
    struct DensityCell {
        QPair<int, int> gridKey;
        int vehicleCount = 0;
        QRectF bounds;
    };
    void buildDensityGrid(QHash<QPair<int, int>, DensityCell>& densityGrid, double cellSizeMeters) const;
};
