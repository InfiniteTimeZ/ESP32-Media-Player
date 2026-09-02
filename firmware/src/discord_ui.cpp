#include "discord_ui.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include "LovyanGFX_Driver.h"
#include "ui.h"

#define MAX_DISCORD_USERS 15
#define AVATAR_WIDTH 75
#define AVATAR_HEIGHT 75
#define AVATAR_BUFFER_SIZE (AVATAR_WIDTH * AVATAR_HEIGHT * 2)

static String last_user_names[MAX_DISCORD_USERS];
static int last_user_count = 0;
static int last_card_width = -1;
static int last_card_height = -1;


static lv_image_dsc_t avatar_img_dsc[MAX_DISCORD_USERS];
static uint16_t *avatar_pixel_buffers[MAX_DISCORD_USERS] = { nullptr };
static bool avatar_sprite_initialized = false;

extern LGFX gfx;
static LGFX_Sprite avatar_sprite(&gfx);



void process_discord_state_packet(const uint8_t* payload, uint32_t length){
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

  int total_count = max(0, (int)(doc["count"]));
  int display_count = min(total_count, MAX_DISCORD_USERS);
  int width = (int)(doc["width"]);
  int height = (int)(doc["height"]);

  if (width != last_card_width || height != last_card_height) {
    lv_obj_clean(ui_DiscordInfo);
    last_user_count = 0;
    last_card_width = width;
    last_card_height = height;
  }

  while (last_user_count > display_count) {
    lv_obj_t *card = lv_obj_get_child(ui_DiscordInfo, last_user_count - 1);
    if (card) lv_obj_del(card);
    last_user_count--;
  }

  for (int i = 0; i < display_count; i++) {
    const char *new_name = doc["users"][i]["name"] | "";

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
  last_user_count = display_count;
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
    return;
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

void set_voice_channel_name(const uint8_t *payload, uint32_t length){
    String channel_name;
    channel_name.reserve(length);

    for (uint32_t i = 0; i < length; i++) {
        channel_name += (char)payload[i];
    }

     lv_label_set_text(ui_Voice_Chat_Name, channel_name.c_str());

}


void init_discord_ui_buffers(){
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