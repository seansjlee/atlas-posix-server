#include "../HttpUtil.hpp"

#include <iostream>
#include <string>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
    } \
} while (0)

static void testPathContained() {
    const std::string root = "/srv/public";

    CHECK(http::pathContained(root, "/srv/public/index.html"));
    CHECK(http::pathContained(root, "/srv/public/css/app.css"));
    CHECK(http::pathContained(root, "/srv/public"));

    // escapes
    CHECK(!http::pathContained(root, "/srv/secret.txt"));
    CHECK(!http::pathContained(root, "/etc/passwd"));
    CHECK(!http::pathContained(root, "/srv"));

    // sibling that shares the prefix string but is a different dir
    CHECK(!http::pathContained(root, "/srv/public-secret/x"));
    CHECK(!http::pathContained(root, "/srv/publicfoo"));

    // trailing slash on root
    CHECK(http::pathContained("/srv/public/", "/srv/public/index.html"));
}

static void testContentType() {
    CHECK(http::contentTypeFor("/srv/public/index.html") == "text/html");
    CHECK(http::contentTypeFor("/srv/public/app.css") == "text/css");
    CHECK(http::contentTypeFor("/srv/public/data.json") == "application/json");
    CHECK(http::contentTypeFor("/srv/public/logo.PNG") == "image/png");

    // extension is taken from the last segment only
    CHECK(http::contentTypeFor("/srv/.htmlsecrets/data.json") == "application/json");

    // no extension, or dot in a parent dir only
    CHECK(http::contentTypeFor("/srv/public/README") == "application/octet-stream");
    CHECK(http::contentTypeFor("/srv/a.b/file") == "application/octet-stream");
    CHECK(http::contentTypeFor("/srv/public/archive.tar") == "application/octet-stream");
}

int main() {
    testPathContained();
    testContentType();

    if (failures == 0) {
        std::cout << "all " << checks << " checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " checks failed\n";
    return 1;
}
