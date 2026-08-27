#pragma once
#include "lvgl.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char song_name[64];
    char artist_name[64];
    int position_seconds;
    int duration_seconds;
    int volume_level;
    bool is_playing;
    const lv_img_dsc_t *album_art;
} player_state_t;

extern player_state_t current_player_state;
 

void music_player_init(void);
void update_slider_and_labels(void);
void switch_to_discord_screen(lv_event_t * e);
void switch_to_music_screen(lv_event_t * e);
void register_screen_switch_handlers(void);
void sync_play_pause_visuals(void);

#ifdef __cplusplus
}
#endif