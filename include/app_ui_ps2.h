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

#if defined(__GNUC__)
#define APP_UI_SELECTOR_SIZE_OPT __attribute__((optimize("Os")))
#else
#define APP_UI_SELECTOR_SIZE_OPT
#endif

/*
 * Standard list navigation used by every post-overhaul menu. UP/DOWN select,
 * X activates an enabled item and TRIANGLE returns -1. Disabled entries remain
 * visible with their reason in hint, so context-sensitive operations do not
 * silently disappear when bootstrap state changes.
 *
 * Both selectors spend almost all of their wall time waiting for controller
 * input or GS presentation. Favor a compact control loop over throughput-style
 * optimization; the renderers they call keep their own optimization policy.
 */
int APP_UI_SELECTOR_SIZE_OPT app_ui_menu_select(
    const char *title, const char *status,
    const app_ui_menu_item_t *items,
    unsigned int item_count,
    unsigned int *selection);

/* Root dashboard navigation. LEFT/RIGHT select a card in the current row,
 * UP/DOWN move between rows, X opens and TRIANGLE returns -1. */
int APP_UI_SELECTOR_SIZE_OPT app_ui_dashboard_select(
    const char *title, const char *status,
    const app_ui_menu_item_t *items,
    unsigned int item_count,
    unsigned int *selection);

#undef APP_UI_SELECTOR_SIZE_OPT

void app_ui_activity_message(const char *title, const char *message);

#endif
