#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#endif

namespace {

struct Update {
    long long update_id = 0;
    long long chat_id = 0;
    std::string text;
    std::string web_app_data;
};

struct Config {
    std::unordered_set<long long> allowed_chat_ids;
    std::map<std::string, std::string> commands;
    int screenshot_width = 1280;
    int screenshot_height = 720;
    bool screenshot_compression = true;
    int screenshot_quality = 85;
    std::string screenshot_format = "jpg";
    std::string webapp_url;
    std::string debug_log_path;
};

constexpr std::size_t kMaxMessageLength = 3500;
constexpr const char* kDefaultLogDirectory = "C:\\Users\\Gleb\\pc\\logs";

std::ofstream g_log_stream;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::size_t total_size = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<char*>(contents), total_size);
    return total_size;
}

std::string httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PCController/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw std::runtime_error("curl error: " + error);
    }

    curl_easy_cleanup(curl);
    return response;
}

void httpPost(const std::string& url, const std::string& data) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PCController/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw std::runtime_error("curl error: " + error);
    }

    curl_easy_cleanup(curl);
}

std::string trim(const std::string& input) {
    const auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(start, end - start + 1);
}

std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void logDebug(const Config& config, const std::string& message) {
    if (config.debug_log_path.empty()) {
        return;
    }
    if (g_log_stream.is_open()) {
        g_log_stream << "[" << currentTimestamp() << "] " << message << "\n";
        g_log_stream.flush();
        return;
    }
    std::ofstream log_file(config.debug_log_path, std::ios::app);
    if (!log_file) {
        return;
    }
    log_file << "[" << currentTimestamp() << "] " << message << "\n";
}

void initLogOutput(const Config& config) {
    if (config.debug_log_path.empty()) {
        return;
    }
    if (g_log_stream.is_open()) {
        return;
    }
    g_log_stream.open(config.debug_log_path, std::ios::app);
    if (!g_log_stream.is_open()) {
        return;
    }
    std::cout.rdbuf(g_log_stream.rdbuf());
    std::cerr.rdbuf(g_log_stream.rdbuf());
}

std::string resolveLogPath(const std::string& configured_path) {
    std::filesystem::path log_dir(kDefaultLogDirectory);
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);

    std::filesystem::path filename;
    if (!configured_path.empty()) {
        std::filesystem::path provided(configured_path);
        if (provided.has_filename() && provided.filename() != "." && provided.filename() != "..") {
            filename = provided.filename();
        }
    }

    if (filename.empty()) {
        filename = "pccontroller.log";
    }

    return (log_dir / filename).string();
}

std::filesystem::path getExecutableDir(const char* argv0) {
#ifdef _WIN32
    char path_buffer[MAX_PATH] = {0};
    DWORD size = GetModuleFileNameA(nullptr, path_buffer, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        return std::filesystem::path(path_buffer).parent_path();
    }
#endif
    if (argv0 && *argv0) {
        std::filesystem::path path(argv0);
        if (path.has_parent_path()) {
            return path.parent_path();
        }
    }
    return std::filesystem::current_path();
}

Config loadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Config file not found: " + path);
    }

    Config config;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        auto delimiter_pos = line.find('=');
        if (delimiter_pos == std::string::npos) {
            continue;
        }
        std::string key = trim(line.substr(0, delimiter_pos));
        std::string value = trim(line.substr(delimiter_pos + 1));

        if (key == "allowed_chat_ids") {
            std::stringstream ids(value);
            std::string id;
            while (std::getline(ids, id, ',')) {
                id = trim(id);
                if (!id.empty()) {
                    config.allowed_chat_ids.insert(std::stoll(id));
                }
            }
        } else if (key.rfind("command.", 0) == 0) {
            std::string name = key.substr(std::strlen("command."));
            if (!name.empty() && !value.empty()) {
                config.commands[name] = value;
            }
        } else if (key == "screenshot.width") {
            config.screenshot_width = std::stoi(value);
        } else if (key == "screenshot.height") {
            config.screenshot_height = std::stoi(value);
        } else if (key == "screenshot.compression") {
            config.screenshot_compression = (value == "1" || value == "true" || value == "yes");
        } else if (key == "screenshot.quality") {
            config.screenshot_quality = std::stoi(value);
        } else if (key == "screenshot.format") {
            config.screenshot_format = value;
        } else if (key == "webapp.url") {
            config.webapp_url = value;
        } else if (key == "debug.log_path") {
            config.debug_log_path = value;
        }
    }

    config.debug_log_path = resolveLogPath(config.debug_log_path);
    return config;
}

bool tryLoadConfig(const std::string& path, Config& config, std::string& error) {
    try {
        config = loadConfig(path);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

void appendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool decodeUnicodeEscape(const std::string& input, std::size_t& index, std::string& output) {
    if (index + 5 >= input.size() || input[index] != '\\' || input[index + 1] != 'u') {
        return false;
    }
    auto hexToInt = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    std::uint32_t codepoint = 0;
    for (int i = 0; i < 4; ++i) {
        int value = hexToInt(input[index + 2 + i]);
        if (value < 0) {
            return false;
        }
        codepoint = (codepoint << 4) | static_cast<std::uint32_t>(value);
    }
    index += 5;

    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
        if (index + 6 < input.size() && input[index + 1] == '\\' && input[index + 2] == 'u') {
            std::uint32_t low = 0;
            for (int i = 0; i < 4; ++i) {
                int value = hexToInt(input[index + 3 + i]);
                if (value < 0) {
                    return false;
                }
                low = (low << 4) | static_cast<std::uint32_t>(value);
            }
            if (low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                index += 6;
            }
        }
    }

    appendUtf8(output, codepoint);
    return true;
}

bool parseJsonString(const std::string& input, std::size_t start_pos, std::string& output) {
    if (start_pos >= input.size() || input[start_pos] != '"') {
        return false;
    }

    std::string result;
    bool escape = false;
    for (std::size_t i = start_pos + 1; i < input.size(); ++i) {
        char c = input[i];
        if (escape) {
            switch (c) {
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case '\\':
                case '"':
                    result.push_back(c);
                    break;
                case 'u': {
                    std::size_t unicode_index = i - 1;
                    if (decodeUnicodeEscape(input, unicode_index, result)) {
                        i = unicode_index;
                    } else {
                        result.push_back('u');
                    }
                    break;
                }
                default:
                    result.push_back(c);
                    break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            output = result;
            return true;
        } else {
            result.push_back(c);
        }
    }

    return false;
}

bool findNumberAfterKey(const std::string& input, const std::string& key, std::size_t start_pos, long long& value) {
    auto key_pos = input.find(key, start_pos);
    if (key_pos == std::string::npos) {
        return false;
    }
    auto colon_pos = input.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    auto number_start = input.find_first_of("-0123456789", colon_pos + 1);
    if (number_start == std::string::npos) {
        return false;
    }
    auto number_end = input.find_first_not_of("-0123456789", number_start);
    std::string number_str = input.substr(number_start, number_end - number_start);
    value = std::stoll(number_str);
    return true;
}

bool findStringAfterKey(const std::string& input, const std::string& key, std::size_t start_pos, std::string& value) {
    auto key_pos = input.find(key, start_pos);
    if (key_pos == std::string::npos) {
        return false;
    }
    auto colon_pos = input.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    auto quote_pos = input.find('"', colon_pos + 1);
    if (quote_pos == std::string::npos) {
        return false;
    }
    return parseJsonString(input, quote_pos, value);
}

bool findRawValueAfterKey(const std::string& input, const std::string& key, std::size_t start_pos, std::string& value) {
    auto key_pos = input.find(key, start_pos);
    if (key_pos == std::string::npos) {
        return false;
    }
    auto colon_pos = input.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return false;
    }
    auto value_start = input.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (value_start == std::string::npos) {
        return false;
    }
    auto value_end = input.find_first_of(",}", value_start);
    if (value_end == std::string::npos) {
        value_end = input.size();
    }
    value = trim(input.substr(value_start, value_end - value_start));
    return !value.empty();
}

std::string urlDecode(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '%' && i + 2 < input.size()) {
            auto hexToInt = [](char ch) -> int {
                if (ch >= '0' && ch <= '9') return ch - '0';
                if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
                if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
                return -1;
            };
            int hi = hexToInt(input[i + 1]);
            int lo = hexToInt(input[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
            } else {
                result.push_back(c);
            }
        } else if (c == '+') {
            result.push_back(' ');
        } else {
            result.push_back(c);
        }
    }
    return result;
}

bool parseQueryString(const std::string& input, std::map<std::string, std::string>& params) {
    if (input.find('=') == std::string::npos) {
        return false;
    }
    std::stringstream ss(input);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto eq_pos = pair.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }
        std::string key = urlDecode(pair.substr(0, eq_pos));
        std::string value = urlDecode(pair.substr(eq_pos + 1));
        if (!key.empty()) {
            params[key] = value;
        }
    }
    return !params.empty();
}

bool parseQueryStringWithDecode(const std::string& input, std::map<std::string, std::string>& params) {
    if (parseQueryString(input, params)) {
        return true;
    }
    std::string decoded = urlDecode(input);
    if (decoded != input) {
        params.clear();
        return parseQueryString(decoded, params);
    }
    return false;
}

bool findWebAppData(const std::string& input, std::size_t start_pos, std::string& value) {
    auto web_app_pos = input.find("\"web_app_data\"", start_pos);
    if (web_app_pos == std::string::npos) {
        return false;
    }
    return findStringAfterKey(input, "\"data\"", web_app_pos, value);
}

bool parseWebAppSettings(const std::string& data, bool& compression, int& width, int& height, int& quality) {
    if (data.find("\"action\"") == std::string::npos) {
        return false;
    }
    std::string action;
    if (!findStringAfterKey(data, "\"action\"", 0, action) || action != "settings") {
        return false;
    }
    std::string compression_value;
    std::string width_value;
    std::string height_value;
    std::string quality_value;
    if (!findRawValueAfterKey(data, "\"compression\"", 0, compression_value) ||
        !findRawValueAfterKey(data, "\"width\"", 0, width_value) ||
        !findRawValueAfterKey(data, "\"height\"", 0, height_value) ||
        !findRawValueAfterKey(data, "\"quality\"", 0, quality_value)) {
        return false;
    }

    compression = (compression_value == "true" || compression_value == "1");
    width = std::stoi(width_value);
    height = std::stoi(height_value);
    quality = std::stoi(quality_value);
    return true;
}

bool parseWebAppScreenshotSettings(const std::string& data, bool& compression, int& width, int& height, int& quality) {
    std::string action;
    if (!findStringAfterKey(data, "\"action\"", 0, action) || action != "screenshot") {
        return false;
    }
    std::string compression_value;
    std::string width_value;
    std::string height_value;
    std::string quality_value;
    if (!findRawValueAfterKey(data, "\"compression\"", 0, compression_value) ||
        !findRawValueAfterKey(data, "\"width\"", 0, width_value) ||
        !findRawValueAfterKey(data, "\"height\"", 0, height_value) ||
        !findRawValueAfterKey(data, "\"quality\"", 0, quality_value)) {
        return false;
    }
    compression = (compression_value == "true" || compression_value == "1");
    width = std::stoi(width_value);
    height = std::stoi(height_value);
    quality = std::stoi(quality_value);
    return true;
}

bool parseWebAppAction(const std::string& data, std::string& action) {
    if (findStringAfterKey(data, "\"action\"", 0, action)) {
        return true;
    }
    std::map<std::string, std::string> params;
    if (parseQueryStringWithDecode(data, params)) {
        auto it = params.find("action");
        if (it != params.end()) {
            action = it->second;
            return true;
        }
    }
    return false;
}

std::vector<Update> parseUpdates(const std::string& input) {
    std::vector<Update> updates;
    std::size_t pos = 0;

    while (true) {
        auto update_pos = input.find("\"update_id\"", pos);
        if (update_pos == std::string::npos) {
            break;
        }

        Update update;
        if (!findNumberAfterKey(input, "\"update_id\"", update_pos, update.update_id)) {
            pos = update_pos + 1;
            continue;
        }

        auto message_pos = input.find("\"message\"", update_pos);
        if (message_pos == std::string::npos) {
            pos = update_pos + 1;
            continue;
        }

        auto chat_pos = input.find("\"chat\"", message_pos);
        if (chat_pos == std::string::npos) {
            pos = message_pos + 1;
            continue;
        }

        if (!findNumberAfterKey(input, "\"id\"", chat_pos, update.chat_id)) {
            pos = chat_pos + 1;
            continue;
        }

        std::string text;
        bool has_text = findStringAfterKey(input, "\"text\"", message_pos, text);

        std::string web_app_data;
        if (findWebAppData(input, message_pos, web_app_data)) {
            update.web_app_data = web_app_data;
        }

        if (!has_text && update.web_app_data.empty()) {
            pos = message_pos + 1;
            continue;
        }

        update.text = text;
        updates.push_back(update);
        pos = message_pos + 1;
    }

    return updates;
}

std::string escapeForUrl(CURL* curl, const std::string& value) {
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    if (!encoded) {
        return "";
    }
    std::string result(encoded);
    curl_free(encoded);
    return result;
}

std::string runCommand(const std::string& command) {
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        return "Failed to execute command.";
    }

    std::array<char, 256> buffer;
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result.append(buffer.data());
        if (result.size() > kMaxMessageLength) {
            result.resize(kMaxMessageLength);
            result.append("...\n");
            break;
        }
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return trim(result.empty() ? "(no output)" : result);
}

std::string buildScreenshotPath(const std::string& format) {
    std::ostringstream path;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
#ifdef _WIN32
    char temp_path[MAX_PATH] = {0};
    DWORD length = GetTempPathA(MAX_PATH, temp_path);
    if (length == 0 || length > MAX_PATH) {
        path << "pccontroller_";
    } else {
        path << temp_path << "pccontroller_";
    }
#else
    path << "/tmp/pccontroller_";
#endif
    path << now << "." << format;
    return path.str();
}

bool captureScreenshot(const Config& config, std::string& path, std::string& error) {
    std::string format = config.screenshot_compression ? "jpg" : "png";
    if (!config.screenshot_format.empty()) {
        format = config.screenshot_format;
    }
    path = buildScreenshotPath(format);

#if defined(_WIN32)
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP screen_bitmap = CreateCompatibleBitmap(screen_dc, screen_width, screen_height);
    if (!screen_bitmap) {
        error = "Failed to capture screenshot.";
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return false;
    }

    HGDIOBJ old_bitmap = SelectObject(memory_dc, screen_bitmap);
    BOOL blt_ok = BitBlt(memory_dc, 0, 0, screen_width, screen_height, screen_dc, 0, 0, SRCCOPY | CAPTUREBLT);
    SelectObject(memory_dc, old_bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);

    if (!blt_ok) {
        error = "Failed to capture screenshot.";
        DeleteObject(screen_bitmap);
        return false;
    }

    int target_width = config.screenshot_width > 0 ? config.screenshot_width : screen_width;
    int target_height = config.screenshot_height > 0 ? config.screenshot_height : screen_height;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool should_uninit = (hr == S_OK || hr == S_FALSE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        DeleteObject(screen_bitmap);
        error = "Failed to initialize COM.";
        return false;
    }

    IWICImagingFactory* factory = nullptr;
    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        if (should_uninit) {
            CoUninitialize();
        }
        DeleteObject(screen_bitmap);
        error = "Failed to create WIC factory.";
        return false;
    }

    IWICBitmap* wic_bitmap = nullptr;
    hr = factory->CreateBitmapFromHBITMAP(screen_bitmap, nullptr, WICBitmapUseAlpha, &wic_bitmap);
    DeleteObject(screen_bitmap);
    if (FAILED(hr)) {
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to create WIC bitmap.";
        return false;
    }

    IWICBitmapSource* source = wic_bitmap;
    IWICBitmapScaler* scaler = nullptr;
    if (target_width != screen_width || target_height != screen_height) {
        hr = factory->CreateBitmapScaler(&scaler);
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(wic_bitmap, target_width, target_height, WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr)) {
                source = scaler;
            }
        }
    }

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to create WIC stream.";
        return false;
    }

    std::wstring wide_path(path.begin(), path.end());
    hr = stream->InitializeFromFilename(wide_path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to open output file.";
        return false;
    }

    GUID container = (format == "jpg" || format == "jpeg") ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng;
    IWICBitmapEncoder* encoder = nullptr;
    hr = factory->CreateEncoder(container, nullptr, &encoder);
    if (FAILED(hr)) {
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to create WIC encoder.";
        return false;
    }

    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        encoder->Release();
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to initialize encoder.";
        return false;
    }

    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* props = nullptr;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) {
        encoder->Release();
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to create encoder frame.";
        return false;
    }

    if (props && (format == "jpg" || format == "jpeg")) {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_R4;
        var.fltVal = static_cast<float>(config.screenshot_quality) / 100.0f;
        props->Write(1, &option, &var);
        VariantClear(&var);
    }

    hr = frame->Initialize(props);
    if (props) {
        props->Release();
    }
    if (FAILED(hr)) {
        frame->Release();
        encoder->Release();
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to initialize frame.";
        return false;
    }

    hr = frame->SetSize(target_width, target_height);
    if (FAILED(hr)) {
        frame->Release();
        encoder->Release();
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to set frame size.";
        return false;
    }

    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    frame->SetPixelFormat(&pixel_format);
    hr = frame->WriteSource(source, nullptr);
    if (FAILED(hr)) {
        frame->Release();
        encoder->Release();
        stream->Release();
        if (scaler) {
            scaler->Release();
        }
        wic_bitmap->Release();
        factory->Release();
        if (should_uninit) {
            CoUninitialize();
        }
        error = "Failed to write frame.";
        return false;
    }

    hr = frame->Commit();
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }

    frame->Release();
    encoder->Release();
    stream->Release();
    if (scaler) {
        scaler->Release();
    }
    wic_bitmap->Release();
    factory->Release();
    if (should_uninit) {
        CoUninitialize();
    }

    if (FAILED(hr)) {
        error = "Failed to save screenshot.";
        return false;
    }
    return true;
#elif defined(__APPLE__)
    std::ostringstream command;
    command << "screencapture -x " << path;
#else
    std::ostringstream command;
    command << "import -window root -resize " << config.screenshot_width << "x" << config.screenshot_height;
    if (config.screenshot_compression && format == "jpg") {
        command << " -quality " << config.screenshot_quality;
    }
    command << " " << path;
#endif

#if !defined(_WIN32)
    int result = std::system(command.str().c_str());
    if (result != 0) {
        error = "Failed to capture screenshot. Ensure ImageMagick (import) is installed.";
        return false;
    }
    return true;
#endif
}

std::string buildWebAppMenuButton(const std::string& url) {
    std::ostringstream button;
    button << "{\"type\":\"web_app\",\"text\":\"Open\",\"web_app\":{\"url\":\"" << url << "\"}}";
    return button.str();
}

std::string getStatus() {
    std::ostringstream status;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    status << "Status: online\n";
    status << "Time: " << std::ctime(&now);
#ifdef _WIN32
    status << "OS: Windows\n";
#elif __APPLE__
    status << "OS: macOS\n";
#elif __linux__
    status << "OS: Linux\n";
#else
    status << "OS: Unknown\n";
#endif
    return status.str();
}

std::string buildHelp(const Config& config) {
    std::ostringstream help;
    help << "Commands:\n";
    help << "/start - greeting\n";
    help << "/help - this help\n";
    help << "/status - system info\n";
    help << "/list - list allowed commands\n";
    help << "/run <name> - execute allowed command\n";
    if (!config.commands.empty()) {
        help << "\nAllowed commands:\n";
        for (const auto& [name, cmd] : config.commands) {
            (void)cmd;
            help << "- " << name << "\n";
        }
    }
    return help.str();
}

void sendMessage(const std::string& token, long long chat_id, const std::string& text) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string escaped = escapeForUrl(curl, text);
    std::string data = "chat_id=" + std::to_string(chat_id) + "&text=" + escaped;
    std::string url = "https://api.telegram.org/bot" + token + "/sendMessage";
    curl_easy_cleanup(curl);
    httpPost(url, data);
}

void setMenuButton(const std::string& token, long long chat_id, const std::string& menu_button_json) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string escaped_button = escapeForUrl(curl, menu_button_json);
    std::string data = "chat_id=" + std::to_string(chat_id) + "&menu_button=" + escaped_button;
    std::string url = "https://api.telegram.org/bot" + token + "/setChatMenuButton";
    curl_easy_cleanup(curl);
    httpPost(url, data);
}

void sendPhoto(const std::string& token, long long chat_id, const std::string& path, const std::string& caption) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string url = "https://api.telegram.org/bot" + token + "/sendPhoto";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PCController/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "chat_id");
    curl_mime_data(part, std::to_string(chat_id).c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "caption");
    curl_mime_data(part, caption.c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "photo");
    curl_mime_filedata(part, path.c_str());

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::string error = curl_easy_strerror(res);
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        throw std::runtime_error("curl error: " + error);
    }

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
}

} // namespace

int main(int argc, char* argv[]) {
    const char* token_env = std::getenv("TELEGRAM_BOT_TOKEN");
    if (!token_env || std::string(token_env).empty()) {
        std::cerr << "TELEGRAM_BOT_TOKEN is not set." << std::endl;
        return 1;
    }
    std::string token = token_env;

    Config config;
    std::string config_error;
    std::string config_path;
    if (argc > 1) {
        config_path = argv[1];
    } else {
        const char* config_env = std::getenv("PCCTRL_CONFIG");
        config_path = config_env ? config_env : "config/config.ini";
    }

    if (!tryLoadConfig(config_path, config, config_error)) {
        std::filesystem::path exe_dir = getExecutableDir(argc > 0 ? argv[0] : nullptr);
        std::vector<std::filesystem::path> fallbacks = {
            exe_dir / "config" / "config.ini",
            exe_dir.parent_path() / "config" / "config.ini"
        };

        bool loaded = false;
        for (const auto& candidate : fallbacks) {
            if (std::filesystem::exists(candidate)) {
                if (tryLoadConfig(candidate.string(), config, config_error)) {
                    loaded = true;
                    config_path = candidate.string();
                    break;
                }
            }
        }

        if (!loaded) {
            std::cerr << "Failed to load config: " << config_error << std::endl;
            std::cout << "Failed to load config: " << config_error << std::endl;
            return 1;
        }
    }

    initLogOutput(config);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    long long offset = 0;
    Config runtime_config = config;
    std::unordered_set<long long> menu_button_set;
    while (true) {
        try {
            std::string url = "https://api.telegram.org/bot" + token + "/getUpdates?timeout=5&offset=" + std::to_string(offset);
            std::string response = httpGet(url);
            if (!runtime_config.debug_log_path.empty()) {
                std::string preview = response;
                constexpr std::size_t kMaxLogSize = 8000;
                if (preview.size() > kMaxLogSize) {
                    preview = preview.substr(0, kMaxLogSize) + "...(truncated)";
                }
                logDebug(runtime_config, "getUpdates response: " + preview);
            }
            auto updates = parseUpdates(response);

            for (const auto& update : updates) {
                offset = (std::max)(offset, update.update_id + 1);
                if (!runtime_config.debug_log_path.empty()) {
                    std::ostringstream oss;
                    oss << "update_id=" << update.update_id
                        << " chat_id=" << update.chat_id
                        << " text=\"" << update.text << "\""
                        << " web_app_data=\"" << update.web_app_data << "\"";
                    logDebug(runtime_config, oss.str());
                }

                if (!config.allowed_chat_ids.empty() &&
                    config.allowed_chat_ids.find(update.chat_id) == config.allowed_chat_ids.end()) {
                    sendMessage(token, update.chat_id, "Access denied.");
                    continue;
                }

                std::string text = trim(update.text);
                if (!runtime_config.webapp_url.empty() &&
                    menu_button_set.find(update.chat_id) == menu_button_set.end()) {
                    setMenuButton(token, update.chat_id, buildWebAppMenuButton(runtime_config.webapp_url));
                    menu_button_set.insert(update.chat_id);
                }

                std::string web_app_data = trim(update.web_app_data);
                std::string action;
                bool has_action = parseWebAppAction(web_app_data, action);
                if (web_app_data == "screenshot" || (has_action && action == "screenshot")) {
                    bool compression = runtime_config.screenshot_compression;
                    int width = runtime_config.screenshot_width;
                    int height = runtime_config.screenshot_height;
                    int quality = runtime_config.screenshot_quality;
                    if (parseWebAppScreenshotSettings(web_app_data, compression, width, height, quality)) {
                        runtime_config.screenshot_compression = compression;
                        runtime_config.screenshot_format = compression ? "jpg" : "png";
                        runtime_config.screenshot_width = width;
                        runtime_config.screenshot_height = height;
                        runtime_config.screenshot_quality = std::clamp(quality, 10, 100);
                    } else {
                        std::map<std::string, std::string> params;
                        if (parseQueryStringWithDecode(web_app_data, params)) {
                            auto comp_it = params.find("compression");
                            auto width_it = params.find("width");
                            auto height_it = params.find("height");
                            auto quality_it = params.find("quality");
                            if (comp_it != params.end() && width_it != params.end() &&
                                height_it != params.end() && quality_it != params.end()) {
                                runtime_config.screenshot_compression = (comp_it->second == "1" || comp_it->second == "true");
                                runtime_config.screenshot_format = runtime_config.screenshot_compression ? "jpg" : "png";
                                runtime_config.screenshot_width = std::stoi(width_it->second);
                                runtime_config.screenshot_height = std::stoi(height_it->second);
                                runtime_config.screenshot_quality = std::clamp(std::stoi(quality_it->second), 10, 100);
                            }
                        }
                    }
                    std::string screenshot_path;
                    std::string error;
                    if (!captureScreenshot(runtime_config, screenshot_path, error)) {
                        sendMessage(token, update.chat_id, error);
                    } else {
                        sendPhoto(token, update.chat_id, screenshot_path, "Скриншот готов.");
                        sendMessage(token, update.chat_id, "Готово.");
                    }
                } else {
                    bool compression = runtime_config.screenshot_compression;
                    int width = runtime_config.screenshot_width;
                    int height = runtime_config.screenshot_height;
                    int quality = runtime_config.screenshot_quality;
                    if (parseWebAppSettings(web_app_data, compression, width, height, quality)) {
                        runtime_config.screenshot_compression = compression;
                        runtime_config.screenshot_format = compression ? "jpg" : "png";
                        runtime_config.screenshot_width = width;
                        runtime_config.screenshot_height = height;
                        runtime_config.screenshot_quality = std::clamp(quality, 10, 100);
                        sendMessage(token, update.chat_id, "Настройки применены.");
                    } else if (!web_app_data.empty() && text.empty()) {
                        sendMessage(token, update.chat_id, "Неизвестные данные Web App.");
                    } else if (text == "Скриншот" || text == "/screenshot") {
                        std::string screenshot_path;
                        std::string error;
                        if (!captureScreenshot(runtime_config, screenshot_path, error)) {
                            sendMessage(token, update.chat_id, error);
                        } else {
                            sendPhoto(token, update.chat_id, screenshot_path, "Скриншот готов.");
                            sendMessage(token, update.chat_id, "Готово.");
                        }
                    } else if (text == "/start") {
                        sendMessage(token, update.chat_id, "PCController is online.");
                    } else {
                        sendMessage(token, update.chat_id, "Используйте /screenshot для снимка экрана.");
                    }
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "Error: " << ex.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    curl_global_cleanup();
    return 0;
}
