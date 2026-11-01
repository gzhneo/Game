# Free trees: broadleaf and conifer trees from CC0 scans and a procedural generator

Freely licensed replacement for EA's tree models and textures. This package holds the broadleaf trees of base type A and
the conifers of base type C in EA's colouring, each with its burnt variant. All textures have soft alpha edges around the
engine's alpha-test threshold. The remaining base types (B, D, E, F) and the pilot trees of type A (dogwood, autumn oaks)
are built on Windows into the same folder.

- `Art/W3D/*.W3D`: 116 models, 56 of them burnt (`*_B`, used by Generals).
- `Art/Textures/*_tree.dds`: 101 tree textures of the project, HD only (BC7, 1024², full mip chain). `texturpaket.txt`
  gives the SD size (512²) per texture; at SD the engine skips two mip levels, at HD and UHD it reads the file as is.
- Base type A: maple `PTMaple01/02`, `PTXMaple01`–`06`; oak `PTOak01`, `PTXOAK01`–`06`; also `PTOrangeTr01`,
  `PTXBON01`–`04`, `PTBlossom01/02`, `PTDogwod01_S` and the maple stump `PTStumpMpl`. Snow variants: `PTXMaple03/04`,
  `PTXOAK03/04`, `PTXBON03/04`, `PTDogwod01_S`.
- Base type C: pines `PTPine01`–`04` (with `_s`, `_s2`, `_hi`), `PTXPine03`–`08`; firs `PTFir01`, `PTXFIR01`–`10`;
  spruce `PTSpruce01` (with `_s`, `_hi`); cypresses `PTCypress01/02`; stumps `PTStumpOak`, `PTStumpPine`, `PTStumpSpr`
  (texture `PTStump`, no burnt variant).

Sources: Poly Haven (Jacaranda Tree for the oaks, fir tree bark) and ambientCG (leaf sets), all **CC0 1.0**; authors and
addresses are listed in [ASSET_CREDITS.md](../../ASSET_CREDITS.md). All other trees come from the project's own generator
`tools/assets/blender/baum_wachsen.py`. No EA file is read while building; the only interface to EA's data are the model
and texture names. Built in the colour variant `gruen`.

Loading: used by default as free content. The build copies the package to `ProjectData\Inhalt\BaeumeFrei`
(`Code/App/CMakeLists.txt`); the engine also accepts models under `Art\W3D` there. `-freeContent ea` switches it off. It
still works as a mod:

    GZHNeoZH.exe -mod <repo>\Data\BaeumeFrei

Rebuilding:

    python tools/assets/blender/baum_frei.py bauen <textures> --variante=gruen --schritte=render,textur,modell
    python tools/assets/blender/baum_frei.py bauen <textures> --variante=gruen --schritte=paket --stufen=hd
    python tools/assets/blender/baum_frei.py ablegen Data/BaeumeFrei <textures> --variante=gruen --stufe=hd
