/*
 * Native test of the VLC port: the same core, modules and configuration as
 * the device (tests/Makefile builds them for Linux with ASan/UBSan, through
 * Docker elsewhere), with capture outputs in place of the Glaze ones
 * (tests/host/vlc). It plays the files in tests/fixtures/vlc
 * (tools/mkvlcfixtures.sh) and checks what comes out:
 *
 * - PCM WAV: the samples, exactly, against the file's data chunk.
 * - IMA ADPCM WAV, MP3 (libmad, stereo) and MP2 in MPEG-1: each tone's pitch,
 *   from its zero crossings, and the length.
 * - Motion-JPEG AVI (codec_mjpeg) and MPEG-1 (libmpeg2, i420_rgb): pictures
 *   shown, their size and the colour of the first one.
 * - An M3U playlist of two files.
 * - Over HTTP from a server thread here: an MP3, an ICY station (its stream
 *   title must reach the input's meta) and an M3U naming an HTTP stream.
 *
 * Run from tests/ (fixture paths are relative to it).
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <vlc/vlc.h>
#include <vlc/intf.h>
#include <vlc/input.h>

#include "capture.h"

/* libvlc.c's: the vlc_t behind a VLC_Create id, held. */
vlc_t *vlc_current_object(int i_object);

#define FIX "../tests/fixtures/vlc"

static int failures, checks;
static int vlc_id;
static int port;
static char title_seen[128];

#define CHECK(c, ...)                                                       \
    do {                                                                    \
        checks++;                                                           \
        if (!(c)) {                                                         \
            failures++;                                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    void *buf = NULL;

    *len = 0;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && (buf = malloc((size_t)n)) && fread(buf, 1, (size_t)n, f) == (size_t)n)
        *len = (size_t)n;
    fclose(f);
    return buf;
}

/* ---- playing ------------------------------------------------------------- */

/* The stream title of what is playing, if any. */
static void poll_title(void)
{
    vlc_t *p_vlc = vlc_current_object(vlc_id);
    playlist_t *pl;
    input_thread_t *in = NULL;

    if (!p_vlc)
        return;
    pl = vlc_object_find(p_vlc, VLC_OBJECT_PLAYLIST, FIND_CHILD);
    if (pl) {
        vlc_mutex_lock(&pl->object_lock);
        if ((in = pl->p_input))
            vlc_object_yield(in);
        vlc_mutex_unlock(&pl->object_lock);
        vlc_object_release(pl);
    }
    if (in) {
        char *t = vlc_input_item_GetInfo(in->input.p_item, VLC_META_INFO_CAT, VLC_META_NOW_PLAYING);
        if (t && *t)
            snprintf(title_seen, sizeof(title_seen), "%s", t);
        free(t);
        vlc_object_release(in);
    }
    vlc_object_release(p_vlc);
}

/* Plays one MRL to the end (or 30 s) into the capture outputs. */
static void play(const char *mrl)
{
    vlc_capture_reset();
    title_seen[0] = 0;
    VLC_PlaylistClear(vlc_id);
    VLC_AddTarget(vlc_id, mrl, NULL, 0, PLAYLIST_APPEND | PLAYLIST_GO, PLAYLIST_END);
    for (int i = 0; i < 100 && !VLC_IsPlaying(vlc_id); i++)
        usleep(50000);
    /* Between the items of a playlist nothing plays for a moment. */
    for (int i = 0, idle = 0; i < 600 && idle < 20; i++) {
        usleep(50000);
        poll_title();
        idle = VLC_IsPlaying(vlc_id) ? 0 : idle + 1;
    }
    VLC_Stop(vlc_id);
    usleep(200000);
    /* Leading silence, in frames: where the sound starts. */
    size_t lead = 0, ns = vlc_capture.pcm_len / 2;
    const int16_t *s = (const int16_t *)vlc_capture.pcm;
    while (lead < ns && s[lead] > -64 && s[lead] < 64)
        lead++;
    printf("%-40s %u Hz x%u, %llu frames (%zu silent first), crossings %u/%u, %d pictures %ux%u\n",
           mrl, vlc_capture.rate, vlc_capture.channels, (unsigned long long)vlc_capture.frames,
           lead / (vlc_capture.channels ? vlc_capture.channels : 1), vlc_capture.crossings[0],
           vlc_capture.crossings[1], vlc_capture.pictures, vlc_capture.width, vlc_capture.height);
}

/* The pitch of a channel from its zero crossings. */
static int pitch(unsigned ch)
{
    if (!vlc_capture.frames)
        return 0;
    return (int)((uint64_t)vlc_capture.crossings[ch] * vlc_capture.rate / (2 * vlc_capture.frames));
}

static void check_tone(const char *what, unsigned rate, unsigned channels, unsigned ms,
                       int hz0, int hz1)
{
    uint64_t want = (uint64_t)rate * ms / 1000;

    CHECK(vlc_capture.rate == rate, "%s: %u Hz, want %u", what, vlc_capture.rate, rate);
    CHECK(vlc_capture.channels == channels, "%s: %u channels, want %u", what, vlc_capture.channels,
          channels);
    CHECK(vlc_capture.frames >= want * 9 / 10 && vlc_capture.frames <= want * 12 / 10,
          "%s: %llu frames, want about %llu", what, (unsigned long long)vlc_capture.frames,
          (unsigned long long)want);
    CHECK(abs(pitch(0) - hz0) <= hz0 / 25, "%s: %d Hz tone, want %d", what, pitch(0), hz0);
    if (channels == 2)
        CHECK(abs(pitch(1) - hz1) <= hz1 / 25, "%s: right %d Hz, want %d", what, pitch(1), hz1);
    /* ffmpeg's sine source is at 1/8 of full scale: a peak near 4096. */
    CHECK(vlc_capture.peak > 3000, "%s: peak %d, too quiet", what, vlc_capture.peak);
}

static void check_picture(const char *what, int min_pictures, uint32_t rgb)
{
    uint16_t v = vlc_capture.first_center;
    int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
    int wr = (int)(rgb >> 16), wg = (int)(rgb >> 8 & 255), wb = (int)(rgb & 255);

    CHECK(vlc_capture.pictures >= min_pictures, "%s: %d pictures, want at least %d", what,
          vlc_capture.pictures, min_pictures);
    CHECK(vlc_capture.width == 64 && vlc_capture.height == 48, "%s: %ux%u, want 64x48", what,
          vlc_capture.width, vlc_capture.height);
    CHECK(abs(r - wr) <= 24 && abs(g - wg) <= 24 && abs(b - wb) <= 24,
          "%s: colour %02x%02x%02x, want %06x", what, r, g, b, (unsigned)rgb);
}

/* The PCM WAV must come out sample for sample. */
static void check_pcm_exact(void)
{
    size_t len;
    uint8_t *wav = read_file(FIX "/pcm.wav", &len);
    size_t off = 12, data = 0, data_len = 0;

    while (wav && off + 8 <= len) {
        uint32_t n = (uint32_t)wav[off + 4] | (uint32_t)wav[off + 5] << 8 |
                     (uint32_t)wav[off + 6] << 16 | (uint32_t)wav[off + 7] << 24;
        if (!memcmp(wav + off, "data", 4)) {
            data = off + 8;
            data_len = n < len - data ? n : len - data;
            break;
        }
        off += 8 + n + (n & 1);
    }
    CHECK(data_len > 0, "pcm.wav: no data chunk");
    size_t n = vlc_capture.pcm_len < data_len ? vlc_capture.pcm_len : data_len;
    /* All but the last partial output buffer (up to about 100 ms). */
    CHECK(n >= data_len * 9 / 10, "pcm.wav: %zu of %zu bytes played", n, data_len);
    size_t diff = 0;
    while (diff < n && wav[data + diff] == vlc_capture.pcm[diff])
        diff++;
    CHECK(diff == n, "pcm.wav: samples differ from byte %zu of %zu", diff, n);
    free(wav);
}

/* ---- the HTTP server ------------------------------------------------------- */

#define METAINT 2048

static uint8_t *mp3;
static size_t mp3_len;
static int icy_served;

static void send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;

    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0)
            return;
        p += n;
        len -= (size_t)n;
    }
}

static void send_icy(int fd)
{
    static const char hdr[] = "ICY 200 OK\r\nicy-name: Test Radio\r\ncontent-type: audio/mpeg\r\n"
                              "icy-metaint: 2048\r\n\r\n";
    char meta[1 + 48] = { 0 };
    static const char none = 0;         /* no new metadata */
    const char *title = "StreamTitle='Test Title';";

    meta[0] = (char)((strlen(title) + 15) / 16);
    memcpy(meta + 1, title, strlen(title));
    send_all(fd, hdr, sizeof(hdr) - 1);
    for (size_t off = 0; off < mp3_len; off += METAINT) {
        size_t n = mp3_len - off < METAINT ? mp3_len - off : METAINT;
        send_all(fd, mp3 + off, n);
        if (n == METAINT) {
            if (off)
                send_all(fd, &none, 1);
            else
                send_all(fd, meta, 1 + (size_t)meta[0] * 16);
        }
        /* At the stream's own rate, 64 kbit/s, as a station sends. */
        usleep((useconds_t)(n * 1000000 / 8000));
    }
}

static void *serve_one(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char req[2048], path[256] = "", body[256];
    size_t got = 0;

    while (got < sizeof(req) - 1) {
        ssize_t n = recv(fd, req + got, sizeof(req) - 1 - got, 0);
        if (n <= 0)
            break;
        got += (size_t)n;
        req[got] = 0;
        if (strstr(req, "\r\n\r\n"))
            break;
    }
    req[got] = 0;
    sscanf(req, "GET %255s", path);
    if (!strcmp(path, "/tone.mp3")) {
        int n = snprintf(body, sizeof(body), "HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
                         "Content-Length: %zu\r\n\r\n", mp3_len);
        send_all(fd, body, (size_t)n);
        send_all(fd, mp3, mp3_len);
    } else if (!strcmp(path, "/icy") && !__atomic_exchange_n(&icy_served, 1, __ATOMIC_SEQ_CST)) {
        send_icy(fd);
    } else if (!strcmp(path, "/list.m3u")) {
        char list[128];
        int ln = snprintf(list, sizeof(list), "#EXTM3U\nhttp://127.0.0.1:%d/tone.mp3\n", port);
        int n = snprintf(body, sizeof(body), "HTTP/1.0 200 OK\r\nContent-Type: audio/x-mpegurl\r\n"
                         "Content-Length: %d\r\n\r\n", ln);
        send_all(fd, body, (size_t)n);
        send_all(fd, list, (size_t)ln);
    } else {
        static const char nf[] = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        send_all(fd, nf, sizeof(nf) - 1);
    }
    close(fd);
    return NULL;
}

static void *serve(void *arg)
{
    int lfd = (int)(intptr_t)arg;

    for (;;) {
        int fd = accept(lfd, NULL, NULL);
        pthread_t t;
        if (fd < 0)
            return NULL;
        if (pthread_create(&t, NULL, serve_one, (void *)(intptr_t)fd) == 0)
            pthread_detach(t);
        else
            close(fd);
    }
}

static int start_server(void)
{
    struct sockaddr_in a = { 0 };
    socklen_t al = sizeof(a);
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    pthread_t t;

    mp3 = read_file(FIX "/tone.mp3", &mp3_len);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (lfd < 0 || !mp3 || bind(lfd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(lfd, 8) < 0 ||
        getsockname(lfd, (struct sockaddr *)&a, &al) < 0)
        return -1;
    port = ntohs(a.sin_port);
    return pthread_create(&t, NULL, serve, (void *)(intptr_t)lfd) == 0 ? 0 : -1;
}

/* ---- main ------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    char *args[] = {
        "vlc", "--intf", "dummy", "--aout", "capture", "--vout", "capture",
        "--no-plugins-cache", "--verbose", getenv("VLC_VERBOSE") ? getenv("VLC_VERBOSE") : "0",
    };
    char url[128];
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IOLBF, 0);
    /* VLC keeps its configuration under HOME. */
    setenv("HOME", "../out/host/vlc-home", 1);
    vlc_id = VLC_Create();
    if (vlc_id < 0 || VLC_Init(vlc_id, (int)(sizeof(args) / sizeof(args[0])), args) != VLC_SUCCESS) {
        printf("FAIL: VLC did not start\n");
        return 1;
    }
    printf("%s\n", VLC_Version());

    play(FIX "/pcm.wav");
    check_tone("pcm.wav", 22050, 1, 1000, 440, 0);
    check_pcm_exact();

    play(FIX "/adpcm.wav");
    check_tone("adpcm.wav", 22050, 1, 1000, 550, 0);

    play(FIX "/tone.mp3");
    check_tone("tone.mp3", 44100, 2, 1000, 440, 660);

    play(FIX "/mjpeg.avi");
    check_picture("mjpeg.avi", 5, 0x3060C0);
    check_tone("mjpeg.avi audio", 8000, 1, 1000, 440, 0);

    play(FIX "/mpeg1.mpg");
    check_picture("mpeg1.mpg", 12, 0xC03020);
    /* VLC 0.8's program stream path drops the first ~0.27 s of the MP2
     * track (measured with a delayed tone: the lead-in silence comes out
     * that much short), so about 0.75 s of the 1.02 s plays. */
    check_tone("mpeg1.mpg audio", 44100, 1, 750, 440, 0);

    play(FIX "/list.m3u");
    CHECK(vlc_capture.frames >= 22050 * 2 * 9 / 10, "list.m3u: %llu frames, want both files",
          (unsigned long long)vlc_capture.frames);

    if (start_server() < 0) {
        CHECK(0, "cannot start the HTTP server");
    } else {
        snprintf(url, sizeof(url), "http://127.0.0.1:%d/tone.mp3", port);
        play(url);
        check_tone("tone.mp3 over HTTP", 44100, 2, 1000, 440, 660);

        snprintf(url, sizeof(url), "http://127.0.0.1:%d/icy", port);
        play(url);
        CHECK(!strcmp(title_seen, "Test Title"), "ICY: stream title '%s', want 'Test Title'",
              title_seen);
        CHECK(vlc_capture.frames >= 44100 / 2, "ICY: %llu frames",
              (unsigned long long)vlc_capture.frames);
        CHECK(abs(pitch(0) - 440) <= 440 / 25, "ICY: %d Hz tone, want 440", pitch(0));

        snprintf(url, sizeof(url), "http://127.0.0.1:%d/list.m3u", port);
        play(url);
        check_tone("M3U over HTTP", 44100, 2, 1000, 440, 660);
    }

    VLC_CleanUp(vlc_id);
    VLC_Destroy(vlc_id);
    vlc_capture_reset();
    free(mp3);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
