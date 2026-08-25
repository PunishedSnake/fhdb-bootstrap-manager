#include "r5900_perf.h"

#include <stddef.h>

#define R5900_PCCR_CTE (1u << 31)
#define R5900_PCCR_EVENT0_SHIFT 5u
#define R5900_PCCR_EVENT1_SHIFT 15u
#define R5900_PCCR_EVENT_MASK 0x1fu

/* Count normal application execution in all three privilege modes while
 * excluding Level-1 exception handlers. This is intentionally explicit rather
 * than inheriting whatever mode bits a debugger happened to leave behind. */
#define R5900_PCCR_APP_MODES0 ((1u << 2) | (1u << 3) | (1u << 4))
#define R5900_PCCR_APP_MODES1 ((1u << 12) | (1u << 13) | (1u << 14))
#define R5900_PCR_VALUE_MASK 0x7fffffffu
#define R5900_PCR_OVERFLOW (1u << 31)

static inline void r5900_perf_sync(void)
{
    /* SYNC.P guarantees completion of preceding instructions before following
     * instructions execute. The memory clobber also prevents GCC from moving
     * ordinary memory operations across the benchmark boundary. */
    __asm__ __volatile__("sync.p" ::: "memory");
}

static inline uint32_t r5900_read_pccr(void)
{
    uint32_t value;

    __asm__ __volatile__("mfps %0, 0" : "=r"(value));
    return value;
}

static inline uint32_t r5900_read_pcr0(void)
{
    uint32_t value;

    __asm__ __volatile__("mfpc %0, 0" : "=r"(value));
    return value;
}

static inline uint32_t r5900_read_pcr1(void)
{
    uint32_t value;

    __asm__ __volatile__("mfpc %0, 1" : "=r"(value));
    return value;
}

static inline void r5900_write_pccr(uint32_t value)
{
    __asm__ __volatile__("mtps %0, 0" :: "r"(value) : "memory");
}

static inline void r5900_write_pcr0(uint32_t value)
{
    __asm__ __volatile__("mtpc %0, 0" :: "r"(value));
}

static inline void r5900_write_pcr1(uint32_t value)
{
    __asm__ __volatile__("mtpc %0, 1" :: "r"(value));
}

static uint32_t r5900_perf_control(r5900_pcr0_event_t event0,
                                    r5900_pcr1_event_t event1)
{
    return R5900_PCCR_CTE | R5900_PCCR_APP_MODES0 | R5900_PCCR_APP_MODES1 |
           (((uint32_t)event0 & R5900_PCCR_EVENT_MASK)
            << R5900_PCCR_EVENT0_SHIFT) |
           (((uint32_t)event1 & R5900_PCCR_EVENT_MASK)
            << R5900_PCCR_EVENT1_SHIFT);
}

int r5900_perf_begin(r5900_perf_scope_t *scope,
                     r5900_pcr0_event_t event0,
                     r5900_pcr1_event_t event1)
{
    uint32_t control;

    if (scope == NULL || scope->active || event0 < 0 || event0 > 16 ||
        event1 < 0 || event1 > 16)
        return -1;

    r5900_perf_sync();
    scope->previous_pccr = r5900_read_pccr();
    scope->previous_pcr0 = r5900_read_pcr0();
    scope->previous_pcr1 = r5900_read_pcr1();
    scope->event0 = (uint32_t)event0;
    scope->event1 = (uint32_t)event1;

    /* Disable first so resetting PCR0/PCR1 cannot race an enabled counter. */
    r5900_write_pccr(scope->previous_pccr & ~R5900_PCCR_CTE);
    r5900_perf_sync();
    r5900_write_pcr0(0);
    r5900_write_pcr1(0);
    control = r5900_perf_control(event0, event1);
    r5900_write_pccr(control);
    r5900_perf_sync();
    scope->active = 1;
    return 0;
}

int r5900_perf_end(r5900_perf_scope_t *scope, r5900_perf_result_t *result)
{
    uint32_t raw0;
    uint32_t raw1;

    if (scope == NULL || result == NULL || !scope->active)
        return -1;

    /* Stop at a serialized boundary, then snapshot before restoring any prior
     * debugger/profiler state. */
    r5900_perf_sync();
    r5900_write_pccr(r5900_read_pccr() & ~R5900_PCCR_CTE);
    r5900_perf_sync();
    raw0 = r5900_read_pcr0();
    raw1 = r5900_read_pcr1();

    result->pcr0 = raw0 & R5900_PCR_VALUE_MASK;
    result->pcr1 = raw1 & R5900_PCR_VALUE_MASK;
    result->pcr0_overflow = (raw0 & R5900_PCR_OVERFLOW) != 0;
    result->pcr1_overflow = (raw1 & R5900_PCR_OVERFLOW) != 0;

    r5900_write_pcr0(scope->previous_pcr0);
    r5900_write_pcr1(scope->previous_pcr1);
    r5900_write_pccr(scope->previous_pccr);
    r5900_perf_sync();
    scope->active = 0;
    return 0;
}
