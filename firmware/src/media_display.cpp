#include "media_display.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include "LovyanGFX_Driver.h"
#include "music_player_logic.h"
#include "ui.h"

#define ALBUM_WIDTH 250
#define ALBUM_HEIGHT 250
#define ALBUM_BUFFER_SIZE (ALBUM_WIDTH * ALBUM_HEIGHT * 2)

extern LGFX gfx;

extern bool user_is_seeking;
extern uint32_t seek_lockout_timer;
extern int currentVolume;
extern uint32_t vol_lockout_timer;

extern bool is_system_muted;
extern void update_mute_visuals();

static uint16_t* album_pixel_buffers[2] = { nullptr, nullptr };
static lv_image_dsc_t received_img_dsc[2];
static int dsc_index = 0;
static LGFX_Sprite art_sprite(&gfx);
static bool sprite_initialized = false;


static void fade_anim_cb(void * obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, v, LV_PART_MAIN);
}


void process_track_json(String &json_str) {
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


void process_album_art(const uint8_t *jpeg_data, uint32_t length) {
  if (!sprite_initialized) {
    art_sprite.setPsram(true);
    art_sprite.setColorDepth(16); 
    if (art_sprite.createSprite(ALBUM_WIDTH , ALBUM_HEIGHT) == nullptr) return;
    sprite_initialized = true;
  }

  if (!art_sprite.drawJpg(jpeg_data, length, 0, 0)){
    Serial.printf("Album JPEG decode failed: %lu bytes\n", (unsigned long)length);
    return;
  } 

  uint16_t* pixels = (uint16_t*)art_sprite.getBuffer();
  for (int i = 0; i < ALBUM_WIDTH * ALBUM_HEIGHT; i++) {
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


void init_media_display() {
    
    for (int i = 0; i < 2; i++){
      album_pixel_buffers[i] = (uint16_t*)heap_caps_malloc(ALBUM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

      if(album_pixel_buffers[i] == nullptr){
        Serial.printf("Failed to allocate album buffer %d\n", i);
      }
    }
}

 