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
 * Live PS2-side HDD activity monitor. The low-level transports publish actual
 * LBA/range information here; this module owns throttled presentation so the
 * UI cannot accidentally turn one raw-sector scan into thousands of full
 * screen redraws. A nested operation supplies the human-readable context while
 * raw transports supply the current physical location.
 */
void disk_status_begin(const char *operation, const char *phase);
void disk_status_phase(const char *phase);
void disk_status_io(disk_status_kind_t kind, unsigned int lba,
                    unsigned int sectors, unsigned int current,
                    unsigned int total);
void disk_status_end(void);

#endif
