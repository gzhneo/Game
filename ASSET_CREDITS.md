# Third-party assets: origin and licences

This file lists every image, model and texture that does **not** come from Electronic Arts and is used in this project,
with its licence and the attribution it asks for. A new third-party asset gets an entry here **before** it goes into an
archive.

The original files of Command & Conquer Generals / Zero Hour (models, textures, sounds, maps) remain the property of
Electronic Arts. They are not in this repository and are not distributed with it; the tools only read them from an
existing installation (see `BUILD.md`).

Third-party libraries (FFmpeg, DirectXTex, zlib, Monocypher and others) are listed with their licences in `NOTICE.md`.

## Overview

| Asset | Used as | Source | Licence | In the repository |
|---|---|---|---|---|
| Rocks "Rock Moss Set 01" and "Rock Moss Set 02", barrel "Barrel 03", fir "Fir Tree 01" | nature objects `PMRocks01`, `PMRocks03`, `PMmcluster`, `PMgaldrum(_D)`, `PTPine01(_hi)(_B)` with their textures | Poly Haven | **CC0 1.0** | yes, `Data/Natur` (loaded as a mod with `-mod`) |
| Conifers (pine, spruce, fir, cypress, thuja, stumps; three scans) | free tree models and textures `pt*_tree.dds`, snow versions derived from the same capture | Poly Haven | **CC0 1.0** | yes, `Data/BaeumeFrei` |
| Broadleaf trees (maple, oak, orange tree, bonsai, cherry blossom, dogwood with snow, maple stump) | free tree models and textures; the oaks from a scan, the others from the project's own generator | Poly Haven, ambientCG | **CC0 1.0** | yes, `Data/BaeumeFrei` |
| Burnt versions `_B` of the free trees | models `<model>_B` and textures `<name>_b_tree.dds`, derived from the same images and meshes (charred, foliage partly removed) | as the trees they come from | **CC0 1.0** | yes for the trees above, `Data/BaeumeFrei` |
| Further trees and plants (dogwoods and autumn oaks, bushes, desert plants, cacti, vines, birch, willow, bamboo, palms) | tree and plant models and textures made by the tools | Poly Haven, ambientCG | **CC0 1.0** | no; made locally by `tools/assets/blender/baum_frei.py` as a test archive |
| Terrain detail (eight photo scans) | `Data/Textures/BodenDetail/detail_a.dds`, `detail_b.dds` | Poly Haven, ambientCG | **CC0 1.0** | yes |
| Free terrain textures (241 textures from 30 photo scans), with relief (`*_n.dds`) and height (`*_h.dds`) | `Data/FreierBoden/Art/Terrain/*.dds`, made by `tools/assets/boden_frei.py` | Poly Haven, ambientCG | **CC0 1.0** | yes, `Data/FreierBoden` |
| Free road textures (63 textures from 24 photo scans) | `Data/FreieStrassen/Art/Textures/*.dds`, made by `tools/assets/strassen_frei.py` | Poly Haven, ambientCG | **CC0 1.0** | yes, `Data/FreieStrassen` |
| Sky for the water reflection (four HDRIs, one per time of day) | `Data/Textures/Wasser/himmel_morgen|tag|abend|nacht.dds` | Poly Haven | **CC0 1.0** | yes |
| Free water colour (wave structure from two HDRIs) | `Data/Textures/Wasser/wasser_see.dds`, `wasser_fluss.dds`, `wasser_trueb.dds`, made by `tools/assets/wasser_frei.py` | Poly Haven | **CC0 1.0** | yes |
| Waves, foam, wind and the water helper textures | `Data/Textures/Wasser/wellen.dds`, `schaum.dds`, `wind.dds`, `wasser_glitzern.dds`, `wasser_rauschen.dds`, `wasser_flusskante.dds`, `wasser_brandung.dds` | computed by the project (`tools/assets/wasser_texturen.py`, `tools/assets/wasser_hilfstexturen.py`) | project's own work, no third-party licence | yes |
| Rock textures for mountains and cliffs (two photo scans) | `Data/Textures/Berge/fels_grob.dds`, `fels_fein.dds` | Poly Haven | **CC0 1.0** | yes |
| Photo textures and models of the Wine test data (13 scans, three models) | generated test data (`tools/wine/testdaten/generate.py --polyhaven`) | Poly Haven | **CC0 1.0** | no; downloaded and generated locally, not shipped |

CC0 asks for no attribution; the sources are named here anyway, with asset names and authors.

**Licences.** Poly Haven (<https://polyhaven.com/license>) and ambientCG (<https://docs.ambientcg.com/license/>) put all
their assets under **CC0 1.0 Universal** (<https://creativecommons.org/publicdomain/zero/1.0/>): they may be changed,
used commercially and passed on without conditions. The licence was checked on the licence pages and on the asset pages
when the assets were retrieved (September and October 2026). Only photographic or photogrammetric scans are used, no
procedural materials. The raw downloads stay outside the repository and can be fetched again with the tools named
below (`tools/assets/cc0_laden.py`, `tools/assets/boden_detail.py laden`, `tools/assets/berge.py laden`,
`tools/assets/wasser_texturen.py`, `tools/assets/wasser_frei.py laden`).

**No EA content.** None of the tools that make these assets read an EA file. What depends on EA's game is only the
interface: the file and model names the INI files load, texture sizes in tiles, road widths and the bone names the game
logic asks for. The W3D structure of the models (mesh header, shader, vertex material, hierarchy, bone positions) is
written by the project itself (`tools/assets/blender/w3d_geruest.py`).

Also in the repository from these sources: the asset descriptions (`*_info.json`, `*_files.json`) of the Poly Haven
assets in `Data/Natur/Quellen/` (metadata only, no models or textures; licence list `Data/Natur/Quellen/LIZENZEN.md`).

## Nature objects (`Data/Natur`)

| Poly Haven asset | Address | Author | Used for | Changes |
|---|---|---|---|---|
| Rock Moss Set 01 (`rock_moss_set_01`) | <https://polyhaven.com/a/rock_moss_set_01> | Kless Gyzen | Rocks1 (6 stones), Rocks3 (7 stones) | stones scaled evenly and arranged into a group by the project's own rule (`natur_anordnen.py`), reduced to about 1,000 triangles per model, colour, occlusion and relief baked into a 256² colour texture and matched channel by channel to a palette of the project |
| Rock Moss Set 02 (`rock_moss_set_02`) | <https://polyhaven.com/a/rock_moss_set_02> | Kless Gyzen | RockClusterMedium01 (3 blocks) | as above, 900 triangles |
| Barrel 03 (`barrel_03`) | <https://polyhaven.com/a/barrel_03> | Serhii Khromov | 55GallonDrum and its damaged version | real size at game scale (6.5 units per metre), reduced to 300 triangles, colour changed from blue to an olive green palette, damaged version darkened |
| Fir Tree 01 (`fir_tree_01`) | <https://polyhaven.com/a/fir_tree_01> | Rob Tuytel (photography), Rico Cilliers (model) | TreePine/TreePine2 and the burnt version | side and layer images rendered from the model (alpha-tested cards), crown compressed into a cone, bark from the bark texture, colour matched to a palette; own card mesh (60 vertices, 28 triangles) |

## Free trees (`Data/BaeumeFrei`) and plants made by the tools

Made with `tools/assets/blender/baum_frei.py` and the job file `tools/assets/blender/baeume_frei.json`, rendered with
Blender (Cycles). From the scans: each tree rendered from several angles (two side images, a top view of the upper crown,
a crown disc), side images of conifers compressed into a cone, foliage pulled part of the way towards a palette of the
project, brightness only raised, snow from a second capture using the height component of the normals, bark from the
bark texture, own card mesh. Birch, willow, bamboo, maple, orange tree, bonsai, cherry blossom and palms have no scan:
trunk, branches, stalks and leaf cards come from the project's own generator `tools/assets/blender/baum_wachsen.py`
(no third-party code; the birch bark is computed); only the images on the cards and tubes are third-party.

**Conifers**

| Asset | Address | Author | Used for |
|---|---|---|---|
| Fir Tree 01 (`fir_tree_01`, trees a, b, c) | <https://polyhaven.com/a/fir_tree_01> | Rob Tuytel (photography), Rico Cilliers (model) | pines 1 and 2 (`PTPine01`), firs `PTXFir01`–`03`, `06`, `07`, `PTFir01`; bark of the stumps and the willow |
| Pine Sapling Medium (`pine_sapling_medium`, trees a, b, c) | <https://polyhaven.com/a/pine_sapling_medium> | Rob Tuytel (photography), Rico Cilliers (model) | pines 3 to 5 (`PTPine02`–`04`) |
| Fir Sapling Medium (`fir_sapling_medium`, trees a, b, c) | <https://polyhaven.com/a/fir_sapling_medium> | Rob Tuytel (photography), Rico Cilliers (model) | spruces (`PTSpruce01`, `PTXPine03`–`08`), firs `PTXFir04`, `05`, `08`–`10`, cypress and thuja (`PTCypress01/02`) |

**Broadleaf trees**

| Asset | Address | Author | Used for |
|---|---|---|---|
| Jacaranda Tree (`jacaranda_tree`) | <https://polyhaven.com/a/jacaranda_tree> | Rico Cilliers, Rob Tuytel | oaks `PTOak01`, `PTXOAK01`–`06` (shape and foliage of the scan, green; the scan has no blossoms) |
| Island Tree 01 (`island_tree_01`) | <https://polyhaven.com/a/island_tree_01> | Rob Tuytel (scan), Rico Cilliers | dogwoods and autumn oaks (test archive); leaves enlarged around their centre for a denser crown |
| Island Tree 02 (`island_tree_02`) | <https://polyhaven.com/a/island_tree_02> | Rob Tuytel (scan), Rico Cilliers | dogwoods and autumn oaks (test archive), narrower crown |
| Leaf Set 006, 007, 008, 012, 021, 027, 028 | <https://ambientcg.com/view?id=LeafSet012> (the others accordingly) | Lennart Demes (ambientCG) | autumn foliage (red, orange, yellow, gold): every leaf cut out on its own, chosen by its own hue and fitted into a leaf slot of the scan's leaf atlas; leaf colour unchanged |
| Leaf Set 010, 014, 023, 024 | <https://ambientcg.com/view?id=LeafSet010> (the others accordingly) | Lennart Demes (ambientCG) | leaf cards of maple (010), birch (014), orange tree (023), bonsai and cherry blossom (024, tinted pink for the blossom) |
| Foliage 006 | <https://ambientcg.com/view?id=Foliage006> | Lennart Demes (ambientCG) | hanging leaf cards of the willow, leaf cards of the bamboo, colour of the palm fronds |
| Bamboo 001 A | <https://ambientcg.com/view?id=Bamboo001A> | Lennart Demes (ambientCG) | surface of the bamboo stalks |
| Palm Tree Bark (`palm_tree_bark`) | <https://polyhaven.com/a/palm_tree_bark> | Dimitrios Savva (photography), Rico Cilliers (editing) | trunk of the palms |

**Bushes, desert plants, cacti and vines** (test archive): rendered from two angles and from above, own card mesh
(bush: three crossed cards and a top card, 16 vertices), colour partly pulled towards a palette, snow from the normals
capture.

| Asset | Address | Author | Used for |
|---|---|---|---|
| Searsia Lucida (`searsia_lucida`, a–e) | <https://polyhaven.com/a/searsia_lucida> | James Ray Cock (model), Jenelle van Heerden (photography) | bushes `PTBush01`–`07`, `11`, snow bushes `PTBush12`, `13` |
| Wild Rooibos Bush (`wild_rooibos_bush`, a, b) | <https://polyhaven.com/a/wild_rooibos_bush> | James Ray Cock (model), Jenelle van Heerden (photography) | desert bushes `PMDsrtSlvr`, `PMDsrtBrGr2` |
| Didelta Spinosa (`didelta_spinosa`) | <https://polyhaven.com/a/didelta_spinosa> | Jenelle van Heerden | `PMDsrtPple`, tumbleweeds `PMTmbWeed01`–`03` (recoloured dry) |
| Leipoldtia Schultzei (`leipoldtia_schultzei`) | <https://polyhaven.com/a/leipoldtia_schultzei> | Jenelle van Heerden | `PMDsrtVlt` |
| Quiver Tree 01 (`quiver_tree_01`) | <https://polyhaven.com/a/quiver_tree_01> | James Ray Cock, Dario Barresi (photography), Rico Cilliers (model) | Joshua tree `PMJoshua`, `PMJoshua_D` |
| Quiver Tree 02 (`quiver_tree_02`) | <https://polyhaven.com/a/quiver_tree_02> | Dario Barresi (photography), Rico Cilliers (model) | yucca `PMYaka`, `PMYakaSnw` |
| Othonna Cerarioides (`othonna_cerarioides`, b) | <https://polyhaven.com/a/othonna_cerarioides> | James Ray Cock (model), Jenelle van Heerden (photography) | cactus `PMCactus` (substitute shape) |
| Cheiridopsis Succulent (`cheiridopsis_succulent`, b, c) | <https://polyhaven.com/a/cheiridopsis_succulent> | James Ray Cock (model), Jenelle van Heerden (photography) | prickly pear `PMPrklyPr`, `PMPrklyPrs` (substitute shape) |
| Shrub 04 (`shrub_04`, a) | <https://polyhaven.com/a/shrub_04> | Rico Cilliers | green vine `PMVines01` |
| Dry Branches Medium 01 (`dry_branches_medium_01`, a) | <https://polyhaven.com/a/dry_branches_medium_01> | Rico Cilliers | brown vine `PMVines02` |

## Terrain detail (`Data/Textures/BodenDetail`)

Eight photographic scans (colour, occlusion, normals; 2K PNG). Changes (`tools/assets/boden_detail.py bauen`): a
brightness structure computed from colour, occlusion and normals, high-pass filtered (large-scale colour removed),
normalised to mean 1 and a fixed spread, reduced to 512 × 512, four materials per file in the RGBA channels.

| Material | Channel | Scan | Source | Authors | Size |
|---|---|---|---|---|---|
| grass | `detail_a.dds` R | Leafy Grass (`leafy_grass`) | Poly Haven, <https://polyhaven.com/a/leafy_grass> | Charlotte Baglioni | 2.0 × 2.0 m |
| dry grass | `detail_a.dds` G | Withered Grass (`withered_grass`) | Poly Haven, <https://polyhaven.com/a/withered_grass> | Charlotte Baglioni | 2.0 × 2.0 m |
| soil | `detail_a.dds` B | Dry Ground 01 (`dry_ground_01`) | Poly Haven, <https://polyhaven.com/a/dry_ground_01> | Rob Tuytel | 4.0 × 4.0 m |
| gravel | `detail_a.dds` A | Rocky Trail (`rocky_trail`) | Poly Haven, <https://polyhaven.com/a/rocky_trail> | Amal Kumar | 2.0 × 2.0 m |
| sand | `detail_b.dds` R | Sandy Gravel 02 (`sandy_gravel_02`) | Poly Haven, <https://polyhaven.com/a/sandy_gravel_02> | Dario Barresi | 2.5 × 2.5 m |
| rock | `detail_b.dds` G | Rock Face (`rock_face`) | Poly Haven, <https://polyhaven.com/a/rock_face> | Greg Zaal (photo), Dario Barresi (editing) | 2.4 × 2.4 m |
| snow | `detail_b.dds` B | Snow 006 (`Snow006`) | ambientCG, <https://ambientcg.com/a/Snow006> | Lennart Demes | 2.5 × 2.5 m |
| asphalt | `detail_b.dds` A | Asphalt 015 (`Asphalt015`) | ambientCG, <https://ambientcg.com/a/Asphalt015> | Lennart Demes | n/a |

## Free terrain textures (`Data/FreierBoden`)

30 photographic scans (28 from Poly Haven, 2 from ambientCG; 2K PNG: colour, occlusion, normals, roughness, height, and
opacity for the line image). `tools/assets/boden_frei.py bauen` makes replacements for 241 terrain textures from them:
per family one or two scans tiled, blended (periodic mask, height map), shaded (occlusion, relief), brought to fixed
colours and brightness spreads of the project, optionally with growth from a second family, own speckles, computed
plant rows (fields) and parking lines in own geometry (line image from `RoadLines003`). `boden_frei.py relief` makes the
relief files `*_n.dds` (normals and gloss from the normal and roughness maps), `boden_frei.py hoehe` the height files
`*_h.dds` for tessellation, both laid out like the colour. The result may be passed on freely (CC0 derivative).

| Family | Scan | Source | Authors | Size |
|---|---|---|---|---|
| grass | Sparse Grass (`sparse_grass`) | Poly Haven, <https://polyhaven.com/a/sparse_grass> | Amal Kumar | 2.0 × 2.0 m |
| grass | Forest Ground 01 (`forrest_ground_01`) | Poly Haven, <https://polyhaven.com/a/forrest_ground_01> | Rob Tuytel | 2.0 × 2.0 m |
| fine grass, dry grass | Grass Ground (`grass_ground`) | Poly Haven, <https://polyhaven.com/a/grass_ground> | Charlotte Baglioni | 2.5 × 2.5 m |
| fine grass | Leafy Grass (`leafy_grass`) | Poly Haven, <https://polyhaven.com/a/leafy_grass> | Charlotte Baglioni | 2.0 × 2.0 m |
| dry grass | Withered Grass (`withered_grass`) | Poly Haven, <https://polyhaven.com/a/withered_grass> | Charlotte Baglioni | 2.0 × 2.0 m |
| gravel | Pebble Ground 01 (`pebble_ground_01`) | Poly Haven, <https://polyhaven.com/a/pebble_ground_01> | Rob Tuytel | 1.5 × 1.5 m |
| gravel | Rocky Trail (`rocky_trail`) | Poly Haven, <https://polyhaven.com/a/rocky_trail> | Amal Kumar | 2.0 × 2.0 m |
| light soil, sand with stones | Dirt Floor (`dirt_floor`) | Poly Haven, <https://polyhaven.com/a/dirt_floor> | eye-candy.xyz | 2.1 × 2.1 m |
| light soil | Dry Ground Rocks (`dry_ground_rocks`) | Poly Haven, <https://polyhaven.com/a/dry_ground_rocks> | Rob Tuytel | 4.0 × 4.0 m |
| rocky ground | Rock Ground (`rock_ground`) | Poly Haven, <https://polyhaven.com/a/rock_ground> | Rob Tuytel | 1.5 × 1.5 m |
| rocky ground | Rocks Ground 03 (`rocks_ground_03`) | Poly Haven, <https://polyhaven.com/a/rocks_ground_03> | Rob Tuytel | 3.0 × 3.0 m |
| rock | Rock Face 03 (`rock_face_03`) | Poly Haven, <https://polyhaven.com/a/rock_face_03> | Dario Barresi, Rico Cilliers | 2.7 × 2.7 m |
| rock | Rock Face (`rock_face`) | Poly Haven, <https://polyhaven.com/a/rock_face> | Greg Zaal, Dario Barresi | 2.4 × 2.4 m |
| sand | Sand 02 (`sand_02`) | Poly Haven, <https://polyhaven.com/a/sand_02> | Charlotte Baglioni | 2.1 × 2.1 m |
| sand | Dense Sand (`dense_sand`) | Poly Haven, <https://polyhaven.com/a/dense_sand> | Dimitrios Savva | 1.8 × 1.8 m |
| sand with stones, toxic ground | Gravelly Sand (`gravelly_sand`) | Poly Haven, <https://polyhaven.com/a/gravelly_sand> | Dario Barresi | 2.5 × 2.5 m |
| snow | Snow 006 (`Snow006`) | ambientCG, <https://ambientcg.com/a/Snow006> | Lennart Demes | 2.5 × 2.5 m |
| asphalt | Asphalt 05 (`asphalt_05`) | Poly Haven, <https://polyhaven.com/a/asphalt_05> | Amal Kumar | 2.0 × 2.0 m |
| asphalt | Asphalt Floor (`asphalt_floor`) | Poly Haven, <https://polyhaven.com/a/asphalt_floor> | eye-candy.xyz | 2.3 × 2.3 m |
| concrete | Concrete Floor 01 (`concrete_floor_01`) | Poly Haven, <https://polyhaven.com/a/concrete_floor_01> | Rob Tuytel | 2.0 × 2.0 m |
| cobblestones | Cobblestone 05 (`cobblestone_05`) | Poly Haven, <https://polyhaven.com/a/cobblestone_05> | Rob Tuytel | 2.4 × 2.4 m |
| tiles | Stone Tiles 02 (`stone_tiles_02`) | Poly Haven, <https://polyhaven.com/a/stone_tiles_02> | Charlotte Baglioni | 2.0 × 2.0 m |
| cracked ground | Dry Ground 01 (`dry_ground_01`) | Poly Haven, <https://polyhaven.com/a/dry_ground_01> | Rob Tuytel | 4.0 × 4.0 m |
| wood | Weathered Brown Planks (`weathered_brown_planks`) | Poly Haven, <https://polyhaven.com/a/weathered_brown_planks> | Dimitrios Savva, Rico Cilliers | 1.8 × 1.8 m |
| wood chips | Wood Chips (`wood_chips`) | Poly Haven, <https://polyhaven.com/a/wood_chips> | eye-candy.xyz | 2.0 × 2.0 m |
| field | Farm Soil (`farm_soil`) | Poly Haven, <https://polyhaven.com/a/farm_soil> | Amal Kumar | 2.0 × 2.0 m |
| toxic ground | Burned Ground 01 (`burned_ground_01`) | Poly Haven, <https://polyhaven.com/a/burned_ground_01> | Rob Tuytel | 1.0 × 1.0 m |
| scree | Gravel Stones (`gravel_stones`) | Poly Haven, <https://polyhaven.com/a/gravel_stones> | Amal Kumar | 2.0 × 2.0 m |
| scree | Rocks Ground 01 (`rocks_ground_01`) | Poly Haven, <https://polyhaven.com/a/rocks_ground_01> | Rob Tuytel | 2.0 × 2.0 m |
| parking lines | Road Lines 003 (`RoadLines003`) | ambientCG, <https://ambientcg.com/a/RoadLines003> | Lennart Demes | n/a |

## Free road textures (`Data/FreieStrassen`)

24 photographic scans (22 from Poly Haven, 2 from ambientCG). `tools/assets/strassen_frei.py bauen` and
`tools/assets/strassen_wege.py` make the 62 road textures of `Roads.ini` and the manhole cover `trstrtholecvr` that EA's
data lacks: surfaces from tiled, recoloured scans (own colours and spreads), dirt roads and tyre tracks laid along the
road, edges, markings, joints, ruts, oil stains, cracks, patches, kerbs and rails in own geometry, wear of the markings
from `RoadLines004`, cracks from the height maps of `cracked_concrete` and `mud_cracked_dry_03`, cobblestone joints from
`cobblestone_05`, arrows, symbols and lettering drawn by the project (own stroke font). The result may be passed on
freely (CC0 derivative).

| Scan | Source | Authors | Size | Used for |
|---|---|---|---|---|
| Asphalt 04 (`asphalt_04`) | Poly Haven, <https://polyhaven.com/a/asphalt_04> | Jenelle van Heerden, Sergej Majboroda | 4.0 × 4.0 m | dark and light asphalt |
| Brown Mud Dry (`brown_mud_dry`) | Poly Haven, <https://polyhaven.com/a/brown_mud_dry> | Rob Tuytel | 1.3 × 1.3 m | mud and dark dirt paths |
| Cobblestone 02 (`cobblestone_02`) | Poly Haven, <https://polyhaven.com/a/cobblestone_02> | Amal Kumar | 1.8 × 1.8 m | cobblestones |
| Cobblestone 05 (`cobblestone_05`) | Poly Haven, <https://polyhaven.com/a/cobblestone_05> | Rob Tuytel | 2.4 × 2.4 m | cobblestone joints |
| Concrete Floor 01 (`concrete_floor_01`) | Poly Haven, <https://polyhaven.com/a/concrete_floor_01> | Rob Tuytel | 2.0 × 2.0 m | concrete roads, kerbs |
| Concrete Floor Worn 001 (`concrete_floor_worn_001`) | Poly Haven, <https://polyhaven.com/a/concrete_floor_worn_001> | Dimitrios Savva, Rico Cilliers | 3.0 × 3.0 m | concrete roads |
| Cracked Concrete (`cracked_concrete`) | Poly Haven, <https://polyhaven.com/a/cracked_concrete> | Dimitrios Savva | 2.0 × 2.0 m | cracks, old and wet asphalt |
| Dirt (`dirt`) | Poly Haven, <https://polyhaven.com/a/dirt> | Charlotte Baglioni | 2.0 × 2.0 m | dirt paths, verges |
| Dirt Floor (`dirt_floor`) | Poly Haven, <https://polyhaven.com/a/dirt_floor> | eye-candy.xyz | 2.1 × 2.1 m | dirt roads, tracks, gullies, verges |
| Forest Ground 05 (`forest_ground_05`) | Poly Haven, <https://polyhaven.com/a/forest_ground_05> | Charlotte Baglioni | 2.0 × 2.0 m | forest paths |
| Gravel Floor (`gravel_floor`) | Poly Haven, <https://polyhaven.com/a/gravel_floor> | Jenelle van Heerden, Matterfield | 2.2 × 2.2 m | gravel paths |
| Gravel Road (`gravel_road`) | Poly Haven, <https://polyhaven.com/a/gravel_road> | Amal Kumar | 2.0 × 2.0 m | gravel roads and tracks |
| Gravel Stones (`gravel_stones`) | Poly Haven, <https://polyhaven.com/a/gravel_stones> | Amal Kumar | 2.0 × 2.0 m | gravel roads, ballast |
| Gravelly Sand (`gravelly_sand`) | Poly Haven, <https://polyhaven.com/a/gravelly_sand> | Dario Barresi | 2.5 × 2.5 m | grey paths |
| Mud Cracked Dry 03 (`mud_cracked_dry_03`) | Poly Haven, <https://polyhaven.com/a/mud_cracked_dry_03> | Dario Barresi, Dimitrios Savva | 1.5 × 1.5 m | cracks |
| Red Dirt Mud 01 (`red_dirt_mud_01`) | Poly Haven, <https://polyhaven.com/a/red_dirt_mud_01> | Rob Tuytel | 1.5 × 1.5 m | orange dirt paths |
| Road Damaged (`road_damaged`) | Poly Haven, <https://polyhaven.com/a/road_damaged> | Dimitrios Savva | 2.2 × 2.2 m | old and wet asphalt |
| Road Lines 004 (`RoadLines004`) | ambientCG, <https://ambientcg.com/a/RoadLines004> | Lennart Demes | n/a | wear of the markings |
| Rocky Gravel (`rocky_gravel`) | Poly Haven, <https://polyhaven.com/a/rocky_gravel> | Dario Barresi, Dimitrios Savva | 1.8 × 1.8 m | brown ballast |
| Sand 02 (`sand_02`) | Poly Haven, <https://polyhaven.com/a/sand_02> | Charlotte Baglioni | 2.1 × 2.1 m | sand paths |
| Snow 006 (`Snow006`) | ambientCG, <https://ambientcg.com/a/Snow006> | Lennart Demes | 2.5 × 2.5 m | snow |
| Snow 02 (`snow_02`) | Poly Haven, <https://polyhaven.com/a/snow_02> | Rob Tuytel | 2.0 × 2.0 m | snow paths |
| Snow 04 (`snow_04`) | Poly Haven, <https://polyhaven.com/a/snow_04> | Rob Tuytel | 4.0 × 4.0 m | tyre tracks in snow |
| Sparse Grass (`sparse_grass`) | Poly Haven, <https://polyhaven.com/a/sparse_grass> | Amal Kumar | 2.0 × 2.0 m | grass verges |
| Weathered Brown Planks (`weathered_brown_planks`) | Poly Haven, <https://polyhaven.com/a/weathered_brown_planks> | Dimitrios Savva, Rico Cilliers | 1.8 × 1.8 m | wood |

## Water (`Data/Textures/Wasser`)

**Sky for the reflection.** Poly Haven HDRIs (1k Radiance `.hdr`). Changes: reduced to 256 × 128, sun clipped (at most
2.5 times the sky median), rotated so that the sun is at u = 0, normalised to the mean brightness between 10 and 70
degrees elevation, below 4 degrees replaced by a smoothed horizon colour, stored as 8 bit with gamma 2.2.

| Time of day | HDRI | Address | Authors | File |
|---|---|---|---|---|
| morning | Citrus Orchard Road (Pure Sky) (`citrus_orchard_road_puresky`) | <https://polyhaven.com/a/citrus_orchard_road_puresky> | Dimitrios Savva (photo), Jarod Guest (editing) | `himmel_morgen.dds` |
| afternoon | Kloofendal 48d Partly Cloudy (Pure Sky) (`kloofendal_48d_partly_cloudy_puresky`) | <https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky> | Greg Zaal (original), Jarod Guest (sky) | `himmel_tag.dds` |
| evening | Belfast Sunset (Pure Sky) (`belfast_sunset_puresky`) | <https://polyhaven.com/a/belfast_sunset_puresky> | Dimitrios Savva (photo), Greg Zaal (editing), Jarod Guest (sky) | `himmel_abend.dds` |
| night | Kloppenheim 02 (Pure Sky) (`kloppenheim_02_puresky`) | <https://polyhaven.com/a/kloppenheim_02_puresky> | Greg Zaal (original), Jarod Guest (sky) | `himmel_nacht.dds` |

**Water colour.** Poly Haven HDRIs (8k Radiance `.hdr`). Changes: a section of the water surface converted to a top
view, only the wave structure kept (high-pass of the logarithm of the brightness), blended seamlessly and recoloured with
the project's own colour and spread values, 512 × 512 with a mip chain.

| HDRI | Address | Author | File | Part used |
|---|---|---|---|---|
| Lake Pier (`lake_pier`) | <https://polyhaven.com/a/lake_pier> | Alexander Scholten | `wasser_see.dds` | lake surface next to the pier, seen from straight above |
| Small Harbour Morning (`small_harbour_morning`) | <https://polyhaven.com/a/small_harbour_morning> | Greg Zaal (photo), Jarod Guest (editing) | `wasser_fluss.dds`, `wasser_trueb.dds` | calm water next to the harbour wall, seen from straight above |

**Waves, foam, wind and helper textures** are not third-party assets: `tools/assets/wasser_texturen.py` computes them
with a fixed seed from a Phillips ocean spectrum (FFT) and tileable cell noise, `tools/assets/wasser_hilfstexturen.py`
computes glitter, noise, river edge and surf.

## Mountains and cliffs (`Data/Textures/Berge`)

Two photographic scans from Poly Haven (colour, occlusion, normals, displacement; 2K PNG). Changes
(`tools/assets/berge.py bauen`): a brightness structure computed from colour and occlusion, high-pass filtered,
normalised to mean 1 and a fixed spread; normals (x, y) and displacement kept; reduced to 1024 or 512 pixels, one file
in RGBA channels each. The colour of the scans is not used.

| File | Scan | Source | Authors | Size |
|---|---|---|---|---|
| `fels_grob.dds` | Marble Cliff 03 (`marble_cliff_03`) | Poly Haven, <https://polyhaven.com/a/marble_cliff_03> | Amal Kumar | 5.7 × 5.7 m |
| `fels_fein.dds` | Rock Face 03 (`rock_face_03`) | Poly Haven, <https://polyhaven.com/a/rock_face_03> | Dario Barresi (photo), Rico Cilliers (editing) | 2.7 × 2.7 m |

## Wine test data (`tools/wine/testdaten`)

13 photographic scans and three models from Poly Haven, colour maps and geometry only (JPG and glTF, 2K by default).
`generate.py --polyhaven` downloads them on the first run through api.polyhaven.com into a cache outside the repository
(`~/.cache/gzh-testdaten/polyhaven`, MD5 checked against the API) and writes `POLYHAVEN_CC0.txt` with names, authors
and addresses into the data folder. Generated test data only, not in the repository and not shipped.

| Generated file | Scan | Address | Authors |
|---|---|---|---|
| `testgrass.tga` | Rocky Terrain 02 (`rocky_terrain_02`) | <https://polyhaven.com/a/rocky_terrain_02> | Amal Kumar |
| `testdirt.tga` | Dry Ground Rocks (`dry_ground_rocks`) | <https://polyhaven.com/a/dry_ground_rocks> | Rob Tuytel |
| `testrock.tga` | Aerial Rocks 02 (`aerial_rocks_02`) | <https://polyhaven.com/a/aerial_rocks_02> | Rob Tuytel |
| `testsand.tga` | Coast Sand 01 (`coast_sand_01`) | <https://polyhaven.com/a/coast_sand_01> | Rob Tuytel |
| `testbuilding.tga` | Corrugated Iron (`corrugated_iron`) | <https://polyhaven.com/a/corrugated_iron> | Dimitrios Savva, Jenelle van Heerden |
| `testunit.tga`, walls of the Blender models | Brushed Concrete (`brushed_concrete`) | <https://polyhaven.com/a/brushed_concrete> | Dario Barresi, Dimitrios Savva |
| `TestRock.w3d` and its texture | Moon Rock 03 (`moon_rock_03`), model | <https://polyhaven.com/a/moon_rock_03> | Greg Zaal, Jenelle van Heerden, Rico Cilliers |
| `TestSupplyPile.w3d` and its texture | Wooden Crate 02 (`wooden_crate_02`), model, three times | <https://polyhaven.com/a/wooden_crate_02> | James Ray Cock, Jurita Burger |
| `TestSupplyPile.w3d` and its texture | Barrel 03 (`barrel_03`), model, twice | <https://polyhaven.com/a/barrel_03> | Serhii Khromov |
| Blender models: vehicle paint, helmet | Green Metal Rust (`green_metal_rust`) | <https://polyhaven.com/a/green_metal_rust> | Rob Tuytel |
| Blender models: roofs | Painted Metal Shutter (`painted_metal_shutter`) | <https://polyhaven.com/a/painted_metal_shutter> | Charlotte Baglioni, Dario Barresi, Rico Cilliers |
| Blender models: foundations | Concrete Tiles (`concrete_tiles`) | <https://polyhaven.com/a/concrete_tiles> | Dimitrios Savva |
| Blender models: dark metal, interface frames | Metal Plate 02 (`metal_plate_02`) | <https://polyhaven.com/a/metal_plate_02> | Rob Tuytel |
| Blender models: tracks and tyres | Rubberized Track (`rubberized_track`) | <https://polyhaven.com/a/rubberized_track> | Charlotte Baglioni |
| Blender models: wood | Brown Planks 05 (`brown_planks_05`) | <https://polyhaven.com/a/brown_planks_05> | Rob Tuytel |
| Blender models: uniform, sandbags | Denim Fabric (`denim_fabric`) | <https://polyhaven.com/a/denim_fabric> | Rob Tuytel |

Changes: reduced seamlessly, brightness adjusted (model textures also stretched in contrast), stored as TGA. Models
read from glTF, turned to Z up, scaled and, for the supply pile, assembled into a stack, written as W3D. The buildings,
vehicles and the soldier are built by the project (`tools/wine/testdaten/blender_modelle.py`); only their textures come
from Poly Haven. The interface art of the test data (`tools/wine/testdaten/gen_ui_art.py`) is drawn by the project; the
logo is set in **DejaVu Sans Bold** (Bitstream Vera licence and public-domain DejaVu changes,
<https://dejavu-fonts.github.io/License.html>); the font itself is not distributed, only the rendered image in the
generated test data.

## Tools (not shipped)

Used only to make the assets, not part of them: Blender (GPL) and the add-on `io_mesh_w3d` 0.7.1 (OpenSAGE, LGPL-3.0).
The models made with them do not fall under these licences.
