//
//  telegram_commands.cpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 03.11.2026
//

#include <exception>
#include <variant>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#include <boost/asio.hpp>

#include "telegram_commands.hpp"
#include "telegram.hpp"
#include "kufar_url.hpp"
#include "queries.hpp"
#include "helperfunctions.hpp"


namespace Telegram {
    using namespace std;
    using namespace TgBot;
    using namespace nlohmann;
    using namespace Telegram;
    using namespace queries;
    using namespace Kufar;

    namespace {
        InlineKeyboardButton::Ptr menuButton(const string &text, const string &callbackData) {
            auto button = make_shared<InlineKeyboardButton>();
            button->text = text;
            button->callbackData = callbackData;
            return button;
        }

        InlineKeyboardMarkup::Ptr menuMarkup(
            const vector<vector<InlineKeyboardButton::Ptr>> &rows
        ) {
            auto markup = make_shared<InlineKeyboardMarkup>();
            markup->inlineKeyboard = rows;
            return markup;
        }

        InlineKeyboardMarkup::Ptr mainMenuKeyboard() {
            return menuMarkup({
                {
                    menuButton("🔎 Мои поиски", "menu:queries"),
                    menuButton("➕ Добавить поиск", "menu:add")
                },
                {
                    menuButton("❓ Помощь", "menu:help")
                }
            });
        }

        InlineKeyboardMarkup::Ptr backKeyboard() {
            return menuMarkup({{menuButton("⬅️ Назад", "menu:main")}});
        }

        string addScreenText() {
            return
                "➕ <b>Добавить поиск</b>\n\n"
                "1. Настройте поиск на Kufar с помощью фильтров.\n"
                "2. Скопируйте ссылку из адресной строки браузера.\n"
                "3. Отправьте её в чат командой:\n\n"
                "<code>/add https://www.kufar.by/l/...</code>\n\n"
                "Объявления будут приходить в ту тему, из которой добавлен поиск.";
        }

        string helpScreenText() {
            return
                "❓ <b>Помощь</b>\n\n"
                "/start — открыть меню\n"
                "/add <code>&lt;ссылка&gt;</code> — добавить поиск с Kufar\n\n"
                "<i>Под каждым объявлением есть кнопка «Открыть на Kufar».</i>";
        }
    }

    CommandsHandler::CommandsHandler(Bot &bot, const TelegramConfiguration &config, QueryStore &queryStore)
        : bot_(bot),
          config_(config),
          queryStore_(queryStore) { 
    }

    void CommandsHandler::registerHandlers() {
        bot_.getEvents().onCommand("start", [this](Message::Ptr message) {
            handleStart(message);
        });

        bot_.getEvents().onCommand("add", [this](Message::Ptr message) {
            handleAdd(message);
        });

        bot_.getEvents().onCallbackQuery([this](CallbackQuery::Ptr query) {
            handleCallback(std::move(query));
        });
    }
    
    vector<string> CommandsHandler::parseArgs(Message::Ptr message) {
        vector<std::string> argv;
        string cur;
        bool inQuotes = false, hasToken = false;
        string text = message -> text.value_or("");

        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '\\' && i + 1 < text.size() && inQuotes) {
                cur += text[++i];
                hasToken = true;
            } else if (c == '"') {
                inQuotes = !inQuotes;
                hasToken = true;
            } else if (isspace(static_cast<unsigned char>(c)) && !inQuotes) {
                if (hasToken) {
                    argv.push_back(cur);
                    cur.clear();
                    hasToken = false;
                }
            } else {
                cur += c;
                hasToken = true;
            }
        }
        if (hasToken) argv.push_back(cur);

        if (!argv.empty()) {
            string& cmd = argv[0];
            if (!cmd.empty() && cmd[0] == '/') cmd.erase(0, 1);
            auto at = cmd.find('@');
            if (at != string::npos) cmd.erase(at);
        }

        return argv;
    }

    void CommandsHandler::replyImpl(const Message::Ptr &message, const string &text) {
        SendMessageArgs replyArgs;

        replyArgs.chatId = message->chat->id;
        if (message->messageThreadId && message->messageThreadId.value())
            replyArgs.messageThreadId = message->messageThreadId.value();

        replyArgs.text = text;

        bot_.getApi().sendMessage(replyArgs);
    }
    
    void CommandsHandler::handleAdd(Message::Ptr message) {
        if (message->chat->id != config_.chatID)
            return;
        
        vector<string> args = parseArgs(message);
        if (args.size() < 2)
        {
            reply(
                message,
                "Укажите ссылку на поиск с Kufar:\n\n"
                "/add https://www.kufar.by/l/..."
            );
            return;
        }

        string url = args[1];

        boost::asio::post(thread_pool_, [this, message, url = std::move(url)] {
            try {
                json newQuery = importSearchQuery(url);
                if (message->messageThreadId)
                    newQuery["message-thread-id"] = message->messageThreadId.value();

                queryStore_.add(newQuery);
                reply(
                    message,
                    "✅ Поиск добавлен. Объявления будут приходить {}.",
                    message->messageThreadId ? "в эту тему" : "в основной чат"
                );
                spdlog::info(
                    "\n- Добавлен запрос:\n{}- Пользователь: {} (ID:{})\n", 
                    parseQuery(newQuery).getDescription(),
                    message->from->username.value_or("no username"),
                    message->from->id
                );
            } catch (const std::exception& exc) {
                spdlog::error("[Telegram]: /add command error: {}", exc.what());
                reply(
                    message,
                    "Не удалось добавить поиск:\n{}\n\n"
                    "Проверьте, что это ссылка на результаты поиска Kufar.",
                    exc.what()
                );
            }
        });
    }

    string CommandsHandler::mainMenuText() const {
        const size_t queryCount = queryStore_.getQueries().size();

        string text = fmt::format(
            "🤖 <b>Kufar Notifier</b>\n\n"
            "Слежу за новыми объявлениями на Kufar и присылаю их в этот чат.\n\n"
            "🔎 Поисков настроено: <b>{}</b>",
            queryCount
        );

        if (queryCount == 0) {
            text += "\n\nПока ни одного. Нажмите «➕ Добавить поиск», чтобы начать.";
        }

        return text;
    }

    string CommandsHandler::queriesText() const {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();

        if (queries.empty()) {
            return
                "🔎 <b>Мои поиски</b>\n\n"
                "Список пуст. Добавьте первый поиск кнопкой «➕ Добавить поиск».";
        }

        string text = fmt::format("🔎 <b>Мои поиски</b> ({})\n", queries.size());
        for (size_t i = 0; i < queries.size(); i++) {
            text += fmt::format(
                "\n<b>{}</b>. {}\n",
                i + 1,
                escapeHTML(queries[i].shortDescription())
            );
        }

        return text;
    }

    void CommandsHandler::sendMenu(const Message::Ptr &message) {
        SendMessageArgs args;

        args.chatId = message->chat->id;
        if (message->messageThreadId && message->messageThreadId.value())
            args.messageThreadId = message->messageThreadId.value();

        args.text = mainMenuText();
        args.parseMode = "HTML";
        args.replyMarkup = mainMenuKeyboard();

        bot_.getApi().sendMessage(args);
    }

    void CommandsHandler::editScreen(const Message::Ptr &message, const string &callbackData) {
        string text = mainMenuText();
        InlineKeyboardMarkup::Ptr markup = mainMenuKeyboard();

        if (callbackData == "menu:queries") {
            text = queriesText();
            markup = backKeyboard();
        } else if (callbackData == "menu:add") {
            text = addScreenText();
            markup = backKeyboard();
        } else if (callbackData == "menu:help") {
            text = helpScreenText();
            markup = backKeyboard();
        }

        EditMessageTextArgs args;
        args.chatId = message->chat->id;
        args.messageId = message->messageId;
        args.text = text;
        args.parseMode = "HTML";
        args.replyMarkup = markup;

        try {
            bot_.getApi().editMessageText(args);
        } catch (const exception &exc) {
            // Telegram also rejects edits that do not change the message.
            spdlog::debug("[Telegram] editMessageText: {}", exc.what());
        }
    }

    void CommandsHandler::handleCallback(CallbackQuery::Ptr query) {
        AnswerCallbackQueryArgs answerArgs;
        answerArgs.callbackQueryId = query->id;

        try {
            bot_.getApi().answerCallbackQuery(answerArgs);
        } catch (const exception &exc) {
            spdlog::warn("[Telegram] answerCallbackQuery: {}", exc.what());
        }

        if (!query->data.has_value() || !query->message) {
            return;
        }

        const auto *message = get_if<shared_ptr<Message>>(&query->message->value);
        if (message == nullptr || !*message) {
            return;
        }

        if ((*message)->chat->id != config_.chatID) {
            return;
        }

        editScreen(*message, query->data.value());
    }

    void CommandsHandler::handleStart(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            reply(message, "Это приватный бот");
            return;
        }

        sendMenu(message);
    }
}
