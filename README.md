# Slopinator

A Windows tool for dumping CD and DVD game discs accurately and checking the result against [Redump](http://redump.org).
It drives [redumper](https://github.com/superg/redumper) as a hidden helper, verifies the dump against the Redump DAT,
renames it to the Redump name, keeps a library of your dumps and can compress them to CHD.

Written in C++ with Qt 6. The GUI opens by default; every feature is also available from the command line.

## What it does

- **Dump** PS1 and PS2 discs (CD and DVD, single and dual layer) with settings tuned for the drive, and learn the drive's profile for next time.
- **Re-read bad sectors automatically:** C2 errors are re-read at 4x, and sectors that fail the ECC check without a C2 flag are re-read at 1x.
- **Verify** against the Redump DAT by hash, rename to the Redump name, and detect common problems (such as a shifted audio track) and fix them.
- **Library tab:** every dump in a folder with its Redump status; **Compress to CHD** with a check that the CHD extracts back to the verified data before anything is deleted.
- **Other systems:** a platform list (PS3, PSP, Saturn, Dreamcast, GameCube, Wii, Xbox and more) for DAT checking, with a note where a drive needs special firmware.

## Getting the tools it uses

redumper, chdman and the Redump DATs are not included. The program offers to download them into its own folder (or `%LOCALAPPDATA%\psxrdc2`)
the first time they are needed, after showing you the source and size. Downloads are checked against the SHA-256 the release publishes.

```
psxrdc2 setup               show what is missing
psxrdc2 setup --yes         download it
psxrdc2 update-dat --platform psx
```

## Command line

```
psxrdc2 verify <folder> [--platform psx|ps2] [--json] [--diagnose] [--fix] [--rename]
psxrdc2 dump --drive H: --name NAME [--platform psx|ps2] [--out DIR] [--speed N] [--retries N] [--overwrite]
psxrdc2 library [FOLDER] [--verify]
psxrdc2 compress <folder> [--delete-originals] [--delete-scratch]
psxrdc2 setup dic --yes     (optional second engine: DiscImageCreator)
psxrdc2 dic-dump --drive H: --name NAME [--out DIR] [--speed N]
psxrdc2 submission <folder>   (Redump submission summary + zip of the logs)
psxrdc2 drives | config | install-menu | uninstall-menu
```

## Building

Needs MSYS2 UCRT64 with Qt 6. For a single-file exe:

```
pacman -S mingw-w64-ucrt-x86_64-qt6-static mingw-w64-ucrt-x86_64-libtiff mingw-w64-ucrt-x86_64-libwebp
sh build_static.sh
```

The installer (`installer/psxrdc2.nsi`) is built with NSIS (`pacman -S mingw-w64-ucrt-x86_64-nsis`, then `makensis installer/psxrdc2.nsi`).
It offers a per-user install or a portable copy on any drive.

## What is in this repository

Source (`src/`), the CMake build, the single-file build script, the NSIS installer script and the app icon.
Not included: build output, the installer exe (published under Releases instead), Redump DAT files, downloaded tools,
your config and any dump files. The program recreates all of that on its own.

## Credits

Slopinator stands on the work of others: [Redump](http://redump.org) (the database and the dumping standards),
[redumper](https://github.com/superg/redumper) by superg, [DiscImageCreator](https://github.com/saramibreak/DiscImageCreator) by saramibreak,
chdman from [MAME](https://github.com/mamedev/mame), and [Qt](https://www.qt.io). None of those tools are part of this repository;
they are downloaded on request and keep their own licences. Full list and licence details: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
Not affiliated with or endorsed by Redump.

## Notes

Dumps made with a generic drive are checked, not trusted: the Redump hash decides whether a dump is good.
Discs that need special hardware (GameCube, Wii, Blu-ray, UMD and others) are listed with a warning, and the Lite-On drive this was developed with cannot dump them.
