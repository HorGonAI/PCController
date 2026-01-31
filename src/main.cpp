#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
};

struct Config {
    std::unordered_set<long long> allowed_chat_ids;
    std::map<std::string, std::string> commands;
    int screenshot_width = 1280;
    int screenshot_height = 720;
    bool screenshot_compression = true;
    int screenshot_quality = 85;
    std::string screenshot_format = "jpg";
};

enum class MenuState {
    Main,
    Settings,
    Quality,
    ScreenshotSettings,
    ResolutionSelect
};

constexpr std::size_t kMaxMessageLength = 3500;

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
        }
    }

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
        if (!findStringAfterKey(input, "\"text\"", message_pos, text)) {
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

std::string buildMainKeyboard() {
    return R"({"keyboard":[[{"text":"Скриншот"}],[{"text":"Настройки"}]],"resize_keyboard":true})";
}

std::string buildSettingsKeyboard(const Config& config) {
    std::ostringstream keyboard;
    keyboard << "{\"keyboard\":[";
    keyboard << "[{\"text\":\"Качество\"}],";
    keyboard << "[{\"text\":\"Назад\"}]";
    keyboard << "],\"resize_keyboard\":true}";
    return keyboard.str();
}

std::string buildQualityKeyboard() {
    return "{\"keyboard\":[[{\"text\":\"Скриншоты\"}],[{\"text\":\"Назад\"}]],\"resize_keyboard\":true}";
}

std::string buildScreenshotSettingsKeyboard(const Config& config) {
    std::ostringstream keyboard;
    keyboard << "{\"keyboard\":[";
    keyboard << "[{\"text\":\"Сжатие: " << (config.screenshot_compression ? "Вкл" : "Выкл") << "\"}],";
    keyboard << "[{\"text\":\"Разрешение: " << config.screenshot_width << "x" << config.screenshot_height << "\"}],";
    keyboard << "[{\"text\":\"Назад\"}]";
    keyboard << "],\"resize_keyboard\":true}";
    return keyboard.str();
}

std::string buildResolutionKeyboard() {
    return "{\"keyboard\":[[{\"text\":\"Разрешение 1280x720\"}],[{\"text\":\"Разрешение 1920x1080\"}],[{\"text\":\"Назад\"}]],\"resize_keyboard\":true}";
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

void sendMessage(const std::string& token, long long chat_id, const std::string& text, const std::string& reply_markup = "") {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string escaped = escapeForUrl(curl, text);
    std::string data = "chat_id=" + std::to_string(chat_id) + "&text=" + escaped;
    if (!reply_markup.empty()) {
        std::string escaped_markup = escapeForUrl(curl, reply_markup);
        data += "&reply_markup=" + escaped_markup;
    }
    std::string url = "https://api.telegram.org/bot" + token + "/sendMessage";
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

    curl_global_init(CURL_GLOBAL_DEFAULT);

    long long offset = 0;
    Config runtime_config = config;
    MenuState menu_state = MenuState::Main;
    while (true) {
        try {
            std::string url = "https://api.telegram.org/bot" + token + "/getUpdates?timeout=5&allowed_updates=message&offset=" + std::to_string(offset);
            std::string response = httpGet(url);
            auto updates = parseUpdates(response);

            for (const auto& update : updates) {
                offset = (std::max)(offset, update.update_id + 1);

                if (!config.allowed_chat_ids.empty() &&
                    config.allowed_chat_ids.find(update.chat_id) == config.allowed_chat_ids.end()) {
                    sendMessage(token, update.chat_id, "Access denied.");
                    continue;
                }

                std::string text = trim(update.text);
                if (text == "Скриншот") {
                    std::string screenshot_path;
                    std::string error;
                    if (!captureScreenshot(runtime_config, screenshot_path, error)) {
                        sendMessage(token, update.chat_id, error, buildMainKeyboard());
                    } else {
                        sendPhoto(token, update.chat_id, screenshot_path, "Скриншот готов.");
                        sendMessage(token, update.chat_id, "Готово.", buildMainKeyboard());
                    }
                    menu_state = MenuState::Main;
                } else if (text == "Настройки") {
                    sendMessage(token, update.chat_id, "Выберите параметр.", buildSettingsKeyboard(runtime_config));
                    menu_state = MenuState::Settings;
                } else if (text == "Качество") {
                    sendMessage(token, update.chat_id, "Раздел качества.", buildQualityKeyboard());
                    menu_state = MenuState::Quality;
                } else if (text == "Скриншоты") {
                    sendMessage(token, update.chat_id, "Настройки скриншотов.", buildScreenshotSettingsKeyboard(runtime_config));
                    menu_state = MenuState::ScreenshotSettings;
                } else if (text.rfind("Разрешение:", 0) == 0) {
                    sendMessage(token, update.chat_id, "Выберите разрешение.", buildResolutionKeyboard());
                    menu_state = MenuState::ResolutionSelect;
                } else if (text == "Разрешение 1280x720") {
                    runtime_config.screenshot_width = 1280;
                    runtime_config.screenshot_height = 720;
                    sendMessage(token, update.chat_id, "Разрешение установлено 1280x720.", buildScreenshotSettingsKeyboard(runtime_config));
                    menu_state = MenuState::ScreenshotSettings;
                } else if (text == "Разрешение 1920x1080") {
                    runtime_config.screenshot_width = 1920;
                    runtime_config.screenshot_height = 1080;
                    sendMessage(token, update.chat_id, "Разрешение установлено 1920x1080.", buildScreenshotSettingsKeyboard(runtime_config));
                    menu_state = MenuState::ScreenshotSettings;
                } else if (text.rfind("Сжатие", 0) == 0) {
                    runtime_config.screenshot_compression = !runtime_config.screenshot_compression;
                    runtime_config.screenshot_format = runtime_config.screenshot_compression ? "jpg" : "png";
                    std::string status = runtime_config.screenshot_compression ? "Вкл" : "Выкл";
                    sendMessage(token, update.chat_id, "Сжатие: " + status + ".", buildScreenshotSettingsKeyboard(runtime_config));
                    menu_state = MenuState::ScreenshotSettings;
                } else if (text == "Назад") {
                    if (menu_state == MenuState::ResolutionSelect) {
                        sendMessage(token, update.chat_id, "Настройки скриншотов.", buildScreenshotSettingsKeyboard(runtime_config));
                        menu_state = MenuState::ScreenshotSettings;
                    } else if (menu_state == MenuState::ScreenshotSettings) {
                        sendMessage(token, update.chat_id, "Раздел качества.", buildQualityKeyboard());
                        menu_state = MenuState::Quality;
                    } else if (menu_state == MenuState::Quality) {
                        sendMessage(token, update.chat_id, "Выберите параметр.", buildSettingsKeyboard(runtime_config));
                        menu_state = MenuState::Settings;
                    } else if (menu_state == MenuState::Settings) {
                        sendMessage(token, update.chat_id, "Главное меню.", buildMainKeyboard());
                        menu_state = MenuState::Main;
                    } else {
                        sendMessage(token, update.chat_id, "Главное меню.", buildMainKeyboard());
                        menu_state = MenuState::Main;
                    }
                } else if (text == "/start") {
                    sendMessage(token, update.chat_id, "PCController is online.", buildMainKeyboard());
                    menu_state = MenuState::Main;
                } else {
                    sendMessage(token, update.chat_id, "Используйте кнопки клавиатуры.", buildMainKeyboard());
                    menu_state = MenuState::Main;
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
