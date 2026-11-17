# Third party notices and format references

This directory is an independent application. No GakuActors, GakuMotion, SCSP Downloader service, or game executable is loaded at runtime.

## Format references

- [kishidanatsumi/gkmasToolkit](https://github.com/kishidanatsumi/gkmasToolkit): published Octo protobuf field layout, manifest cryptographic format and bundle header masking format. Do not infer a code redistribution license for that repository where none is provided. This project's catalog and download implementation is maintained separately.
- Local prior investigations provided sample manifest and Unity version evidence. Imported game catalog data and downloaded assets are excluded from the source repository and retain their respective rights.
- [patchouliwxy/SCSP-Resource-Downloader](https://github.com/patchouliwxy/SCSP-Resource-Downloader): architecture reference for dependency planning and use of external asset converters. Its game-specific catalog, download protocol, UI and running application are not dependencies of this project.

## Python packages

The direct dependency versions are pinned in `requirements.txt`, with the installed environment recorded in `requirements.lock.txt`. Package license metadata is retained by pip in `.venv/Lib/site-packages/*dist-info/`.

- [UnityPy](https://github.com/K0lb3/UnityPy), 1.25.4: MIT; Unity assets, images and version-header working copies.
- [PyCryptodome](https://github.com/Legrandin/pycryptodome), 3.23.0: its applicable BSD/public-domain licensing; AES manifest decoding.
- [Pillow](https://github.com/python-pillow/Pillow), 11.3.0: HPND/Pillow licensing; PNG encoding.
- [NumPy](https://github.com/numpy/numpy), 2.0.2: BSD-3-Clause.
- Transitive packages retain their own notices and licenses; these are not relicensed as project code.

## External export tools

- [AssetStudioMod](https://github.com/aelurum/AssetStudioMod), CLI distribution identifying as 0.19.0.0, .NET Framework 4.7.2 x64: MIT for AssetStudioMod code; dependencies and the FBX SDK integration retain their applicable terms. The supplied license is preserved in `tools/AssetStudio/LICENSE`.
- [AssetRipper](https://github.com/AssetRipper/AssetRipper), supplied 2.0.0 executable, compile timestamp `Mon Aug 24 18:48:30 UTC 2026`: GPL-3.0; the supplied license is preserved in `tools/AssetRipper/LICENSE`.

The initially installed converter binaries were copied into this project's own tools directory from the existing, SHA-256-verified SCSP Downloader v0.1.5 distribution. Transport archive SHA-256: `1842d7177eb7aea15e4b09c336a7954f0d57b14e1c2629438e21d39dad3f12d6`. This identifies the provided distribution, not an independently verified upstream converter commit. Exact local tool hashes are recorded in `tool-manifest.json`.

`GakumasModelFbx.cs` is this application's integration of the AssetStudioMod public API. Its source is included; `Build-Exporter.ps1` builds it against the installed converter libraries. AssetRipper is invoked in its own headless process, with explicit input/output directories and without modifying original downloaded files.

Before redistributing a binary package, retain applicable third-party notices and satisfy the source and dependency licensing obligations for the specific tools being distributed. Game content is not licensed by this application's GPL license.
