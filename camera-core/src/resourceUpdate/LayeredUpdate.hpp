#pragma once

#include "data/LocalifyData.hpp"

namespace GkmsResourceUpdate {
inline bool HasUsablePackageVersion(const std::string& version) {
    return !version.empty() && version != "Unknown";
}
struct LayeredUpdateResult {
    std::string id;
    bool selected{};
};

// The caller captures beforeDownload before starting network work. Publishing a
// pack changes the index revision, but only a selection changes this kind's
// token. Keep both download-time and confirmation-time user choices intact.
template<class Confirm, class Commit>
LayeredUpdateResult CompleteDownloadedPack(const std::filesystem::path& incoming,
    LocalifyData::Kind kind, const std::string& version,
    const LocalifyData::SelectionToken& beforeDownload, Confirm&& confirm, Commit&& commit) {
    LayeredUpdateResult result{LocalifyData::InstallPack(incoming, kind, version)};
    const auto latest = LocalifyData::ReadDisk();
    if (LocalifyData::CaptureSelection(*latest, kind) != beforeDownload) return result;
    if (!LocalifyData::PackUnmodified(*latest, kind) && !confirm()) return result;
    // SelectSet rechecks under the index lock, after the prompt and the caller's
    // shutdown gate. A newer choice keeps the complete pack available, unselected.
    result.selected = commit([&] {
        return LocalifyData::SelectSet(kind, "managed", result.id, {}, beforeDownload);
    });
    return result;
}
} // namespace GkmsResourceUpdate
