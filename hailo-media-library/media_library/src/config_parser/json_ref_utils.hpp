/**
 * @file json_ref_utils.hpp
 * @brief Internal helpers for detecting and anchoring relative filesystem
 *        path references inside medialib JSON configs.
 *
 * Several layers of the media-library accept JSON configs that contain
 * string values which are filesystem paths -- either ".json" sub-config
 * refs or path-like fields such as "sensor_calib_path" and
 * "backup_folder_path". When a config originates from a file on disk,
 * any relative path inside it is interpreted relative to that file's
 * directory. The utilities here implement that single rule so all
 * callers behave identically.
 *
 * @note Internal to media-library. Not part of the installed public API.
 */
#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace json_ref_utils
{
/**
 * @brief Test whether a JSON string value is a relative path reference.
 *
 * A value is a relative ref iff it parses to a path that has a filename,
 * is not absolute, and either ends in ".json" or sits under a well-known
 * non-".json" path-like key (e.g. "sensor_calib_path",
 * "backup_folder_path").
 *
 * @param value The string value of the JSON node.
 * @param key The JSON key the value belongs to (used for the
 *        non-".json" whitelist).
 * @return true if @p value should be anchored against a base directory.
 */
bool is_relative_ref(const std::string &value, const std::string &key);

/**
 * @brief Recursively scan a JSON tree for any relative path reference.
 *
 * @param node Root of the JSON subtree to scan.
 * @return true if any string value in @p node would be classified as a
 *         relative ref by is_relative_ref().
 */
bool has_relative_refs(const nlohmann::json &node);

/**
 * @brief Anchor a single path string against a base directory.
 *
 * If @p value is already absolute, or @p base_dir is empty, the value is
 * returned unchanged. Otherwise the result is
 * `(base_dir / value).lexically_normal()` as a string.
 *
 * @param value Path string to anchor.
 * @param base_dir Directory to interpret @p value relative to.
 * @return Anchored path string.
 */
std::string anchor_path(const std::string &value, const std::filesystem::path &base_dir);

/**
 * @brief Rewrite every relative path reference in @p node in place.
 *
 * Walks @p node recursively; for each string value classified as a
 * relative ref by is_relative_ref(), replaces it with the result of
 * anchor_path(value, base_dir). Non-string values and string values
 * that aren't relative refs are left untouched.
 *
 * @param node JSON tree to mutate in place.
 * @param base_dir Directory to anchor relative refs against. If empty,
 *        no rewriting occurs.
 */
void resolve_refs_in_place(nlohmann::json &node, const std::filesystem::path &base_dir);
} // namespace json_ref_utils
