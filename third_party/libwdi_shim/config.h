/*
 * Build settings for compiling libwdi's pki.c on its own inside hori-driver.exe
 * (libwdi's configure normally generates this file).
 */
#pragma once

/* Route pki.c's wdi_info/wdi_warn/wdi_err messages to hori_driver.c's wdi_log(). */
#define INCLUDE_DEBUG_LOGGING 1
