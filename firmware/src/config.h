/*
 * config.h — Runtime configuration for the IOGear GPSU21 print server.
 *
 * Holds device-wide settings that are shared between the HTTP server
 * (which updates them from web-form submissions) and the service threads
 * (mDNS, IPP) which read them to advertise the correct name and behaviour.
 *
 * All settings are plain global variables so that no locking is required for
 * the simple read/write patterns used here (atomic word-size reads on MIPS32).
 */

#ifndef GPSU21_CONFIG_H
#define GPSU21_CONFIG_H

/* Maximum length of the device name, including the NUL terminator. */
#define CONFIG_DEVICE_NAME_MAX  32

/*
 * g_device_name — human-readable name advertised as the AirPrint service
 * instance name in mDNS (e.g. "GPSU21" → "GPSU21._ipp._tcp.local.").
 * Corresponds to the PSName form field saved via the web UI.
 */
extern char g_device_name[CONFIG_DEVICE_NAME_MAX];

/*
 * g_airprint_enabled — controls whether the mDNS thread advertises the
 * printer via AirPrint (_ipp._tcp Bonjour records).
 *   1 = AirPrint advertising enabled (default)
 *   0 = AirPrint advertising disabled
 */
extern int g_airprint_enabled;

/*
 * g_scanner_enabled — controls whether the eSCL (AirScan) scanner server
 * starts and whether the mDNS thread advertises the scanner via
 * _uscan._tcp Bonjour records.
 *   1 = AirScan advertising and eSCL server enabled (default)
 *   0 = AirScan disabled
 *
 * This setting has effect only when a USB multi-function device with a
 * scanner interface is connected.  The eSCL server on port 9290 handles
 * iOS 13+ / macOS 10.15+ AirScan requests.
 */
extern int g_scanner_enabled;

/* Initialise configuration with compile-time defaults. */
void config_init(void);

/* Update the device name (called when the web UI saves settings). */
void config_set_device_name(const char *name);

/* Update the AirPrint enabled flag (called when the web UI saves settings). */
void config_set_airprint_enabled(int enabled);

/* Update the AirScan (eSCL) scanner enabled flag. */
void config_set_scanner_enabled(int enabled);

#endif /* GPSU21_CONFIG_H */
