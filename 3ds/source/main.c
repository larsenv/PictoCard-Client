#include <3ds.h>
#include <3dsupdate.h>
#include "app.h"
#include "gfx.h"
#include "net.h"
#include "version.h"

int main(int argc, char **argv)
{
    gfx_init();
    CTRUPDATE_SetAppPath(argc > 0 ? argv[0] : NULL);
    CTRUPDATE_Start("larsenv", "PictoCard-Client", APP_VERSION);
    app_run();

    /* let the update check thread finish before the network goes away */
    CTRUPDATE_Cancel();
    CTRUPDATE_Wait(5000);

    net_stop();
    gfx_exit();
    return 0;
}
