# v2v_map_project

## Prérequis

- Qt 6.6+ (Widgets, Network, éventuellement Gui)
- CMake ≥ 3.16
- Compilateur C++17 (MSVC, MinGW, Clang, etc.)
- Optionnel : libosmium + protozero pour charger des fichiers `.osm.pbf`

## Cloner et configurer

```bash
git clone https://github.com/ton-compte/v2v_map_project.git
cd v2v_map_project
```

Si vous comptez charger des `.osm.pbf`, installez libosmium/protozero et exposez leurs en-têtes dans `CMAKE_PREFIX_PATH` ou directement via `cmake -DLIBOSMIUM_INCLUDE_DIR=...`.

## Génération avec CMake (Qt Creator)

1. Ouvrir `CMakeLists.txt` avec Qt Creator.
2. Laisser Qt détecter Qt 6.6+ et générer un kit (MSVC/MinGW).
3. Cliquer sur **Configure** puis **Build**.
4. Lancer depuis Qt Creator.

## Génération avec CMake en ligne de commande (Windows)

```powershell
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_PREFIX_PATH="C:\Qt\6.6.2\mingw_64" ..
ninja
```

Où `CMAKE_PREFIX_PATH` pointe vers votre installation Qt (route selon kit).

## Exécution

Dans le dossier de build :

```powershell
./v2v_map.exe
```

Au démarrage, une boîte de dialogue propose de charger un fichier `.osm`. Choisissez par exemple `src/colmar_centre.osm`. L’interface affiche :

- le fond de carte OpenStreetMap (tuiles OSM),
- les routes extraites du fichier (lignes rouges),
- ~60 véhicules (points dorés avec infobulle),
- boutons `+` / `−` pour zoomer/dézoomer.

Vous pouvez aussi zoomer (molette/double-clic) et déplacer la carte en maintenant le clic gauche. Les tuiles sont mises en cache (50 Mo) dans le répertoire cache utilisateur.

## Dépannage

- Si les tuiles ne se chargent pas : vérifier la connexion réseau et le respect de la politique OSM (User-Agent/Referer dans `TileManager`).
- Si libosmium n’est pas détectée, les `.osm.pbf` ne seront pas chargés (message explicite). Convertissez vers `.osm` ou installez la bibliothèque.
