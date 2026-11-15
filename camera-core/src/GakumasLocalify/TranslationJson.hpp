#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace GakumasLocal {
// An absent optional file is normal. An installed file that cannot be read or
// parsed must fail the whole snapshot, rather than silently removing entries.
inline std::optional<nlohmann::json> ReadTranslationJson(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) return {};
    if (error) throw std::filesystem::filesystem_error("Cannot inspect translation source", path, error);
    if (!std::filesystem::is_regular_file(status))
        throw std::runtime_error("Translation source is not a regular file");
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open translation source");
    const std::string content(std::istreambuf_iterator<char>(file), {});
    if (file.bad()) throw std::runtime_error("Cannot read translation source");
    auto json = nlohmann::json::parse(content);
    if (!json.is_object()) throw std::runtime_error("Translation source must be a JSON object");
    return json;
}
}
