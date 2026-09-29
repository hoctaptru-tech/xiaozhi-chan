#include "remote_control_server.h"
#include <esp_log.h>
#include <cstring> // Thư viện dùng hàm strcmp

#define TAG "REMOTE_CTRL_SERVER"

RemoteControlServer::RemoteControlServer(Board* board) {
    // Lưu con trỏ motor_controller từ board (nếu board hỗ trợ)
    // Tùy cấu trúc board của bạn, điều chỉnh lại dòng này nếu cần
    if (board != nullptr) {
        motor_controller_ = board->GetMotorController();
    }
}

RemoteControlServer::~RemoteControlServer() {
    Stop();
}

esp_err_t RemoteControlServer::Start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;

    ESP_LOGI(TAG, "Dang khoi tao HTTP Server tren port: %d", config.server_port);

    if (httpd_start(&server_handle_, &config) == ESP_OK) {
        httpd_uri_t move_uri = {
            .uri       = "/api/move",
            .method    = HTTP_GET,
            .handler   = MoveApiHandler,
            .user_ctx  = this
        };
        httpd_register_uri_handler(server_handle_, &move_uri);
        ESP_LOGI(TAG, "Da khoi tao HTTP Server thanh cong!");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Loi khoi tao HTTP Server!");
    return ESP_FAIL;
}

esp_err_t RemoteControlServer::Stop() {
    if (server_handle_) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
    }
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
            
            // So sánh chuỗi trực tiếp bằng strcmp (Không bị lỗi std::string)
            if (strcmp(action, "forward") == 0) {
                self->motor_controller_->Forward(100, 800);
            } else if (strcmp(action, "backward") == 0) {
                self->motor_controller_->Backward(100, 800);
            } else if (strcmp(action, "turn_left") == 0) {
                self->motor_controller_->TurnLeft(100, 500);
            } else if (strcmp(action, "turn_right") == 0) {
                self->motor_controller_->TurnRight(100, 500);
            } else if (strcmp(action, "stop") == 0) {
                self->motor_controller_->Stop();
            }

        }
    }

    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}