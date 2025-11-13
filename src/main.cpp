
#include <QApplication>
#include <QTimer>

#include "MapView.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MapView view;
    view.resize(800,600);
    view.show();

    // Utiliser un timer pour s'assurer que le widget est complètement rendu avant de définir le centre
    QTimer::singleShot(0, &view, [&view]() {
        view.setCenterLatLon(47.75, 7.335888, 14);
       // view.setCenterLatLon(48.8566, 2.3522, 5); // paris
    });

    return app.exec();
}
