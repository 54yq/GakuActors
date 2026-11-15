#pragma once
#include <filesystem>
#include <string>
#include <utility>

namespace GkmsResourceUpdate {
// Access must be serialized by the caller. The active resource view can change
// after an asynchronous reload, so the path is part of the cache identity.
class VersionCache {
    std::filesystem::path path_;
    std::string value_;
public:
    template<class Reader>
    std::string Read(const std::filesystem::path& path, bool useCache, Reader&& reader) {
        if (useCache && path_ == path && !value_.empty() && value_ != "Unknown") return value_;
        path_ = path;
        value_ = std::forward<Reader>(reader)(path);
        return value_;
    }
    void Clear() { value_.clear(); path_.clear(); }
};
}
