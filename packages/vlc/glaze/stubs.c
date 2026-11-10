/*
 * Core entry points of the parts of VLC this build leaves out: stream output
 * (and its SAP announces), the OSD menu's configuration parser, and the
 * privileged-port helper. Each fails the way upstream does when the module
 * behind it is missing, so callers take their existing error paths.
 */
#include <errno.h>

#include <vlc/vlc.h>
#include <vlc/sout.h>
#include <vlc_osd.h>

sout_instance_t *__sout_NewInstance(vlc_object_t *p_parent, char *psz_dest)
{
    msg_Err(p_parent, "stream output is not available (%s)", psz_dest ? psz_dest : "");
    return NULL;
}

void sout_DeleteInstance(sout_instance_t *p_sout)
{
    (void)p_sout;
}

sout_packetizer_input_t *sout_InputNew(sout_instance_t *p_sout, es_format_t *p_fmt)
{
    (void)p_sout;
    (void)p_fmt;
    return NULL;
}

int sout_InputDelete(sout_packetizer_input_t *p_input)
{
    (void)p_input;
    return VLC_SUCCESS;
}

int sout_InputSendBuffer(sout_packetizer_input_t *p_input, block_t *p_buffer)
{
    (void)p_input;
    block_Release(p_buffer);
    return VLC_EGENERIC;
}

int announce_HandlerDestroy(announce_handler_t *p_announce)
{
    (void)p_announce;
    return VLC_SUCCESS;
}

int osd_ConfigLoader(vlc_object_t *p_this, const char *psz_file, osd_menu_t **pp_menu)
{
    msg_Err(p_this, "OSD menus are not available (%s)", psz_file);
    *pp_menu = NULL;
    return VLC_EGENERIC;
}

void osd_ConfigUnload(vlc_object_t *p_this, osd_menu_t **pp_menu)
{
    (void)p_this;
    *pp_menu = NULL;
}

/* Binding a privileged port needs no help on Glaze. */
int rootwrap_bind(int family, int socktype, int protocol, const struct sockaddr *addr,
                  size_t alen)
{
    (void)family;
    (void)socktype;
    (void)protocol;
    (void)addr;
    (void)alen;
    errno = ENOSYS;
    return -1;
}
