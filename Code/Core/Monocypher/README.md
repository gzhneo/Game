# Monocypher 4.0.2

Cryptography library for encrypting and authenticating the network packets
(`Code/Core/EngineCore/Engine/PacketSeal.cpp`).

- Source: <https://monocypher.org/download/monocypher-4.0.2.tar.gz>, from it `src/monocypher.c`,
  `src/monocypher.h`, `LICENCE.md` and `AUTHORS.md`. The only change: in `crypto_argon2` an explicit
  cast `(u32)` on `index` (MSVC C4244 under `/W4 /WX`); it does not change the result, and the game does
  not use Argon2.
- SHA-512 of the archive (as published on monocypher.org):
  `bf275d4c53ff94af6cdc723a4e002e9f080f4d1436c86c76bb37870b34807f1d7b32331d8ff8a1aeb369e946f3769021e03e63efac25b82efc5abf54dc084714`
- License: BSD 2-Clause or CC0 1.0 (`LICENCE.md`), at your option; GPL-compatible.
- The file is compiled as C++ through `Code/Core/EngineCore/Engine/Monocypher.cpp` (the project only
  knows the language CXX). To update, replace both files, set the cast above again and run the
  EngineTests (they check the algorithms against fixed test vectors).
