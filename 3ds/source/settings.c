#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "version.h"

#define DIR_PATH "sdmc:/3ds/PictoCard"
#define FILE_PATH DIR_PATH "/settings.txt"

settings_t settings;

void settings_load(void)
{
    snprintf(settings.server, sizeof settings.server, "%s", DEFAULT_SERVER);
    FILE *f = fopen(FILE_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(line, "server") && *v) snprintf(settings.server, sizeof settings.server, "%s", v);
        else if (!strcmp(line, "name")) snprintf(settings.sender_name, sizeof settings.sender_name, "%s", v);
        else if (!strcmp(line, "email")) snprintf(settings.sender_email, sizeof settings.sender_email, "%s", v);
        else if (!strcmp(line, "wii")) {
            /* digits only, so "1234-5678-9012-3456" works too */
            int n = 0;
            for (; *v && n < (int)sizeof settings.wii_number - 1; v++)
                if (*v >= '0' && *v <= '9') settings.wii_number[n++] = *v;
            settings.wii_number[n] = 0;
        } else if (!strcmp(line, "theme")) settings.light = !strcmp(v, "light");
    }
    fclose(f);
    size_t n = strlen(settings.server);
    while (n && settings.server[n - 1] == '/') settings.server[--n] = 0;
}

void settings_save(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(DIR_PATH, 0777);
    FILE *f = fopen(FILE_PATH, "w");
    if (!f) return;
    if (strcmp(settings.server, DEFAULT_SERVER)) fprintf(f, "server=%s\n", settings.server);
    fprintf(f, "name=%s\nemail=%s\nwii=%s\ntheme=%s\n", settings.sender_name, settings.sender_email,
            settings.wii_number, settings.light ? "light" : "dark");
    fclose(f);
}
