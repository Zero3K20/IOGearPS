/*
 * config.c — Runtime configuration storage for the IOGear GPSU21.
 */

#include "config.h"
#include <string.h>

/* ── Defaults ──────────────────────────────────────────────────────────── */

char g_device_name[CONFIG_DEVICE_NAME_MAX] = "GPSU21";
int  g_airprint_enabled = 1;   /* AirPrint on by default */

/* ── Public API ────────────────────────────────────────────────────────── */

void config_init(void)
{
    /* Static initialisers already set the defaults above.
     * This function exists as an explicit initialisation point so that
     * future flash-based persistence can be added here without changing
     * the call sites in main.c. */
}

void config_set_device_name(const char *name)
{
    if (!name || !*name) {
        return;
    }
    strncpy(g_device_name, name, CONFIG_DEVICE_NAME_MAX - 1);
    g_device_name[CONFIG_DEVICE_NAME_MAX - 1] = '\0';
}

void config_set_airprint_enabled(int enabled)
{
    g_airprint_enabled = enabled ? 1 : 0;
}
