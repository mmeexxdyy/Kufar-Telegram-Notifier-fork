**English** | [Русский](README.ru.md)

# Kufar Telegram Notifier

A small self-hosted bot that watches [Kufar](https://kufar.by) for new listings and posts them to a Telegram chat. You describe your searches in a JSON file; the bot polls Kufar at a configurable interval and sends every listing it has not seen before. Each search can target its own thread (topic) in a Telegram supergroup, so one bot can run several independent feeds.

> **Disclaimer:** This project is intended for informational and educational purposes only. It is not affiliated with or endorsed by Kufar. Use it at your own risk and in compliance with Kufar's terms of service and applicable law. If you represent Kufar and have concerns about this project, please open an issue in this repository.

## How it works

1. The bot reads `kufar-configuration.json` and runs each entry in `queries` against the Kufar search API. Queries can also be added and removed at runtime with Telegram commands; changes are written back to the configuration file.
2. New listings are sent to the Telegram chat. If a query has `message-thread-id`, its listings go to that topic; otherwise they go to the chat's General topic.
3. Sent listing IDs are stored in `cached-data.json`, separately for every topic, so the same listing is not posted twice.
4. After all queries have run, the bot waits for the configured loop delay and starts again. It runs in the foreground until stopped.
5. In parallel, the bot receives Telegram updates through long polling. No public IP or webhook is required; it answers `/start`, `/help`, `/add`, `/list`, and `/del`, and serves the inline-button menu.

Telegram messages produced by the bot are currently written in Russian.

## Requirements

- A C++20 compiler (`g++` or `clang++`)
- CMake 3.16 or newer
- libcurl development headers
- spdlog development headers
- Boost headers
- nlohmann-json development headers
- Git and network access for the first configure (tgbot-cpp is downloaded automatically)

## Building on Linux

Install the dependencies with your distribution's package manager.

Debian / Ubuntu:

```sh
sudo apt update
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libspdlog-dev libboost-dev nlohmann-json3-dev
```

Fedora:

```sh
sudo dnf install -y gcc-c++ make cmake git libcurl-devel spdlog-devel boost-devel json-devel
```

Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake git curl spdlog boost nlohmann-json
```

Then clone and build:

```sh
git clone https://github.com/Am80286/Kufar-Telegram-Notifier-fork.git
cd Kufar-Telegram-Notifier-fork
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is written to `build/bin/Kufar-Telegram-Notifier`.

The first configure downloads tgbot-cpp v1.13 into `build/_deps`, so it does not need to be installed system-wide. Later builds reuse the downloaded copy and can run offline. If the source is already available locally, pass `-DFETCHCONTENT_SOURCE_DIR_TGBOT_CPP=/path/to/tgbot-cpp` to the configure command.

## Building on macOS

Only the command-line toolchain is needed; there is no Xcode project and no IDE step.

```sh
xcode-select --install
brew install cmake boost nlohmann-json curl spdlog
```

If Homebrew is not installed yet, follow the instructions at [brew.sh](https://brew.sh).

Then clone and build:

```sh
git clone https://github.com/Am80286/Kufar-Telegram-Notifier-fork.git
cd Kufar-Telegram-Notifier-fork
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is written to `build/bin/Kufar-Telegram-Notifier`.

If CMake cannot find Homebrew's Boost, libcurl, nlohmann-json, or spdlog, pass the Homebrew prefix explicitly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build --parallel
```

## Building on Windows

Install Visual Studio 2022 with the "Desktop development with C++" workload and CMake 3.30 or newer.

The dependencies are easiest to get with [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install curl spdlog nlohmann-json boost-asio
```

Then clone and build:

```powershell
git clone https://github.com/Am80286/Kufar-Telegram-Notifier-fork.git
cd Kufar-Telegram-Notifier-fork
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

The executable is written to `build\bin\Release\Kufar-Telegram-Notifier.exe`.

To keep the bot running after signing out, use a process manager such as [NSSM](https://nssm.cc/) or a Task Scheduler task that starts the executable at logon.

## Running

The build produces a single executable and does not install anything system-wide. Run it from a directory the user can write to, because the cache and log files are created in the current working directory by default.

```sh
./build/bin/Kufar-Telegram-Notifier
```

The program keeps running in the foreground. Stop it with `Ctrl+C`. To keep it running after the terminal closes, use a process manager such as systemd, tmux, or screen.

### Command-line options

| Option | Description |
| --- | --- |
| `-c`, `--config <file>` | Path to the configuration file. Default: `kufar-configuration.json` in the current directory. |
| `--cache <file>` | Path to the cache file. Default: `cached-data.json` in the current directory. Created automatically if missing. |
| `-l`, `--log <file>` | Path to the log file. Default: `kufar-notifier.log` in the current directory. |
| `--no-log` | Do not write a log file; log to the console only. |
| `--log-level <level>` | Log level: `trace`, `debug`, `info`, `warn`, `error`, or `critical`. Default: `info`. |
| `-V`, `--version` | Print the program version and exit. |
| `-h`, `--help` | Print usage information and exit. |

Examples:

```sh
# Use the configuration, cache, and log files in the current directory
./build/bin/Kufar-Telegram-Notifier

# Use files in another location
./build/bin/Kufar-Telegram-Notifier \
  --config /etc/kufar-notifier/kufar-configuration.json \
  --cache /var/lib/kufar-notifier/cached-data.json \
  --log /var/log/kufar-notifier.log

# Log to the console only, with debug output
./build/bin/Kufar-Telegram-Notifier --no-log --log-level debug
```

### Telegram commands

The bot uses long polling (`getUpdates`), so it can receive commands without a public IP address or a webhook. Commands are answered in the chat and topic where they were sent. `/add` and `/del` are ignored outside the chat configured in `chat-id`.

| Command | Description |
| --- | --- |
| `/start` | Open the bot menu. |
| `/help` | Show the command reference. |
| `/add <URL> [title]` | Add a new search from a Kufar search-results URL. The optional title is used in listing cards and can be changed later; without it the bot takes the Kufar category name. |
| `/list` | List the searches assigned to the current topic, each with a number. |
| `/del <N>` | Delete search number `N` from the current topic. |

### Chat interface

In private chats `/start` installs a persistent bottom keyboard: **Мои поиски**, **Добавить поиск** and **Помощь** on the main screen, and only **Назад** inside a screen. The bot keeps a single menu message: when you switch screens it sends the new one and deletes the previous one. The bot also publishes its commands, so Telegram shows the **Menu** button.

In groups the same screens are inline buttons under the menu message: a bottom keyboard there would post messages on behalf of the user.

Opening **Мои поиски** lists the searches with a button per search. A search card can change its title and price, hide or show the feed, and delete it with confirmation. A hidden search stays in the configuration with `"disabled": true` and is not polled.

Every listing is sent as a formatted card: hashtag, title, price, location, date, description, seller with a profile link, and the seller ID. A listing with several photos arrives as one album whose caption contains the card and a clickable link to Kufar; single-photo and text listings carry buttons for the listing and the seller profile. The description and seller details cost one extra request per new listing; cached listings are not fetched again.

#### `/add`

Send a link to a Kufar search-results page, for example:

```text
/add https://www.kufar.by/l?query=iphone
```

The bot downloads the page, reads the search parameters Kufar resolved for it, converts them to the same fields used in the configuration file, assigns an ID, and appends the query to `kufar-configuration.json`. The page is fetched in the background, so the bot keeps polling while the import runs. The new search is picked up on the next loop; no restart is needed. If the command is sent inside a topic, the query is bound to that topic; in the General topic it is left without a thread ID.

Only `https` links to search pages on `www.kufar.by`, `auto.kufar.by`, and `re.kufar.by` are accepted; the path must be `/l` or start with `/l/`. Links to individual listings or accounts are rejected. Search filters that cannot be represented in the query format cause an error instead of being silently dropped. The original link is stored in the query's `source-url` field.

#### `/list` and `/del`

`/list` shows only the searches assigned to the topic where the command was sent, numbered from `1`. The numbering is local to the topic and can change after a search is deleted. `/del N` removes the search with that number from the current topic and rewrites the configuration file. Deleting a search does not clear the cache; see [Cache file](#cache-file).

Both commands rewrite `kufar-configuration.json`, so the bot needs write access to that file.

## Configuration

The bot reads a JSON file, `kufar-configuration.json` by default. A complete example lives in the repository root. The file has three top-level sections: `telegram`, `queries`, and `delays`.

Minimal example:

```json
{
    "telegram": {
        "bot-token": "1111111111:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "chat-id": -1001234567890
    },
    "queries": [
        {
            "tag": "iPhone",
            "message-thread-id": 42,
            "only-title-search": true,
            "limit": 10
        },
        {
            "tag": "велосипед",
            "message-thread-id": 43,
            "price": {
                "min": 100,
                "max": 800
            },
            "region": 7
        }
    ],
    "delays": {
        "query": 10,
        "loop": 1800
    }
}
```

### Telegram settings

Before configuring the file, create a bot with [@BotFather](https://t.me/BotFather) and add it to the target chat.

| Key | Required | Description |
| --- | --- | --- |
| `bot-token` | Yes | The token issued by BotFather. |
| `chat-id` | Yes | ID of the chat that receives the listings. Group and supergroup IDs are negative, for example `-1001234567890`. |

To send into separate topics, the chat must be a supergroup with **Topics** (also called Forum mode) enabled, and the bot must have permission to post messages there. Add the bot to the supergroup and grant it the necessary admin rights.

You can obtain the chat ID from the Telegram Bot API (`getUpdates`) or with any chat-ID inspection tool. The value must include the minus sign for groups.

### Query settings

Every object in `queries` describes one search feed. All query fields are optional, but a query with no filters at all will simply return Kufar's default results.

| Key | Type | Description |
| --- | --- | --- |
| `id` | integer | Internal query ID. Assigned automatically and managed by the bot; do not edit it by hand. |
| `tag` | string | Search text. Also used as the `#hashtag` at the top of the Telegram message. |
| `message-thread-id` | integer | Topic ID in the supergroup where this query's listings are posted. Must be positive. If omitted, listings go to the General topic. |
| `only-title-search` | boolean | Search only in listing titles. |
| `price` | object | Price range with optional `min` and `max` values, in whole BYN. |
| `price.min` | integer | Minimum price. |
| `price.max` | integer | Maximum price. |
| `language` | string | Search language code, for example `ru`. |
| `limit` | integer | Maximum number of listings requested per poll. Kufar's default is used when omitted. |
| `currency` | string | Currency code passed to the Kufar API. The bundled example uses `BYR`. Notifications always display the price in BYN. |
| `condition` | integer | Item condition: `1` = used, `2` = new. |
| `seller-type` | integer | Seller type: `0` = private person, `1` = company. |
| `kufar-delivery-required` | boolean | Only listings with Kufar Delivery. |
| `kufar-payment-required` | boolean | Only listings with Kufar Payment. |
| `kufar-halva-required` | boolean | Only listings with Kufar Halva installment. |
| `only-with-photos` | boolean | Only listings with photos. |
| `only-with-videos` | boolean | Only listings with videos. |
| `only-with-exchange-available` | boolean | Only listings that allow exchange. |
| `sort-type` | integer | Price sorting: `1` = descending, `2` = ascending. |
| `category` | integer | Main category ID. See the category table below. |
| `sub-category` | integer | Subcategory ID. See `include/kufar.hpp` for the full list. |
| `computer-component-type` | integer | Computer component type (`cct`) used with subcategory `16010`. See the component type table below. |
| `region` | integer | Region ID. See the region table below. |
| `areas` | array of integers | Area (district/city) IDs to filter by. See `include/kufar.hpp` for the full list. |
| `source-url` | string | Kufar search URL the query was imported from with `/add`. Informational only. |
| `disabled` | boolean | Keep the query in the configuration but stop polling it. Searches can be paused from the bot menu. |

Boolean filters are sent to Kufar only when set to `true`. Setting them to `false` is the same as omitting them.

When the bot starts, it assigns an `id` to every query that does not have one and rewrites the configuration file. The file is also rewritten when a query is added or deleted with `/add` or `/del`.

#### Categories

| ID | Category | ID | Category |
| --- | --- | --- | --- |
| `1000` | Real estate | `12000` | Everything for children and mothers |
| `2000` | Cars and transport | `13000` | Services |
| `3000` | Everything for home | `14000` | Repair and construction |
| `4000` | Hobbies, sports and tourism | `15000` | Household appliances |
| `5000` | Electronics | `16000` | Computer equipment |
| `6000` | Jobs | `17000` | Phones and tablets |
| `7000` | Other | `18000` | Beauty and health |
| `8000` | Women's wardrobe | `19000` | Men's wardrobe |
| `9000` | Wedding and holidays | `20000` | Ready business and equipment |
| `10000` | Garden | `21000` | Furniture |
| `11000` | Animals | | |

Some commonly used subcategories:

| ID | Subcategory |
| --- | --- |
| `17010` | Mobile phones |
| `16040` | Laptops |
| `2010` | Passenger cars |
| `1010` | Apartments |

The complete list of subcategories and area IDs is defined in [`include/kufar.hpp`](include/kufar.hpp).

#### Computer component types

Use `computer-component-type` with `"sub-category": 16010` (Components). The main `"category": 16000` (Computer equipment) is optional. For example, graphics cards use `"computer-component-type": 11`.

| ID | Component type |
| --- | --- |
| `1` | Cases |
| `3` | Coolers |
| `5` | Power supplies and batteries |
| `7` | Motherboards |
| `8` | Laptops for parts |
| `9` | RAM |
| `10` | Processors |
| `11` | Graphics cards |
| `13` | Hard drives |
| `15` | SSDs |
| `17` | Sound cards |
| `19` | Optical drives |
| `21` | Other computer components |

#### Regions

| ID | Region |
| --- | --- |
| `1` | Brest region |
| `2` | Gomel region |
| `3` | Grodno region |
| `4` | Mogilev region |
| `5` | Minsk region |
| `6` | Vitebsk region |
| `7` | Minsk city |

`region` and `areas` can be used together. For example, `"region": 7` with `"areas": [22, 23, 24]` limits the search to Minsk and those districts.

### Topics and message threads

Each query can post to a different topic. To find a topic's ID, open the topic in Telegram and look at the link of any message inside it:

```text
https://t.me/c/1234567890/42/123
```

In this example, `1234567890` is the chat ID without the `-100` prefix and `42` is the topic ID, so the configuration value is `"message-thread-id": 42`.

The cache is kept per topic, not per query. If two queries post to the same topic, a listing is sent only once to that topic. The same listing may still be sent to a different topic. If you delete a topic and create a new one, it receives a new ID and starts with an empty history.

### Delays

| Key | Type | Description |
| --- | --- | --- |
| `delays.query` | integer | Seconds to wait after each query before starting the next one. |
| `delays.loop` | integer | Seconds to wait after the last query before starting the loop again. |

If the whole `delays` section is omitted, the defaults are `5` seconds between queries and `30` seconds between loops. If the section is present, both `query` and `loop` must be set.

## Cache file

`cached-data.json` stores the IDs of listings that were already sent, grouped by topic. It is created automatically and should not be edited while the bot is running. The structure is:

```json
{
    "version": 2,
    "buckets": {
        "default": [123456789],
        "42": [123456789, 987654321]
    }
}
```

- `default` contains listings sent to the General topic (queries without `message-thread-id`).
- `"42"` contains listings sent to the topic with ID 42.

A listing is added to the cache only after Telegram confirms the send. If sending fails, the bot logs the error and retries the listing on the next poll.

To make the bot forget what it has sent, stop it, delete `cached-data.json`, and start it again. A new empty cache will be created automatically. Older cache formats are not migrated; if an old flat-array cache is found, it is ignored with a warning and tracking starts over.

Deleting a query with `/del` does not remove anything from the cache. If you add the same search back to the same topic, listings that were already sent there will not be sent again until the cache is cleared.

The configuration and cache files are written atomically: the bot writes a temporary `.tmp` file and renames it over the destination. If the process is interrupted while saving, the previous valid file is kept.

## Logging

By default the bot writes to `kufar-notifier.log` in the current working directory and also prints to the console. Use `--no-log` to disable the file, `--log <file>` to choose another path, and `--log-level <level>` to change verbosity.

## Credits

This project is a fork of [TechUnRestricted/Kufar-Telegram-Notifier](https://github.com/TechUnRestricted/Kufar-Telegram-Notifier). This fork adds per-query Telegram topics, a sent-listing cache split by topic, command-line argument parsing with CLI11, structured logging with spdlog, Telegram Bot API integration through [tgbot-cpp](https://github.com/reo7sp/tgbot-cpp) with long polling, runtime query management with the `/add`, `/list`, and `/del` commands, and atomic configuration and cache writes.

## License

This project is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for the full text.
