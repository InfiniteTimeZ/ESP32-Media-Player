#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "pins_config.h"
#include "ui.h"
#include "music_player_logic.h"
#include "serial_protocol.h"
#include "LovyanGFX_Driver.h"
#include "discord_ui.h"


#define ALBUM_WIDTH 250
#define ALBUM_HEIGHT 250
#define ALBUM_BUFFER_SIZE (ALBUM_WIDTH * ALBUM_HEIGHT * 2)
#define PACKET_MAGIC_1 0xA5
#define PACKET_MAGIC_2 0x5A
#define MAX_IMAGE_PAYLOAD 60000
#define MAX_PACKET_PAYLOAD (MAX_IMAGE_PAYLOAD + 1)

enum SerialState {
  FIND_MAGIC_1, 
  FIND_MAGIC_2, 
  READ_PACKET_TYPE, 
  READ_PACKET_LENGTH, 
  READ_PACKET_PAYLOAD
}; 

SerialState serial_state = FIND_MAGIC_1;
 
static uint32_t last_byte_time = 0;
static uint32_t discarded_bytes = 0;

static uint8_t packet_type = 0;
static uint8_t packet_length_bytes[4];
static uint8_t packet_length_index = 0;

static uint32_t expected_payload_length = 0;
static uint32_t payload_bytes_read = 0;

static uint8_t* shared_packet_buffer = nullptr;


extern bool user_is_seeking;
extern uint32_t seek_lockout_timer;
extern int currentVolume;
extern uint32_t vol_lockout_timer;
extern LGFX gfx;
extern bool is_system_muted;
extern void update_mute_visuals();

static uint16_t* album_pixel_buffers[2] = { nullptr, nullptr };
static lv_image_dsc_t received_img_dsc[2];
static int dsc_index = 0;
static LGFX_Sprite art_sprite(&gfx);
static bool sprite_initialized = false;






void process_json_message(String &json_str);
void process_image_message(uint8_t* jpeg_data, uint32_t length);


static String payload_to_string(const uint8_t* payload, uint32_t length){
  String result;
  result.reserve(length);

  for(uint32_t i = 0; i < length; i++){
    result += (char)payload[i];
  }

  return result;
}



static bool is_valid_packet_type(uint8_t type){
  switch (type){
    case 'R':
    case 'T':
    case 'I':
    case 'U':
    case 'A':
    case 'D':
    case 'N':
      return true;
    
    default:
      return false;
  }
}

static void process_packet(uint8_t type, const uint8_t* payload, uint32_t length){
  switch(type){
    case 'R':
      if (length ==0){
        Serial.println("ESP32_READY");
      }
      break;

    case 'T': {
      String json = payload_to_string(payload, length);
      process_json_message(json);
      break;
    }

    case 'I': {
      if(length == 0 || length > MAX_IMAGE_PAYLOAD){
        Serial.printf("Invalid album length: %lu\n", (unsigned long)length);
        return;
      }

      process_image_message((uint8_t*)payload, length);
      break;
    }

    case 'U': {
      String user_json = payload_to_string(payload, length);
      process_voice_users_json(user_json);
      break;
    }

    case 'A': {
      if(length < 2){
        Serial.println("Invalid avatar packet");
        return;
      }

      uint8_t user_index = payload[0];
      const uint8_t* jpeg_data = &payload[1];
      uint32_t jpeg_length = length - 1;

      if(jpeg_length > MAX_IMAGE_PAYLOAD){
        Serial.println("Avatar image exceeds maximum size");
        return;
      }

      process_avatar_message(user_index, jpeg_data, jpeg_length);
      break;
    }

    case 'D':
      process_discord_state_packet(payload, length);
      break;

    case 'N': {
      set_voice_channel_name(payload, length);
      break;
    }

    default:
      Serial.printf("Unknown packet type: 0x%02X\n", type);
      break;
  }
}

static void reset_serial_parser(){
  serial_state = FIND_MAGIC_1;

  packet_type = 0;
  packet_length_index = 0;
  expected_payload_length = 0;
  payload_bytes_read = 0;

}
static void fade_anim_cb(void * obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, LV_PART_MAIN);
}

void process_json_message(String &json_str) {
  JsonDocument doc;
  if (deserializeJson(doc, json_str)) return;
  
  strlcpy(current_player_state.song_name, doc["title"] | "", sizeof(current_player_state.song_name));
  strlcpy(current_player_state.artist_name, doc["artist"] | "", sizeof(current_player_state.artist_name));
  current_player_state.duration_seconds = (int)(doc["duration"] | 0.0);

  if (!user_is_seeking && (millis() - seek_lockout_timer > 1500)) {
      current_player_state.position_seconds = (int)(doc["position"] | 0.0);
  } 

  current_player_state.is_playing = (strcmp(doc["playback_status"] | "", "PLAYING") == 0);

  lv_label_set_text(ui_Song_Name, current_player_state.song_name);
  lv_label_set_text(ui_Song_Name2, current_player_state.song_name);
  lv_label_set_text(ui_Artist_Name, current_player_state.artist_name);
  update_slider_and_labels();

  sync_play_pause_visuals();

  int incoming_vol = doc["volume"] | -1;
  if (incoming_vol != -1 && (millis() - vol_lockout_timer > 1000)) {
    currentVolume = incoming_vol;
    lv_bar_set_value(ui_Bar1, currentVolume, LV_ANIM_OFF);
    char str_buf[8];
    snprintf(str_buf, sizeof(str_buf), "%d%%", currentVolume);
    lv_label_set_text(ui_Vol_Percent, str_buf);
  }

  if (doc.containsKey("bg_color")) {
      JsonArray color_arr = doc["bg_color"];
      uint32_t bright_hex = ((uint32_t)color_arr[0] << 16) | ((uint32_t)color_arr[1] << 8) | (int)color_arr[2];
      lv_obj_set_style_bg_color(ui_SongPositionSlider, lv_color_hex(bright_hex), LV_PART_INDICATOR);
      
      uint32_t dim_hex = (((uint32_t)color_arr[0]/2) << 16) | (((uint32_t)color_arr[1]/2) << 8) | ((int)color_arr[2]/2);
      lv_obj_set_style_bg_color(ui_Music_Screen, lv_color_hex(dim_hex), LV_PART_MAIN);
      lv_obj_set_style_bg_grad_color(ui_Music_Screen, lv_color_hex(0x000000), LV_PART_MAIN); 
      lv_obj_set_style_bg_grad_dir(ui_Music_Screen, LV_GRAD_DIR_VER, LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_Bar1, lv_color_hex(bright_hex), LV_PART_INDICATOR);
  }

  if (doc.containsKey("time")) {
      lv_label_set_text(ui_TimeLabel, doc["time"]); 
  }

  if (doc.containsKey("is_muted")) {
      bool incoming_mute = doc["is_muted"];
      if (incoming_mute != is_system_muted) {
          is_system_muted = incoming_mute;
          update_mute_visuals(); 
      }
  }
}


static void init_packet_buffer(){
  shared_packet_buffer = (uint8_t*)heap_caps_malloc(MAX_PACKET_PAYLOAD, MALLOC_CAP_SPIRAM);
  if (shared_packet_buffer == nullptr) {
        Serial.println("Failed to allocate serial packet buffer");
        abort();
  }
}



void init_album_art_buffers() {
    
    for (int i = 0; i < 2; i++){
      album_pixel_buffers[i] = (uint16_t*)heap_caps_malloc(ALBUM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

      if(album_pixel_buffers[i] == nullptr){
        Serial.printf("Failed to allocate album buffer %d\n", i);
      }
    }
}


void init_serial_protocol(){
  init_packet_buffer();
  init_album_art_buffers();
}


void process_image_message(uint8_t *jpeg_data, uint32_t length) {
  if (!sprite_initialized) {
    art_sprite.setPsram(true);
    art_sprite.setColorDepth(16); 
    if (art_sprite.createSprite(250, 250) == nullptr) return;
    sprite_initialized = true;
  }

  if (!art_sprite.drawJpg(jpeg_data, length, 0, 0)){
    Serial.printf("Album JPEG decode failed: %lu bytes\n", (unsigned long)length);
    return;
  } 

  uint16_t* pixels = (uint16_t*)art_sprite.getBuffer();
  for (int i = 0; i < 250 * 250; i++) {
      pixels[i] = (pixels[i] << 8) | (pixels[i] >> 8);
  }

  dsc_index = (dsc_index + 1) % 2; 

  if(album_pixel_buffers[dsc_index] == nullptr){
  Serial.println("Album image buffer unavailable");
  return;
  }

  lv_image_cache_drop(&received_img_dsc[dsc_index]);

  memcpy(album_pixel_buffers[dsc_index], art_sprite.getBuffer(), ALBUM_BUFFER_SIZE);


  received_img_dsc[dsc_index].header.magic = LV_IMAGE_HEADER_MAGIC;
  received_img_dsc[dsc_index].header.cf = LV_COLOR_FORMAT_RGB565;
  received_img_dsc[dsc_index].header.flags = 0;
  received_img_dsc[dsc_index].header.w = ALBUM_WIDTH;
  received_img_dsc[dsc_index].header.h = ALBUM_HEIGHT;
  received_img_dsc[dsc_index].header.stride = ALBUM_WIDTH * 2; 
  received_img_dsc[dsc_index].data_size = ALBUM_BUFFER_SIZE;
  received_img_dsc[dsc_index].data = (const uint8_t *)album_pixel_buffers[dsc_index];

  lv_image_set_src(ui_SongImage, &received_img_dsc[dsc_index]);
  lv_obj_invalidate(ui_SongImage);
  Serial.printf("Album image applied: %lu bytes\n", (unsigned long)length);

  lv_obj_set_style_opa(ui_SongImage, 0, LV_PART_MAIN);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, ui_SongImage);
  lv_anim_set_values(&a, 0, 255);           
  lv_anim_set_time(&a, 500);            
  lv_anim_set_exec_cb(&a, fade_anim_cb);   
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out); 
  lv_anim_start(&a);
}

void handle_serial_input() {
  if (serial_state != FIND_MAGIC_1 && (millis() - last_byte_time > 1500)) {
    Serial.println("Serial stall detected! Stream desynced. Flushing trash...");
    reset_serial_parser();
  }

  while (Serial.available() > 0) {
    last_byte_time = millis();

    if (serial_state == READ_PACKET_PAYLOAD) {
      uint32_t remaining = expected_payload_length - payload_bytes_read;
      uint32_t available = Serial.available();
      uint32_t bytes_to_read = min(remaining, available);
      
      if (bytes_to_read > 0){
        int bytes_read = Serial.readBytes((char*)&shared_packet_buffer[payload_bytes_read], bytes_to_read);
        payload_bytes_read += bytes_read;
      }

      if (payload_bytes_read >= expected_payload_length){
        process_packet(packet_type, shared_packet_buffer, expected_payload_length);
        reset_serial_parser();
      }
      continue;
    }
    
    uint8_t byte = Serial.read();

    switch (serial_state) {
      case FIND_MAGIC_1:
        if(byte == PACKET_MAGIC_1){
          if(discarded_bytes > 0){
            Serial.printf("Serial resync after discarding %lu bytes\n", (unsigned long)discarded_bytes);
            discarded_bytes = 0;
          }

          serial_state = FIND_MAGIC_2;

        }else{
          discarded_bytes++;
        }
        break;
      
      case FIND_MAGIC_2:
        if(byte == PACKET_MAGIC_2){
          serial_state = READ_PACKET_TYPE;
        } 
        else if(byte == PACKET_MAGIC_1){
          serial_state = FIND_MAGIC_2;
        }
        else{
          serial_state = FIND_MAGIC_1;
        }
        break;

      case READ_PACKET_TYPE:
        if (!is_valid_packet_type(byte)){
          Serial.printf("Invalid packet type: 0x%02X\n", byte);
          reset_serial_parser();
          break;
         }

         packet_type = byte;
         packet_length_index = 0;
         expected_payload_length = 0;
         serial_state = READ_PACKET_LENGTH;

         break;

      case READ_PACKET_LENGTH:
        packet_length_bytes[packet_length_index++] = byte;

        if(packet_length_index == 4){
          expected_payload_length = (uint32_t)packet_length_bytes[0] | ((uint32_t)packet_length_bytes[1] << 8) | ((uint32_t)packet_length_bytes[2] << 16) | ((uint32_t)packet_length_bytes[3] << 24);

          if (expected_payload_length > MAX_PACKET_PAYLOAD){
            Serial.printf("Invalid packet length: %lu\n", (unsigned long)expected_payload_length);
            reset_serial_parser();
            break;
          }

          payload_bytes_read = 0;

          if (expected_payload_length == 0){
            process_packet(packet_type, shared_packet_buffer, 0);
            reset_serial_parser();
          }
          else{
            serial_state = READ_PACKET_PAYLOAD;
          }
        }
        break;

      case READ_PACKET_PAYLOAD:
        break;
       
    }
  }
}