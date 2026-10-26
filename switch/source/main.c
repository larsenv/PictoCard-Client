/* PictoCard for Switch — send PictoCards by email or to a Wii Message Board.
 * https://pictocard.net */
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "platform.h"
#include "app.h"
#include "api.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "nand.h"
#include "settings.h"
#include "sfx.h"
#include "ui.h"
#include "wiidata.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    /* SDL runs the screen, audio and the USB keyboard; the controllers and the
     * touch screen are read directly (input.c) */
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    gfx_init();
    input_init();
    input_set_canvas(SW, SH);
    settings_load(NULL);
    font_init();
    ui_init();
    ui_set_theme(settings.light);
    nand_init();
    wiidata_load();
    api_init();
    srand((unsigned)gettime());
    sfx_init();

    app_run();
    camera_shutdown();

    sfx_shutdown();
    nand_shutdown();
    input_shutdown();
    gfx_shutdown();
    SDL_Quit(); /* back to the Homebrew Menu */
    return 0;
}
