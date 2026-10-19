#pragma once

typedef struct {
    char server[128];
    char sender_name[64];
    char sender_email[128];
    int light;
} settings_t;

extern settings_t settings;
void settings_load(const char *argv0);
void settings_save(void);
/* the app's own folder on the SD card / USB drive, with a trailing slash */
const char *settings_dir(void);
