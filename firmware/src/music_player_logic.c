#include "music_player_logic.h"

player_state_t current_player_state;
bool user_is_seeking = false;

static void playback_tick_cb(lv_timer_t *timer);

void update_slider_and_labels(void) {
    // Ensure the slider's max value matches the current song's duration
    lv_slider_set_range(ui_SongPositionSlider, 0, current_player_state.duration_seconds);

    if (!user_is_seeking) {
        lv_slider_set_value(ui_SongPositionSlider, current_player_state.position_seconds, LV_ANIM_OFF);
    }
    
    lv_label_set_text_fmt(ui_Current_Time_T1, "%d:%02d",
        current_player_state.position_seconds / 60, current_player_state.position_seconds % 60);
        
    lv_label_set_text_fmt(ui_Total_Time_T2, "%d:%02d",
        current_player_state.duration_seconds / 60, current_player_state.duration_seconds % 60);
}

void sync_play_pause_visuals(void) {
    if (current_player_state.is_playing) {
        // PLAYING: Set to CHECKED (Pause Icon) and force LVGL to redraw
        if (ui_PlayPauseButton != NULL) {
            lv_obj_add_state(ui_PlayPauseButton, LV_STATE_CHECKED);
            lv_obj_invalidate(ui_PlayPauseButton); 
        }
        if (ui_PlayPauseButton3 != NULL) {
            lv_obj_add_state(ui_PlayPauseButton3, LV_STATE_CHECKED);
            lv_obj_invalidate(ui_PlayPauseButton3); 
        }
    } else {
       
        if (ui_PlayPauseButton != NULL) {
            lv_obj_clear_state(ui_PlayPauseButton, LV_STATE_CHECKED);
            lv_obj_invalidate(ui_PlayPauseButton);
        }
        if (ui_PlayPauseButton3 != NULL) {
            lv_obj_clear_state(ui_PlayPauseButton3, LV_STATE_CHECKED);
            lv_obj_invalidate(ui_PlayPauseButton3);
        }
    }
}

static void playback_tick_cb(lv_timer_t *timer) {
    if (current_player_state.is_playing && !user_is_seeking) {
        current_player_state.position_seconds++;
        
        if (current_player_state.position_seconds > current_player_state.duration_seconds) {
            current_player_state.position_seconds = current_player_state.duration_seconds;
        }
        
        update_slider_and_labels();
    }
}

void music_player_init(void) {
    current_player_state.is_playing = false;
    current_player_state.volume_level = 25;
    current_player_state.position_seconds = 0;
    current_player_state.duration_seconds = 100; 
    
    lv_timer_create(playback_tick_cb, 1000, NULL);
    
    lv_label_set_text(ui_Song_Name, "Waiting for PC...");
    lv_label_set_text(ui_Artist_Name, "");

    sync_play_pause_visuals();
}