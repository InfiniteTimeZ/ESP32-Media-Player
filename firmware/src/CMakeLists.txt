SET(SOURCES components/ui_comp_image2.c
    components/ui_comp_panel1.c
    components/ui_comp.c
    screens/ui_Music_Screen.c
    screens/ui_Discord_Screen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.cpp
    images/ui_img_backbutton_png.c
    images/ui_img_1518759221.c
    images/ui_img_2115154753.c
    images/ui_img_nextbutton_png.c
    images/ui_img_anim_25_png.c
    images/ui_img_2111856141.c
    images/ui_img_1902415918.c
    images/ui_img_231780434.c
    images/ui_img_micnomutemini_png.c
    images/ui_img_micmutedmini_png.c
    images/ui_img_headphonesnodeafenmini_png.c
    images/ui_img_headphonesdeafenedmini_png.c
    images/ui_img_endcallnotogglemini_png.c
    images/ui_img_endcallleftmini_png.c
    images/ui_img_backbuttonmini_png.c
    images/ui_img_378055568.c
    images/ui_img_1909143296.c
    images/ui_img_nextbuttonmini_png.c
    images/ui_img_1771568924.c
    images/ui_img_micnomute_png.c
    images/ui_img_micmuted_png.c
    images/ui_img_headphonesnodeafen_png.c
    images/ui_img_headphonesdeafened_png.c
    images/ui_img_endcallnotoggle_png.c
    images/ui_img_endcallleft_png.c)

add_library(ui ${SOURCES})
