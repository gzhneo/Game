GZH Neo {VERSION} – a modernised Command & Conquer Generals and Zero Hour, built from EA's GPL source release.

**You need your own copy of the game.** The setup contains no game data of EA. At the end of the installation it finds
your installation of Generals and Zero Hour (Steam, EA app, Origin or disc) and copies its data into the program folder;
your installation is only read.

{CHANGES}## Downloads

| File | |
|---|---|
| `GZHNeo-{VERSION}-Setup.exe` | Setup with the free ground, road and tree textures (about 550 MB) |
| `GZHNeo-{VERSION}-Setup-NoFreeContent.exe` | Setup without them; the games use EA's textures only (about 50 MB) |
| `SHA256SUMS.txt` | checksums of all files |
| `ffmpeg-*-source.tar.gz`, `ffmpeg-vcpkg-port-*.zip` | source code of the FFmpeg DLLs in the setup (LGPL 2.1), with the vcpkg port that built them |

Installation per user, without administrator rights; Windows 10 or 11, 64-bit, a processor with AVX2 and Direct3D 11.
Uninstalling removes the program folder including the copied game data; settings and saved games in
`Documents\GZH Neo` stay.

## Please note

- **Alpha version:** both games run and are playable, but not everything is finished. See "Known limitations" in the
  README.
- **Not code-signed:** Windows SmartScreen warns when you start the setup the first time ("More info", "Run anyway").
- **Network games, replays and saved games** only work between identical builds. Everybody using this setup can play
  together; programs built from the source yourself are a different build.
- Not affiliated with EA. "Command & Conquer" and "Generals" are trademarks of Electronic Arts Inc.

Built by GitHub Actions from commit {COMMIT}. Licence: GNU GPL v3 with additional terms (LICENSE.md, NOTICE.md);
third-party licences in the `Licenses` folder of the installation.
