#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APP_UI_PS2_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APP_UI_PS2_H

typedef struct {
    const char *label;
    const char *hint;
    int enabled;
} app_ui_menu_item_t;

void app_ui_restart_to_browser(void);
void app_ui_power_menu(void);
void app_ui_fatal_screen(const char *message, int code);
void app_ui_wait_to_return(void);
void app_ui_choose_storage(void);
int app_ui_choose_signing_card(void);

/*
 * Standard list navigation used by every post-overhaul menu. UP/DOWN select,
 * X activates an enabled item and TRIANGLE returns -1. Disabled entries remain
 * visible with their reason in hint, so context-sensitive operations do not
 * silently disappear when bootstrap state changes.
 */
int app_ui_menu_select(const char *title, const char *status,
                       const app_ui_menu_item_t *items,
                       unsigned int item_count,
                       unsigned int *selection);

/* Root dashboard navigation. LEFT/RIGHT select a card in the current row,
 * UP/DOWN move between rows, X opens and TRIANGLE returns -1. */
int app_ui_dashboard_select(const char *title, const char *status,
                            const app_ui_menu_item_t *items,
                            unsigned int item_count,
                            unsigned int *selection);

void app_ui_activity_message(const char *title, const char *message);

#endif
