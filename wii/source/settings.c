#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "settings.h"
#include "version.h"

settings_t settings;
static char path[256] = "sd:/apps/PictoCard/settings.txt";

void settings_load(const char *argv0)
{
    snprintf(settings.server, sizeof settings.server, "%s", DEFAULT_SERVER);
    if (argv0 && strchr(argv0, '/') && (!strncmp(argv0, "sd:", 3) || !strncmp(argv0, "usb:", 4))) {
        snprintf(path, sizeof path, "%s", argv0);
        char *slash = strrchr(path, '/');
        snprintf(slash + 1, sizeof path - (slash + 1 - path), "settings.txt");
    }
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
    FILE *f = fopen(path, "w");
    if (!f) return;
    if (strcmp(settings.server, DEFAULT_SERVER)) fprintf(f, "server=%s\n", settings.server);
    fprintf(f, "name=%s\nemail=%s\ntheme=%s\n", settings.sender_name, settings.sender_email,
            settings.light ? "light" : "dark");
    fclose(f);
}

const char *settings_dir(void)
{
    static char dir[256];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) slash[1] = 0;
    return dir;
}
