/*
 * VLC's configuration for Glaze, written by hand in place of autoconf's
 * config.h. Only what Glaze's libc, the compat layer (compat.h) and the
 * SDK provide is declared present; everything else takes upstream's
 * fallback. Modules are builtins only: no plugin loader, no NLS, no FPU.
 */
#ifndef GLAZE_VLC_CONFIG_H
#define GLAZE_VLC_CONFIG_H

#define PACKAGE                 "vlc"
#define PACKAGE_NAME            "vlc"
#define PACKAGE_STRING          "vlc 0.8.6i"
#define PACKAGE_TARNAME         "vlc"
#define PACKAGE_VERSION         "0.8.6i"
#define PACKAGE_BUGREPORT       ""
#define VERSION                 "0.8.6i"
#define VERSION_MESSAGE         "vlc 0.8.6i Janus (Glaze)"
#define COPYRIGHT_MESSAGE       "VLC media player - version 0.8.6i Janus - (c) 1996-2008 the VideoLAN team"
#define CONFIGURE_LINE          "Glaze SDK build"
#define VLC_COMPILE_BY          "glaze"
#define VLC_COMPILE_HOST        "glaze"
#define VLC_COMPILE_DOMAIN      "glaze"
#define VLC_COMPILER            "gcc"
#define VLC_CHANGESET           "0.8.6i"
#define DISTRO_VERSION          "Glaze"
#define MODULE_SUFFIX           ""
#define LIBEXT                  ""
#define PLUGIN_PATH             ""
#define DATA_PATH               "/"
#define LOCALEDIR               "/"

/* The thread and clock backends added for Glaze (vlc_threads.h, mtime.c). */
#define GLAZE_THREADS           1

/* The compiler (GCC). */
#define HAVE_VARIADIC_MACROS    1
#define HAVE_ATTRIBUTE_PACKED   1
#define HAVE_ATTRIBUTE_FORMAT   1
#define ATTRIBUTE_ALIGNED_MAX   8

/* Headers Glaze's libc or glaze/sysinc provides. */
#define STDC_HEADERS            1
#define HAVE_STDDEF_H           1
#define HAVE_STDINT_H           1
#define HAVE_INTTYPES_H         1
#define HAVE_STDLIB_H           1
#define HAVE_STRING_H           1
#define HAVE_STRINGS_H          1
#define HAVE_UNISTD_H           1
#define HAVE_FCNTL_H            1
#define HAVE_ERRNO_H            1
#define HAVE_LIMITS_H           1
#define HAVE_SYS_TYPES_H        1
#define HAVE_SYS_STAT_H         1
#define HAVE_SYS_SOCKET_H       1
#define HAVE_NETINET_IN_H       1
#define HAVE_ARPA_INET_H        1
#define HAVE_NETDB_H            1
#define HAVE_POLL_H             1
#define HAVE_DIRENT_H           1
#define HAVE_SYS_TIME_H         1
#define HAVE_TIME_H             1

/* Functions available (natively or from glaze/compat.c). */
#define HAVE_STRDUP             1
#define HAVE_STRNDUP            1
#define HAVE_STRCASECMP         1
#define HAVE_STRNCASECMP        1
#define HAVE_STRSEP             1
#define HAVE_STRTOL             1
#define HAVE_STRTOD             1
#define HAVE_INET_PTON          1
#define HAVE_STRTOLL            1
#define HAVE_ATOF               1
#define HAVE_STRTOF             1
#define HAVE_ATOLL              1
#define HAVE_LLDIV              1
#define HAVE_SNPRINTF           1
#define HAVE_GETENV             1
#define HAVE_GETADDRINFO        1
#define HAVE_GETNAMEINFO        1
#define HAVE_GAI_STRERROR       1
#define HAVE_STRUCT_ADDRINFO    1
#define HAVE_STRUCT_TIMESPEC    1
#define HAVE_GETTIMEOFDAY       1
#define HAVE_LOCALTIME_R        1
#define HAVE_SOCKLEN_T          1
#define HAVE_STRLCPY            1

/* VLC's own fallbacks (src/extras/libc.c) supply strcasestr, vasprintf,
 * asprintf, scandir and alphasort.
 *
 * Deliberately absent: HAVE_DYNAMIC_PLUGINS, HAVE_FPU, ENABLE_NLS, ENABLE_SOUT,
 * ENABLE_VLM, ENABLE_HTTPD, HAVE_ICONV, HAVE_SIGNAL_H, HAVE_SETLOCALE. */

/* configure's --enable-optimize-memory, as the Windows CE builds had: a
 * 128 KiB stream cache instead of 12 MiB, and a 5-picture video heap. */
#define OPTIMIZE_MEMORY         1

#define DEFAULT_CODESET         "UTF-8"
#define WORDS_BIGENDIAN_IS_NOT_SET
#define CAN_COMPILE_C_ALTIVEC_IS_NOT_SET

#endif
