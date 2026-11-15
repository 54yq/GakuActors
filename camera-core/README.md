# GakuActors camera core

This camera implementation derives from the local GakuLocalify hosted tree at base commit
`cd5db7bda8009bdd52aed43309c7b827f681fbb3`, including local modifications. It
is distributed under GPL-3.0; see `LICENSE`. The remaining upstream sources are
currently retained to satisfy compile and link dependencies. They are not all
enabled at runtime: `GKMS_CAMERA_ONLY` selects a camera-only lifecycle, and
installs no translation detours or Localify panel.

The implementation is linked into `GakuActors.dll`; it does not export its own
Loader plugin entry. It provides `gakuactors.camera-core` and publishes the existing
camera write/render interfaces for Motion compatibility. Its control service
refuses activation if required hooks or shortcut protection are unavailable.
When another Localify camera provider already exists at runtime startup, the
embedded implementation remains dormant instead of acquiring conflicting hooks. GakuActors
prefers the compatible existing Localify control service when present.

The detached camera, input fence, provider coordination and runtime startup are
not yet verified together inside the game. Compile success alone does not
establish that the game version and installed plugin combination are compatible.
