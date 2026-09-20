#ifndef PS2_HDD_BOOTSTRAP_MANAGER_R5900_PERF_H
#define PS2_HDD_BOOTSTRAP_MANAGER_R5900_PERF_H

#include <stdint.h>

/*
 * EE Core User's Manual v6.0, Table 7-1.
 *
 * PCR0 and PCR1 share the numeric event selector but not always the event
 * meaning. Keep separate enums so callers cannot accidentally describe a PCR1
 * value using the PCR0 name. Event 16 deliberately means "no event" and is
 * useful when a benchmark needs only one hardware counter.
 */
typedef enum {
    R5900_PCR0_RESERVED = 0,
    R5900_PCR0_PROCESSOR_CYCLE = 1,
    R5900_PCR0_SINGLE_ISSUE = 2,
    R5900_PCR0_BRANCH_ISSUED = 3,
    R5900_PCR0_BTAC_MISS = 4,
    R5900_PCR0_ITLB_MISS = 5,
    R5900_PCR0_ICACHE_MISS = 6,
    R5900_PCR0_DTLB_ACCESS = 7,
    R5900_PCR0_NONBLOCKING_LOAD = 8,
    R5900_PCR0_WBB_SINGLE_REQUEST = 9,
    R5900_PCR0_WBB_BURST_REQUEST = 10,
    R5900_PCR0_CPU_ADDRESS_BUS_BUSY = 11,
    R5900_PCR0_INSTRUCTION_COMPLETED = 12,
    R5900_PCR0_NON_BDS_INSTRUCTION_COMPLETED = 13,
    R5900_PCR0_COP2_INSTRUCTION_COMPLETED = 14,
    R5900_PCR0_LOAD_COMPLETED = 15,
    R5900_PCR0_NO_EVENT = 16
} r5900_pcr0_event_t;

typedef enum {
    R5900_PCR1_LOW_ORDER_BRANCH_ISSUED = 0,
    R5900_PCR1_PROCESSOR_CYCLE = 1,
    R5900_PCR1_DUAL_ISSUE = 2,
    R5900_PCR1_BRANCH_MISPREDICTED = 3,
    R5900_PCR1_TLB_MISS = 4,
    R5900_PCR1_DTLB_MISS = 5,
    R5900_PCR1_DCACHE_MISS = 6,
    R5900_PCR1_WBB_SINGLE_UNAVAILABLE = 7,
    R5900_PCR1_WBB_BURST_UNAVAILABLE = 8,
    R5900_PCR1_WBB_BURST_ALMOST_FULL = 9,
    R5900_PCR1_WBB_BURST_FULL = 10,
    R5900_PCR1_CPU_DATA_BUS_BUSY = 11,
    R5900_PCR1_INSTRUCTION_COMPLETED = 12,
    R5900_PCR1_NON_BDS_INSTRUCTION_COMPLETED = 13,
    R5900_PCR1_COP1_INSTRUCTION_COMPLETED = 14,
    R5900_PCR1_STORE_COMPLETED = 15,
    R5900_PCR1_NO_EVENT = 16
} r5900_pcr1_event_t;

typedef struct {
    uint32_t previous_pccr;
    uint32_t previous_pcr0;
    uint32_t previous_pcr1;
    uint32_t event0;
    uint32_t event1;
    int active;
} r5900_perf_scope_t;

typedef struct {
    uint32_t pcr0;
    uint32_t pcr1;
    int pcr0_overflow;
    int pcr1_overflow;
} r5900_perf_result_t;

/*
 * Start both counters for normal application execution in user/supervisor/
 * kernel mode. Level-1 exception-handler work is deliberately excluded so an
 * interrupt does not silently become part of the measured application region.
 * Level-2 handlers are excluded by hardware.
 *
 * The scope preserves the previous counter state and r5900_perf_end() restores
 * it. This makes the harness composable with debuggers or future project-wide
 * profiling code instead of assuming ownership of COP0 Perf forever.
 */
int r5900_perf_begin(r5900_perf_scope_t *scope,
                     r5900_pcr0_event_t event0,
                     r5900_pcr1_event_t event1);
int r5900_perf_end(r5900_perf_scope_t *scope, r5900_perf_result_t *result);

#endif
