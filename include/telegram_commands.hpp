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
        std::vector<std::string> parseArgs(const TgBot::Message::Ptr message);

        template <typename... Args>
        void reply(const TgBot::Message::Ptr &message, fmt::format_string<Args...> fmtStr, Args&&... args)
        {
            replyImpl(message, fmt::vformat(fmtStr.get(), fmt::make_format_args(args...)));
        }

        void replyImpl(const TgBot::Message::Ptr &message, const std::string &text);

        void showScreen(const TgBot::Message::Ptr &message, const std::string &screen, bool editCurrentMessage);
        void deleteTriggerMessage(const TgBot::Message::Ptr &message);

        void rememberScreen(std::int64_t chatId, std::int32_t threadId, std::int32_t messageId);
        std::int32_t findScreen(std::int64_t chatId, std::int32_t threadId) const;

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
        // Bottom-keyboard presses edit that message instead of posting new ones.
        mutable std::mutex screensMutex_;
        std::map<std::string, std::int32_t> screens_;

        boost::asio::thread_pool thread_pool_{4};
    };
}

#endif
