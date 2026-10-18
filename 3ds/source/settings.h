#pragma once

typedef struct {
    char server[128];
    char sender_name[64];
    char sender_email[128];
    char wii_number[20]; /* the Wii letters go to, kept between runs */
    int light;
} settings_t;

extern settings_t settings;
void settings_load(void);
void settings_save(void);
