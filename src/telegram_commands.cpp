//
//  telegram_commands.cpp
//  Kufar Telegram Notifier
//
//  Created by Am80286 on 03.11.2026
//

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
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
        const string BUTTON_BACK = "⬅️ Назад";

        const string BUTTON_PRICE = "💰 Изменить цену";
        const string BUTTON_HIDE = "⏸ Скрыть";
        const string BUTTON_SHOW = "▶️ Включить";
        const string BUTTON_DELETE = "🗑 Удалить";
        const string BUTTON_DELETE_CONFIRM = "🗑 Да, удалить";
        const string BUTTON_CANCEL = "⬅️ Отмена";

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

        InlineKeyboardMarkup::Ptr menuMarkup(
            const vector<vector<InlineKeyboardButton::Ptr>> &rows
        ) {
            auto markup = make_shared<InlineKeyboardMarkup>();
            markup->inlineKeyboard = rows;
            return markup;
        }

        ReplyKeyboardMarkup::Ptr replyMarkup(
            const vector<vector<KeyboardButton::Ptr>> &rows
        ) {
            auto markup = make_shared<ReplyKeyboardMarkup>();
            markup->keyboard = rows;
            markup->resizeKeyboard = true;
            markup->isPersistent = true;
            return markup;
        }

        InlineKeyboardMarkup::Ptr mainMenuKeyboard() {
            return menuMarkup({
                {
                    menuButton(BUTTON_QUERIES, "menu:queries"),
                    menuButton(BUTTON_ADD, "menu:add")
                },
                {
                    menuButton(BUTTON_HELP, "menu:help")
                }
            });
        }

        InlineKeyboardMarkup::Ptr backKeyboard() {
            return menuMarkup({{menuButton(BUTTON_BACK, "menu:main")}});
        }

        ReplyKeyboardMarkup::Ptr mainReplyKeyboard() {
            return replyMarkup({
                {keyboardButton(BUTTON_QUERIES)},
                {keyboardButton(BUTTON_ADD)},
                {keyboardButton(BUTTON_HELP)}
            });
        }

        ReplyKeyboardMarkup::Ptr backReplyKeyboard() {
            return replyMarkup({{keyboardButton(BUTTON_BACK)}});
        }

        bool isPrivateChat(const Message::Ptr &message) {
            return message->chat->type == Chat::Type::Private;
        }

        string trim(const string &value) {
            const size_t begin = value.find_first_not_of(" \t\n\r");
            if (begin == string::npos) {
                return "";
            }
            const size_t end = value.find_last_not_of(" \t\n\r");
            return value.substr(begin, end - begin + 1);
        }

        string stateKey(int64_t chatId, int32_t threadId) {
            return to_string(chatId) + ":" + to_string(threadId);
        }

        optional<int> parsePriceBound(const string &value) {
            const string text = trim(value);
            if (text.empty()) {
                return nullopt;
            }

            try {
                size_t processed = 0;
                const int number = stoi(text, &processed);
                if (processed != text.size() || number < 0) {
                    return nullopt;
                }
                return number;
            } catch (const exception &) {
                return nullopt;
            }
        }

        // Accepts "100-500", "от 100", "до 500", a single "500" (as a maximum)
        // and "нет" to remove the price filter.
        optional<PriceRange> parsePriceRangeText(const string &raw, string &error) {
            const string text = trim(raw);
            if (text.empty()) {
                error = "Пустое сообщение. Укажите цену.";
                return nullopt;
            }

            string lower = text;
            transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(tolower(c));
            });

            if (lower == "нет" || lower == "убрать" || lower == "без цены") {
                return PriceRange();
            }

            PriceRange range;

            if (lower.rfind("от", 0) == 0) {
                const optional<int> minimum = parsePriceBound(lower.substr(2));
                if (!minimum.has_value()) {
                    error = "Не понял минимальную цену.";
                    return nullopt;
                }
                range.priceMin = minimum;
                return range;
            }

            if (lower.rfind("до", 0) == 0) {
                const optional<int> maximum = parsePriceBound(lower.substr(2));
                if (!maximum.has_value()) {
                    error = "Не понял максимальную цену.";
                    return nullopt;
                }
                range.priceMax = maximum;
                return range;
            }

            const size_t dash = lower.find_first_of("-–—");
            if (dash != string::npos) {
                const optional<int> minimum = parsePriceBound(lower.substr(0, dash));
                const optional<int> maximum = parsePriceBound(lower.substr(dash + 1));
                if (!minimum.has_value() || !maximum.has_value()) {
                    error = "Не понял диапазон. Пример: 100-500.";
                    return nullopt;
                }
                if (minimum.value() > maximum.value()) {
                    error = "Минимум больше максимума.";
                    return nullopt;
                }
                range.priceMin = minimum;
                range.priceMax = maximum;
                return range;
            }

            const optional<int> maximum = parsePriceBound(lower);
            if (!maximum.has_value()) {
                error = "Не понял цену. Пример: 100-500, от 100 или до 500.";
                return nullopt;
            }
            range.priceMax = maximum;
            return range;
        }

        optional<size_t> parseIndex(const string &data, const string &prefix) {
            if (data.rfind(prefix, 0) != 0) {
                return nullopt;
            }

            try {
                size_t processed = 0;
                const unsigned long index = stoul(data.substr(prefix.size()), &processed);
                if (processed != data.size() - prefix.size()) {
                    return nullopt;
                }
                return static_cast<size_t>(index);
            } catch (const exception &) {
                return nullopt;
            }
        }

        optional<size_t> parseQueryButtonText(const string &text) {
            const size_t dot = text.find('.');
            if (dot == string::npos || dot == 0) {
                return nullopt;
            }

            for (size_t i = 0; i < dot; i++) {
                if (!isdigit(static_cast<unsigned char>(text[i]))) {
                    return nullopt;
                }
            }

            try {
                const int number = stoi(text.substr(0, dot));
                return number > 0 ? optional<size_t>(static_cast<size_t>(number - 1)) : nullopt;
            } catch (const exception &) {
                return nullopt;
            }
        }

        string queryButtonLabel(const QueryConfiguration &query) {
            if (query.kufar.tag.has_value() && !query.kufar.tag->empty()) {
                return "#" + query.kufar.tag.value();
            }
            return "без ключевого слова";
        }

        string queryDetailText(size_t index, const QueryConfiguration &query) {
            const string price = formatPriceRange(query.kufar.priceRange);

            string details = "🏷 " + escapeHTML(queryButtonLabel(query)) + "\n";
            details += "💰 " + (price.empty() ? string("не указана") : price) + "\n";

            if (query.kufar.region.has_value()) {
                details += "📍 " + EnumString::region(query.kufar.region.value()) + "\n";
            }
            if (query.kufar.category.has_value()) {
                details += "📂 " + EnumString::category(query.kufar.category.value()) + "\n";
            }

            details += "💬 " + (query.messageThreadID.has_value()
                ? "тема " + to_string(query.messageThreadID.value())
                : string("основной чат"));

            const string status = query.enabled
                ? "✅ <b>Активен</b>\n<i>Объявления приходят в этот чат</i>"
                : "⏸ <b>Скрыт</b>\n<i>Опрос остановлен, поиск сохранён</i>";

            return fmt::format(
                "🔎 <b>Поиск {}</b>\n\n<blockquote>{}</blockquote>\n\n{}",
                index + 1,
                details,
                status
            );
        }

        string addScreenText() {
            return
                "➕ <b>Добавить поиск</b>\n\n"
                "1. Настройте фильтры на Kufar.\n"
                "2. Скопируйте ссылку из адресной строки.\n"
                "3. Отправьте её командой:\n\n"
                "<code>/add https://www.kufar.by/l/...</code>\n\n"
                "<blockquote>Объявления будут приходить в ту тему, "
                "из которой добавлен поиск.</blockquote>";
        }

        string helpScreenText() {
            return
                "❓ <b>Помощь</b>\n\n"
                "<b>Команды</b>\n"
                "/start — открыть меню\n"
                "/help — эта справка\n"
                "/add <code>ссылка</code> — добавить поиск с Kufar\n\n"
                "<b>Управление поисками</b>\n"
                "Откройте «🔎 Мои поиски»: в карточке поиска можно изменить цену, "
                "скрыть его или удалить.\n\n"
                "<i>У объявления есть кнопка «Открыть на Kufar», "
                "а у альбома — ссылка в подписи.</i>";
        }
    }

    CommandsHandler::CommandsHandler(Bot &bot, const TelegramConfiguration &config, QueryStore &queryStore)
        : bot_(bot),
          queryStore_(queryStore),
          config_(config) {
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

    string CommandsHandler::mainMenuText() const {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();
        const size_t active = count_if(queries.begin(), queries.end(), [](const QueryConfiguration &query) {
            return query.enabled;
        });

        string text = fmt::format(
            "🤖 <b>Kufar Notifier</b>\n"
            "<i>Слежу за Kufar и присылаю свежие объявления</i>\n\n"
            "🔎 Поисков: <b>{}</b> · активных: <b>{}</b>",
            queries.size(),
            active
        );

        if (queries.empty()) {
            text += "\n\n<i>Пока ни одного. Нажмите «➕ Добавить поиск», чтобы начать.</i>";
        }

        return text;
    }

    string CommandsHandler::queriesText() const {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();

        if (queries.empty()) {
            return
                "🔎 <b>Мои поиски</b>\n\n"
                "<i>Пока пусто. Добавьте первый поиск — и объявления начнут приходить сюда.</i>";
        }

        string text =
            "🔎 <b>Мои поиски</b>\n"
            "<i>Выберите поиск, чтобы изменить цену, скрыть или удалить его</i>";

        for (size_t i = 0; i < queries.size(); i++) {
            const QueryConfiguration &query = queries[i];
            const string price = formatPriceRange(query.kufar.priceRange);

            string details = "💰 " + (price.empty() ? string("цена не указана") : price);
            if (query.kufar.region.has_value()) {
                details += " · 📍 " + EnumString::region(query.kufar.region.value());
            }
            details += query.enabled ? "\n✅ активен" : "\n⏸ скрыт";

            text += fmt::format(
                "\n\n<blockquote><b>{}</b>. {}\n{}</blockquote>",
                i + 1,
                escapeHTML(queryButtonLabel(query)),
                details
            );
        }

        return text;
    }

    void CommandsHandler::renderScreen(const Message::Ptr &message, const string &text,
                                       InlineKeyboardMarkup::Ptr inlineMarkup,
                                       ReplyKeyboardMarkup::Ptr replyKeyboard,
                                       bool editCurrentMessage) {
        const bool privateChat = isPrivateChat(message);
        const int64_t chatId = message->chat->id;
        const int32_t threadId = message->messageThreadId.value_or(0);

        const int32_t previousId = editCurrentMessage
            ? message->messageId
            : findScreen(chatId, threadId);

        // Telegram rejects edits to a message that installed a reply keyboard,
        // so private chats replace the previous screen instead of editing it.
        if (privateChat) {
            SendMessageArgs args;
            args.chatId = chatId;
            if (threadId != 0) {
                args.messageThreadId = threadId;
            }
            args.text = text;
            args.parseMode = "HTML";
            if (replyKeyboard) {
                args.replyMarkup = replyKeyboard;
            }

            const Message::Ptr sent = bot_.getApi().sendMessage(args);
            if (!sent) {
                return;
            }

            rememberScreen(chatId, threadId, sent->messageId);

            if (previousId != 0 && previousId != sent->messageId) {
                try {
                    bot_.getApi().deleteMessage(chatId, previousId);
                } catch (const exception &exc) {
                    spdlog::debug("[Telegram] deleteMessage: {}", exc.what());
                }
            }
            return;
        }

        // Groups edit the menu message in place.
        if (previousId != 0) {
            EditMessageTextArgs args;
            args.chatId = chatId;
            args.messageId = previousId;
            args.text = text;
            args.parseMode = "HTML";
            args.replyMarkup = inlineMarkup;

            try {
                bot_.getApi().editMessageText(args);
                rememberScreen(chatId, threadId, previousId);
                return;
            } catch (const exception &exc) {
                if (string(exc.what()).find("message is not modified") != string::npos) {
                    rememberScreen(chatId, threadId, previousId);
                    return;
                }
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
        args.replyMarkup = inlineMarkup;

        const Message::Ptr sent = bot_.getApi().sendMessage(args);
        if (sent) {
            rememberScreen(chatId, threadId, sent->messageId);
        }
    }

    void CommandsHandler::showScreen(const Message::Ptr &message, const string &screen, bool editCurrentMessage) {
        if (screen == "menu:queries") {
            queriesListScreen(message, editCurrentMessage);
            return;
        }

        string text = mainMenuText();
        if (screen == "menu:add") {
            text = addScreenText();
        } else if (screen == "menu:help") {
            text = helpScreenText();
        }

        const bool mainMenu = screen == "menu:main";

        setState(
            message->chat->id,
            message->messageThreadId.value_or(0),
            ChatState{mainMenu ? "main" : screen, 0, false}
        );

        renderScreen(
            message,
            text,
            mainMenu ? mainMenuKeyboard() : backKeyboard(),
            mainMenu ? mainReplyKeyboard() : backReplyKeyboard(),
            editCurrentMessage
        );
    }

    void CommandsHandler::queriesListScreen(const Message::Ptr &message, bool editCurrentMessage) {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();

        vector<vector<InlineKeyboardButton::Ptr>> inlineRows;
        vector<vector<KeyboardButton::Ptr>> replyRows;

        for (size_t i = 0; i < queries.size(); i++) {
            inlineRows.push_back({
                menuButton(fmt::format("{}. {}", i + 1, queryButtonLabel(queries[i])), "q:" + to_string(i))
            });
            replyRows.push_back({
                keyboardButton(fmt::format("{}. {}", i + 1, queryButtonLabel(queries[i])))
            });
        }
        inlineRows.push_back({menuButton(BUTTON_BACK, "menu:main")});
        replyRows.push_back({keyboardButton(BUTTON_BACK)});

        setState(
            message->chat->id,
            message->messageThreadId.value_or(0),
            ChatState{"list", 0, false}
        );

        renderScreen(
            message,
            queriesText(),
            menuMarkup(inlineRows),
            replyMarkup(replyRows),
            editCurrentMessage
        );
    }

    void CommandsHandler::queryScreen(const Message::Ptr &message, size_t index, bool editCurrentMessage) {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();
        if (index >= queries.size()) {
            queriesListScreen(message, editCurrentMessage);
            return;
        }

        const QueryConfiguration &query = queries[index];

        InlineKeyboardMarkup::Ptr inlineMarkup = menuMarkup({
            {menuButton(BUTTON_PRICE, "qprice:" + to_string(index))},
            {menuButton(query.enabled ? BUTTON_HIDE : BUTTON_SHOW, "qtoggle:" + to_string(index))},
            {menuButton(BUTTON_DELETE, "qdel:" + to_string(index))},
            {menuButton(BUTTON_BACK, "qlist")}
        });

        ReplyKeyboardMarkup::Ptr replyKeyboard = replyMarkup({
            {keyboardButton(BUTTON_PRICE)},
            {keyboardButton(query.enabled ? BUTTON_HIDE : BUTTON_SHOW)},
            {keyboardButton(BUTTON_DELETE)},
            {keyboardButton(BUTTON_BACK)}
        });

        setState(
            message->chat->id,
            message->messageThreadId.value_or(0),
            ChatState{"detail", index, false}
        );

        renderScreen(message, queryDetailText(index, query), inlineMarkup, replyKeyboard, editCurrentMessage);
    }

    void CommandsHandler::pricePromptScreen(const Message::Ptr &message, size_t index,
                                            bool editCurrentMessage, const string &error) {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();
        if (index >= queries.size()) {
            queriesListScreen(message, editCurrentMessage);
            return;
        }

        const string price = formatPriceRange(queries[index].kufar.priceRange);

        string text = fmt::format(
            "💰 <b>Цена поиска {}</b>\n\n"
            "<blockquote>Сейчас: {}</blockquote>\n\n"
            "Отправьте новую цену одним сообщением:\n"
            "• <code>100-500</code> — диапазон\n"
            "• <code>от 100</code> — минимум\n"
            "• <code>до 500</code> — максимум\n"
            "• <code>нет</code> — убрать ограничение",
            index + 1,
            price.empty() ? "не указана" : price
        );

        if (!error.empty()) {
            text += "\n\n❌ " + escapeHTML(error);
        }

        InlineKeyboardMarkup::Ptr inlineMarkup = menuMarkup({
            {menuButton(BUTTON_BACK, "q:" + to_string(index))}
        });

        ReplyKeyboardMarkup::Ptr replyKeyboard = replyMarkup({
            {keyboardButton(BUTTON_BACK)}
        });

        setState(
            message->chat->id,
            message->messageThreadId.value_or(0),
            ChatState{"price", index, true}
        );

        renderScreen(message, text, inlineMarkup, replyKeyboard, editCurrentMessage);
    }

    void CommandsHandler::deleteConfirmScreen(const Message::Ptr &message, size_t index, bool editCurrentMessage) {
        const vector<QueryConfiguration> queries = queryStore_.getQueries();
        if (index >= queries.size()) {
            queriesListScreen(message, editCurrentMessage);
            return;
        }

        const QueryConfiguration &query = queries[index];
        const string price = formatPriceRange(query.kufar.priceRange);

        string text = fmt::format(
            "🗑 <b>Удалить поиск {}</b>?\n\n"
            "<blockquote>🏷 {}\n💰 {}</blockquote>\n\n"
            "<i>Поиск исчезнет из конфигурации. "
            "История отправленных объявлений останется.</i>",
            index + 1,
            escapeHTML(queryButtonLabel(query)),
            price.empty() ? "цена не указана" : price
        );

        InlineKeyboardMarkup::Ptr inlineMarkup = menuMarkup({
            {menuButton(BUTTON_DELETE_CONFIRM, "qdelok:" + to_string(index))},
            {menuButton(BUTTON_CANCEL, "q:" + to_string(index))}
        });

        ReplyKeyboardMarkup::Ptr replyKeyboard = replyMarkup({
            {keyboardButton(BUTTON_DELETE_CONFIRM)},
            {keyboardButton(BUTTON_CANCEL)}
        });

        setState(
            message->chat->id,
            message->messageThreadId.value_or(0),
            ChatState{"confirm", index, false}
        );

        renderScreen(message, text, inlineMarkup, replyKeyboard, editCurrentMessage);
    }

    void CommandsHandler::toggleQuery(const Message::Ptr &message, size_t index) {
        try {
            json raw = queryStore_.getRawQuery(index);
            const bool disabled = raw.value("disabled", false);

            if (disabled) {
                raw.erase("disabled");
            } else {
                raw["disabled"] = true;
            }

            queryStore_.update(index, raw);
            queryScreen(message, index, false);
        } catch (const exception &exc) {
            spdlog::error("[Telegram] Ошибка переключения поиска: {}", exc.what());
            replyImpl(message, "Не удалось изменить поиск:\n" + string(exc.what()));
        }
    }

    void CommandsHandler::deleteQuery(const Message::Ptr &message, size_t index) {
        try {
            queryStore_.remove(index);
            spdlog::info("[Telegram] Поиск #{} удалён.", index + 1);
            queriesListScreen(message, false);
        } catch (const exception &exc) {
            spdlog::error("[Telegram] Ошибка удаления поиска: {}", exc.what());
            replyImpl(message, "Не удалось удалить поиск:\n" + string(exc.what()));
        }
    }

    void CommandsHandler::applyPrice(const Message::Ptr &message, size_t index, const string &text) {
        string error;
        const optional<PriceRange> range = parsePriceRangeText(text, error);

        if (!range.has_value()) {
            pricePromptScreen(message, index, false, error);
            return;
        }

        try {
            json raw = queryStore_.getRawQuery(index);
            raw.erase("price");

            if (range->priceMin.has_value() || range->priceMax.has_value()) {
                json price = json::object();
                if (range->priceMin.has_value()) {
                    price["min"] = range->priceMin.value();
                }
                if (range->priceMax.has_value()) {
                    price["max"] = range->priceMax.value();
                }
                raw["price"] = price;
            }

            queryStore_.update(index, raw);
            queryScreen(message, index, false);
        } catch (const exception &exc) {
            spdlog::error("[Telegram] Ошибка изменения цены: {}", exc.what());
            pricePromptScreen(message, index, false, exc.what());
        }
    }

    void CommandsHandler::goBack(const Message::Ptr &message) {
        const ChatState state = getState(
            message->chat->id,
            message->messageThreadId.value_or(0)
        );

        if (state.screen == "list") {
            showScreen(message, "menu:main", false);
        } else if (state.screen == "detail") {
            queriesListScreen(message, false);
        } else if (state.screen == "price" || state.screen == "confirm") {
            queryScreen(message, state.queryIndex, false);
        } else {
            showScreen(message, "menu:main", false);
        }
    }

    void CommandsHandler::rememberScreen(int64_t chatId, int32_t threadId, int32_t messageId) {
        lock_guard<mutex> lock(screensMutex_);
        screens_[stateKey(chatId, threadId)] = messageId;
    }

    int32_t CommandsHandler::findScreen(int64_t chatId, int32_t threadId) const {
        lock_guard<mutex> lock(screensMutex_);
        const auto screen = screens_.find(stateKey(chatId, threadId));
        return screen == screens_.end() ? 0 : screen->second;
    }

    CommandsHandler::ChatState CommandsHandler::getState(int64_t chatId, int32_t threadId) const {
        lock_guard<mutex> lock(statesMutex_);
        const auto state = states_.find(stateKey(chatId, threadId));
        return state == states_.end() ? ChatState() : state->second;
    }

    void CommandsHandler::setState(int64_t chatId, int32_t threadId, const ChatState &state) {
        lock_guard<mutex> lock(statesMutex_);
        states_[stateKey(chatId, threadId)] = state;
    }

    void CommandsHandler::deleteTriggerMessage(const Message::Ptr &message) {
        if (!isPrivateChat(message)) {
            return;
        }

        try {
            bot_.getApi().deleteMessage(message->chat->id, message->messageId);
        } catch (const exception &exc) {
            // Deleting the service message is a convenience, not a requirement.
            spdlog::debug("[Telegram] deleteMessage: {}", exc.what());
        }
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

        const string data = query->data.value();

        if (const optional<size_t> index = parseIndex(data, "q:"); index.has_value()) {
            queryScreen(*message, index.value(), true);
            return;
        }
        if (const optional<size_t> index = parseIndex(data, "qprice:"); index.has_value()) {
            pricePromptScreen(*message, index.value(), true);
            return;
        }
        if (const optional<size_t> index = parseIndex(data, "qtoggle:"); index.has_value()) {
            toggleQuery(*message, index.value());
            return;
        }
        if (const optional<size_t> index = parseIndex(data, "qdel:"); index.has_value()) {
            deleteConfirmScreen(*message, index.value(), true);
            return;
        }
        if (const optional<size_t> index = parseIndex(data, "qdelok:"); index.has_value()) {
            deleteQuery(*message, index.value());
            return;
        }
        if (data == "qlist") {
            queriesListScreen(*message, true);
            return;
        }

        showScreen(*message, data, true);
    }

    void CommandsHandler::handleHelp(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            reply(message, "Это приватный бот");
            return;
        }

        deleteTriggerMessage(message);
        showScreen(message, "menu:help", false);
    }

    void CommandsHandler::handleMenuText(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            return;
        }

        const string text = trim(message->text.value_or(""));
        const ChatState state = getState(
            message->chat->id,
            message->messageThreadId.value_or(0)
        );

        if (state.pendingPrice) {
            deleteTriggerMessage(message);
            applyPrice(message, state.queryIndex, text);
            return;
        }

        if (text == BUTTON_QUERIES) {
            deleteTriggerMessage(message);
            queriesListScreen(message, false);
        } else if (text == BUTTON_ADD) {
            deleteTriggerMessage(message);
            showScreen(message, "menu:add", false);
        } else if (text == BUTTON_HELP) {
            deleteTriggerMessage(message);
            showScreen(message, "menu:help", false);
        } else if (text == BUTTON_BACK || text == BUTTON_CANCEL) {
            deleteTriggerMessage(message);
            goBack(message);
        } else if (const optional<size_t> index = parseQueryButtonText(text); index.has_value()) {
            deleteTriggerMessage(message);
            queryScreen(message, index.value(), false);
        } else if (state.screen == "detail" && text == BUTTON_PRICE) {
            deleteTriggerMessage(message);
            pricePromptScreen(message, state.queryIndex, false);
        } else if (state.screen == "detail" && (text == BUTTON_HIDE || text == BUTTON_SHOW)) {
            deleteTriggerMessage(message);
            toggleQuery(message, state.queryIndex);
        } else if (state.screen == "detail" && text == BUTTON_DELETE) {
            deleteTriggerMessage(message);
            deleteConfirmScreen(message, state.queryIndex, false);
        } else if (state.screen == "confirm" && text == BUTTON_DELETE_CONFIRM) {
            deleteTriggerMessage(message);
            deleteQuery(message, state.queryIndex);
        }
    }

    void CommandsHandler::handleStart(Message::Ptr message) {
        if (message->chat->id != config_.chatID) {
            reply(message, "Это приватный бот");
            return;
        }

        deleteTriggerMessage(message);
        showScreen(message, "menu:main", false);
    }
}
