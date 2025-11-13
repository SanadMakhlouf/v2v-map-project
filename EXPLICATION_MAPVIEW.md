# Explication de la classe MapView

## Vue d'ensemble

`MapView` est la classe principale qui gère l'affichage de la carte interactive. Elle hérite de `QGraphicsView` (widget Qt pour afficher une scène graphique) et orchestre tous les éléments : tuiles cartographiques, routes, véhicules, et interactions utilisateur.

---

## 🔧 Fonctions publiques (API)

### `MapView(QWidget* parent = nullptr)` - Constructeur

**Rôle** : Initialise la vue de la carte au démarrage.

**Ce qu'elle fait** :

- Crée une scène graphique (`QGraphicsScene`) pour afficher les éléments
- Configure le mode de drag (pas de drag par défaut)
- Active l'antialiasing pour un rendu plus lisse
- Connecte le signal `tileReady` du `TileManager` pour recevoir les tuiles téléchargées
- Crée les boutons de zoom (+/-)
- Charge les premières tuiles visibles

**Pourquoi c'est important** : C'est le point d'entrée, tout est initialisé ici.

---

### `~MapView()` - Destructeur

**Rôle** : Nettoie les ressources avant la destruction.

**Ce qu'elle fait** :

- Supprime tous les éléments graphiques (routes, véhicules, tuiles)
- Libère la mémoire

**Pourquoi c'est important** : Évite les fuites mémoire.

---

### `void setCenterLatLon(double lat, double lon, int zoom, bool preserveIfOutOfBounds = false)`

**Rôle** : Définit le centre géographique de la carte et le niveau de zoom.

**Paramètres** :

- `lat` : Latitude du centre (ex: 47.75 pour Colmar)
- `lon` : Longitude du centre (ex: 7.33)
- `zoom` : Niveau de zoom (0-19)
- `preserveIfOutOfBounds` : Si vrai, garde l'ancien centre si le nouveau est hors limites

**Ce qu'elle fait** :

1. Normalise les coordonnées (latitude entre -85° et 85°, longitude entre -180° et 180°)
2. Vérifie si le zoom ou le centre ont changé
3. Met à jour `m_centerLat`, `m_centerLon`, `m_zoom`
4. Recharge les tuiles, routes et véhicules pour la nouvelle position/zoom

**Pourquoi c'est important** : C'est la fonction principale pour changer la vue. Toutes les autres fonctions l'utilisent.

**Exemple d'utilisation** :

```cpp
view.setCenterLatLon(48.8566, 2.3522, 15); // Centre sur Paris avec zoom 15
```

---

### `void zoomToLevel(int newZoom)`

**Rôle** : Change le niveau de zoom en gardant le centre actuel de la vue.

**Paramètres** :

- `newZoom` : Nouveau niveau de zoom (0-19)

**Ce qu'elle fait** :

1. Récupère le centre actuel de la vue (ce qui est visible à l'écran)
2. Convertit ce centre en coordonnées géographiques (lat/lon)
3. Met à jour le zoom
4. Reconvertit en coordonnées scène avec le nouveau zoom
5. Recharge tout et centre la vue

**Pourquoi c'est important** : Permet de zoomer sans perdre la position actuelle. Utilisée par les boutons +/- et la molette.

**Différence avec `setCenterLatLon`** : `zoomToLevel` garde toujours le centre visible, alors que `setCenterLatLon` peut changer le centre.

---

### `bool loadRoadGraphFromFile(const QString& filePath)`

**Rôle** : Charge un fichier OSM (.osm ou .osm.pbf) et construit le graphe routier.

**Paramètres** :

- `filePath` : Chemin vers le fichier OSM

**Retour** : `true` si succès, `false` si erreur

**Ce qu'elle fait** :

1. Utilise `RoadGraphLoader` pour parser le fichier OSM
2. Construit le graphe routier (nœuds et arêtes)
3. Génère ~60 véhicules aléatoirement sur les routes
4. Calcule la bounding box (zone couverte) et centre la carte dessus
5. Affiche les routes en rouge et les véhicules en points dorés

**Pourquoi c'est important** : C'est la fonction qui charge les données routières pour la simulation.

**Exemple d'utilisation** :

```cpp
view.loadRoadGraphFromFile("colmar.osm");
```

---

## 🖱️ Fonctions d'interaction utilisateur (protected, événements Qt)

### `void wheelEvent(QWheelEvent* event)` - Zoom avec la molette

**Rôle** : Gère le zoom avec la molette de la souris.

**Ce qu'elle fait** :

1. Détecte si la molette tourne vers le haut (zoom in) ou vers le bas (zoom out)
2. Récupère la position du curseur
3. Convertit cette position en coordonnées géographiques
4. Change le zoom en gardant le curseur au même endroit géographique
5. Recharge les tuiles et éléments graphiques

**Pourquoi c'est important** : Interaction naturelle pour zoomer. Le zoom se fait sur le point sous le curseur, pas sur le centre.

---

### `void mousePressEvent(QMouseEvent* event)` - Début du déplacement

**Rôle** : Détecte quand l'utilisateur commence à déplacer la carte.

**Ce qu'elle fait** :

1. Si clic gauche, active le mode "panning" (déplacement)
2. Enregistre la position de départ du curseur
3. Change le curseur en main fermée

**Pourquoi c'est important** : Démarre le processus de déplacement de la carte.

---

### `void mouseMoveEvent(QMouseEvent* event)` - Déplacement en cours

**Rôle** : Gère le déplacement de la carte pendant que l'utilisateur maintient le clic.

**Ce qu'elle fait** :

1. Calcule le déplacement depuis la dernière position
2. Déplace les scrollbars (barres de défilement) pour faire bouger la vue
3. Ne recharge pas les tuiles pendant le déplacement (performance)

**Pourquoi c'est important** : Permet un déplacement fluide sans lag.

---

### `void mouseReleaseEvent(QMouseEvent* event)` - Fin du déplacement

**Rôle** : Finalise le déplacement et synchronise les coordonnées.

**Ce qu'elle fait** :

1. Désactive le mode "panning"
2. Récupère le nouveau centre de la vue
3. Met à jour `m_centerLat` et `m_centerLon` avec les nouvelles coordonnées
4. Recharge les tuiles si nécessaire

**Pourquoi c'est important** : Synchronise les coordonnées après le déplacement pour que le zoom suivant fonctionne correctement.

---

### `void mouseDoubleClickEvent(QMouseEvent* event)` - Double-clic pour zoomer

**Rôle** : Zoom sur le point double-cliqué.

**Ce qu'elle fait** :

1. Récupère la position du double-clic
2. Convertit en coordonnées géographiques
3. Augmente le zoom de 1 niveau
4. Centre la carte sur ce point avec le nouveau zoom

**Pourquoi c'est important** : Permet de zoomer rapidement sur une zone précise.

---

### `void resizeEvent(QResizeEvent* event)` - Redimensionnement de la fenêtre

**Rôle** : Réajuste les boutons de zoom quand la fenêtre change de taille.

**Ce qu'elle fait** :

1. Repositionne les boutons +/- pour qu'ils restent en haut à droite

**Pourquoi c'est important** : Maintient l'interface utilisable même après redimensionnement.

---

## 📡 Slot (réception de signaux Qt)

### `void onTileReady(int z, int x, int y, const QPixmap& pix)`

**Rôle** : Reçoit une tuile téléchargée et l'affiche.

**Paramètres** :

- `z, x, y` : Coordonnées de la tuile (zoom, colonne, ligne)
- `pix` : Image de la tuile (256×256 pixels)

**Ce qu'elle fait** :

1. Trouve ou crée l'item graphique correspondant à cette tuile
2. Remplace le placeholder gris par la vraie image
3. Positionne la tuile au bon endroit dans la scène

**Pourquoi c'est important** : C'est comme ça que les tuiles OSM apparaissent progressivement sur la carte.

**Connexion** : Connecté au signal `tileReady` du `TileManager`.

---

## 🔄 Fonctions privées - Conversions de coordonnées

### `QPointF lonLatToScene(double lon, double lat, int z)`

**Rôle** : Convertit des coordonnées géographiques (latitude/longitude) en coordonnées scène (pixels).

**Paramètres** :

- `lon` : Longitude (-180° à 180°)
- `lat` : Latitude (-85° à 85°)
- `z` : Niveau de zoom

**Retour** : Position en pixels dans la scène

**Ce qu'elle fait** :

1. Utilise la projection Web Mercator (standard pour les cartes web)
2. Calcule la position de la tuile correspondante
3. Convertit en pixels (chaque tuile fait 256×256 pixels)

**Pourquoi c'est important** : Permet de placer les éléments (routes, véhicules) aux bonnes positions sur la carte.

**Formule utilisée** : Projection Mercator sphérique

- `x = (lon + 180) / 360 * 2^z * 256`
- `y = (1 - log(tan(lat) + sec(lat)) / π) / 2 * 2^z * 256`

---

### `QPointF sceneToLonLat(const QPointF& scenePoint, int z) const`

**Rôle** : Convertit des coordonnées scène (pixels) en coordonnées géographiques (latitude/longitude).

**Paramètres** :

- `scenePoint` : Position en pixels dans la scène
- `z` : Niveau de zoom

**Retour** : Coordonnées géographiques (longitude, latitude)

**Ce qu'elle fait** : Inverse de `lonLatToScene` - convertit les pixels en coordonnées GPS.

**Pourquoi c'est important** : Permet de savoir quelle zone géographique est visible à l'écran, ou où se trouve le curseur.

**Exemple** : Quand tu cliques sur la carte, cette fonction dit "tu as cliqué sur Paris (48.8566°N, 2.3522°E)".

---

## 🗺️ Fonctions privées - Gestion des tuiles

### `void loadVisibleTiles(const QPointF& centerScene = QPointF())`

**Rôle** : Charge les tuiles cartographiques visibles à l'écran.

**Paramètres** :

- `centerScene` : Centre de la vue en coordonnées scène (optionnel)

**Ce qu'elle fait** :

1. Calcule quelles tuiles sont visibles selon le zoom et le centre
2. Pour chaque tuile visible :
   - Si elle existe déjà, la marque comme "encore nécessaire"
   - Sinon, crée un placeholder gris et demande le téléchargement
3. Supprime les tuiles qui ne sont plus visibles
4. Centre la vue sur le point spécifié

**Pourquoi c'est important** : C'est cette fonction qui affiche le fond de carte. Elle est appelée à chaque zoom/déplacement.

**Optimisation** : Ne télécharge que les tuiles visibles (pas toute la Terre !).

---

### `QString tileKey(int z, int x, int y) const`

**Rôle** : Génère une clé unique pour identifier une tuile.

**Paramètres** :

- `z, x, y` : Coordonnées de la tuile

**Retour** : Clé sous forme de chaîne (ex: "15/16384/10944")

**Ce qu'elle fait** : Crée une chaîne unique pour stocker/récupérer les tuiles dans un hash.

**Pourquoi c'est important** : Permet de retrouver rapidement une tuile déjà chargée sans la retélécharger.

---

## 🛣️ Fonctions privées - Gestion des routes

### `void reloadRoadGraphics()`

**Rôle** : Redessine toutes les routes sur la carte.

**Ce qu'elle fait** :

1. Supprime les anciennes routes
2. Pour chaque arête du graphe routier :
   - Convertit les nœuds en coordonnées scène
   - Dessine une ligne rouge entre les deux nœuds
3. Place les routes au-dessus des tuiles (z-value = 10)

**Pourquoi c'est important** : Affiche le réseau routier chargé depuis le fichier OSM.

**Quand c'est appelé** : Après chaque zoom/déplacement pour repositionner les routes.

---

### `void clearRoadGraphics()`

**Rôle** : Supprime toutes les routes de la scène.

**Ce qu'elle fait** : Parcourt toutes les routes et les supprime de la scène graphique.

**Pourquoi c'est important** : Nettoie avant de redessiner, évite les doublons.

---

## 🚗 Fonctions privées - Gestion des véhicules

### `void generateVehicles(int count)`

**Rôle** : Génère des véhicules aléatoirement sur les routes.

**Paramètres** :

- `count` : Nombre de véhicules à générer (ex: 60)

**Ce qu'elle fait** :

1. Parcourt les arêtes du graphe routier
2. Pour chaque arête, place un véhicule à une position aléatoire (entre 10% et 90% de l'arête)
3. Assigne des attributs aléatoires :
   - Vitesse : selon la vitesse max de la route
   - Rayon de transmission : entre 100 et 500 mètres
   - Type de route : copié depuis l'arête

**Pourquoi c'est important** : Crée la simulation de véhicules pour l'étape 3 du projet.

---

### `void reloadVehicleGraphics()`

**Rôle** : Redessine tous les véhicules sur la carte.

**Ce qu'elle fait** :

1. Supprime les anciens véhicules
2. Pour chaque véhicule :
   - Convertit sa position (lat/lon) en coordonnées scène
   - Dessine un cercle doré (5 pixels de rayon)
   - Ajoute une infobulle avec les informations du véhicule
3. Place les véhicules au-dessus des routes (z-value = 30)

**Pourquoi c'est important** : Affiche les véhicules simulés.

**Quand c'est appelé** : Après chaque zoom/déplacement pour repositionner les véhicules.

---

### `void clearVehicleGraphics()`

**Rôle** : Supprime tous les véhicules de la scène.

**Ce qu'elle fait** : Parcourt tous les véhicules et les supprime de la scène graphique.

**Pourquoi c'est important** : Nettoie avant de redessiner.

---

## 🎛️ Fonctions privées - Boutons de zoom

### `void createZoomControls()`

**Rôle** : Crée les boutons + et - pour zoomer/dézoomer.

**Ce qu'elle fait** :

1. Crée deux `QToolButton` (boutons +/-)
2. Les connecte à `zoomToLevel()` pour zoomer/dézoomer
3. Les positionne en haut à droite
4. Active/désactive selon le zoom min/max

**Pourquoi c'est important** : Interface utilisateur pour contrôler le zoom sans clavier/souris.

---

### `void positionZoomControls()`

**Rôle** : Repositionne les boutons de zoom.

**Ce qu'elle fait** : Calcule la position en haut à droite selon la taille de la fenêtre.

**Pourquoi c'est important** : Maintient les boutons visibles même après redimensionnement.

---

### `void updateZoomButtons()`

**Rôle** : Active/désactive les boutons selon le niveau de zoom.

**Ce qu'elle fait** :

- Désactive le bouton + si zoom = 19 (max)
- Désactive le bouton - si zoom = 0 (min)

**Pourquoi c'est important** : Empêche de zoomer au-delà des limites.

---

## 🔧 Fonctions privées - Utilitaires

### `bool clampCenterToBounds(double& lat, double& lon) const`

**Rôle** : Limite le centre à une zone géographique (optionnel, désactivé par défaut).

**Paramètres** :

- `lat, lon` : Coordonnées à limiter (modifiées si hors limites)

**Retour** : `true` si les coordonnées ont été modifiées

**Ce qu'elle fait** : Si `m_limitRegion` est activé, force les coordonnées à rester dans `[m_minLat, m_maxLat] × [m_minLon, m_maxLon]`.

**Pourquoi c'est important** : Permet de limiter la navigation à une zone (ex: Alsace), mais c'est désactivé dans le projet actuel.

---

### `double normalizeLongitude(double lon) const`

**Rôle** : Normalise la longitude entre -180° et 180°.

**Paramètres** :

- `lon` : Longitude à normaliser

**Retour** : Longitude normalisée

**Ce qu'elle fait** : Si la longitude est > 180° ou < -180°, la ramène dans la plage valide.

**Pourquoi c'est important** : Évite les problèmes de coordonnées invalides (ex: 200° devient -160°).

**Exemple** : `normalizeLongitude(200.0)` → `-160.0`

---

### `double clampLatitude(double lat) const`

**Rôle** : Limite la latitude aux valeurs valides pour la projection Mercator.

**Paramètres** :

- `lat` : Latitude à limiter

**Retour** : Latitude entre -85.05112878° et 85.05112878°

**Ce qu'elle fait** : La projection Mercator ne peut pas représenter les pôles (90°), donc on limite à ~85°.

**Pourquoi c'est important** : Évite les erreurs mathématiques (division par zéro, etc.) aux pôles.

---

## 📊 Variables membres importantes

- `m_centerLat`, `m_centerLon` : Centre géographique de la carte
- `m_zoom` : Niveau de zoom actuel (0-19)
- `m_roadGraph` : Graphe routier chargé depuis OSM
- `m_vehicles` : Liste des véhicules simulés
- `m_tileItems` : Hash des tuiles chargées (clé = "z/x/y")
- `m_scene` : Scène graphique Qt qui contient tous les éléments

---

## 🔄 Flux de fonctionnement

1. **Initialisation** : `MapView()` crée la scène, charge les premières tuiles
2. **Interaction utilisateur** : Les événements (clic, molette) modifient le centre/zoom
3. **Mise à jour** : `setCenterLatLon()` ou `zoomToLevel()` recharge tout
4. **Affichage** : Les tuiles arrivent via `onTileReady()`, les routes/véhicules sont redessinés
5. **Synchronisation** : Après déplacement, `mouseReleaseEvent()` met à jour les coordonnées

---

## 💡 Points clés pour la présentation

1. **Architecture** : `MapView` orchestre tout (tuiles, routes, véhicules, interactions)
2. **Projection Mercator** : Conversion entre coordonnées GPS et pixels
3. **Gestion mémoire** : Suppression des éléments non visibles (tuiles, routes)
4. **Performance** : Ne charge que les tuiles visibles, réutilise celles déjà chargées
5. **Interactions** : Gère tous les événements utilisateur (clic, molette, double-clic)

---

## 🎯 Exemple de présentation

"La classe `MapView` est le cœur de l'interface. Elle hérite de `QGraphicsView` pour afficher une scène graphique. Les fonctions principales sont :

- `setCenterLatLon()` : Change la vue de la carte
- `loadRoadGraphFromFile()` : Charge les données routières
- `wheelEvent()` : Gère le zoom avec la molette
- `loadVisibleTiles()` : Charge les tuiles OSM visibles

Les conversions de coordonnées (`lonLatToScene`, `sceneToLonLat`) utilisent la projection Web Mercator, standard pour les cartes web. Les routes et véhicules sont redessinés à chaque zoom pour rester à la bonne échelle."
