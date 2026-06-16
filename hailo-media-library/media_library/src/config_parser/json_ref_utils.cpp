#include "json_ref_utils.hpp"

#include <unordered_set>

namespace json_ref_utils
{

namespace
{
const std::unordered_set<std::string> &anchored_non_json_keys()
{
    static const std::unordered_set<std::string> keys = {
        "sensor_calib_path",
        "backup_folder_path",
    };
    return keys;
}
} // namespace

bool is_relative_ref(const std::string &value, const std::string &key)
{
    std::filesystem::path candidate(value);
    if (!candidate.has_filename() || candidate.is_absolute())
    {
        return false;
    }
    return candidate.extension() == ".json" || anchored_non_json_keys().count(key) > 0;
}

std::string anchor_path(const std::string &value, const std::filesystem::path &base_dir)
{
    std::filesystem::path candidate(value);
    if (candidate.is_absolute() || base_dir.empty())
    {
        return value;
    }
    return (base_dir / candidate).lexically_normal().string();
}

bool has_relative_refs(const nlohmann::json &node)
{
    if (node.is_object())
    {
        for (const auto &element : node.items())
        {
            const auto &value = element.value();
            if (value.is_string())
            {
                if (is_relative_ref(value.get<std::string>(), element.key()))
                {
                    return true;
                }
            }
            else if (has_relative_refs(value))
            {
                return true;
            }
        }
    }
    else if (node.is_array())
    {
        for (const auto &item : node)
        {
            if (has_relative_refs(item))
            {
                return true;
            }
        }
    }
    return false;
}

void resolve_refs_in_place(nlohmann::json &node, const std::filesystem::path &base_dir)
{
    if (node.is_object())
    {
        for (auto &element : node.items())
        {
            auto &value = element.value();
            if (value.is_string())
            {
                if (is_relative_ref(value.get<std::string>(), element.key()))
                {
                    value = anchor_path(value.get<std::string>(), base_dir);
                }
            }
            else if (value.is_object() || value.is_array())
            {
                resolve_refs_in_place(value, base_dir);
            }
        }
    }
    else if (node.is_array())
    {
        for (auto &item : node)
        {
            resolve_refs_in_place(item, base_dir);
        }
    }
}

} // namespace json_ref_utils
