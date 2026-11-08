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

        std::string cacheKey() const;  
        std::string getDescription() const;
        // One-line summary for the Telegram menu, without trailing newlines.
        std::string shortDescription() const;
    };

    QueryConfiguration parseQuery(const nlohmann::json &query);
    std::string describeQuery(const QueryConfiguration &query);

    class QueryStore {
        public:
            QueryStore(std::string path, nlohmann::json json);

            std::vector<QueryConfiguration> getQueries() const;
            nlohmann::json getRawQueries() const;

            void add(nlohmann::json &query);

        private:
            std::string path_;
            nlohmann::json json_;
            mutable std::mutex mtx_;
    };
};

#endif /* queries_hpp */
