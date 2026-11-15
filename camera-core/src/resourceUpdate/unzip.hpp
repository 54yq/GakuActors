#pragma once

#include "ArchivePath.hpp"
#include "minizip/unzip.h"
#include "minizip/iowin32.h"
#include <fstream>

namespace GkmsResourceUpdate {
// Two passes: validate every entry and the mapped tree before creating a file.
// Only a fresh transaction's incoming directory may be passed as destination.
inline bool ExtractResourcePackage(const std::filesystem::path& archiveRaw,
    const std::filesystem::path& destinationRaw, SafeResource::PackageKind kind,
    std::string& error) {
    unzFile zip = nullptr;
    try {
        const auto archive = SafeResource::ExtendedAbsolute(archiveRaw);
        const auto destination = SafeResource::ExtendedAbsolute(destinationRaw);
        SafeResource::CheckNoReparse(archive);
        SafeResource::CheckNoReparse(destination);
        if (std::filesystem::exists(destination))
            throw std::runtime_error("Extraction destination must be new");
        zlib_filefunc64_def callbacks{};
        fill_win32_filefunc64W(&callbacks);
        zip = unzOpen2_64(archive.c_str(), &callbacks);
        if (!zip) throw std::runtime_error("Cannot open resource archive");
        std::vector<SafeResource::ArchiveEntry> entries;
        int status = unzGoToFirstFile(zip);
        while (status == UNZ_OK) {
            unz_file_info64 info{};
            if (unzGetCurrentFileInfo64(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
                info.size_filename == 0 || info.size_filename > 32760 || entries.size() >= 250000)
                throw std::runtime_error("Invalid or oversized archive directory");
            std::vector<char> name(static_cast<size_t>(info.size_filename) + 1, '\0');
            if (unzGetCurrentFileInfo64(zip, &info, name.data(), static_cast<uLong>(name.size()),
                nullptr, 0, nullptr, 0) != UNZ_OK)
                throw std::runtime_error("Cannot read complete archive entry name");
            const std::string raw(name.data(), static_cast<size_t>(info.size_filename));
            const bool directory = raw.back() == '/' || raw.back() == '\\';
            const unsigned unixType = (info.external_fa >> 16) & 0170000;
            if ((unixType != 0 && unixType != 0100000 && unixType != 0040000) ||
                (info.external_fa & FILE_ATTRIBUTE_REPARSE_POINT) || (info.flag & 1))
                throw std::runtime_error("Links, special files and encrypted entries are not resource files");
            entries.push_back({SafeResource::NormalizeEntry(raw), directory, {}});
            status = unzGoToNextFile(zip);
        }
        if (status != UNZ_END_OF_LIST_OF_FILE)
            throw std::runtime_error("Cannot enumerate complete resource archive");
        SafeResource::MapArchive(entries, kind);
        std::filesystem::create_directories(destination);
        status = unzGoToFirstFile(zip);
        for (const auto& entry : entries) {
            if (status != UNZ_OK) throw std::runtime_error("Resource archive changed while extracting");
            if (!entry.target.empty()) {
                const auto target = destination / SafeResource::NativePath(entry.target);
                SafeResource::CheckNoReparse(target);
                if (entry.directory) {
                    std::filesystem::create_directories(target);
                } else {
                    std::filesystem::create_directories(target.parent_path());
                    if (unzOpenCurrentFile(zip) != UNZ_OK)
                        throw std::runtime_error("Cannot open archive entry");
                    std::ofstream output(target, std::ios::binary | std::ios::trunc);
                    if (!output) {
                        unzCloseCurrentFile(zip);
                        throw std::runtime_error("Cannot create extracted file");
                    }
                    char buffer[64 * 1024];
                    int read = 0;
                    while ((read = unzReadCurrentFile(zip, buffer, sizeof(buffer))) > 0) {
                        output.write(buffer, read);
                        if (!output) break;
                    }
                    output.close();
                    const int closed = unzCloseCurrentFile(zip);
                    if (read < 0 || !output || closed != UNZ_OK)
                        throw std::runtime_error("Archive data or CRC check failed");
                    HANDLE durable = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (durable == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot flush extracted resource");
                    const BOOL flushed = FlushFileBuffers(durable);
                    CloseHandle(durable);
                    if (!flushed) throw std::runtime_error("Cannot persist extracted resource");
                }
            }
            status = unzGoToNextFile(zip);
        }
        if (status != UNZ_END_OF_LIST_OF_FILE)
            throw std::runtime_error("Archive directory changed while extracting");
        unzClose(zip);
        return true;
    } catch (const std::exception& exception) {
        if (zip) unzClose(zip);
        error = exception.what();
        return false; // Failed private transaction is retained; never touch live files.
    }
}
} // namespace GkmsResourceUpdate
