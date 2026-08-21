#ifndef PS2_HDD_BOOTSTRAP_MANAGER_APP_ERROR_H
#define PS2_HDD_BOOTSTRAP_MANAGER_APP_ERROR_H

#define APP_ERROR_CONTEXT_SIZE 64u

typedef enum {
    APP_ERROR_DOMAIN_GENERIC = 0,
    APP_ERROR_DOMAIN_BOOTSTRAP_SOURCE,
    APP_ERROR_DOMAIN_KELF,
    APP_ERROR_DOMAIN_HDD_BOUNDS,
    APP_ERROR_DOMAIN_HDD_WRITE,
    APP_ERROR_DOMAIN_MASTER_REPAIR,
    APP_ERROR_DOMAIN_REPAIR_SNAPSHOT,
    APP_ERROR_DOMAIN_FORENSIC_SNAPSHOT,
    APP_ERROR_DOMAIN_FORENSIC_REPAIR,
    APP_ERROR_DOMAIN_IOP,
    APP_ERROR_DOMAIN_STARTUP
} app_error_domain_t;

typedef struct {
    app_error_domain_t domain;
    int code;
    char context[APP_ERROR_CONTEXT_SIZE];
} app_error_record_t;

typedef struct {
    const char *symbol;
    const char *summary;
    const char *detail;
    const char *action;
} app_error_info_t;

void app_error_record(app_error_domain_t domain, int code,
                      const char *context);
void app_error_clear(void);
int app_error_get(app_error_record_t *record);
void app_error_describe(app_error_domain_t domain, int code,
                        app_error_info_t *info);

#endif
