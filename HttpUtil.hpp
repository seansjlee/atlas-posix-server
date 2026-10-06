#ifndef HTTP_UTIL_HPP
#define HTTP_UTIL_HPP

#include <string>

namespace http {

// true if `candidate` is inside `root`
// both must already be absolute, canonical paths
bool pathContained(const std::string& root, const std::string& candidate);

std::string contentTypeFor(const std::string& path);

}

#endif
