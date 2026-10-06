#ifndef GEMINI_SEARCH_H
#define GEMINI_SEARCH_H

#include "board.h"
#include "sdkconfig.h"

#if __has_include("gemini_secret.h")
#include "gemini_secret.h"
#endif
#ifndef GEMINI_API_KEY_LOCAL
#define GEMINI_API_KEY_LOCAL ""
#endif
#ifndef CONFIG_GEMINI_API_KEY
#define CONFIG_GEMINI_API_KEY ""
#endif
#ifndef CONFIG_GEMINI_MODEL
#define CONFIG_GEMINI_MODEL "gemini-3.5-flash-lite"
#endif

#include <cJSON.h>
#include <esp_log.h>

#include <memory>
#include <string>
#include <utility>

class GeminiSearch {
public:
    static constexpr int kTimeoutMs = 12000;
    static constexpr size_t kMaxAnswerBytes = 700;

    static std::string Search(const std::string& query) {
        std::string api_key = GEMINI_API_KEY_LOCAL;
        if (api_key.empty() || api_key == "PASTE_YOUR_GEMINI_API_KEY_HERE") {
            api_key = CONFIG_GEMINI_API_KEY;
        }
        // Nếu không có API Key, trả về rỗng để Xiaozhi server tự nhảy sang MCP
        if (api_key.empty() || query.empty()) {
            ESP_LOGW(TAG, "No API key or empty query, falling back to Xiaozhi MCP / Search.");
            return ""; 
        }

        ESP_LOGI(TAG, "Querying Gemini direct: %s", query.c_str());

        const std::string url = std::string("https://generativelanguage.googleapis.com/v1beta/models/")
                                + CONFIG_GEMINI_MODEL + ":generateContent";

        auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
        http->SetTimeout(kTimeoutMs);
        http->SetHeader("Content-Type", "application/json");
        http->SetHeader("x-goog-api-key", api_key);
        
        std::string payload = BuildRequest(query);
        http->SetContent(std::move(payload));

        if (auto opened = http->Open("POST", url); !opened) {
            ESP_LOGE(TAG, "Failed to connect to Gemini API, fallback to MCP.");
            return ""; 
        }

        auto status = http->GetStatusCode();
        if (!status) {
            ESP_LOGE(TAG, "Gemini request timeout, fallback to MCP.");
            http->Close();
            return ""; 
        }

        std::string response = http->ReadAll();
        http->Close();

        // Nếu dính lỗi HTTP (429 Exceeded Quota, 404, 400...), log lỗi và trả về rỗng
        // để ép Xiaozhi server dùng MCP / Dịch vụ tìm kiếm trên Web Console
        if (*status != 200) {
            ESP_LOGE(TAG, "Gemini returned HTTP %d: %s. Falling back to Xiaozhi MCP...", *status, ErrorMessage(response).c_str());
            return ""; 
        }

        std::string answer = ParseAnswer(response);
        if (answer.empty()) {
            ESP_LOGW(TAG, "Gemini parse answer empty, fallback to MCP.");
            return "";
        }

        ESP_LOGI(TAG, "Gemini Search Success: %s", answer.c_str());
        return answer;
    }

private:
    static constexpr const char* TAG = "GeminiSearch";

    static constexpr const char* kSystemPrompt =
        "You are the voice of a small home robot. Use Google Search for current facts. "
        "Answer in the same language as the question, in at most three short sentences "
        "that sound natural when spoken aloud. No markdown, no lists, no links.";

    using JsonPtr = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

    static std::string BuildRequest(const std::string& query) {
        JsonPtr root(cJSON_CreateObject(), cJSON_Delete);

        cJSON* system_instruction = cJSON_AddObjectToObject(root.get(), "systemInstruction");
        cJSON* system_parts = cJSON_AddArrayToObject(system_instruction, "parts");
        cJSON* system_part = cJSON_CreateObject();
        cJSON_AddStringToObject(system_part, "text", kSystemPrompt);
        cJSON_AddItemToArray(system_parts, system_part);

        cJSON* contents = cJSON_AddArrayToObject(root.get(), "contents");
        cJSON* content = cJSON_CreateObject();
        cJSON_AddStringToObject(content, "role", "user");
        cJSON* parts = cJSON_AddArrayToObject(content, "parts");
        cJSON* part = cJSON_CreateObject();
        cJSON_AddStringToObject(part, "text", query.c_str());
        cJSON_AddItemToArray(parts, part);
        cJSON_AddItemToArray(contents, content);

        cJSON* tools = cJSON_AddArrayToObject(root.get(), "tools");
        cJSON* tool = cJSON_CreateObject();
        cJSON_AddItemToObject(tool, "googleSearch", cJSON_CreateObject());
        cJSON_AddItemToArray(tools, tool);

        cJSON* generation_config = cJSON_AddObjectToObject(root.get(), "generationConfig");
        cJSON_AddNumberToObject(generation_config, "maxOutputTokens", 1024);

        char* printed = cJSON_PrintUnformatted(root.get());
        std::string body = printed != nullptr ? printed : "";
        cJSON_free(printed);
        return body;
    }

    static std::string ErrorMessage(const std::string& response) {
        JsonPtr root(cJSON_Parse(response.c_str()), cJSON_Delete);
        if (root) {
            cJSON* error = cJSON_GetObjectItem(root.get(), "error");
            cJSON* message = error != nullptr ? cJSON_GetObjectItem(error, "message") : nullptr;
            if (cJSON_IsString(message) && message->valuestring != nullptr) {
                return message->valuestring;
            }
        }
        return response.substr(0, 200);
    }

    static std::string ParseAnswer(const std::string& response) {
        JsonPtr root(cJSON_Parse(response.c_str()), cJSON_Delete);
        if (!root) return "";

        cJSON* candidates = cJSON_GetObjectItem(root.get(), "candidates");
        cJSON* first = cJSON_IsArray(candidates) ? cJSON_GetArrayItem(candidates, 0) : nullptr;
        if (first == nullptr) return "";

        cJSON* content = cJSON_GetObjectItem(first, "content");
        cJSON* parts = content != nullptr ? cJSON_GetObjectItem(content, "parts") : nullptr;

        std::string text;
        cJSON* part = nullptr;
        cJSON_ArrayForEach(part, parts) {
            if (cJSON_IsTrue(cJSON_GetObjectItem(part, "thought"))) continue;
            cJSON* piece = cJSON_GetObjectItem(part, "text");
            if (cJSON_IsString(piece) && piece->valuestring != nullptr) {
                text += piece->valuestring;
            }
        }
        return CleanForSpeech(text);
    }

    static std::string CleanForSpeech(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        bool last_space = true;
        for (char c : in) {
            if (c == '*' || c == '#' || c == '`' || c == '_') continue;
            if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
                if (!last_space) out += ' ';
                last_space = true;
                continue;
            }
            out += c;
            last_space = false;
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (out.size() > kMaxAnswerBytes) {
            size_t cut = kMaxAnswerBytes;
            while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
            out.resize(cut);
        }
        return out;
    }
};

#endif  // GEMINI_SEARCH_H