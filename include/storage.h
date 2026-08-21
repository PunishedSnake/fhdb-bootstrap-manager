#ifndef PS2_HDD_BOOTSTRAP_MANAGER_STORAGE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_STORAGE_H

#include "capsule_format.h"

#define STORAGE_TARGET_COUNT 3u
#define STORAGE_LAUNCH_PATH_SIZE 256u

typedef struct {
    const char *name;
    const char *prefix;
    int memory_card_port;
} storage_target_t;

/* Storage target metadata remains read-only to callers. */
extern const storage_target_t storage_targets[STORAGE_TARGET_COUNT];

/* Selected-target state is owned by storage.c. */
unsigned int storage_selected(void);
int storage_set_selected(unsigned int storage);

void storage_path(char *destination, unsigned int capacity,
                  unsigned int storage, const char *filename);
void select_launch_storage(int argc, char **argv);

/*
 * Build a path beside the launched ELF when argv[0] contains a usable device
 * path. Some launchers expose only a device/root or no path at all; in that
 * case the selected backup/report storage root is used as a deterministic
 * fallback. This is intended for small application-local files such as
 * MICHISHIRUBE.CFG, not recovery evidence whose destination remains explicit.
 */
int storage_launch_file_path(char *destination, unsigned int capacity,
                             const char *filename);
const char *storage_launch_directory(void);

int write_whole_file(const char *path, const void *data, int size);
int append_log_file(const char *path, const void *data, int size, int truncate);
int read_exact_file(const char *path, void *data, int size);
int read_bounded_file(const char *path, unsigned int maximum_size,
                      unsigned char **data_out, unsigned int *size_out);
int path_exists(const char *path);
int read_text_file(const char *path, char *buffer, unsigned int capacity);
int read_romver(char destination[RESCUE_CAPSULE_ROMVER_SIZE]);

#endif
