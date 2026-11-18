//
//  queries.cpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 01.11.2026
//

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_set>

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

        void addLine(std::string& out, std::string_view label, const std::optional<std::string>& value) {
            if (value.has_value())
                out += fmt::format("\t- {}: {}\n", label, *value);
        }

        template <typename T, typename F>
        std::optional<std::string> mapOpt(const std::optional<T>& opt, F&& fn) {
            if (!opt) return std::nullopt;
            return fn(*opt);
        }

        QueryID parseQueryID(const json &query) {
            if (!query.is_object() || !query.contains("id")) {
                throw runtime_error("Поле \"id\" является обязательным.");
            }

            const json &idValue = query.at("id");
            QueryID id = 0;

            if (idValue.is_number_unsigned()) {
                id = idValue.get<QueryID>();
            } else if (idValue.is_number_integer()) {
                const auto signedID = idValue.get<std::int64_t>();
                if (signedID > 0) {
                    id = static_cast<QueryID>(signedID);
                }
            } else {
                throw runtime_error("Поле \"id\" должно быть положительным целым числом.");
            }

            if (id == 0) {
                throw runtime_error("Поле \"id\" должно быть положительным целым числом.");
            }

            return id;
        }
    }

    string formatPriceRange(const PriceRange &priceRange) {
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

    string formatTag(const string &tag) {
        if (tag.empty()) {
            return tag;
        }
        if (tag.find_first_of(" \t\n") == string::npos) {
            return "#" + tag;
        }
        return tag;
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

    string QueryConfiguration::getDescriptionConcise() const {
        string out;

        addLine(out, "Название", toStringExplicit(this->kufar.tag));
        addLine(out, "Поиск только по заголовку", toStringExplicit(this->kufar.onlyTitleSearch));
        addLine(out, "Минимальная цена", toStringExplicit(this->kufar.priceRange.priceMin));
        addLine(out, "Максимальная цена", toStringExplicit(this->kufar.priceRange.priceMax));
        addLine(out, "Язык", toStringExplicit(this->kufar.language));
        addLine(out, "Макс. кол-во объявлений за один запрос", toStringExplicit(this->kufar.limit));
        addLine(out, "Валюта", toStringExplicit(this->kufar.currency));
        addLine(out, "Состояние", mapOpt(this->kufar.condition, EnumString::itemCondition));
        addLine(out, "Продавец", mapOpt(this->kufar.sellerType, EnumString::sellerType));
        addLine(out, "Только с Kufar Доставкой", toStringExplicit(this->kufar.kufarDeliveryRequired));
        addLine(out, "Только с Kufar Оплатой", toStringExplicit(this->kufar.kufarPaymentRequired));
        addLine(out, "Только с Kufar Рассрочкой (Халва)", toStringExplicit(this->kufar.kufarHalvaRequired));
        addLine(out, "Только с фото", toStringExplicit(this->kufar.onlyWithPhotos));
        addLine(out, "Только с видео", toStringExplicit(this->kufar.onlyWithVideos));
        addLine(out, "Только с возможностью обмена", toStringExplicit(this->kufar.onlyWithExchangeAvailable));
        addLine(out, "Тип сортировки", mapOpt(this->kufar.sortType, EnumString::sortType));
        addLine(out, "Категория", mapOpt(this->kufar.category, EnumString::category));
        addLine(out, "Подкатегория", mapOpt(this->kufar.subCategory, EnumString::subCategory));
        addLine(out, "Тип компьютерных комплектующих", mapOpt(this->kufar.computerComponentType, EnumString::computerComponentType));
        addLine(out, "Город", mapOpt(this->kufar.region, EnumString::region));
        addLine(out, "Район", mapOpt(this->kufar.areas, joinAreas));

        return out;
    }

    string QueryConfiguration::shortDescription() const {
        vector<string> parts;

        if (kufar.tag.has_value() && !kufar.tag->empty()) {
            parts.push_back(formatTag(kufar.tag.value()));
        } else {
            parts.push_back("без названия");
        }

        if (const string price = formatPriceRange(kufar.priceRange); !price.empty()) {
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
        if (!query.is_object()) {
            throw runtime_error("Запрос должен быть объектом.");
        }

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
        queryConfiguration.id = parseQueryID(query);
        queryConfiguration.kufar = kufarConfiguration;
        queryConfiguration.messageThreadID = getOptionalValue<int>(query, "message-thread-id");
        queryConfiguration.enabled = !getOptionalValue<bool>(query, "disabled").value_or(false);

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

        QueryID highestID = 0;
        unordered_set<QueryID> queryIDs;
        size_t index = 0;

        for (const json &query : json_.at("queries")) {
            try {
                if (!query.is_object()) {
                    throw runtime_error("Запрос должен быть объектом.");
                }

                if (query.contains("id")) {
                    const QueryID id = parseQueryID(query);
                    if (!queryIDs.insert(id).second) {
                        throw runtime_error("Дублируется значение поля \"id\": " + to_string(id));
                    }
                    highestID = std::max(highestID, id);
                }
            } catch (const exception &exc) {
                throw runtime_error("Некорректный запрос #" + to_string(index + 1) + ": " + exc.what());
            }
            index++;
        }

        bool queriesModified = false;
        index = 0;
        for (json &query : json_.at("queries")) {
            try {
                if (!query.contains("id")) {
                    if (highestID == numeric_limits<QueryID>::max()) {
                        throw runtime_error("Невозможно назначить запросу новый ID: достигнут максимум.");
                    }
                    query["id"] = ++highestID;
                    queriesModified = true;
                }
                parseQuery(query);
            } catch (const exception &exc) {
                throw runtime_error("Некорректный запрос #" + to_string(index + 1) + ": " + exc.what());
            }
            index++;
        }

        if (highestID == numeric_limits<QueryID>::max()) {
            nextQueryID_.reset();
        } else {
            nextQueryID_ = highestID + 1;
        }

        if (queriesModified) {
            saveFile(path_, json_.dump(4));
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

    int QueryStore::queryCount() const {
        lock_guard<mutex> lock(mtx_);
        return json_.at("queries").size();
    }

    json QueryStore::getRawQueries() const {
        lock_guard<mutex> lock(mtx_);
        return json_.at("queries");
    }

    void QueryStore::add(json &query) {
        lock_guard<mutex> lock(mtx_);

        if (!nextQueryID_.has_value()) {
            throw runtime_error("Невозможно добавить запрос: достигнут максимальный ID.");
        }
        if (!query.is_object()) {
            throw runtime_error("Запрос должен быть объектом.");
        }

        const QueryID id = nextQueryID_.value();
        json storedQuery = query;
        storedQuery["id"] = id;
        parseQuery(storedQuery);

        json updated = json_;
        updated["queries"].push_back(storedQuery);

        saveFile(path_, updated.dump(4));
        json_ = std::move(updated);
        query = std::move(storedQuery);

        if (id == numeric_limits<QueryID>::max()) {
            nextQueryID_.reset();
        } else {
            nextQueryID_ = id + 1;
        }
    }

    optional<QueryConfiguration> QueryStore::remove(QueryID id) {
        lock_guard<mutex> lock(mtx_);
        json updated = json_;
        json &storedQueries = updated.at("queries");
        auto queryIterator = find_if(
            storedQueries.begin(),
            storedQueries.end(),
            [id](const json &query) { return parseQueryID(query) == id; }
        );

        if (queryIterator == storedQueries.end()) {
            return nullopt;
        }

        QueryConfiguration removedQuery = parseQuery(*queryIterator);
        storedQueries.erase(queryIterator);

        saveFile(path_, updated.dump(4));
        json_ = std::move(updated);
        return removedQuery;
    }

    json QueryStore::getRawQuery(size_t index) const {
        lock_guard<mutex> lock(mtx_);
        const json &queries = json_.at("queries");
        if (index >= queries.size()) {
            throw out_of_range("Запрос с таким номером не найден.");
        }
        return queries.at(index);
    }

    void QueryStore::update(size_t index, const json &query) {
        parseQuery(query);

        lock_guard<mutex> lock(mtx_);
        json updated = json_;
        if (index >= updated.at("queries").size()) {
            throw out_of_range("Запрос с таким номером не найден.");
        }

        updated["queries"][index] = query;
        saveFile(path_, updated.dump(4));
        json_ = std::move(updated);
    }

    void QueryStore::remove(size_t index) {
        lock_guard<mutex> lock(mtx_);
        json updated = json_;
        auto &queries = updated.at("queries");
        if (index >= queries.size()) {
            throw out_of_range("Запрос с таким номером не найден.");
        }

        queries.erase(queries.begin() + static_cast<ptrdiff_t>(index));
        saveFile(path_, updated.dump(4));
        json_ = std::move(updated);
    }
}
