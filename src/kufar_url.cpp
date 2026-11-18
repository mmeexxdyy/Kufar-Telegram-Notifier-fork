//
//  kufar_url.cpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 01.11.2026
//

#include <algorithm>
#include <charconv>
#include <climits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <curl/curl.h>

#include "kufar_url.hpp"
#include "networking.hpp"

namespace Kufar {
    using namespace std;
    using nlohmann::json;

    namespace {
        constexpr size_t MAX_SEARCH_PAGE_SIZE = 4 * 1024 * 1024;

        const unordered_map<string, string> BOOLEAN_PARAMETERS = {
            {"ot", "only-title-search"},
            {"dle", "kufar-delivery-required"},
            {"sde", "kufar-payment-required"},
            {"hlv", "kufar-halva-required"},
            {"oph", "only-with-photos"},
            {"ovi", "only-with-videos"},
            {"pse", "only-with-exchange-available"}
        };

        const unordered_set<string> IGNORED_PARAMETERS = {
            "cursor",
            "elementType",
            "etext",
            // UI-only parameters: they do not change the search results.
            "b2c",
            "r_pageType"
        };

        string getURLPart(CURLU *url, CURLUPart part, const string &name) {
            char *value = nullptr;
            const CURLUcode result = curl_url_get(url, part, &value, 0);
            if (result != CURLUE_OK) {
                throw runtime_error("Unable to read " + name + " from Kufar URL.");
            }

            unique_ptr<char, decltype(&curl_free)> valueHolder(value, curl_free);
            return string(value);
        }

        void validateSearchURL(const string &sourceURL) {
            unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
            if (!url) {
                throw runtime_error("Unable to initialize URL parser.");
            }

            const CURLUcode result = curl_url_set(url.get(), CURLUPART_URL, sourceURL.c_str(), 0);
            if (result != CURLUE_OK) {
                throw runtime_error("Invalid Kufar search URL.");
            }

            if (getURLPart(url.get(), CURLUPART_SCHEME, "scheme") != "https") {
                throw runtime_error("Kufar search URL must use HTTPS.");
            }

            const string host = getURLPart(url.get(), CURLUPART_HOST, "host");
            static const unordered_set<string> supportedHosts = {
                "www.kufar.by",
                "auto.kufar.by",
                "re.kufar.by"
            };
            if (!supportedHosts.contains(host)) {
                throw runtime_error("Unsupported Kufar host: " + host);
            }

            const string path = getURLPart(url.get(), CURLUPART_PATH, "path");
            if (path != "/l" && path.rfind("/l/", 0) != 0) {
                throw runtime_error("URL must point to a Kufar search page, not an item or account page.");
            }
        }

        int parseInteger(const string &parameter, const string &text) {
            int value = 0;
            const char *begin = text.data();
            const char *end = begin + text.size();
            const auto [position, error] = from_chars(begin, end, value);

            if (text.empty() || error != errc() || position != end) {
                throw runtime_error("Kufar parameter '" + parameter + "' must contain an integer.");
            }
            return value;
        }

        long long parsePriceValue(const string &text) {
            long long value = 0;
            const char *begin = text.data();
            const char *end = begin + text.size();
            const auto [position, error] = from_chars(begin, end, value);

            if (text.empty() || error != errc() || position != end || value < 0) {
                throw runtime_error("Kufar parameter 'prc' contains an invalid price.");
            }
            if (value % 100 != 0 || value / 100 > INT_MAX) {
                throw runtime_error(
                    "Kufar parameter 'prc' contains a price that cannot be represented "
                    "as whole BYN in the current query format."
                );
            }
            return value / 100;
        }

        bool parseBoolean(const string &parameter, const string &text) {
            if (text == "1") {
                return true;
            }
            if (text == "0") {
                return false;
            }
            throw runtime_error("Kufar parameter '" + parameter + "' must be either 0 or 1.");
        }

        json parsePriceRange(const string &text) {
            if (!text.starts_with("r:")) {
                throw runtime_error("Kufar parameter 'prc' must use the r:<min>,<max> format.");
            }

            const size_t separator = text.find(',', 2);
            if (separator == string::npos || text.find(',', separator + 1) != string::npos) {
                throw runtime_error("Kufar parameter 'prc' must use the r:<min>,<max> format.");
            }

            const string minimum = text.substr(2, separator - 2);
            const string maximum = text.substr(separator + 1);
            if (minimum.empty() && maximum.empty()) {
                throw runtime_error("Kufar parameter 'prc' must contain at least one price bound.");
            }

            json price = json::object();
            if (!minimum.empty()) {
                price["min"] = parsePriceValue(minimum);
            }
            if (!maximum.empty()) {
                price["max"] = parsePriceValue(maximum);
            }
            if (!minimum.empty() && !maximum.empty() && price["min"] > price["max"]) {
                throw runtime_error("Kufar parameter 'prc' has a minimum price above its maximum.");
            }
            return price;
        }

        vector<int> parseAreas(const string &text) {
            static const string orPrefix = "v.or:";
            const string values = text.starts_with(orPrefix) ? text.substr(orPrefix.size()) : text;
            if (values.empty() || values.front() == ',' || values.back() == ',') {
                throw runtime_error("Kufar parameter 'ar' contains an invalid area list.");
            }

            vector<int> areas;
            size_t begin = 0;
            while (begin < values.size()) {
                const size_t end = values.find(',', begin);
                const string value = values.substr(begin, end == string::npos ? string::npos : end - begin);
                const int area = parseInteger("ar", value);
                if (area <= 0) {
                    throw runtime_error("Kufar parameter 'ar' must contain positive area IDs.");
                }
                areas.push_back(area);

                if (end == string::npos) {
                    break;
                }
                begin = end + 1;
            }
            return areas;
        }

        bool isIgnoredParameter(const string &key) {
            return IGNORED_PARAMETERS.contains(key) || key.starts_with("utm_");
        }

        bool isSupportedParameter(const string &key) {
            static const unordered_set<string> supportedParameters = {
                "query", "lang", "size", "cur", "cnd", "cmp", "sort",
                "cat", "prn", "cct", "rgn", "ar", "prc"
            };
            return supportedParameters.contains(key) || BOOLEAN_PARAMETERS.contains(key);
        }

        string joinStrings(const vector<string> &values) {
            string result;
            for (size_t i = 0; i < values.size(); i++) {
                if (i > 0) {
                    result += ", ";
                }
                result += values[i];
            }
            return result;
        }
    }

    json extractSearchParameters(const string &html) {
        const string marker = "__NEXT_DATA__";
        const size_t markerPosition = html.find(marker);
        if (markerPosition == string::npos) {
            throw runtime_error("Kufar page does not contain __NEXT_DATA__.");
        }

        const size_t scriptStart = html.rfind("<script", markerPosition);
        const size_t scriptOpeningEnd = html.find('>', markerPosition);
        if (scriptStart == string::npos || scriptOpeningEnd == string::npos) {
            throw runtime_error("Kufar page contains an invalid __NEXT_DATA__ script.");
        }

        const size_t scriptEnd = html.find("</script>", scriptOpeningEnd + 1);
        if (scriptEnd == string::npos) {
            throw runtime_error("Kufar page contains an unterminated __NEXT_DATA__ script.");
        }

        const string documentText = html.substr(
            scriptOpeningEnd + 1,
            scriptEnd - scriptOpeningEnd - 1
        );

        json document;
        try {
            document = json::parse(documentText);
        } catch (const exception &exc) {
            throw runtime_error("Unable to parse Kufar __NEXT_DATA__: " + string(exc.what()));
        }

        if (!document.is_object() || !document.contains("query") || !document.at("query").is_object()) {
            throw runtime_error("Kufar __NEXT_DATA__ does not contain a resolved search query.");
        }
        return document.at("query");
    }

    json convertSearchParameters(const string &sourceURL, const json &parameters) {
        if (!parameters.is_object()) {
            throw runtime_error("Resolved Kufar search parameters must be a JSON object.");
        }

        vector<string> unsupportedParameters;
        for (const auto &[key, value] : parameters.items()) {
            if (isIgnoredParameter(key)) {
                continue;
            }
            if (!isSupportedParameter(key)) {
                unsupportedParameters.push_back(key);
                continue;
            }
            if (!value.is_string()) {
                throw runtime_error("Kufar search parameter '" + key + "' must be a string.");
            }
        }

        if (!unsupportedParameters.empty()) {
            throw runtime_error(
                "Unsupported Kufar search parameter(s): " + joinStrings(unsupportedParameters)
            );
        }

        json query = {
            {"source-url", sourceURL}
        };

        for (const auto &[key, value] : parameters.items()) {
            if (isIgnoredParameter(key)) {
                continue;
            }

            const string parameter = value.get<string>();
            if (key == "query") {
                if (!parameter.empty()) {
                    query["tag"] = parameter;
                }
            } else if (key == "lang") {
                if (!parameter.empty()) {
                    query["language"] = parameter;
                }
            } else if (key == "size") {
                const int limit = parseInteger(key, parameter);
                if (limit <= 0) {
                    throw runtime_error("Kufar parameter 'size' must be positive.");
                }
                query["limit"] = limit;
            } else if (key == "cur") {
                if (!parameter.empty()) {
                    query["currency"] = parameter;
                }
            } else if (key == "cnd") {
                const int condition = parseInteger(key, parameter);
                if (condition != 1 && condition != 2) {
                    throw runtime_error("Kufar parameter 'cnd' must be either 1 or 2.");
                }
                query["condition"] = condition;
            } else if (key == "cmp") {
                const int sellerType = parseInteger(key, parameter);
                if (sellerType != 0 && sellerType != 1) {
                    throw runtime_error("Kufar parameter 'cmp' must be either 0 or 1.");
                }
                query["seller-type"] = sellerType;
            } else if (key == "sort") {
                if (parameter == "prc.d" || parameter == "prs.d") {
                    query["sort-type"] = 1;
                } else if (parameter == "prc.a" || parameter == "prs.a") {
                    query["sort-type"] = 2;
                } else if (parameter != "lst.d") {
                    throw runtime_error("Unsupported Kufar sort type: " + parameter);
                }
            } else if (key == "cat") {
                const int category = parseInteger(key, parameter);
                if (category <= 0) {
                    throw runtime_error("Kufar parameter 'cat' must contain a positive category ID.");
                }
                query["sub-category"] = category;
            } else if (key == "prn") {
                const int category = parseInteger(key, parameter);
                if (category <= 0) {
                    throw runtime_error("Kufar parameter 'prn' must contain a positive category ID.");
                }
                query["category"] = category;
            } else if (key == "cct") {
                static const unordered_set<int> supportedComponentTypes = {
                    1, 3, 5, 7, 8, 9, 10, 11, 13, 15, 17, 19, 21
                };
                const int componentType = parseInteger(key, parameter);
                if (!supportedComponentTypes.contains(componentType)) {
                    throw runtime_error("Kufar parameter 'cct' contains an unknown computer component type ID.");
                }
                query["computer-component-type"] = componentType;
            } else if (key == "rgn") {
                if (parameter != "all" && parameter != "0") {
                    const int region = parseInteger(key, parameter);
                    if (region < 1 || region > 7) {
                        throw runtime_error("Kufar parameter 'rgn' contains an unknown region ID.");
                    }
                    query["region"] = region;
                }
            } else if (key == "ar") {
                query["areas"] = parseAreas(parameter);
            } else if (key == "prc") {
                query["price"] = parsePriceRange(parameter);
            } else if (BOOLEAN_PARAMETERS.contains(key)) {
                query[BOOLEAN_PARAMETERS.at(key)] = parseBoolean(key, parameter);
            }
        }

        return query;
    }

    json importSearchQuery(const string &sourceURL) {
        validateSearchURL(sourceURL);
        const string html = Networking::getTextFromURL(sourceURL, MAX_SEARCH_PAGE_SIZE);
        const json parameters = extractSearchParameters(html);
        return convertSearchParameters(sourceURL, parameters);
    }
}
