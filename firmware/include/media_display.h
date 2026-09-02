#pragma once

#include <Arduino.h>

void init_media_display();
void process_track_json(String &json_str);
void process_album_art( const uint8_t *jpeg_data, uint32_t length);
void update_mute_visuals();