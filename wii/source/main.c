/* PictoCard for Wii — send PictoCards by email or to a Wii Message Board.
 * https://pictocard.net */
#include <stdlib.h>
#include <gccore.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <wiiupdate.h>
#include "app.h"
#include "api.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "settings.h"
#include "sfx.h"
#include "ui.h"
#include "wiidata.h"

int main(int argc, char **argv)
{
    gfx_init();
    input_init();
    input_set_canvas(SW, SH);
    fatInitDefault();
    WIIUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    WIIUPDATE_Start("larsenv", "PictoCard-Client", APP_VERSION);
    settings_load(argc > 0 ? argv[0] : NULL);
    font_init();
    ui_init();
    ui_set_theme(settings.light);
    wiidata_load();
    api_init();
    srand(gettime());
    sfx_init();

    app_run();

    sfx_shutdown();
    input_shutdown();
    return 0;
}
