
#include <QApplication>
#include <QFileDialog>

#include "MapView.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MapView view;
    view.resize(800,600);
    view.show();
    view.setCenterLatLon(47.750839, 7.335888, 13);

    QString osmPath = QFileDialog::getOpenFileName(
        &view,
        QObject::tr("Ouvrir un fichier OSM"),
        QString(),
        QObject::tr("Fichiers OSM (*.osm);;Tous les fichiers (*.*)"));
    if (!osmPath.isEmpty()) {
        view.loadRoadGraphFromFile(osmPath);
    }

    return app.exec();
}
