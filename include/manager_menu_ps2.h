#ifndef PS2_HDD_BOOTSTRAP_MANAGER_MANAGER_MENU_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_MANAGER_MENU_PS2_H

#include "apa.h"
#include "boot_chain.h"

/* Persistent hierarchical manager UI. It returns only if a caller deliberately
 * wants to leave the menu loop; restart/power actions normally do not return. */
void manager_menu_run(unsigned char header[APA_HEADER_SIZE],
                      boot_chain_info_t *boot_chain);

#endif