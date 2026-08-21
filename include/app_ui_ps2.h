#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APP_UI_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APP_UI_PS2_H

void app_ui_restart_to_browser(void);
void app_ui_power_menu(void);
void app_ui_fatal_screen(const char *message, int code);
void app_ui_wait_to_return(void);
void app_ui_choose_storage(void);
int app_ui_choose_signing_card(void);

#endif
