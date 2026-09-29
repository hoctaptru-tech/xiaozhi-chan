#ifndef REMOTE_CONTROL_SERVER_H_
#define REMOTE_CONTROL_SERVER_H_

#include <esp_http_server.h>
#include "boards/chan/motor_controller.h"

class RemoteControlServer {
private:
    httpd_handle_t server_ = nullptr;
    MotorController* motor_controller_ = nullptr;

    static esp_err_t IndexHandler(httpd_req_t *req);
    static esp_err_t MoveApiHandler(httpd_req_t *req);

public:
    RemoteControlServer();
    ~RemoteControlServer();

    // Khởi chạy Web Server với motor controller truyền vào
    bool Start(MotorController* motor_controller);
    // Dừng Web Server
    void Stop();
};

#endif // REMOTE_CONTROL_SERVER_H_