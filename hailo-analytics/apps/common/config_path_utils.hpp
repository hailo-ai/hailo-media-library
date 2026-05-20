#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

namespace apps::utils
{

/**
 * @brief Rewrite relative .json refs in a medialib_config JSON to absolute paths.
 *
 * Walks the parsed JSON; for every string value that is a non-absolute path
 * ending in .json, rewrites it to an absolute path anchored at source_path's
 * parent directory (symlink-resolved via std::filesystem::weakly_canonical).
 *
 * Use this before MediaLibrary::initialize() when you have the config JSON in
 * memory (typically because you've mutated it programmatically) but still know
 * the source file path. If you have the config on disk untouched, pass the
 * path directly to MediaLibrary::initialize() instead.
 *
 * @param json_string medialib_config JSON content.
 * @param source_path Path the JSON content originated from on disk.
 * @return Rewritten JSON as a string. Returns the input unchanged on parse failure.
 */
inline std::string resolve_relative_refs(const std::string &json_string, const std::filesystem::path &source_path)
{
    std::error_code ec;
    auto resolved = std::filesystem::weakly_canonical(source_path, ec);
    auto base_dir = (ec ? source_path : resolved).parent_path();
    auto cfg = nlohmann::json::parse(json_string, nullptr, false);
    if (cfg.is_discarded())
    {
        return json_string;
    }
    std::function<void(nlohmann::json &)> walk = [&](nlohmann::json &node) {
        if (node.is_object())
        {
            for (auto &element : node.items())
            {
                auto &value = element.value();
                if (value.is_string())
                {
                    std::filesystem::path candidate(value.get<std::string>());
                    if (!candidate.is_absolute() && candidate.has_filename() &&
                        (candidate.extension() == ".json" || element.key() == "backup_folder_path"))
                    {
                        value = (base_dir / candidate).lexically_normal().string();
                    }
                }
                else if (value.is_object() || value.is_array())
                {
                    walk(value);
                }
            }
        }
        else if (node.is_array())
        {
            for (auto &item : node)
            {
                walk(item);
            }
        }
    };
    walk(cfg);
    return cfg.dump();
}

} // namespace apps::utils
