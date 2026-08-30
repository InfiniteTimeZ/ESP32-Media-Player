#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "pins_config.h"
#include "ui.h"
#include "music_player_logic.h"
#include "serial_protocol.h"
#include "LovyanGFX_Driver.h"

#define MAX_DISCORD_USERS 15
#define AVATAR_WIDTH 75
#define AVATAR_HEIGHT 75
#define AVATAR_BUFFER_SIZE (AVATAR_WIDTH * AVATAR_HEIGHT * 2)
#define ALBUM_WIDTH 250
#define ALBUM_HEIGHT 250
#define ALBUM_BUFFER_SIZE (ALBUM_WIDTH * ALBUM_HEIGHT * 2)

static String last_user_names[MAX_DISCORD_USERS];
static int last_user_count = 0;
static int last_card_width = -1;
static int last_card_height = -1;

static lv_image_dsc_t avatar_img_dsc[MAX_DISCORD_USERS];
uint16_t* avatar_pixel_buffers[MAX_DISCORD_USERS] = { nullptr };
bool avatar_sprite_initialized = false;

extern lv_obj_t* ui_DiscordInfo; 
extern lv_obj_t* ui_PlayPauseButton3; 
extern bool is_system_muted;
extern void update_mute_visuals();


extern LGFX gfx;
static LGFX_Sprite avatar_sprite(&gfx);


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

extern bool user_is_seeking;
extern uint32_t seek_lockout_timer;
extern int currentVolume;
extern uint32_t vol_lockout_timer;

static uint16_t* album_pixel_buffers[2] = { nullptr, nullptr };
static lv_image_dsc_t received_img_dsc[2];
static int dsc_index = 0;
static LGFX_Sprite art_sprite(&gfx);
static bool sprite_initialized = false;

static uint32_t discarded_bytes = 0;
static uint8_t packet_type = 0;
static uint8_t packet_length_bytes[4];
static uint8_t packet_length_index = 0;
static uint32_t expected_payload_length = 0;
static uint32_t payload_bytes_read = 0;
static uint8_t* shared_packet_buffer = nullptr;

void process_json_message(String &json_str);
void process_voice_users_json(String &discord_user_buffer);
void process_avatar_message(uint8_t index, const uint8_t* jpeg_data, size_t jpeg_len);
void process_image_message(uint8_t* jpeg_data, uint32_t length);


static String payload_to_string(const uint8_t* payload, uint32_t length){
  String result;
  result.reserve(length);

  for(uint32_t i = 0; i < length; i++){
    result += (char)payload[i];
  }

  return result;
}

static void process_discord_state_packet(const uint8_t* payload, uint32_t length){
  if (length != 2){
    Serial.printf("Invalid Discord state packet length: %lu\n", (unsigned long)length);
    return;
  }
  
  bool is_muted =  payload[0] != 0;
  bool is_deafened = payload[1] != 0;

  if (is_muted){
    lv_obj_add_state(ui_MuteButton, LV_STATE_CHECKED);
    lv_obj_add_state(ui_MuteButton2, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(ui_MuteButton, LV_STATE_CHECKED);
    lv_obj_clear_state(ui_MuteButton2, LV_STATE_CHECKED);
  }
  
  if (is_deafened){

   lv_obj_add_state(ui_DeafenButton, LV_STATE_CHECKED);
    lv_obj_add_state(ui_DeafenButton2, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(ui_DeafenButton, LV_STATE_CHECKED);
    lv_obj_clear_state(ui_DeafenButton2, LV_STATE_CHECKED);
  }

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
      String channel_name = payload_to_string(payload, length);
      lv_label_set_text(ui_Voice_Chat_Name, channel_name.c_str());
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
  
  strncpy(current_player_state.song_name, doc["title"] | "", sizeof(current_player_state.song_name));
  strncpy(current_player_state.artist_name, doc["artist"] | "", sizeof(current_player_state.artist_name));
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

static lv_obj_t* create_user_card(int width, int height, const char* name) {
  lv_obj_t *card = lv_obj_create(ui_DiscordInfo);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_set_width(card, width);
  lv_obj_set_height(card, height);
  lv_obj_set_style_outline_width(card, 2, LV_PART_MAIN);
  lv_obj_set_style_outline_pad(card, 3, LV_PART_MAIN);
  lv_obj_set_style_outline_color(card, lv_color_hex(0x23a559), LV_PART_MAIN);
  lv_obj_set_style_outline_opa(card, 0, LV_PART_MAIN); 

  lv_obj_t *avatar = lv_img_create(card);
  lv_obj_align(avatar, LV_ALIGN_TOP_MID, 0, 3);
  lv_obj_t *name_label = lv_label_create(card);
  lv_label_set_text(name_label, name);
  lv_obj_align(name_label, LV_ALIGN_BOTTOM_MID, 0, -3);
  return card;
}

void process_voice_users_json(String &discord_user_buffer){
  JsonDocument doc;
  if (deserializeJson(doc, discord_user_buffer)) return;

  int count = (int)(doc["count"]);
  int width = (int)(doc["width"]);
  int height = (int)(doc["height"]);

  if (width != last_card_width || height != last_card_height) {
    lv_obj_clean(ui_DiscordInfo);
    last_user_count = 0;
    last_card_width = width;
    last_card_height = height;
  }

  while (last_user_count > count) {
    lv_obj_t *card = lv_obj_get_child(ui_DiscordInfo, last_user_count - 1);
    if (card) lv_obj_del(card);
    last_user_count--;
  }

  for (int i = 0; i < count; i++) {
    const char* new_name = doc["users"][i]["name"];
    if (i < last_user_count) {
      if (last_user_names[i] != new_name) {
        lv_obj_t *card = lv_obj_get_child(ui_DiscordInfo, i);
        if (card) {
          lv_obj_t *name_label = lv_obj_get_child(card, 1);
          if (name_label) lv_label_set_text(name_label, new_name);
        }
        last_user_names[i] = new_name;
      }
    } else {
      create_user_card(width, height, new_name);
      last_user_names[i] = new_name;
    }
  }
  last_user_count = count;
}

void init_avatar_buffers() {
    shared_packet_buffer = (uint8_t*)heap_caps_malloc(MAX_PACKET_PAYLOAD, MALLOC_CAP_SPIRAM);
    
    if (shared_packet_buffer == nullptr){
      Serial.println("Fialed to allocate serial packet buffer");
    }
    
    for (int i = 0; i < 2; i++){
      album_pixel_buffers[i] = (uint16_t*)heap_caps_malloc(ALBUM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

      if(album_pixel_buffers[i] == nullptr){
        Serial.printf("Failed to allocate album buffer %d\n", i);
      }
    }

    for (int i = 0; i < MAX_DISCORD_USERS; i++) {
        avatar_pixel_buffers[i] = (uint16_t*)heap_caps_malloc(AVATAR_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

        if (avatar_pixel_buffers[i] == nullptr) continue;

        avatar_img_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        avatar_img_dsc[i].header.flags = 0;
        avatar_img_dsc[i].header.stride = AVATAR_WIDTH * 2;
        avatar_img_dsc[i].header.w = AVATAR_WIDTH;
        avatar_img_dsc[i].header.h = AVATAR_HEIGHT;
        avatar_img_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        avatar_img_dsc[i].data_size = AVATAR_BUFFER_SIZE;
        avatar_img_dsc[i].data = (uint8_t*)avatar_pixel_buffers[i];
    }
}

void process_avatar_message(uint8_t index, const uint8_t* jpeg_data, size_t jpeg_len){
  if (index >= MAX_DISCORD_USERS || avatar_pixel_buffers[index] == nullptr) return;

  if (!avatar_sprite_initialized){
    avatar_sprite.setColorDepth(16);
    avatar_sprite.setPsram(true); 
    if(avatar_sprite.createSprite(AVATAR_WIDTH, AVATAR_HEIGHT) == nullptr) return;
    avatar_sprite_initialized = true;
  }

  if (!avatar_sprite.drawJpg(jpeg_data, jpeg_len,0,0)){
    Serial.printf("Avatar JPEG decode failed: %lu bytes\n", (unsigned long)jpeg_len);
  }

  uint16_t *sprite_ptr = (uint16_t*)avatar_sprite.getBuffer();
  for (int i =0; i < AVATAR_HEIGHT * AVATAR_WIDTH; i++){
    uint16_t pixel = sprite_ptr[i];
    sprite_ptr[i] = (pixel >> 8) | (pixel << 8);
  }

  memcpy(avatar_pixel_buffers[index], sprite_ptr, AVATAR_BUFFER_SIZE);

  if (ui_DiscordInfo != nullptr){
    lv_obj_t *card = lv_obj_get_child(ui_DiscordInfo, index);
    if (card != nullptr){
      lv_obj_t *avatar_image_obj = lv_obj_get_child(card, 0);
      if (avatar_image_obj != nullptr){
        lv_image_set_src(avatar_image_obj, &avatar_img_dsc[index]);
        lv_obj_invalidate(avatar_image_obj);
      }
    }
  }
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
          Serial.printf("Invalid packet type: 0x%02x\n", byte);
          reset_serial_parser();
          break;
         }

         packet_type = byte;
         packet_length_index = 0;
         expected_payload_length = 0;
         serial_state = READ_PACKET_LENGTH;

         break;

      case READ_PACKET_LENGTH:
        packet_length_bytes[ packet_length_index++] = byte;

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