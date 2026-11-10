//
//  queries.hpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 01.11.2026
//

#ifndef queries_hpp
#define queries_hpp

#include <string>
#include <vector>
#include <mutex>

#include "kufar.hpp"
#include <nlohmann/json.hpp>

namespace queries {
    const std::string DEFAULT_CACHE_BUCKET = "default";

    struct QueryConfiguration {
        Kufar::KufarConfiguration kufar;
        std::optional<int> messageThreadID;
        // Disabled queries stay in the configuration but are not polled.
        bool enabled = true;

        std::string cacheKey() const;  
        std::string getDescription() const;
        // One-line summary for the Telegram menu, without trailing newlines.
        std::string shortDescription() const;
    };

    // Formats a price range as "100–500 BYN", "от 100 BYN" or "до 500 BYN".
    // Returns an empty string when no bounds are set.
    std::string formatPriceRange(const Kufar::PriceRange &priceRange);

    // Renders a query title as a hashtag when it is a single word,
    // and as plain text otherwise.
    std::string formatTag(const std::string &tag);

    QueryConfiguration parseQuery(const nlohmann::json &query);
    std::string describeQuery(const QueryConfiguration &query);

    class QueryStore {
        public:
            QueryStore(std::string path, nlohmann::json json);

            std::vector<QueryConfiguration> getQueries() const;
            nlohmann::json getRawQueries() const;
            nlohmann::json getRawQuery(size_t index) const;

            void add(nlohmann::json &query);
            void update(size_t index, const nlohmann::json &query);
            void remove(size_t index);

        private:
            std::string path_;
            nlohmann::json json_;
            mutable std::mutex mtx_;
    };
};

#endif /* queries_hpp */
