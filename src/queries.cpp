//
//  queries.cpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 01.11.2026
//

#include <spdlog/fmt/fmt.h>

#include "queries.hpp"
#include "helperfunctions.hpp"
#include "kufar.hpp"

namespace queries {
    using namespace std;
    using namespace nlohmann;
    using namespace Kufar;

    namespace {
        string joinAreas(const std::optional<std::vector<int>> &areas) {
            if (!areas.has_value()) {
                return PROPERTY_UNDEFINED;
            }

            string joinedAreas;
            const vector<int> &values = areas.value();

            for (size_t i = 0; i < values.size(); i++) {
                if (i > 0) {
                    joinedAreas += ", ";
                }
                joinedAreas += EnumString::area(values[i]);
            }

            return joinedAreas;
        }

        /**
          Returns a compact price range, for example "100–800 BYN",
          or an empty string when no bounds are set.
         */
        string compactPriceRange(const PriceRange &priceRange) {
            const optional<int> &priceMin = priceRange.priceMin;
            const optional<int> &priceMax = priceRange.priceMax;

            if (priceMin.has_value() && priceMax.has_value()) {
                return to_string(priceMin.value()) + "–" + to_string(priceMax.value()) + " BYN";
            }
            if (priceMin.has_value()) {
                return "от " + to_string(priceMin.value()) + " BYN";
            }
            if (priceMax.has_value()) {
                return "до " + to_string(priceMax.value()) + " BYN";
            }

            return "";
        }
    }

    string QueryConfiguration::cacheKey() const {
        return messageThreadID.has_value() ? std::to_string(messageThreadID.value()) : DEFAULT_CACHE_BUCKET;
    }

    string QueryConfiguration::getDescription() const {
        return fmt::format(
                "\t- Название: {}\n"
            "\t- ID темы чата: {}\n"
            "\t- Поиск только по заголовку: {}\n"
            "\t- Цена:\n"
            "\t\t- Минимальная: {} BYN\n"
            "\t\t- Максимальная: {} BYN\n"
            "\t- Язык: {}\n"
            "\t- Макс. кол-во объявлений за один запрос: {}\n"
            "\t- Валюта: {}\n"
            "\t- Состояние: {}\n"
            "\t- Продавец: {}\n"
            "\t- Только с Kufar Доставкой: {}\n"
            "\t- Только с Kufar Оплатой: {}\n"
            "\t- Только с Kufar Рассрочкой (Халва): {}\n"
            "\t- Только с фото: {}\n"
            "\t- Только с видео: {}\n"
            "\t- Только с возможностью обмена: {}\n"
            "\t- Тип сортировки: {}\n"
            "\t- Категория: {}\n"
            "\t- Подкатегория: {}\n"
            "\t- Тип компьютерных комплектующих: {}\n"
            "\t- Город: {}\n"
            "\t- Район: {}\n",
            toString(this->kufar.tag),
            (this->messageThreadID.has_value() ? toString(this->messageThreadID) : "[основной чат]"),
            toString(this->kufar.onlyTitleSearch),
            toString(this->kufar.priceRange.priceMin),
            toString(this->kufar.priceRange.priceMax),
            toString(this->kufar.language),
            toString(this->kufar.limit),
            toString(this->kufar.currency),
            (this->kufar.condition.has_value() ? EnumString::itemCondition(this->kufar.condition.value()) : PROPERTY_UNDEFINED),
            (this->kufar.sellerType.has_value() ? EnumString::sellerType(this->kufar.sellerType.value()) : PROPERTY_UNDEFINED),
            toString(this->kufar.kufarDeliveryRequired),
            toString(this->kufar.kufarPaymentRequired),
            toString(this->kufar.kufarHalvaRequired),
            toString(this->kufar.onlyWithPhotos),
            toString(this->kufar.onlyWithVideos),
            toString(this->kufar.onlyWithExchangeAvailable),
            (this->kufar.sortType.has_value() ? EnumString::sortType(this->kufar.sortType.value()) : PROPERTY_UNDEFINED),
            (this->kufar.category.has_value() ? EnumString::category(this->kufar.category.value()) : PROPERTY_UNDEFINED),
            (this->kufar.subCategory.has_value() ? EnumString::subCategory(this->kufar.subCategory.value()) : PROPERTY_UNDEFINED),
            (this->kufar.computerComponentType.has_value() ? EnumString::computerComponentType(this->kufar.computerComponentType.value()) : PROPERTY_UNDEFINED),
            (this->kufar.region.has_value() ? EnumString::region(this->kufar.region.value()) : PROPERTY_UNDEFINED),
            joinAreas(this->kufar.areas)
        );
    }

    string QueryConfiguration::shortDescription() const {
        vector<string> parts;

        if (kufar.tag.has_value() && !kufar.tag->empty()) {
            parts.push_back("#" + kufar.tag.value());
        } else {
            parts.push_back("без ключевого слова");
        }

        if (const string price = compactPriceRange(kufar.priceRange); !price.empty()) {
            parts.push_back(price);
        }
        if (kufar.region.has_value()) {
            parts.push_back(EnumString::region(kufar.region.value()));
        }
        if (kufar.category.has_value()) {
            parts.push_back(EnumString::category(kufar.category.value()));
        }

        parts.push_back(
            messageThreadID.has_value()
                ? "тема " + to_string(messageThreadID.value())
                : "основной чат"
        );

        string description;
        for (size_t i = 0; i < parts.size(); i++) {
            if (i > 0) {
                description += " · ";
            }
            description += parts[i];
        }

        return description;
    }

    QueryConfiguration parseQuery(const json &query) {

        KufarConfiguration kufarConfiguration;

        kufarConfiguration.onlyTitleSearch = getOptionalValue<bool>(query, "only-title-search");
        kufarConfiguration.tag = getOptionalValue<string>(query, "tag");
        if (query.contains("price")) {
            json queryPriceData = query.at("price");
            kufarConfiguration.priceRange.priceMin = getOptionalValue<int>(queryPriceData, "min");
            kufarConfiguration.priceRange.priceMax = getOptionalValue<int>(queryPriceData, "max");
        }

        kufarConfiguration.language = getOptionalValue<string>(query, "language");
        kufarConfiguration.limit = getOptionalValue<int>(query, "limit");
        kufarConfiguration.currency = getOptionalValue<string>(query, "currency");
        kufarConfiguration.condition = getOptionalValue<ItemCondition>(query, "condition");
        kufarConfiguration.sellerType = getOptionalValue<SellerType>(query, "seller-type");
        kufarConfiguration.kufarDeliveryRequired = getOptionalValue<bool>(query, "kufar-delivery-required");
        kufarConfiguration.kufarPaymentRequired = getOptionalValue<bool>(query, "kufar-payment-required");
        kufarConfiguration.kufarHalvaRequired = getOptionalValue<bool>(query, "kufar-halva-required");
        kufarConfiguration.onlyWithPhotos = getOptionalValue<bool>(query, "only-with-photos");
        kufarConfiguration.onlyWithVideos = getOptionalValue<bool>(query, "only-with-videos");
        kufarConfiguration.onlyWithExchangeAvailable = getOptionalValue<bool>(query, "only-with-exchange-available");
        kufarConfiguration.sortType = getOptionalValue<SortType>(query, "sort-type");
        kufarConfiguration.category = getOptionalValue<Category>(query, "category");
        kufarConfiguration.subCategory = getOptionalValue<int>(query, "sub-category");
        kufarConfiguration.computerComponentType = getOptionalValue<ComputerComponentType>(query, "computer-component-type");
        kufarConfiguration.region = getOptionalValue<Region>(query, "region");
        kufarConfiguration.areas = getOptionalValue<vector<int>>(query, "areas");

        QueryConfiguration queryConfiguration;
        queryConfiguration.kufar = kufarConfiguration;
        queryConfiguration.messageThreadID = getOptionalValue<int>(query, "message-thread-id");

        if (queryConfiguration.messageThreadID.has_value() && queryConfiguration.messageThreadID.value() <= 0) {
            throw runtime_error(
                    "\"message-thread-id\" должен быть положительным числом (ID темы чата), "
                    "либо отсутствовать, чтобы публиковать в основной чат."
                    );
        }

        return queryConfiguration;
    }

    QueryStore::QueryStore(string path, json document) 
    : path_(std::move(path)), json_(std::move(document)) {
        if (!json_.contains("queries")) {
            json_["queries"] = json::array();
        }

        if (!json_.at("queries").is_array()) {
            throw runtime_error("Поле \"queries\" должно быть массивом.");
        }

        size_t index = 0;
        for (const json &query : json_.at("queries")) {
            try {
                parseQuery(query);
            } catch (const exception &exc) {
                throw runtime_error("Некорректный запрос #" + to_string(index + 1) + ": " + exc.what());
            }
            index++;
        }
    }

    vector<QueryConfiguration> QueryStore::getQueries() const {
        lock_guard<mutex> lock(mtx_);
        vector<QueryConfiguration> ret;

        for (const json &query : json_.at("queries")) {
            ret.push_back(parseQuery(query));
        }

        return ret;
    }

    json QueryStore::getRawQueries() const {
        lock_guard<mutex> lock(mtx_);
        return json_.at("queries");
    }

    void QueryStore::add(json &query) {
        parseQuery(query);
        lock_guard<mutex> lock(mtx_);
        json updated = json_;
        updated["queries"].push_back(query);

        saveFile(path_, updated.dump(4));
        json_ = std::move(updated);
    }
}
