#pragma once
#include "GakuLanguage.h"
#include "../gkmsGUI/GUII18n.hpp"

namespace GkmsHosted {
// Optional extension: old/headless hosts preserve the standalone system-locale
// behavior. A malformed optional table never makes translation activation fail.
inline GakuLanguageServiceV1 QueryUiLanguage(const GakuHostApi* host) {
    GakuLanguageServiceV1 service{};
    GkmsGUII18n::UseSystemLanguage();
    if (!host || host->abiVersion != GAKU_ABI_VERSION ||
        host->structSize < sizeof(GakuHostApi) || !host->queryService) return {};
    if (host->queryService(host->hostContext, GAKU_SERVICE_LANGUAGE,
        GAKU_LANGUAGE_VERSION, sizeof(service), &service) != GAKU_OK ||
        service.abiVersion != GAKU_LANGUAGE_VERSION ||
        service.structSize < sizeof(service) || !service.getLanguage) return {};
    return service;
}

inline void RefreshUiLanguage(const GakuLanguageServiceV1& service) {
    if (!service.getLanguage) return;
    GakuUiLanguageV1 language{};
    if (service.getLanguage(sizeof(language), &language) == GAKU_OK &&
        language.abiVersion == GAKU_LANGUAGE_VERSION &&
        language.structSize >= sizeof(language)) {
        GkmsGUII18n::SetLanguage(language.language);
    }
}
}
