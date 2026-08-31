#pragma once

#include <Arduino.h>

void init_discord_ui_buffers();

void process_discord_state_packet(const uint8_t *payload, uint32_t length);

void process_voice_users_json(String &discord_user_buffer);

void process_avatar_message(uint8_t index, const uint8_t *jpeg_data, size_t jpeg_len);

void set_voice_channel_name(const uint8_t *payload, uint32_t length);