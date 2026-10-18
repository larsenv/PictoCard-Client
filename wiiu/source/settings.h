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
