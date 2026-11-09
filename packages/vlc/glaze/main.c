/*
 * VLC for Glaze: the application entry. It boots libvlc with the Glaze
 * audio and video outputs and runs the Glaze interface (intf_glaze) on this,
 * the app's main thread, until VLC is told to quit. Glaze starts apps with
 * no arguments (main takes none), so the options are fixed here. ui_init
 * comes first: it sets the bundle path that HOME (getenv in libc.c), and so
 * VLC's configuration directory, is under.
 */
#include <glaze/ui.h>
#include <stdio.h>
#include <vlc/vlc.h>

int main(void)
{
    ui_init(SB_STATUS_BLACK);

    char *args[] = {
        "vlc", "--aout", "glaze", "--vout", "glaze",
        "--no-plugins-cache", "--verbose", "1",
    };
    int n = (int)(sizeof(args) / sizeof(args[0]));

    int id = VLC_Create();
    if (id < 0) {
        fprintf(stderr, "vlc: create failed: %s\n", VLC_Error(id));
        return 1;
    }
    int r = VLC_Init(id, n, args);
    if (r != VLC_SUCCESS) {
        fprintf(stderr, "vlc: init failed: %s\n", VLC_Error(r));
        VLC_Destroy(id);
        return 1;
    }
    fprintf(stderr, "vlc: %s ready\n", VLC_Version());
    r = VLC_AddIntf(id, "glaze", VLC_TRUE, VLC_FALSE);
    if (r != VLC_SUCCESS)
        fprintf(stderr, "vlc: no interface: %s\n", VLC_Error(r));
    VLC_CleanUp(id);
    VLC_Destroy(id);
    fprintf(stderr, "vlc: exited\n");
    return 0;
}
