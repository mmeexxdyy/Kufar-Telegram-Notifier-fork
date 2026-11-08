//
//  telegram_commands.hpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 03.11.2026
//

#ifndef telegram_commands_hpp
#define telegram_commands_hpp 

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include <spdlog/details/registry.h>
#include <tgbot/tgbot.h>
#include <spdlog/fmt/fmt.h>
#include <boost/asio/thread_pool.hpp>

namespace queries {
    class QueryStore;
}

namespace Telegram {
    class TelegramConfiguration;

    class CommandsHandler {
    public:
        CommandsHandler(TgBot::Bot &bot, const Telegram::TelegramConfiguration &config, queries::QueryStore &queryStore);

        void registerHandlers();

    private:
        struct ChatState {
            std::string screen = "main";
            size_t queryIndex = 0;
            bool pendingPrice = false;
        };

        std::vector<std::string> parseArgs(const TgBot::Message::Ptr message);

        template <typename... Args>
        void reply(const TgBot::Message::Ptr &message, fmt::format_string<Args...> fmtStr, Args&&... args)
        {
            replyImpl(message, fmt::vformat(fmtStr.get(), fmt::make_format_args(args...)));
        }

        void replyImpl(const TgBot::Message::Ptr &message, const std::string &text);

        void showScreen(const TgBot::Message::Ptr &message, const std::string &screen, bool editCurrentMessage);
        void renderScreen(const TgBot::Message::Ptr &message, const std::string &text,
                          TgBot::InlineKeyboardMarkup::Ptr inlineMarkup,
                          TgBot::ReplyKeyboardMarkup::Ptr replyMarkup,
                          bool editCurrentMessage);

        void queriesListScreen(const TgBot::Message::Ptr &message, bool editCurrentMessage);
        void queryScreen(const TgBot::Message::Ptr &message, size_t index, bool editCurrentMessage);
        void pricePromptScreen(const TgBot::Message::Ptr &message, size_t index, bool editCurrentMessage,
                               const std::string &error = "");
        void deleteConfirmScreen(const TgBot::Message::Ptr &message, size_t index, bool editCurrentMessage);

        void toggleQuery(const TgBot::Message::Ptr &message, size_t index);
        void deleteQuery(const TgBot::Message::Ptr &message, size_t index);
        void applyPrice(const TgBot::Message::Ptr &message, size_t index, const std::string &text);

        void goBack(const TgBot::Message::Ptr &message);

        void deleteTriggerMessage(const TgBot::Message::Ptr &message);

        void rememberScreen(std::int64_t chatId, std::int32_t threadId, std::int32_t messageId);
        std::int32_t findScreen(std::int64_t chatId, std::int32_t threadId) const;

        ChatState getState(std::int64_t chatId, std::int32_t threadId) const;
        void setState(std::int64_t chatId, std::int32_t threadId, const ChatState &state);

        void setBotCommands();

        std::string mainMenuText() const;
        std::string queriesText() const;

        void handleStart(TgBot::Message::Ptr message);
        void handleHelp(TgBot::Message::Ptr message);
        void handleAdd(TgBot::Message::Ptr message);
        void handleMenuText(TgBot::Message::Ptr message);
        void handleCallback(TgBot::CallbackQuery::Ptr query);

        TgBot::Bot &bot_;
        queries::QueryStore &queryStore_;
        const Telegram::TelegramConfiguration &config_;

        // Message that currently shows the menu screens, per chat and topic.
        mutable std::mutex screensMutex_;
        std::map<std::string, std::int32_t> screens_;

        // Current screen and selected query per chat and topic.
        mutable std::mutex statesMutex_;
        std::map<std::string, ChatState> states_;

        boost::asio::thread_pool thread_pool_{4};
    };
}

#endif
