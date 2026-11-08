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
          Uses "сегодня"/"вчера" for recent listings.
          Returns an empty string for missing dates.
         */
        string formatDate(time_t date) {
            if (date == 0) {
                return "";
            }

            tm value{};
            localtime_r(&date, &value);

            const time_t now = time(nullptr);
            tm today{};
            localtime_r(&now, &today);

            char time[8] = {};
            strftime(time, sizeof(time), "%H:%M", &value);

            if (value.tm_year == today.tm_year && value.tm_yday == today.tm_yday) {
                return string("сегодня, ") + time;
            }
            if (value.tm_year == today.tm_year && value.tm_yday + 1 == today.tm_yday) {
                return string("вчера, ") + time;
            }

            char buffer[32] = {};
            strftime(buffer, sizeof(buffer), "%d.%m.%Y, %H:%M", &value);
            return buffer;
        }

        /**
          Builds the caption of a listing. The text is parsed as HTML,
          so every value that comes from Kufar is escaped.

          Albums cannot carry inline buttons, so their caption gets a
          clickable link instead; other messages use a button.
         */
        string formatAdvert(const Kufar::Ad &ad, bool withLink) {
            string text;

            if (ad.tag.has_value() && !ad.tag->empty()) {
                text += "🏷 <b>#" + escapeHTML(ad.tag.value()) + "</b>\n\n";
            }

            text += "🛍 <b>" + escapeHTML(ad.title) + "</b>\n";
            text += "────────────\n";
            text += "💰 <b>" + formatPrice(ad.price) + "</b>\n";

            if (const string date = formatDate(ad.date); !date.empty()) {
                text += "🕒 " + date + "\n";
            }

            text += "👤 " + escapeHTML(ad.sellerName);
            text += ad.phoneNumberIsVisible ? " · ✅ телефон показан" : " · 🔒 телефон скрыт";

            if (withLink) {
                text += "\n\n🔗 <a href=\"" + escapeHTML(ad.link) + "\">Открыть на Kufar</a>";
            }

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
        const int32_t threadIDValue = messageThreadID(threadID);
        const TgBot::Api &api = bot_->getApi();
        const TgBot::InlineKeyboardMarkup::Ptr keyboard = advertKeyboard(ad);
        const size_t imageCount = min(ad.images.size(), MAX_IMAGES_IN_GROUP);

        // Albums cannot carry reply_markup, so their caption includes a
        // clickable link instead of a button.
        const string text = formatAdvert(ad, imageCount > 1);

        if (imageCount > 1) {
            TgBot::SendMediaGroupArgs args;
            args.chatId = configuration_.chatID;
            args.messageThreadId = threadIDValue;
            args.media.reserve(imageCount);

            for (size_t i = 0; i < imageCount; i++) {
                auto photo = make_shared<TgBot::InputMediaPhoto>();
                photo->media = ad.images[i];
                if (i == 0) {
                    photo->caption = text;
                    photo->parseMode = "HTML";
                }
                args.media.push_back(photo);
            }

            api.sendMediaGroup(args);
            return;
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
