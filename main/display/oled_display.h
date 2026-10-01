#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "gif/lvgl_gif.h"
#include "lvgl_display.h"

#include <memory>
#include <string>

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;
    lv_obj_t* emoji_image_ = nullptr;
    std::unique_ptr<LvglGif> gif_controller_ = nullptr;
    std::string current_face_;
    bool face_only_ = false;

    // Face-only mode: which face to show depends on the conversation state.
    enum class FaceState { kOther, kIdle, kConnecting, kListening, kSpeaking };
    FaceState face_state_ = FaceState::kOther;
    std::string server_emotion_;  // last emotion sent by the server for this turn

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();
    bool ShowFaceGif(const char* emotion);
    void ApplyFace(const char* emotion);

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    // Opt-in: show only an animated face (GIF) on the whole screen.
    // Must be called before SetupUI(). Only applies to 128x64 panels.
    void SetFaceOnly(bool on) { face_only_ = on; }

    virtual void SetupUI() override;
    virtual void SetStatus(const char* status) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual bool IsMonochrome() const override { return true; }
    void SetPowerSaveMode(bool on) override;
};

#endif  // OLED_DISPLAY_H
