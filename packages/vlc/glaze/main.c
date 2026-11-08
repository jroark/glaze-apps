/*
 * VLC for Glaze: the application entry. Until the libui interface (Phase 5)
 * replaces it, it boots libvlc with the dummy interface and the Glaze audio
 * and video outputs, and, as a test hook, plays /Card/autoplay.m3u to
 * the end if that playlist exists, reporting to the debug log. Glaze starts
 * apps with no arguments (main takes none), so the options are fixed here.
 * ui_init comes first: it sets the bundle path that HOME (getenv in libc.c),
 * and so VLC's configuration directory, is under.
 */
#include <glaze/audio.h>
#include <glaze/sys.h>
#include <glaze/ui.h>
#include <sys/stat.h>
#include <vlc/vlc.h>

#define AUTOPLAY "/Card/autoplay.m3u"

static void play_autoplay(int id)
{
    struct stat st;
    if (stat(AUTOPLAY, &st) != 0)
        return;

    struct audio_state before, after;
    int have_state = audio_get_state(&before) == 0;
    uint64_t start = gz_time_us();

    VLC_AddTarget(id, AUTOPLAY, NULL, 0, PLAYLIST_APPEND | PLAYLIST_GO, PLAYLIST_END);
    for (int i = 0; i < 100 && !VLC_IsPlaying(id); i++)
        gz_sleep_ms(50);
    /* Between items nothing is playing for a moment: allow 3 s for the next. */
    for (int idle = 0; idle < 30; ) {
        gz_sleep_ms(100);
        if (!VLC_IsPlaying(id)) {
            idle++;
            continue;
        }
        idle = 0;
        static uint64_t next;
        if (gz_time_us() >= next) {
            fprintf(stderr, "vlc: playing %d/%d s\n", VLC_TimeGet(id), VLC_LengthGet(id));
            next = gz_time_us() + 1000000;
        }
    }
    fprintf(stderr, "vlc: playlist done after %d ms", (int)((gz_time_us() - start) / 1000));
    if (have_state && audio_get_state(&after) == 0)
        fprintf(stderr, ", %u underruns", (unsigned)(after.underruns - before.underruns));
    fprintf(stderr, "\n");
}

int main(void)
{
    ui_init(SB_STATUS_GRAY);

    char *args[] = {
        "vlc", "--intf", "dummy", "--aout", "glaze", "--vout", "glaze",
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
    play_autoplay(id);
    VLC_CleanUp(id);
    VLC_Destroy(id);
    fprintf(stderr, "vlc: exited cleanly\n");
    return 0;
}
