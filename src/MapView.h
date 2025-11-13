
#pragma once
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QVector>
#include <QString>
#include <QHash>
#include <QToolButton>

#include "TileManager.h"
#include "RoadGraph.h"
#include "Vehicle.h"

class QGraphicsLineItem;
class QGraphicsEllipseItem;

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
    QHash<QString, TileInfo> m_tileItems;

    QPointF lonLatToScene(double lon, double lat, int z);
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
};
