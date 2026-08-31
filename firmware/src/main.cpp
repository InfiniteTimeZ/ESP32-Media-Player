#include "pins_config.h"
#include "LovyanGFX_Driver.h"
#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <SPI.h>
#include <stdbool.h>
#include "ui.h"
#include "music_player_logic.h"
#include "serial_protocol.h"

LGFX gfx;
static uint8_t *buf;
static uint8_t *buf1;
constexpr uint32_t LVGL_BUFFER_LINES = 60;
uint16_t touch_x, touch_y;
uint32_t vol_lockout_timer = 0;

#define ENC_PIN_A 19
#define ENC_PIN_B 20
#define ENC_PIN_D 8

constexpr uint32_t DOUBLE_CLICK_TIME = 450;

volatile int32_t encoder_counter = 0;
volatile uint8_t encoder_history = 0;
int32_t last_pos = 0;
int currentVolume = 25;

bool is_system_muted = false; 

static const int8_t KNOB_STATES[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void uart_error(hardwareSerial_error_t error){
  Serial.printf("UART ERROR: %d\n", error);
}


void IRAM_ATTR encoder_isr() {
    encoder_history <<= 2;
    encoder_history |= (digitalRead(ENC_PIN_A) << 1) | digitalRead(ENC_PIN_B);
    int8_t movement = KNOB_STATES[encoder_history & 0x0F];
    if (movement != 0) {
        encoder_counter += movement;
    }
}


void setup_encoder() {
    pinMode(ENC_PIN_A, INPUT_PULLUP);
    pinMode(ENC_PIN_B, INPUT_PULLUP);
    pinMode(ENC_PIN_D, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_PIN_A), encoder_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_PIN_B), encoder_isr, CHANGE);
}


void my_encoder_read(lv_indev_t *indev, lv_indev_data_t *data) {
    int32_t diff = encoder_counter - last_pos;
    last_pos = encoder_counter;
    if (diff > 0) data->enc_diff = 1;
    else if (diff < 0) data->enc_diff = -1;
    else data->enc_diff = 0;
    data->state = (digitalRead(ENC_PIN_D) == LOW) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}


void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    gfx.startWrite();
    gfx.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)px_map);
    gfx.endWrite(); 
    
    lv_display_flush_ready(disp);
}


void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_REL;
    bool touched = gfx.getTouch(&touch_x, &touch_y);
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch_x;
        data->point.y = touch_y;
    }
}


uint32_t my_tick_get_cb() {
    return millis();
}


 bool i2cScanForAddress(uint8_t address) {
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}


void sendI2CCommand(uint8_t command) {
    uint8_t error;
    Wire.beginTransmission(0x30);
    Wire.write(command);
    error = Wire.endTransmission();
    if (error == 0) {
        Serial.print("command 0x");
        Serial.print(command, HEX);
        Serial.println(" Sent successfully");
    } else {
        Serial.print("Command sent error, error code:");
        Serial.println(error);
    }
}


void update_mute_visuals() {
    lv_obj_t* album_container = lv_obj_get_parent(ui_SongImage);

    if (is_system_muted) {
        lv_obj_set_style_outline_width(album_container, 6, LV_PART_MAIN);
        lv_obj_set_style_outline_color(album_container, lv_color_hex(0xF65552), LV_PART_MAIN); 
        lv_obj_set_style_outline_opa(album_container, 255, LV_PART_MAIN);
    } else {
        lv_obj_set_style_outline_width(album_container, 0, LV_PART_MAIN);
    }
    
    lv_obj_invalidate(album_container);
}


 void handle_volume_logic() {
    static int32_t last_volume_pos = 0;
    static uint32_t last_click_time = 0; 
    
    int32_t current_pos = encoder_counter / 4;

    static uint32_t interaction_timer = 0;
    static int current_opacity = 0; 
    static bool is_active = false;

    if (current_pos != last_volume_pos) {
        int32_t diff = current_pos - last_volume_pos;
        last_volume_pos = current_pos;

        uint32_t current_time = millis();
        uint32_t time_between_clicks = current_time - last_click_time;
        last_click_time = current_time;

        int step_multiplier = 1;  
        
        if (time_between_clicks < 50) {
            step_multiplier = 10; 
        } else if (time_between_clicks < 120) {
            step_multiplier = 5;  
        } else if (time_between_clicks < 250) {
            step_multiplier = 2;  
        }

        currentVolume += (diff * step_multiplier);
 
        if (currentVolume > 100) currentVolume = 100;
        if (currentVolume < 0) currentVolume = 0;

        Serial.print("CMD:VOL:");
        Serial.println(currentVolume);

        if (is_system_muted) {
            is_system_muted = false;
            update_mute_visuals();
        }

        vol_lockout_timer = millis();

        lv_bar_set_value(ui_Bar1, currentVolume, LV_ANIM_OFF);

        char str_buf[8];
        snprintf(str_buf, sizeof(str_buf), "%d%%", currentVolume);
        lv_label_set_text(ui_Vol_Percent, str_buf);

        if (!is_active) {
            lv_obj_remove_flag(ui_Volume_Panel, LV_OBJ_FLAG_HIDDEN);
            is_active = true;
        }

        interaction_timer = millis();
    }

    if (is_active) {
        uint32_t time_elapsed = millis() - interaction_timer;

        if (time_elapsed < 2000) {
            if (current_opacity < 255) {
                current_opacity += 51; 
                if (current_opacity > 255) current_opacity = 255;
                lv_obj_set_style_opa(ui_Volume_Panel, current_opacity, LV_PART_MAIN);
            }
        } else {
            if (current_opacity > 0) {
                current_opacity -= 25; 
                if (current_opacity < 0) current_opacity = 0;
                lv_obj_set_style_opa(ui_Volume_Panel, current_opacity, LV_PART_MAIN);
            } else {
                lv_obj_add_flag(ui_Volume_Panel, LV_OBJ_FLAG_HIDDEN);
                is_active = false;
            }
        }
    }
}


void setup_serial(){
    size_t rx_size = Serial.setRxBufferSize(1024 * 40);
    Serial.begin(230400);  
    Serial.setRxFIFOFull(64);
    Serial.onReceiveError(uart_error);
    Serial.printf("Serial RX buffer allocated: %u bytes\n", rx_size);
}


void setup_controllers(){
    Wire.begin(15, 16);
    delay(50);

    while (true) {
        if (i2cScanForAddress(0x30) && i2cScanForAddress(0x5D)) {
            Serial.println("Both controllers detected");
            break;
        } else {
            sendI2CCommand(250);
            pinMode(1, OUTPUT);
            digitalWrite(1, LOW);
            delay(120);
            pinMode(1, INPUT);
            delay(100);
        }
    }
    sendI2CCommand(0);
}

void setup_display(){
    
    gfx.init();
    gfx.initDMA(); 
    
    gfx.startWrite();
    gfx.fillScreen(TFT_BLACK);
    gfx.endWrite();

    lv_init();
    lv_tick_set_cb(my_tick_get_cb);

    constexpr size_t buffer_size = LCD_H_RES * LVGL_BUFFER_LINES * sizeof(uint16_t);
    
    buf = static_cast<uint8_t *>(heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    buf1 = static_cast<uint8_t *>(heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    
    if (buf == nullptr || buf1 == nullptr) {
        Serial.println("Failed to allocate INTERNAL LVGL display buffers");
        abort();
    }

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, my_disp_flush);
    lv_display_set_buffers(display, buf, buf1, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);

    setup_encoder();
    
    lv_indev_t *encoder_indev = lv_indev_create();
    lv_indev_set_type(encoder_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(encoder_indev, my_encoder_read);
}

void setup_ui(){
    delay(100);
    gfx.fillScreen(TFT_BLACK);

    ui_init();
    register_screen_switch_handlers();
    lv_obj_set_parent(ui_Volume_Panel, lv_layer_top());
    lv_obj_set_parent(ui_TimeLabel, lv_layer_top());
    lv_obj_set_style_text_color(ui_TimeLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_add_event_cb(ui_SongPositionSlider, track_slider_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_SongPositionSlider, track_slider_cb, LV_EVENT_RELEASED, NULL);
    music_player_init();
    
    lv_obj_add_flag(ui_Volume_Panel, LV_OBJ_FLAG_HIDDEN);
    init_avatar_buffers();

    char str_buf[8];
    snprintf(str_buf, sizeof(str_buf), "%d%%", currentVolume);
    lv_label_set_text(ui_Vol_Percent, str_buf);

    last_pos = encoder_counter;
}

void signal_ready(){

    while (Serial.available() > 0) {
        Serial.read();
    }
      
    delay(50);
    Serial.println();
    Serial.println("ESP32_READY");
}


void handle_encoder_button(){
    static int lastButtonState = HIGH;
    static uint32_t last_button_click_time = 0;
    static bool pending_single_click = false;
   

    int currentButtonState = digitalRead(ENC_PIN_D); 

    if (currentButtonState != lastButtonState) {
        delay(10); 
        currentButtonState = digitalRead(ENC_PIN_D); 
        if (currentButtonState != lastButtonState) {
            lastButtonState = currentButtonState;
            
            if (currentButtonState == LOW) { 
                if (pending_single_click && (millis() - last_button_click_time < DOUBLE_CLICK_TIME)) {
                    Serial.println("CMD:MUTE");
                    is_system_muted = !is_system_muted;
                    update_mute_visuals();
                    pending_single_click = false;

                } else {
                     pending_single_click = true;
                }
                 last_button_click_time = millis();
            }
        }
       
    }  
    if (pending_single_click && (millis() - last_button_click_time > DOUBLE_CLICK_TIME)) {
        Serial.println("CMD:TOGGLE");
        pending_single_click = false;    
    }
}


 void setup() {
    setup_serial();
    setup_controllers();
    setup_display();
    setup_ui();
    
    Serial.println("Setup done");

    signal_ready(); 
}

void loop() {
    lv_timer_handler();
    handleVolumeLogic();
    handle_serial_input();
    handle_encoder_button();

    delay(1);
}