# Nature from CC0 scans: finished files and origin

Pilot for nature objects and decoration from photographic **CC0 scans by Poly Haven** (CC0 1.0, no attribution required;
listed in [ASSET_CREDITS.md](../../ASSET_CREDITS.md) anyway).

| Folder | Content |
|---|---|
| `Art/Textures/` | seven DDS textures (rocks, barrel, pine; `*cc.dds`, the burnt pine `ptpine01ccb.dds`, the damaged barrel `pmgaldrumccd.dds`), SD tier |
| `Art/W3D/` | nine W3D models (rocks, barrel and damaged barrel, pine in four variants): geometry from the CC0 scans, W3D framework written by the project's own rules (see below) |
| `Art/Terrain/` | `PTPine01.tga`, the pine texture (32 bit, placed into the tree atlas in 64-pixel tiles) |
| `Quellen/` | `LIZENZEN.md` (asset, author, licence, address, download date) and, per asset, Poly Haven's API description (`*_info.json`, `*_files.json`) |

The paths under `Art/` match those in the game archives; the test archive (`NaturCC0.big`) contains the same files. The
build does **not** copy this folder next to the programs.

## W3D framework

The W3D files in `Art/W3D/` contain no data from EA models. Geometry (vertices, normals, triangles, UV) comes from the
CC0 scans; every other value — mesh header, bounding volumes, triangle planes, the 16-byte shader, vertex material,
hierarchy, HLOD and the bone positions — is written by `tools/assets/blender/w3d_geruest.py` according to the project's
own rules (each value derived from its meaning in the engine source). Only the **names** under which the INI files load
the models are EA's: the model name (`PMRocks01`, `PMgaldrum_D`, `PTPine01_hi_B`, ...; for rocks and barrel also the mesh
name, for the pine the hierarchy, HLOD and container name) and the bone `AFLAME01` (fire point, `ParticleSysBone =
AFlame01 ...` in Generals). The fire point sits at the centroid of the branch layers of the project's own crown.

Check: `python tools/assets/blender/natur_bauen.py pruefen PMRocks01 PMRocks03 PMmcluster PMgaldrum PTPine01
--ordner=repo` (every file must be byte-identical to a rebuild from its own geometry). The comparison with EA's
originals is kept separate in `tools/assets/blender/natur_messen.py` (reads EA files, produces nothing).

Size, arrangement and colour are the project's own choices as well (`tools/assets/blender/natur_objekte.json`): the scan
parts at natural size on the game's scale (6.5 units per metre, derived from the collision shapes of soldier and Humvee)
times a design factor, arranged by the project's own rule (`tools/assets/blender/natur_anordnen.py`), colours from the
project's own palette. No number comes from a measurement of EA files.

## Not included here

- **The raw models** (289 MB, 263 MB of which is the fir with 4k textures). They can be downloaded again at any time;
  address and format are listed in `Quellen/LIZENZEN.md`:

      python tools/assets/cc0_laden.py barrel_03 --format gltf --aufloesung 2k --ziel <folder>
      python tools/assets/cc0_laden.py fir_tree_01 --format blend --aufloesung 1k --ziel <folder>
      python tools/assets/cc0_laden.py rock_moss_set_01 rock_moss_set_02 --format gltf --aufloesung 4k --ziel <folder>

- **The HD and UHD versions** of the textures (12 and 47 MB as archives): they are built from the raw files with
  `tools/assets/blender/natur_bauen.py packen ... --stufe=hd|uhd`.
