#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APP_CONFIG_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APP_CONFIG_H

#include "video_mode.h"

/* Stable application configuration name. Codenames are release-specific and
 * must not leak into the long-lived configuration ABI. */
#define APP_CONFIG_FILENAME "HDDMAN.CFG"
#define APP_CONFIG_LEGACY_FILENAME "MICHISHIRUBE.CFG"

int app_config_load(void);
int app_config_save(void);
int app_config_path(char *destination, unsigned int capacity);

video_mode_id_t app_config_video_mode(void);
int app_config_set_video_mode(video_mode_id_t mode);

#endif
