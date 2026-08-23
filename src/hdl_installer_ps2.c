/* HDL Tools controller is split by responsibility to keep the large PS2-only
 * implementation reviewable. These fragments form one translation unit. */
#include "hdl_tools/source_ui.inc"

/*
 * A raw APA LBA points at the start of the 4 KiB partition-information area.
 * HDLoader's 1 MiB extended-attribute offset is relative to the file view that
 * begins after those 4 KiB, so the physical metadata sector is main+0x808,
 * not main+0x800. OPL uses the same (0x100000 + 4096) / 512 geometry.
 */
#undef HDL_METADATA_LBA_OFFSET
#define HDL_METADATA_LBA_OFFSET 0x0808u

#include "hdl_tools/catalog.inc"
#include "hdl_tools/transaction.inc"
#include "hdl_tools/install_ui.inc"
#include "hdl_tools/game_ui.inc"
