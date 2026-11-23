#include "Config.h"
#include <algorithm>
#include <cctype>
#include <iterator>

// Spec 020 FR-036: SQMeter's address, whatever form it is typed in, kept as
// scheme://host[:port]; RoofRoof adds SQMeter's paths itself.

namespace SQM
{
    namespace
    {
        const char *const SQMETER_PATHS[] = {"/api/safety", "/api/sensors", "/api/status", "/api/config"};

        std::string lowered(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::string trimmed(const std::string &text)
        {
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
                return "";
            return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        }

        bool knownPath(const std::string &path)
        {
            const std::string lower = lowered(path);
            return path.empty() || std::find(std::begin(SQMETER_PATHS), std::end(SQMETER_PATHS), lower) != std::end(SQMETER_PATHS);
        }
    } // namespace

    std::string Config::sqmeterAddress(const std::string &typed, bool anyPath)
    {
        const std::string text = trimmed(typed);
        if (text.empty())
            return "";
        const size_t separator = text.find("://");
        const std::string scheme = separator == std::string::npos ? "http" : lowered(text.substr(0, separator));
        const std::string rest = separator == std::string::npos ? text : text.substr(separator + 3);
        const size_t pathStart = rest.find_first_of("/?#");
        const std::string authority = rest.substr(0, pathStart);
        std::string path = pathStart == std::string::npos ? "" : rest.substr(pathStart);
        path = path.substr(0, path.find_first_of("?#"));
        while (!path.empty() && path.back() == '/')
            path.pop_back();
        return scheme + "://" + authority + (anyPath || knownPath(path) ? "" : path);
    }
} // namespace SQM
