#ifndef SHARED_SPRITE_ASSET_REFERENCE_H
#define SHARED_SPRITE_ASSET_REFERENCE_H

#include <algorithm>
#include <string>

namespace SpriteAssetReference {

inline std::string Encode(const std::string &image_resource_name, int row,
                          int column) {
    if (image_resource_name.empty()) return {};

    return "sprite://" + image_resource_name + "?row=" +
           std::to_string(std::max(row, 1)) + "&column=" +
           std::to_string(std::max(column, 1));
}

inline bool TryDecode(const std::string &encoded_value,
                      std::string &out_image_resource_name, int &out_row,
                      int &out_column) {
    constexpr const char *kPrefix = "sprite://";
    constexpr const char *kRowKey = "?row=";
    constexpr const char *kColumnKey = "&column=";

    if (encoded_value.rfind(kPrefix, 0) != 0) return false;

    const std::size_t row_key_pos = encoded_value.find(kRowKey);
    const std::size_t column_key_pos = encoded_value.find(kColumnKey);
    if (row_key_pos == std::string::npos ||
        column_key_pos == std::string::npos ||
        column_key_pos <= row_key_pos + std::char_traits<char>::length(kRowKey)) {
        return false;
    }

    out_image_resource_name = encoded_value.substr(
        std::char_traits<char>::length(kPrefix),
        row_key_pos - std::char_traits<char>::length(kPrefix));
    if (out_image_resource_name.empty()) return false;

    try {
        out_row = std::max(
            1, std::stoi(encoded_value.substr(
                   row_key_pos + std::char_traits<char>::length(kRowKey),
                   column_key_pos -
                       (row_key_pos + std::char_traits<char>::length(kRowKey)))));
        out_column = std::max(1, std::stoi(encoded_value.substr(
                                     column_key_pos +
                                     std::char_traits<char>::length(kColumnKey))));
    } catch (...) {
        return false;
    }

    return true;
}

} // namespace SpriteAssetReference

#endif
