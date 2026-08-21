#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDD_BOUNDS_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDD_BOUNDS_H

/* Stable project-owned pointer/bounds errors; PS2SDK/IOP errors are separate. */
enum {
    HDD_PAYLOAD_ERR_EMPTY_POINTER = -170,
    HDD_PAYLOAD_ERR_TOO_LARGE = -171,
    HDD_PAYLOAD_ERR_BEFORE_RESERVED_AREA = -172,
    HDD_PAYLOAD_ERR_OUTSIDE_MBR = -173
};

/* Validate pointer shape without requiring any device geometry. */
int hdd_validate_payload_shape(unsigned int start, unsigned int sectors);

/*
 * Validate a pointer against an explicit __mbr geometry. This function has no
 * PS2SDK dependency and is used by synthetic host-HDD fixtures as well as the
 * PS2 read-only adapter after it obtains live partition geometry.
 */
int hdd_validate_payload_bounds_geometry(unsigned int start,
                                         unsigned int sectors,
                                         unsigned int mbr_start,
                                         unsigned int mbr_size);

#endif
