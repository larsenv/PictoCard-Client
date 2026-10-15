#include <3ds.h>
#include "app.h"
#include "gfx.h"
#include "net.h"

int main(void)
{
    gfx_init();
    app_run();
    net_stop();
    gfx_exit();
    return 0;
}
