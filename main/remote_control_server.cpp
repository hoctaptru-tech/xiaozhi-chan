#include "remote_control_server.h"
#include <esp_log.h>
#include <cstring>

#define TAG "REMOTE_CTRL_SERVER"

RemoteControlServer::RemoteControlServer() {}

RemoteControlServer::~RemoteControlServer() {
    Stop();
}

esp_err_t RemoteControlServer::IndexHandler(httpd_req_t *req) {
    // Trả về giao diện Web HTML/JS
    const char* html_page = R"html(
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
            <title>Robot Controller</title>
            <style>
                body { font-family: sans-serif; text-align: center; background: #121212; color: #fff; margin:0; padding:20px; user-select: none; }
                h2 { margin-bottom: 20px; color: #4CAF50; }
                .btn { width: 75px; height: 75px; margin: 8px; font-size: 24px; border: none; border-radius: 12px; background: #2196F3; color: white; cursor: pointer; touch-action: manipulation; }
                .btn:active { background: #0b7dda; transform: scale(0.95); }
                .stop { background: #f44336; width: 160px; font-weight: bold; }
                .stop:active { background: #da190b; }
                .grid { display: inline-grid; grid-template-columns: repeat(3, 1fr); gap: 5px; align-items: center; justify-items: center; }
            </style>
        </head>
        <body>
            <h2>🤖 Robot Remote</h2>
            <div class="grid">
                <div></div><button class="btn" onclick="move('forward')">▲</button><div></div>
                <button class="btn" onclick="move('turn_left')">◄</button>
                <button class="btn stop" onclick="move('stop')">STOP</button>
                <button class="btn" onclick="move('turn_right')">►</button>
                <div></div><button class="btn" onclick="move('backward')">▼</button><div></div>
            </div>
            <script>
                function move(act) {
                    fetch('/api/move?action=' + act)
                        .catch(err => console.error('Error:', err));
                }
            </script>
        </body>
        </html>
    )html";
    
    httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t RemoteControlServer::MoveApiHandler(httpd_req_t *req) {
    auto* self = static_cast<RemoteControlServer*>(req->user_ctx);
    if (!self || !self->motor_controller_) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char buf[64];
if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char action[16] = {0};
    if (httpd_query_key_value(buf, "action", action, sizeof(action)) == ESP_OK) {
        
        // So sánh chuỗi trực tiếp bằng strcmp
        if (strcmp(action, "forward") == 0) self->motor_controller_->Forward(100, 800);
        else if (strcmp(action, "backward") == 0) self->motor_controller_->Backward(100, 800);
        else if (strcmp(action, "turn_left") == 0) self->motor_controller_->TurnLeft(100, 500);
        else if (strcmp(action, "turn_right") == 0) self->motor_controller_->TurnRight(100, 500);
        else if (strcmp(action, "stop") == 0) self->motor_controller_->Stop();
        
    }
}

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

bool RemoteControlServer::Start(MotorController* motor_controller) {
    if (server_ != nullptr) {
        ESP_LOGW(TAG, "Server already running");
        return true;
    }

    motor_controller_ = motor_controller;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 4096;

    if (httpd_start(&server_, &config) == ESP_OK) {
        // Route 1: Trang chủ HTML
        httpd_uri_t index_uri = {
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = IndexHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &index_uri);

        // Route 2: API nhận lệnh di chuyển
        httpd_uri_t move_api_uri = {
            .uri      = "/api/move",
            .method   = HTTP_GET,
            .handler  = MoveApiHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &move_api_uri);

        ESP_LOGI(TAG, "Remote Control Web Server started successfully!");
        return true;
    }

    ESP_LOGE(TAG, "Failed to start Remote Control Web Server");
    return false;
}

void RemoteControlServer::Stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGI(TAG, "Remote Control Web Server stopped");
    }
}