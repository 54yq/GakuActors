# Public host SDK provenance

The checked-in C-compatible SDK headers are declarations, not Loader
implementation. They retain the unmodified [MIT notice](GakuPlugin.LICENSE.txt),
copyright 2026 GakuLoader Contributors. Localify otherwise retains its
[GPLv3 license](../../LICENSE) and existing dependency notices.

The original three-header snapshot comes from GakuLoader source commit
`ed5ad70819907c0a0f5acc25e804b0aea205fa57`. Hashes below cover canonical Git
blob bytes (LF), as enforced by the public staging branch's `.gitattributes`:

| Header | SHA256 |
| --- | --- |
| `GakuPlugin.h` | `12f4e4db22ff0505990b66cfeaf97b1cae54486c9eb415e7efc3adfd9984867e` |
| `GakuPaths.h` | `a1182d4903c642299fe28e3687a46ac23c19dfdb40e47a83e84da7af52865ea3` |
| `GakuLanguage.h` | `6b1e39362171f9ba3588cec296da31043a9c93c5d4cf68344844bf70e78cd185` |

`GakuPlugin.h` retains ABI v1 layouts and signatures; the update adds two theme
enum values and clarifies hotkey identity documentation. The original snapshot
was already present in Localify `02f6b23de339fc9ea99a54c4d83924e9df011593`
(blob `48e959ff0b7975c9857b9e8a37e7bc6297f74e7e`).

`GakuPaths.h` is the separately versioned owner-scoped `gakuloader.paths` v1
service, originally integrated from Loader
`0426648bd0601486347513af200aa4167a9fa09c`. It guides paths without intercepting
arbitrary I/O or providing a security sandbox.

`GakuLanguage.h` declares the optional `gakuloader.language` v1 service. Old
hosts without it preserve system-locale panel labels; game translation resource
selection is independent. Neither extension changes the frozen ABI v1 table.

The selective 2026-11-06 startup recovery update adds `GakuLifecycle.h` from
GakuLoader commit `6c4ce365d731f491f129c84f2abb9ccf9c116754`. Its canonical
Git/LF SHA256 is
`f7a8c5708bb35356089eb8a83a9288cabd75389fc3952dfbe79adc887972e648`.
The optional `gakuloader.lifecycle` v1 service exposes owner/host stopping state
without entering the host callback gate. This lets an unfinished native parse
stop waiting when paired-host shutdown is requested. Older hosts may omit the
service; translation initialization remains supported, with no early host
cancellation signal. The declaration retains the same MIT notice and does not
change `GakuPlugin.h` or its existing tables.

Hosted can explicitly use these vendored headers without an adjacent checkout.
The development and public staging wrappers differ in their default SDK choice;
see [BUILDING.md](../../BUILDING.md). An explicit SDK directory is compared
against all four headers by the wrapper and CMake. A compatible runtime providing the required services in
`HostedPlugin.cpp` is needed to run Hosted; Standalone does not need Loader.

`GakuLocalifyCameraWrite.h` and `GakuLocalifyCameraRender.h` remain Localify's
coexistence interfaces, implemented in this repository. No Loader, Motion or
VRify implementation is included. See [BUILDING.md](../../BUILDING.md).
