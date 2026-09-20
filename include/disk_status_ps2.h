#ifndef PS2_HDD_BOOTSTRAP_MANAGER_DISK_STATUS_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_DISK_STATUS_PS2_H

typedef enum {
    DISK_STATUS_READ = 0,
    DISK_STATUS_WRITE,
    DISK_STATUS_VERIFY,
    DISK_STATUS_FLUSH,
    DISK_STATUS_POINTER,
    DISK_STATUS_SCAN
} disk_status_kind_t;

/*
 * Live PS2-side HDD activity publisher. Low-level transports provide real
 * LBA/range information while nested operations provide human-readable phase
 * and semantic-location context. Presentation is GS/GIF-DMA backed and
 * intentionally unthrottled: every published event may become a frame.
 */
void disk_status_begin(const char *operation, const char *phase);
void disk_status_begin_at(const char *operation, const char *phase,
                          const char *location);
void disk_status_phase(const char *phase);
void disk_status_location(const char *location);
void disk_status_phase_at(const char *phase, const char *location);
/* Mark the active semantic operation as destructive/write-capable even while
 * it is still performing mandatory validation reads. This keeps the current
 * I/O truthful without advertising the armed path as read-only. */
void disk_status_set_write_intent(int armed);
void disk_status_io(disk_status_kind_t kind, unsigned int lba,
                    unsigned int sectors, unsigned int current,
                    unsigned int total);
void disk_status_end(void);

#endif
