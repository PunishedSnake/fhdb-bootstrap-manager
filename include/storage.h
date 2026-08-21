#ifndef PS2_HDD_BOOTSTRAP_MANAGER_STORAGE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_STORAGE_H

#include "capsule_format.h"

#define STORAGE_TARGET_COUNT 3u

typedef struct {
    const char *name;
    const char *prefix;
    int memory_card_port;
} storage_target_t;

/*
 * Transitional state exported during the first 0.4 modularization pass.
 * Keeping these names preserves call-site behaviour while code moves out of
 * main.c. A later Michishirube step can encapsulate selection behind accessors
 * once the split itself has regression coverage.
 */
extern const storage_target_t storage_targets[STORAGE_TARGET_COUNT];
extern unsigned int selected_storage;

void storage_path(char *destination, unsigned int capacity,
                  unsigned int storage, const char *filename);
void select_launch_storage(int argc, char **argv);
int write_whole_file(const char *path, const void *data, int size);
int append_log_file(const char *path, const void *data, int size, int truncate);
int read_exact_file(const char *path, void *data, int size);
int read_bounded_file(const char *path, unsigned int maximum_size,
                      unsigned char **data_out, unsigned int *size_out);
int path_exists(const char *path);
int read_text_file(const char *path, char *buffer, unsigned int capacity);
int read_romver(char destination[RESCUE_CAPSULE_ROMVER_SIZE]);

#endif
