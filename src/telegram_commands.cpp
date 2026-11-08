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
        const string BUTTON_QUERIES = "🔎 Мои поиски";
        const string BUTTON_ADD = "➕ Добавить поиск";
        const string BUTTON_HELP = "❓ Помощь";

        InlineKeyboardButton::Ptr menuButton(const string &text, const string &callbackData) {
            auto button = make_shared<InlineKeyboardButton>();
            button->text = text;
            button->callbackData = callbackData;
            return button;
        }

        KeyboardButton::Ptr keyboardButton(const string &text) {
            auto button = make_shared<KeyboardButton>();
            button->text = text;
            return button;
        }

        // Persistent bottom keyboard for private chats. Groups get the
        // inline menu instead: a reply keyboard there posts a message
        // to the chat on behalf of the user.
        ReplyKeyboardMarkup::Ptr mainReplyKeyboard() {
            auto markup = make_shared<ReplyKeyboardMarkup>();
            markup->keyboard = {
                {keyboardButton(BUTTON_QUERIES)},
                {keyboardButton(BUTTON_ADD)},
                {keyboardButton(BUTTON_HELP)}
            };
            markup->resizeKeyboard = true;
            markup->isPersistent = true;
            return markup;
        }

        bool isPrivateChat(const Message::Ptr &message) {
            return message->chat->type == Chat::Type::Private;
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
                "/help — показать эту справку\n"
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

        bot_.getEvents().onCommand("help", [this](Message::Ptr message) {
            handleHelp(message);
        });

        // Bottom-keyboard buttons and any plain text arrive here.
        bot_.getEvents().onNonCommandMessage([this](Message::Ptr message) {
            handleMenuText(message);
        });

        bot_.getEvents().onCallbackQuery([this](CallbackQuery::Ptr query) {
            handleCallback(std::move(query));
        });

        setBotCommands();
    }

    void CommandsHandler::setBotCommands() {
        try {
            auto start = make_shared<BotCommand>();
            start->command = "start";
            start->description = "Главное меню";

            auto add = make_shared<BotCommand>();
            add->command = "add";
            add->description = "Добавить поиск по ссылке";

            auto help = make_shared<BotCommand>();
            help->command = "help";
            help->description = "Помощь";

            if (bot_.getApi().setMyCommands({start, add, help})) {
                spdlog::info("[Telegram] Список команд обновлён.");
            }
        } catch (const exception &exc) {
            // Command registration is cosmetic: the bot still works without it.
            spdlog::warn("[Telegram] Не удалось обновить список команд: {}", exc.what());
        }
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

        if (isPrivateChat(message)) {
            // /start may need to bring the bottom keyboard back.
            args.replyMarkup = mainReplyKeyboard();
        } else {
            args.replyMarkup = mainMenuKeyboard();
        }

        const Message::Ptr sent = bot_.getApi().sendMessage(args);
        if (sent) {
            rememberScreen(
                message->chat->id,
                message->messageThreadId.value_or(0),
                sent->messageId
            );
        }
    }

    void CommandsHandler::showScreen(const Message::Ptr &message, const string &screen, bool editCurrentMessage) {
        const bool privateChat = isPrivateChat(message);

        string text = mainMenuText();
        InlineKeyboardMarkup::Ptr markup;

        if (screen == "menu:queries") {
            text = queriesText();
            markup = backKeyboard();
        } else if (screen == "menu:add") {
            text = addScreenText();
            markup = backKeyboard();
        } else if (screen == "menu:help") {
            text = helpScreenText();
            markup = backKeyboard();
        } else if (!privateChat) {
            markup = mainMenuKeyboard();
        }

        if (privateChat && !markup) {
            // The main menu in a private chat is driven by the bottom keyboard.
            // Send an empty inline keyboard so the "Back" button disappears.
            markup = make_shared<InlineKeyboardMarkup>();
        }

        const int64_t chatId = message->chat->id;
        const int32_t threadId = message->messageThreadId.value_or(0);

        const int32_t messageId = editCurrentMessage
            ? message->messageId
            : findScreen(chatId, threadId);

        if (messageId != 0) {
            EditMessageTextArgs args;
            args.chatId = chatId;
            args.messageId = messageId;
            args.text = text;
            args.parseMode = "HTML";
            args.replyMarkup = markup;

            try {
                bot_.getApi().editMessageText(args);
                rememberScreen(chatId, threadId, messageId);
                return;
            } catch (const exception &exc) {
                // Telegram also rejects edits that do not change the message.
                spdlog::debug("[Telegram] editMessageText: {}", exc.what());
            }
        }

        SendMessageArgs args;
        args.chatId = chatId;
        if (threadId != 0) {
            args.messageThreadId = threadId;
        }
        args.text = text;
        args.parseMode = "HTML";
        args.replyMarkup = markup;

        const Message::Ptr sent = bot_.getApi().sendMessage(args);
        if (sent) {
            rememberScreen(chatId, threadId, sent->messageId);
        }
    }

    void CommandsHandler::rememberScreen(int64_t chatId, int32_t threadId, int32_t messageId) {
        lock_guard<mutex> lock(screensMutex_);
        screens_[to_string(chatId) + ":" + to_string(threadId)] = messageId;
    }

    int32_t CommandsHandler::findScreen(int64_t chatId, int32_t threadId) const {
        lock_guard<mutex> lock(screensMutex_);
        const auto screen = screens_.find(to_string(chatId) + ":" + to_string(threadId));
        return screen == screens_.end() ? 0 : screen->second;
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

        showScreen(*message, query->data.value(), true);
    }

    void CommandsHandler::handleHelp(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            reply(message, "Это приватный бот");
            return;
        }

        showScreen(message, "menu:help", false);
    }

    void CommandsHandler::handleMenuText(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            return;
        }

        const string text = message->text.value_or("");

        if (text == BUTTON_QUERIES) {
            showScreen(message, "menu:queries", false);
        } else if (text == BUTTON_ADD) {
            showScreen(message, "menu:add", false);
        } else if (text == BUTTON_HELP) {
            showScreen(message, "menu:help", false);
        }
    }

    void CommandsHandler::handleStart(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            reply(message, "Это приватный бот");
            return;
        }

        sendMenu(message);
    }
}
