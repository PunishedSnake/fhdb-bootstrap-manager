#include <stdio.h>
#include <string.h>

#include "app_error.h"

static int failures;

static void expect_description(app_error_domain_t domain, int code,
                               const char *symbol)
{
    app_error_info_t info;

    app_error_describe(domain, code, &info);
    if (strcmp(info.symbol, symbol) != 0 || info.summary[0] == '\0' ||
        info.detail[0] == '\0' || info.action[0] == '\0') {
        fprintf(stderr, "error catalog mismatch: domain=%d code=%d got=%s\n",
                (int)domain, code, info.symbol);
        failures++;
    }
}

int main(void)
{
    app_error_record_t record;

    expect_description(APP_ERROR_DOMAIN_BOOTSTRAP_SOURCE, -120,
                       "BOOTSTRAP_SOURCE_SIZE_INVALID");
    expect_description(APP_ERROR_DOMAIN_BOOTSTRAP_SOURCE, -4,
                       "BOOTSTRAP_SOURCE_IO");
    expect_description(APP_ERROR_DOMAIN_HDD_BOUNDS, -173,
                       "HDD_PAYLOAD_OUTSIDE_MBR");
    expect_description(APP_ERROR_DOMAIN_MASTER_REPAIR, -335,
                       "MASTER_REPAIR_COMPARE_FAILED");
    expect_description(APP_ERROR_DOMAIN_FORENSIC_REPAIR, -372,
                       "FORENSIC_REPAIR_SOURCE_CHANGED");
    expect_description(APP_ERROR_DOMAIN_FORENSIC_SNAPSHOT, -364,
                       "HDDMETA_VERIFY_FAILED");
    expect_description(APP_ERROR_DOMAIN_IOP, -4, "IOP_DRIVER_ERROR");

    app_error_clear();
    if (app_error_get(&record)) {
        fprintf(stderr, "error record should start clear\n");
        failures++;
    }
    app_error_record(APP_ERROR_DOMAIN_BOOTSTRAP_SOURCE, -4, "load source");
    if (!app_error_get(&record) || record.code != -4 ||
        record.domain != APP_ERROR_DOMAIN_BOOTSTRAP_SOURCE ||
        strcmp(record.context, "load source") != 0) {
        fprintf(stderr, "error record round-trip failed\n");
        failures++;
    }
    app_error_clear();

    if (failures != 0)
        return 1;
    puts("All contextual error-catalog tests passed.");
    return 0;
}
