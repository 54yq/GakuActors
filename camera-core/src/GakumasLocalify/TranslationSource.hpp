#pragma once

#include <filesystem>
#include <system_error>
#include "resourceUpdate/ArchivePath.hpp"

namespace GakumasLocal {
// genericTrans is an optional package directory: a first run without
// translation packages legitimately has none, and that is not an error. A path
// that exists but is not a readable directory is different: treating it as "no
// translations yet" would replace a good published snapshot with an empty one.
// The caller decides what to do; this only classifies without throwing.
enum class GenericTransState {
    Missing,   // absent: first run, or the package is not installed
    Ready,     // present and a directory; enumerate it
    Unusable,  // present but not a directory, or the status query failed
};

inline GenericTransState ClassifyGenericTransDirectory(
    const std::filesystem::path& directory, std::error_code& error) {
    error.clear();
    std::error_code statusError;
    const auto status = std::filesystem::status(directory, statusError);
    if (status.type() == std::filesystem::file_type::not_found) {
        // MSVC returns not_found with a clear error; libstdc++ reports the
        // underlying ENOENT through the error code. Both mean "absent".
        error.clear();
        return GenericTransState::Missing;
    }
    if (statusError) {
        error = statusError;
        return GenericTransState::Unusable;
    }
    if (!std::filesystem::is_directory(status)) {
        error = std::make_error_code(std::errc::not_a_directory);
        return GenericTransState::Unusable;
    }
    return GenericTransState::Ready;
}

// Enumerates a Ready package directory and hands every regular file to the
// visitor. Failures throw filesystem_error; nothing is skipped. The enumeration
// deliberately does not use skip_permission_denied: a directory that exists but
// cannot be listed would otherwise look empty, the parse would "succeed", and
// CommandLoop would publish a bundle missing the package translations over the
// working snapshot.
template <class Visitor>
void ForEachGenericTransFile(const std::filesystem::path& directory, Visitor&& visit) {
    std::error_code walkError;
    std::filesystem::recursive_directory_iterator entry(directory, walkError);
    if (walkError) {
        throw std::filesystem::filesystem_error(
            "cannot enumerate genericTrans", directory, walkError);
    }
    const std::filesystem::recursive_directory_iterator end;
    while (entry != end) {
        GkmsResourceUpdate::SafeResource::CheckNoReparse(entry->path());
        std::error_code entryError;
        const bool isRegularFile = entry->is_regular_file(entryError);
        if (entryError) {
            throw std::filesystem::filesystem_error(
                "cannot inspect a genericTrans entry", entry->path(), entryError);
        }
        if (isRegularFile) visit(entry->path());
        entry.increment(walkError);
        if (walkError) {
            throw std::filesystem::filesystem_error(
                "genericTrans enumeration failed", directory, walkError);
        }
    }
}
} // namespace GakumasLocal
