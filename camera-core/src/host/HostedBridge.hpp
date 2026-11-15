#pragma once
// The one seam between Localify's hook translation unit and the hosted loader
// bridge. Only the hosted build uses it: standalone keeps its own detour.
#ifdef GKMS_HOSTED
#include "GakuLocalifyCameraWrite.h"
#include "CameraRenderProvider.hpp"

namespace GkmsHosted {

using ActorEntryCallback = void (*)(void* userData, void* actor);

// Same process-creation baseline as the loader's startup timing messages.
unsigned long long ProcessUptimeMs();

// Registers this plugin's before-phase participant on the game's actor update.
// The loader owns the only detour on that entry; the callback never calls the
// original and must not assume it has already run.
bool RegisterActorEntryBefore(const char* name, ActorEntryCallback callback);

// Publishes only successful Localify-owned detours. Called after acquire has
// enabled the target, never merely because an original pointer was returned.
void RecordCameraWriteTarget(uint32_t kind, void* target);
void NotifyCameraWrite(uint32_t kind, void* transformSelf);

void RecordCameraRenderTarget(uint32_t kind, void* target);
bool NotifyMainCamera(void** camera);
CameraRenderPushDispatch BeforeCameraRenderPush(void* self, void* state);
void AfterCameraRenderPush(const CameraRenderPushDispatch& dispatch, void* self);
bool NotifyEndCamera(void* camera);

}

#endif
