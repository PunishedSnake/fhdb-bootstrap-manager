/* Run conservative repair planning against complete generated raw HDD fixtures. */

#include "apa.h"
#include "apa_repair.h"

#include <stdio.h>
#include <string.h>

static int read_header(const char *directory, const char *name,
                       unsigned char header[APA_HEADER_SIZE])
{
    char path[512];
    FILE *file;

    snprintf(path, sizeof(path), "%s/%s.raw", directory, name);
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    if (fread(header, 1, APA_HEADER_SIZE, file) != APA_HEADER_SIZE) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static int require_header_repair(const char *directory, const char *name)
{
    unsigned char header[APA_HEADER_SIZE];
    unsigned char repaired[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    if (!read_header(directory, name, header) ||
        apa_repair_analyze(header, &plan) != 0 ||
        !plan.header_patch_safe || plan.blockers != 0 ||
        apa_repair_build_header(header, &plan, repaired) != 0 ||
        !is_standard_apa_header(repaired) || is_hybrid_gpt(repaired)) {
        fprintf(stderr, "%s: expected safe header repair failed.\n", name);
        return 0;
    }
    return 1;
}

static int require_pointer_clear_only(const char *directory, const char *name)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    if (!read_header(directory, name, header) ||
        apa_repair_analyze(header, &plan) != 0 ||
        plan.header_patch_safe || !plan.pointer_clear_recommended ||
        plan.blockers != 0) {
        fprintf(stderr, "%s: expected pointer-clear recommendation failed.\n",
                name);
        return 0;
    }
    return 1;
}

static int require_refused(const char *directory, const char *name)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    if (!read_header(directory, name, header) ||
        apa_repair_analyze(header, &plan) != 0 ||
        plan.header_patch_safe || plan.blockers == 0) {
        fprintf(stderr, "%s: unsafe disk was considered repairable.\n", name);
        return 0;
    }
    return 1;
}

static int require_clean(const char *directory, const char *name)
{
    unsigned char header[APA_HEADER_SIZE];
    apa_repair_plan_t plan;

    if (!read_header(directory, name, header) ||
        apa_repair_analyze(header, &plan) != 0 ||
        plan.header_patch_safe || plan.blockers != 0 ||
        plan.issues != 0) {
        fprintf(stderr, "%s: clean disk produced a repair plan.\n", name);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : "tests/generated_hdds";
    static const char *const repairable[] = {
        "bad_apa_magic",
        "bad_mbr_id",
        "bad_sony_magic"
    };
    static const char *const pointer_only[] = {
        "pointer_start_only",
        "pointer_size_only"
    };
    static const char *const refused[] = {
        "bad_checksum",
        "torn_disable_stale_checksum",
        "apa_pc_signature_only",
        "hybrid_apa_gpt",
        "gpt_only",
        "deterministic_garbage"
    };
    unsigned int i;

    if (!require_clean(directory, "valid_disabled") ||
        !require_clean(directory, "valid_enabled"))
        return 1;

    for (i = 0; i < sizeof(repairable) / sizeof(repairable[0]); i++)
        if (!require_header_repair(directory, repairable[i]))
            return 2;

    for (i = 0; i < sizeof(pointer_only) / sizeof(pointer_only[0]); i++)
        if (!require_pointer_clear_only(directory, pointer_only[i]))
            return 3;

    for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
        if (!require_refused(directory, refused[i]))
            return 4;

    puts("All generated HDD repair-policy cases passed.");
    return 0;
}
