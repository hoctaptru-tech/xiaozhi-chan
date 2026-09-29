#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// Dong co - driver DRV8833 (H-bridge kep)
// Theo xac nhan cua nguoi dung: lf=11, lb=12, rf=14, rb=13
#define MOTOR_LEFT_IN1_GPIO  GPIO_NUM_11   // pin_motor_lf
#define MOTOR_LEFT_IN2_GPIO  GPIO_NUM_12   // pin_motor_lb
#define MOTOR_RIGHT_IN1_GPIO GPIO_NUM_13   // pin_motor_rf
#define MOTOR_RIGHT_IN2_GPIO GPIO_NUM_14   // pin_motor_rb

#endif // _BOARD_CONFIG_H_
