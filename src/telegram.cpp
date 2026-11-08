//
//  telegram.cpp
//  Kufar Telegram Notifier
//
//  Created by Macintosh on 04.06.2022.
//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <exception>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>
#include <tgbot/tgbot.h>

#include "telegram.hpp"
#include "telegram_commands.hpp"
#include "queries.hpp"
#include "helperfunctions.hpp"

namespace Telegram {
    using namespace std;
    using namespace queries;

    namespace {
        constexpr size_t MAX_IMAGES_IN_GROUP = 10;

        /**
          Formats the price stored in kopecks as a human-readable
          BYN amount with thousands separators.
         */
        string formatPrice(int priceInKopecks) {
            if (priceInKopecks <= 0) {
                return "цена не указана";
            }

            string digits = to_string(priceInKopecks / 100);
            for (ptrdiff_t position = static_cast<ptrdiff_t>(digits.size()) - 3;
                 position > 0;
                 position -= 3) {
                digits.insert(static_cast<size_t>(position), " ");
            }

            return digits + " BYN";
        }

        /**
          Formats the listing date in the server's local time.
          Returns an empty string for missing dates.
         */
        string formatDate(time_t date) {
            if (date == 0) {
                return "";
            }

            tm localTime{};
            localtime_r(&date, &localTime);

            char buffer[32] = {};
            strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M", &localTime);
            return buffer;
        }

        /**
          Builds the caption of a listing. The text is parsed as HTML,
          so every value that comes from Kufar is escaped.
         */
        string formatAdvert(const Kufar::Ad &ad) {
            string text;

            if (ad.tag.has_value() && !ad.tag->empty()) {
                text += "🏷 <b>#" + escapeHTML(ad.tag.value()) + "</b>\n\n";
            }

            text += "🛍 <b>" + escapeHTML(ad.title) + "</b>\n";
            text += "💰 <b>" + formatPrice(ad.price) + "</b>\n";

            if (const string date = formatDate(ad.date); !date.empty()) {
                text += "🕒 " + date + "\n";
            }

            text += "\n👤 " + escapeHTML(ad.sellerName);
            text += ad.phoneNumberIsVisible ? " · телефон показан" : " · телефон скрыт";

            return text;
        }

        /**
          Adds the "open on Kufar" button to every listing.
         */
        TgBot::InlineKeyboardMarkup::Ptr advertKeyboard(const Kufar::Ad &ad) {
            auto button = make_shared<TgBot::InlineKeyboardButton>();
            button->text = "🔗 Открыть на Kufar";
            button->url = ad.link;

            auto markup = make_shared<TgBot::InlineKeyboardMarkup>();
            markup->inlineKeyboard = {{button}};
            return markup;
        }

        int32_t messageThreadID(const optional<int> &threadID) {
            return threadID.value_or(0);
        }
    }

    Client::Client(const TelegramConfiguration &configuration, QueryStore &queryStore)
        : configuration_(configuration),
          httpClient_(make_unique<TgBot::CurlHttpClient>()),
          bot_(make_unique<TgBot::Bot>(configuration_.botToken, *httpClient_)), 
          commandsHandler_(make_unique<CommandsHandler>(*bot_, configuration_, queryStore)) {
        commandsHandler_->registerHandlers(); 
    }

    Client::~Client() {
        stopPolling();
    }

    void Client::sendAdvert(const Kufar::Ad &ad, optional<int> threadID) const {
        const string text = formatAdvert(ad);
        const int32_t threadIDValue = messageThreadID(threadID);
        const TgBot::Api &api = bot_->getApi();
        const TgBot::InlineKeyboardMarkup::Ptr keyboard = advertKeyboard(ad);
        const size_t imageCount = min(ad.images.size(), MAX_IMAGES_IN_GROUP);

        // Telegram does not accept reply_markup for media groups, so an album
        // is sent first and a separate card with the button follows it.
        if (imageCount > 1) {
            TgBot::SendMediaGroupArgs albumArgs;
            albumArgs.chatId = configuration_.chatID;
            albumArgs.messageThreadId = threadIDValue;
            albumArgs.media.reserve(imageCount);

            for (size_t i = 0; i < imageCount; i++) {
                auto photo = make_shared<TgBot::InputMediaPhoto>();
                photo->media = ad.images[i];
                albumArgs.media.push_back(photo);
            }

            api.sendMediaGroup(albumArgs);
        }

        if (imageCount == 1) {
            TgBot::SendPhotoArgs args;
            args.chatId = configuration_.chatID;
            args.photo = ad.images.front();
            args.caption = text;
            args.parseMode = "HTML";
            args.replyMarkup = keyboard;
            args.messageThreadId = threadIDValue;
            api.sendPhoto(args);
            return;
        }

        // Listings without photos, and cards that follow an album.
        TgBot::SendMessageArgs args;
        args.chatId = configuration_.chatID;
        args.text = text;
        args.parseMode = "HTML";
        args.replyMarkup = keyboard;
        args.messageThreadId = threadIDValue;
        api.sendMessage(args);
    }

    void Client::startPolling() {
        if (pollingThread_.joinable()) {
            return;
        }

        // Long polling does not work while a webhook is set for the bot.
        try {
            bot_->getApi().deleteWebhook();
        } catch (const exception &exc) {
            spdlog::warn("[Telegram] Не удалось удалить webhook: {}", exc.what());
        }

        try {
            const TgBot::User::Ptr me = bot_->getApi().getMe();
            spdlog::info("[Telegram] Long polling запущен (бот @{}).", me->username.value_or("?"));
        } catch (const exception &exc) {
            spdlog::warn("[Telegram] Не удалось получить данные бота: {}", exc.what());
        }

        longPoll_ = make_unique<TgBot::TgLongPoll>(*bot_, 100, 10);
        pollingThread_ = jthread([this] { pollingLoop(); });
    }

    void Client::stopPolling() {
        if (longPoll_) {
            longPoll_->stop();
        }
        if (pollingThread_.joinable()) {
            pollingThread_.join();
        }
    }

    void Client::pollingLoop() {
        try {
            // Empty signal list: the process lifecycle is managed by
            // the main thread, not by TgLongPoll.
            longPoll_->startLoop(
                [this](const exception &exc) {
                    spdlog::error("[Telegram polling] {}", exc.what());

                    // Without a pause, an unavailable Telegram API (e.g.
                    // with an invalid token) causes a continuous stream of
                    // errors in the log.
                    if (longPoll_->isRunning()) {
                        this_thread::sleep_for(chrono::seconds(3));
                    }
                },
                {}
            );
        } catch (const exception &exc) {
            spdlog::error("[Telegram polling] Остановлен: {}", exc.what());
        }
    }
};
