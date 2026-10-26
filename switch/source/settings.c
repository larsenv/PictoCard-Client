#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"
#include "settings.h"
#include "version.h"

settings_t settings;
static const char path[] = APP_DIR "/settings.txt";

void settings_load(const char *argv0)
{
    snprintf(settings.server, sizeof settings.server, "%s", DEFAULT_SERVER);
    (void)argv0; /* a .nro is found by its name: settings live in sd:/switch/PictoCard */
    FILE *f = fopen(path, "r");
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
        else if (!strcmp(line, "theme")) settings.light = !strcmp(v, "light");
    }
    fclose(f);
    size_t n = strlen(settings.server);
    while (n && settings.server[n - 1] == '/') settings.server[--n] = 0;
}

void settings_save(void)
{
    mkdir(APP_PARENT, 0777);
    mkdir(APP_DIR, 0777);
    FILE *f = fopen(path, "w");
    if (!f) return;
    if (strcmp(settings.server, DEFAULT_SERVER)) fprintf(f, "server=%s\n", settings.server);
    fprintf(f, "name=%s\nemail=%s\ntheme=%s\n", settings.sender_name, settings.sender_email,
            settings.light ? "light" : "dark");
    fclose(f);
}
