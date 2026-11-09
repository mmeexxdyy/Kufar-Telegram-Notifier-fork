//
//  main.cpp
//  Kufar Telegram Notifier
//
//  Created by Macintosh on 02.06.2022.
//

#include <iostream>
#include <signal.h>
#include <fstream>
#include <map>
#include <vector>

#include "spdlog/spdlog.h"
#include "spdlog/cfg/env.h"

#include <nlohmann/json.hpp>
#include "kufar.hpp"
#include "telegram.hpp"
#include "networking.hpp"
#include "helperfunctions.hpp"
#include "logging.hpp"
#include "arguments.hpp"
#include "queries.hpp"

using namespace std;
using namespace Kufar;
using namespace Telegram;
using namespace queries;
using nlohmann::json;

const string DEFAULT_CACHE_FILE_NAME = "cached-data.json";
const string DEFAULT_CONFIGURATION_FILE_NAME = "kufar-configuration.json";
const string DEFAULT_LOG_FILE_NAME = "kufar-notifier.log";

const int CACHE_VERSION = 2;

struct ConfigurationFile {
    string path;
    json contents;
};

struct CacheFile {
    string path;
    json contents;
};

struct Files {
    ConfigurationFile configuration;
    CacheFile cache;
};

using CacheBuckets = map<string, vector<int>>;

struct ProgramConfiguration {
    TelegramConfiguration telegramConfiguration;
    Files files;
    
    int queryDelaySeconds = 5;
    int loopDelaySeconds = 30;
};

void loadJSONConfigurationData(const json &data, ProgramConfiguration &programConfiguration) {
    {
        json telegramData = data.at("telegram");
        programConfiguration.telegramConfiguration.botToken = telegramData.at("bot-token");
        programConfiguration.telegramConfiguration.chatID = telegramData.at("chat-id");
    }
    {
        if (data.contains("delays")) {
            json delaysData = data.at("delays");
            programConfiguration.queryDelaySeconds = delaysData.at("query");
            programConfiguration.loopDelaySeconds = delaysData.at("loop");
        }
    }
}

void printJSONConfigurationData(const ProgramConfiguration &programConfiguration) {
    spdlog::info(
        "\n- Telegram:\n"
        "\t- Токен: {}\n"
        "\t- ID Чата: {}\n",
        maskToken(programConfiguration.telegramConfiguration.botToken),
        programConfiguration.telegramConfiguration.chatID
    );
    
    spdlog::info(
        "\n- Задержки:\n"
        "\t- Перед новым запросом: {}с. \n"
        "\t- После прохода всего списка запросов: {}c.",
        programConfiguration.queryDelaySeconds,
        programConfiguration.loopDelaySeconds
    );
}

json getJSONDataFromPath(const string &JSONFilePath) {
    spdlog::info("[Загрузка файла]: \"{}\"", JSONFilePath);

    if (!fileExists(JSONFilePath)){
        throw runtime_error("Файл не существует по данному пути или к нему нет доступа: \"" + JSONFilePath + "\"");
    }
    
    if (getFileSize(JSONFilePath) > 4000000) {
        throw runtime_error("Размер файла превышает 4МБ: \"" + JSONFilePath + "\"");
    }
        
    try {
       json textFromFile = json::parse(getTextFromFile(JSONFilePath));
       return textFromFile;
    } catch (const exception &exc) {
       throw runtime_error("Невозможно получить данные из файла \"" + JSONFilePath + "\": " + exc.what());
    }

}

/**
  Cache representation in JSON:
  {
      "version": 2,
      "buckets": {
          "default": [1, 2, 3],
          "42": [1, 2]
      }
  }
 */
json cacheBucketsToJSON(const CacheBuckets &cacheBuckets) {
    json bucketsData = json::object();
    for (const auto &[cacheKey, adIDs] : cacheBuckets) {
        bucketsData[cacheKey] = adIDs;
    }

    return json{
        {"version", CACHE_VERSION},
        {"buckets", bucketsData}
    };
}

CacheBuckets loadCacheBuckets(const json &data) {
    CacheBuckets cacheBuckets;

    if (data.is_array()) {
        if (!data.empty()) {
            spdlog::warn(
                "[Кеш] Обнаружен устаревший формат кеша (плоский массив). "
                "Идентификаторы проигнорированы, кеш будет создан заново."
            );
        }
        return cacheBuckets;
    }

    if (!data.is_object() || !data.contains("buckets")) {
        throw runtime_error("Некорректный формат файла кеша: ожидается объект с полем \"buckets\".");
    }

    const int version = data.value("version", CACHE_VERSION);
    if (version > CACHE_VERSION) {
        throw runtime_error("Файл кеша создан более новой версией программы (version " + to_string(version) + ").");
    }

    const json &bucketsData = data.at("buckets");
    if (!bucketsData.is_object()) {
        throw runtime_error("Некорректный формат файла кеша: поле \"buckets\" должно быть объектом.");
    }

    for (const auto &[cacheKey, adIDs] : bucketsData.items()) {
        cacheBuckets[cacheKey] = adIDs.get<vector<int>>();
    }

    return cacheBuckets;
}

void createEmptyCacheFile(const string &path) {
    spdlog::info("[Кеш] Файл не найден, создаю новый: \"{}\"", path);

    ofstream cacheFile(path, ofstream::trunc);
    if (!cacheFile) {
        throw runtime_error("Не удалось создать файл кеша: \"" + path + "\"");
    }
    cacheFile << cacheBucketsToJSON(CacheBuckets()).dump();
    cacheFile.close();

    if (!cacheFile) {
        throw runtime_error("Не удалось записать файл кеша: \"" + path + "\"");
    }
}

Files loadFiles(const arguments::Options &options) {
    Files files;
    files.configuration.path = options.configPath;
    files.cache.path = options.cachePath;

    files.configuration.contents = getJSONDataFromPath(files.configuration.path);

    if (!fileExists(files.cache.path)) {
        createEmptyCacheFile(files.cache.path);
    }
    files.cache.contents = getJSONDataFromPath(files.cache.path);

    return files;
}

int main(int argc, char **argv) {
    int exitCode = 0;
    optional<arguments::Options> parsedOptions = arguments::parse(argc, argv, exitCode);
    if (!parsedOptions.has_value()) {
        return exitCode;
    }

    arguments::Options options = parsedOptions.value();

    // By default, files are looked up in the current working directory.
    if (options.configPath.empty()) {
        options.configPath = DEFAULT_CONFIGURATION_FILE_NAME;
    }
    if (options.cachePath.empty()) {
        options.cachePath = DEFAULT_CACHE_FILE_NAME;
    }
    if (options.logToFile && options.logPath.empty()) {
        options.logPath = DEFAULT_LOG_FILE_NAME;
    }

    try {
        logging::init(options.logPath, options.logLevel, options.logToFile);
    } catch (const spdlog::spdlog_ex &exc) {
        spdlog::warn("Не удалось инициализировать файловый логгер ({}): {}", options.logPath, exc.what());
        options.logToFile = false;
        logging::init("", options.logLevel, false);
    }

    spdlog::cfg::load_env_levels();
    if (options.logLevelExplicit) {
        spdlog::set_level(options.logLevel);
    }

    spdlog::info("Starting logger");
    spdlog::info("Файл конфигурации: \"{}\"", options.configPath);
    spdlog::info("Файл кеша: \"{}\"", options.cachePath);
    if (options.logToFile) {
        spdlog::info("Файл лога: \"{}\"", options.logPath);
    }

    ProgramConfiguration programConfiguration;
    CacheBuckets cacheBuckets;
    optional<QueryStore> queryStoreStorage;

    try {
        programConfiguration.files = loadFiles(options);
        loadJSONConfigurationData(programConfiguration.files.configuration.contents, programConfiguration);
        printJSONConfigurationData(programConfiguration);
        cacheBuckets = loadCacheBuckets(programConfiguration.files.cache.contents);
        queryStoreStorage.emplace(programConfiguration.files.configuration.path, programConfiguration.files.configuration.contents);
    } catch (const exception &exc) {
        spdlog::error("{}", exc.what());
        return 1;
    }

    QueryStore &queryStore = *queryStoreStorage;
    
    for (const QueryConfiguration &query : queryStore.getQueries()) {
        spdlog::info(
            "\n- Добавлен запрос:\n {}", 
            query.getDescription()
        );
    }

    Telegram::Client telegramClient(programConfiguration.telegramConfiguration, queryStore);
    telegramClient.startPolling();

    while (true) {
        for (const auto &requestConfiguration : queryStore.getQueries()) {
            if (!requestConfiguration.enabled) {
                DEBUG_MSG("(Query) Skipping disabled query, topic: {}", requestConfiguration.cacheKey());
                continue;
            }

            const string cacheKey = requestConfiguration.cacheKey();
            vector<int> &viewedAds = cacheBuckets[cacheKey];
            
            try {
                for (const auto &advert : getAds(requestConfiguration.kufar)) {
                    if (!vectorContains(viewedAds, advert.id)) {
                        spdlog::info(
                            "[New]: Adding [Title: {}], [ID: {}], [Tag: {}], [Topic: {}], [Link: {}]",
                            advert.title,
                            advert.id,
                            advert.tag.value_or(PROPERTY_UNDEFINED),
                            cacheKey,
                            advert.link
                        );

                        try {
                            Ad detailedAd = advert;
                            try {
                                enrichAdFromPage(detailedAd);
                            } catch (const exception &exc) {
                                // The card is still useful without the page data.
                                spdlog::warn("(enrichAd) [ID: {}]: {}", advert.id, exc.what());
                            }
                            telegramClient.sendAdvert(detailedAd, requestConfiguration.messageThreadID);
                            // Only ads that were actually sent are added to the cache.
                            viewedAds.push_back(advert.id);
                            saveFile(programConfiguration.files.cache.path, cacheBucketsToJSON(cacheBuckets).dump());
                        } catch (const exception &exc) {
                            spdlog::error("(sendAdvert) [Topic: {}]: {}", cacheKey, exc.what());
                        }
                        
                    } else {
                        //spdlog::debug("[Already was!]");
                    }
                    sleepMilliseconds(300);
                }
            } catch (const exception &exc) {
                spdlog::error("(getAds): {}", exc.what());
            }
            DEBUG_MSG("(QueryDelay) Sleeping for: {}s.", programConfiguration.queryDelaySeconds);
            sleepSeconds(programConfiguration.queryDelaySeconds);
        }
        DEBUG_MSG("(LoopDelay) Sleeping for: {}s.", programConfiguration.loopDelaySeconds);
        sleepSeconds(programConfiguration.loopDelaySeconds);
    }
    return 0;
}
