#include "http_params.h"

#include <optional>

namespace P1 {
namespace {

constexpr std::optional<unsigned char> FromHex(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned char>(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(c - 'a' + 10);
    }
    return std::nullopt;
}

} // namespace

std::string UrlDecode(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '+') {
            out.push_back(' ');
        } else if (src[i] == '%') {
            if (i + 2 >= src.size()) {
                out.push_back('%');
                continue;
            }
            auto high = FromHex(src[++i]);
            auto low = FromHex(src[++i]);
            if (!high || !low) {
                out.push_back('%');
                out.push_back(src[i - 1]);
                out.push_back(src[i]);
                continue;
            }
            out.push_back(static_cast<char>((*high << 4) + *low));
        } else {
            out.push_back(src[i]);
        }
    }
    return out;
}

ParsedTarget ParseRequestTarget(std::string_view target)
{
    ParsedTarget parsed;

    const auto queryPos = target.find('?');
    if (queryPos == std::string_view::npos) {
        parsed.path = std::string(target);
        return parsed;
    }

    parsed.path = std::string(target.substr(0, queryPos));
    std::string_view querySv = target.substr(queryPos + 1);

    while (!querySv.empty()) {
        const auto andPos = querySv.find('&');
        const std::string_view pairSv = querySv.substr(0, andPos); // "&" 前的一段

        const auto eqPos = pairSv.find('=');
        if (eqPos != std::string_view::npos) {
            auto key = UrlDecode(std::string { pairSv.substr(0, eqPos) });
            auto value = UrlDecode(std::string { pairSv.substr(eqPos + 1) });
            parsed.params[key] = value;
        }

        if (andPos == std::string_view::npos) {
            break;
        }
        querySv.remove_prefix(andPos + 1); // 跳过 "&"
    }
    return parsed;
}

} // namespace P1
