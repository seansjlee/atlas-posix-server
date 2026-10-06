#include "HttpUtil.hpp"

#include <unordered_map>
#include <cctype>

namespace http {

bool pathContained(const std::string& root, const std::string& candidate) {
    std::string safe_root(root);
    if (safe_root.size() > 1 && safe_root.back() == '/') {
        safe_root.pop_back();
    }

    if (candidate.size() < safe_root.size() ||
        candidate.compare(0, safe_root.size(), safe_root) != 0 ||
        (candidate.size() > safe_root.size() && candidate[safe_root.size()] != '/')) {
        return false;
    }

    return true;
}

std::string contentTypeFor(const std::string& path) {
    static const std::unordered_map<std::string, std::string> mime_types = {
        {"html", "text/html"},
        {"htm",  "text/html"},
        {"css",  "text/css"},
        {"js",   "application/javascript"},
        {"json", "application/json"},
        {"png",  "image/png"},
        {"jpg",  "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"gif",  "image/gif"},
        {"svg",  "image/svg+xml"},
        {"ico",  "image/x-icon"},
        {"txt",  "text/plain"},
    };

    size_t slash = path.find_last_of('/');
    size_t dot = path.find_last_of('.');

    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash) ||
        dot + 1 >= path.size()) {
        return "application/octet-stream";
    }

    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    auto it = mime_types.find(ext);
    if (it != mime_types.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

}
