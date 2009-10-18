/**
 * @file udev.c
 * @brief List of multimedia devices for VLC media player
 */
/*****************************************************************************
 * Copyright © 2009 Rémi Denis-Courmont
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <libudev.h>
#include <vlc_common.h>
#include <vlc_services_discovery.h>
#include <vlc_plugin.h>
#include <poll.h>
#include <errno.h>

static int OpenV4L (vlc_object_t *);
static void Close (vlc_object_t *);

/*
 * Module descriptor
 */
vlc_module_begin ()
    set_shortname (N_("Devices"))
    set_description (N_("Capture devices"))
    set_category (CAT_PLAYLIST)
    set_subcategory (SUBCAT_PLAYLIST_SD)
    set_capability ("services_discovery", 0)
    set_callbacks (OpenV4L, Close)

    add_shortcut ("udev")
vlc_module_end ()

struct subsys
{
    const char *name;
    char * (*get_mrl) (struct udev_device *dev);
    char * (*get_name) (struct udev_device *dev);
    char * (*get_cat) (struct udev_device *dev);
};

struct services_discovery_sys_t
{
    const struct subsys *subsys;
    struct udev_monitor *monitor;
    vlc_thread_t         thread;
    input_item_t       **itemv;
    size_t               itemc;
};

static void *Run (void *);
static void HandleDevice (services_discovery_t *, struct udev_device *, bool);

/**
 * Probes and initializes.
 */
static int Open (vlc_object_t *obj, const struct subsys *subsys)
{
    services_discovery_t *sd = (services_discovery_t *)obj;
    services_discovery_sys_t *p_sys = malloc (sizeof (*p_sys));

    if (p_sys == NULL)
        return VLC_ENOMEM;
    sd->p_sys = p_sys;
    p_sys->subsys = subsys;
    p_sys->itemv = NULL;
    p_sys->itemc = 0;

    struct udev_monitor *mon = NULL;
    struct udev *udev = udev_new ();
    if (udev == NULL)
        goto error;

    mon = udev_monitor_new_from_netlink (udev, "udev");
    if (mon == NULL
     || udev_monitor_filter_add_match_subsystem_devtype (mon, subsys->name,
                                                         NULL))
        goto error;
    p_sys->monitor = mon;

    /* Enumerate existing devices */
    struct udev_enumerate *devenum = udev_enumerate_new (udev);
    if (devenum == NULL)
        goto error;
    if (udev_enumerate_add_match_subsystem (devenum, subsys->name))
    {
        udev_enumerate_unref (devenum);
        goto error;
    }

    udev_monitor_enable_receiving (mon);
    /* Note that we enumerate _after_ monitoring is enabled so that we do not
     * loose device events occuring while we are enumerating. We could still
     * loose events if the Netlink socket receive buffer overflows. */
    udev_enumerate_scan_devices (devenum);
    struct udev_list_entry *devlist = udev_enumerate_get_list_entry (devenum);
    struct udev_list_entry *deventry;
    udev_list_entry_foreach (deventry, devlist)
    {
        const char *path = udev_list_entry_get_name (deventry);
        struct udev_device *dev = udev_device_new_from_syspath (udev, path);
        HandleDevice (sd, dev, true);
        udev_device_unref (dev);
    }
    udev_enumerate_unref (devenum);

    if (vlc_clone (&p_sys->thread, Run, sd, VLC_THREAD_PRIORITY_LOW))
    {   /* Fallback without thread */
        udev_monitor_unref (mon);
        udev_unref (udev);
        p_sys->monitor = NULL;
    }
    return VLC_SUCCESS;

error:
    if (mon != NULL)
        udev_monitor_unref (mon);
    if (udev != NULL)
        udev_unref (udev);
    free (p_sys);
    return VLC_EGENERIC;
}


/**
 * Releases resources
 */
static void Close (vlc_object_t *obj)
{
    services_discovery_t *sd = (services_discovery_t *)obj;
    services_discovery_sys_t *p_sys = sd->p_sys;

    if (p_sys->monitor != NULL)
    {
        struct udev *udev = udev_monitor_get_udev (p_sys->monitor);

        vlc_cancel (p_sys->thread);
        vlc_join (p_sys->thread, NULL);
        udev_monitor_unref (p_sys->monitor);
        udev_unref (udev);
    }

    for (size_t i = 0; i < p_sys->itemc; i++)
        vlc_gc_decref (p_sys->itemv[i]);
    free (p_sys->itemv);
    free (p_sys);
}


static void *Run (void *data)
{
    services_discovery_t *sd = data;
    services_discovery_sys_t *p_sys = sd->p_sys;
    struct udev_monitor *mon = p_sys->monitor;

    int fd = udev_monitor_get_fd (mon);
    struct pollfd ufd = { .fd = fd, .events = POLLIN, };

    for (;;)
    {
        while (poll (&ufd, 1, -1) == -1)
            if (errno != EINTR)
                break;

        struct udev_device *dev = udev_monitor_receive_device (mon);
        if (dev == NULL)
            continue;

        /* FIXME: handle change, offline, online */
        const char *action = udev_device_get_action (dev);
        if (!strcmp (action, "add"))
            HandleDevice (sd, dev, true);
        else if (!strcmp (action, "remove"))
            HandleDevice (sd, dev, false);

        udev_device_unref (dev);
    }
    return NULL;
}

static int hex (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c + 10 - 'A';
    if (c >= 'a' && c <= 'f')
        return c + 10 - 'a';
    return -1;
}

static char *decode (const char *enc)
{
    char *ret = enc ? strdup (enc) : NULL;
    if (ret == NULL)
        return NULL;

    char *out = ret;
    for (const char *in = ret; *in; out++)
    {
        int h1, h2;

        if ((in[0] == '\\') && (in[1] == 'x')
         && ((h1 = hex (in[2])) != -1)
         && ((h2 = hex (in[3])) != -1))
        {
            *out = (h1 << 4) | h2;
            in += 4;
        }
        else
        {
            *out = *in;
            in++;
        }
    }
    *out = 0;
    return ret;
}

static char *decode_property (struct udev_device *dev, const char *name)
{
    return decode (udev_device_get_property_value (dev, name));
}

static void HandleDevice (services_discovery_t *sd, struct udev_device *dev,
                          bool add)
{
    services_discovery_sys_t *p_sys = sd->p_sys;

    /* Determine media location */
    char *mrl = p_sys->subsys->get_mrl (dev);
    if (mrl == NULL)
        return;

    /* Find item in list */
    input_item_t *item = NULL;
    size_t i;

    for (i = 0; i < p_sys->itemc; i++)
    {
        input_item_t *oitem = p_sys->itemv[i];
        char *omrl = input_item_GetURI (oitem);

        if (!strcmp (omrl, mrl))
        {
            item = oitem;
            break;
        }
    }

    /* Add/Remove old item */
    if (add && (item == NULL))
    {
        char *name = p_sys->subsys->get_name (dev);
        item = input_item_NewWithType (VLC_OBJECT (sd), mrl,
                                       name ? name : "Unnamed",
                                       0, NULL, 0, -1, ITEM_TYPE_CARD);
        free (name);
        if (item != NULL)
        {
            msg_Dbg (sd, "adding %s", mrl);
            name = p_sys->subsys->get_cat (dev);
            services_discovery_AddItem (sd, item, name ? name : "Generic");
            free (name);

            TAB_APPEND (p_sys->itemc, p_sys->itemv, item);
        }
    }
    else if (!add && (item != NULL))
    {
        msg_Dbg (sd, "removing %s", mrl);
        services_discovery_RemoveItem (sd, item);
        vlc_gc_decref (item);
        TAB_REMOVE (p_sys->itemc, p_sys->itemv, i);
    }
    free (mrl);
}

/*** Video4Linux support ***/
static bool is_v4l_legacy (struct udev_device *dev)
{
    const char *version;

    version = udev_device_get_property_value (dev, "ID_V4L_VERSION");
    return version && !strcmp (version, "1");
}

static char *v4l_get_mrl (struct udev_device *dev)
{
    /* Determine media location */
    const char *scheme = "v4l2";
    if (is_v4l_legacy (dev))
        scheme = "v4l";
    const char *node = udev_device_get_devnode (dev);
    char *mrl;

    if (asprintf (&mrl, "%s://%s", scheme, node) == -1)
        mrl = NULL;
    return mrl;
}

static char *v4l_get_name (struct udev_device *dev)
{
    const char *prd = udev_device_get_property_value (dev, "ID_V4L_PRODUCT");
    return prd ? strdup (prd) : NULL;
}

static char *v4l_get_cat (struct udev_device *dev)
{
    return decode_property (dev, "ID_VENDOR_ENC");
}

int OpenV4L (vlc_object_t *obj)
{
    static const struct subsys subsys = {
        "video4linux", v4l_get_mrl, v4l_get_name, v4l_get_cat,
    };

    return Open (obj, &subsys);
}
