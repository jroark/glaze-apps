/*
 * VLC for Glaze: the application entry. For now (Phase 1) it boots libvlc
 * with the dummy interface and outputs, reports to the debug log, and exits;
 * the libui interface replaces the dummy one in Phase 5. Glaze starts apps
 * with no arguments (main takes none), so the options are fixed here.
 * ui_init comes first: it sets the bundle path that HOME (getenv in libc.c),
 * and so VLC's configuration directory, is under.
 */
#include <glaze/ui.h>
#include <vlc/vlc.h>

int main(void)
{
    ui_init(SB_STATUS_GRAY);

    char *args[] = {
        "vlc", "--intf", "dummy", "--aout", "dummy", "--vout", "dummy",
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
    VLC_CleanUp(id);
    VLC_Destroy(id);
    fprintf(stderr, "vlc: exited cleanly\n");
    return 0;
}
