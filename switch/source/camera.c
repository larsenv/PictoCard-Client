/* The Wii U version takes photos with the GamePad's camera. The Switch has no
 * camera app programs can use, so these do nothing; the picture list doesn't
 * offer the camera. */
#include "app.h"

void camera_open(camera_cb cb, void (*on_error)(const char *message))
{
    (void)cb;
    if (on_error) on_error("The Switch has no camera to use.");
}
int camera_active(void) { return 0; }
void camera_update(void *input) { (void)input; }
void camera_draw(int frame) { (void)frame; }
void camera_shutdown(void) {}
