//
//  helperfunctions.cpp
//  Kufar Telegram Notifier
//
//  Created by Macintosh on 04.06.2022.
//

#include <stdexcept>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <cstdint>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "helperfunctions.hpp"
#include "kufar.hpp"

using namespace std;
using namespace Kufar;

bool vectorContains(const vector<int> &vector, const int &value) {
    if (find(vector.begin(), vector.end(), value) != vector.end()) {
        return true;
    }
    return false;
}

bool fileExists(const string &path) {
    ifstream f(path);
    return f.good();
}

uint64_t getFileSize(const string &path) {
    ifstream f(path, ios::binary | ios::ate);
    return f.tellg();
}

string getTextFromFile(const string &path) {
    ifstream ifs(path);
    return string((istreambuf_iterator<char>(ifs)),
                  (istreambuf_iterator<char>()));
}

time_t zuluToTimestamp(const string &zuluDate) {
    tm t{};
    istringstream stringStream(zuluDate);
    
    stringStream >> get_time(&t, "%Y-%m-%dT%H:%M:%S");
    if (stringStream.fail()) {
        throw runtime_error{"failed to parse time string"};
    }
    
    return mktime(&t);
}

string joinIntVector(const vector<int> &nums, const string &delim) {
    stringstream result;
    copy(nums.begin(), nums.end(), std::ostream_iterator<int>(result, delim.c_str()));
    
    string temp = result.str();
    
    if (!temp.empty()) {
        temp.pop_back();
    }
    
    return temp;
}

time_t timestampShift(const time_t &timestamp, int shift) {
    return timestamp + (3600 * shift);
}

bool stringHasPrefix(const string &originalString, const string &prefix) {
    return originalString.rfind(prefix, 0) == 0;
}

void saveFile(const string &path, const string &contents) {
    string tmpPath = path + ".tmp";

    ofstream ofs(tmpPath, ofstream::trunc);
    if (!ofs.is_open())
        throw runtime_error("Couldn't create a temporary file for writing");

    ofs << contents;
    if (!ofs)
        throw runtime_error("Couldn't write file during saving"); 
    
    ofs.close();
    if (!ofs)
        throw runtime_error("Couldn't flush/close file during saving");

#ifdef _WIN32
    // std::rename does not overwrite an existing file on Windows.
    std::remove(path.c_str());
#endif

    if (std::rename(tmpPath.c_str(), path.c_str()))
    {
        std::remove(tmpPath.c_str());
        throw runtime_error("Couldn't rename the temporary file during saving");
    }
}

string escapeHTML(const string &text) {
    string escaped;
    escaped.reserve(text.size());

    for (const char character : text) {
        switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            default:
                escaped += character;
                break;
        }
    }

    return escaped;
}

tm localTime(const time_t &value) {
    tm result{};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

void sleepSeconds(unsigned int seconds) {
#ifdef _WIN32
    Sleep(seconds * 1000);
#else
    sleep(seconds);
#endif
}

void sleepMilliseconds(unsigned int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

string maskToken(const string &token) {
    if (token.empty()) {
        return PROPERTY_UNDEFINED;
    }

    const size_t separator = token.find(':');
    if (separator == string::npos) {
        // Unknown format: keep only the last 4 characters.
        if (token.size() <= 4) {
            return string(token.size(), '*');
        }
        return "***" + token.substr(token.size() - 4);
    }

    const string botID = token.substr(0, separator);
    const string secret = token.substr(separator + 1);
    const string tail = secret.size() > 4 ? secret.substr(secret.size() - 4) : secret;

    return botID + ":***" + tail;
}
