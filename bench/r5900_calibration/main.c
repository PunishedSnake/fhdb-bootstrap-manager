#include <debug.h>
#include <kernel.h>
#include <timer.h>

#include <stdint.h>
#include <string.h>

#include "r5900_perf.h"

#define CALIBRATION_SAMPLES 32u
#define CALIBRATION_ITERATIONS 100000u
#define CALIBRATION_BODY_INSNS_PER_ITERATION 4u

typedef struct {
    uint32_t cycles;
    uint32_t instructions;
    uint32_t timer_busclocks;
    int counter_overflow;
    int timer_overflow;
} calibration_sample_t;

typedef struct {
    uint32_t p50;
    uint32_t p95;
    uint32_t p99;
    uint32_t max;
} calibration_distribution_t;

static volatile uint32_t calibration_sink;

/*
 * Measurement reference kernel, not an application optimization.
 *
 * The loop body deliberately executes four fixed instructions per iteration:
 * two ADDIU operations, BNEZ and its NOP delay slot. The one-time accumulator
 * initialization and function return sit outside that repeated four-instruction
 * body. Keeping this tiny body explicit makes the instruction-completed counter
 * sanity check independent of GCC's loop transforms. NOCLONE keeps a stable
 * callable symbol so CI can validate the emitted reference sequence directly.
 */
static __attribute__((noinline, noclone)) uint32_t calibration_integer_loop(uint32_t iterations)
{
    uint32_t accumulator;

    __asm__ __volatile__(
        "move %0, $zero\n"
        "1:\n"
        "addiu %0, %0, 1\n"
        "addiu %1, %1, -1\n"
        "bnez %1, 1b\n"
        "nop\n"
        : "=&r"(accumulator), "+r"(iterations)
        :
        : "memory");

    return accumulator;
}

static uint32_t timer_delta32(uint64_t start, uint64_t end, int *overflow)
{
    uint64_t delta = end - start;

    if (delta > UINT32_MAX) {
        *overflow = 1;
        return UINT32_MAX;
    }
    *overflow = 0;
    return (uint32_t)delta;
}

static int measure_empty(calibration_sample_t *sample)
{
    r5900_perf_scope_t scope;
    r5900_perf_result_t result;
    uint64_t timer_start;
    uint64_t timer_end;

    memset(&scope, 0, sizeof(scope));
    memset(&result, 0, sizeof(result));
    memset(sample, 0, sizeof(*sample));

    timer_start = GetTimerSystemTime();
    if (r5900_perf_begin(&scope,
                         R5900_PCR0_PROCESSOR_CYCLE,
                         R5900_PCR1_INSTRUCTION_COMPLETED) != 0)
        return -1;
    if (r5900_perf_end(&scope, &result) != 0)
        return -1;
    timer_end = GetTimerSystemTime();

    sample->cycles = result.pcr0;
    sample->instructions = result.pcr1;
    sample->counter_overflow = result.pcr0_overflow || result.pcr1_overflow;
    sample->timer_busclocks = timer_delta32(timer_start, timer_end,
                                            &sample->timer_overflow);
    return 0;
}

static int measure_loop(calibration_sample_t *sample, uint32_t salt)
{
    r5900_perf_scope_t scope;
    r5900_perf_result_t result;
    uint64_t timer_start;
    uint64_t timer_end;
    uint32_t value;

    memset(&scope, 0, sizeof(scope));
    memset(&result, 0, sizeof(result));
    memset(sample, 0, sizeof(*sample));

    timer_start = GetTimerSystemTime();
    if (r5900_perf_begin(&scope,
                         R5900_PCR0_PROCESSOR_CYCLE,
                         R5900_PCR1_INSTRUCTION_COMPLETED) != 0)
        return -1;

    value = calibration_integer_loop(CALIBRATION_ITERATIONS);

    if (r5900_perf_end(&scope, &result) != 0)
        return -1;
    timer_end = GetTimerSystemTime();

    /* Consume the result after the measured region. */
    calibration_sink ^= value + salt;

    sample->cycles = result.pcr0;
    sample->instructions = result.pcr1;
    sample->counter_overflow = result.pcr0_overflow || result.pcr1_overflow;
    sample->timer_busclocks = timer_delta32(timer_start, timer_end,
                                            &sample->timer_overflow);
    return value == CALIBRATION_ITERATIONS ? 0 : -1;
}

static void sort_u32(uint32_t *values, unsigned int count)
{
    unsigned int i;

    for (i = 1; i < count; ++i) {
        uint32_t value = values[i];
        unsigned int j = i;

        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

static uint32_t nearest_rank(const uint32_t *values,
                             unsigned int count,
                             unsigned int percentile)
{
    unsigned int rank = (count * percentile + 99u) / 100u;

    if (rank == 0)
        rank = 1;
    if (rank > count)
        rank = count;
    return values[rank - 1];
}

static calibration_distribution_t distribution(const calibration_sample_t *samples,
                                                 int field)
{
    uint32_t values[CALIBRATION_SAMPLES];
    calibration_distribution_t result;
    unsigned int i;

    for (i = 0; i < CALIBRATION_SAMPLES; ++i) {
        if (field == 0)
            values[i] = samples[i].cycles;
        else if (field == 1)
            values[i] = samples[i].instructions;
        else
            values[i] = samples[i].timer_busclocks;
    }

    sort_u32(values, CALIBRATION_SAMPLES);
    result.p50 = nearest_rank(values, CALIBRATION_SAMPLES, 50);
    result.p95 = nearest_rank(values, CALIBRATION_SAMPLES, 95);
    result.p99 = nearest_rank(values, CALIBRATION_SAMPLES, 99);
    result.max = values[CALIBRATION_SAMPLES - 1];
    return result;
}

static uint32_t subtract_floor(uint32_t value, uint32_t overhead)
{
    return value > overhead ? value - overhead : 0;
}

static void print_distribution(const char *name,
                               const calibration_distribution_t *value)
{
    scr_printf("%-11s p50=%lu p95=%lu p99=%lu max=%lu\n",
               name,
               (unsigned long)value->p50,
               (unsigned long)value->p95,
               (unsigned long)value->p99,
               (unsigned long)value->max);
}

int main(void)
{
    calibration_sample_t empty[CALIBRATION_SAMPLES];
    calibration_sample_t loop[CALIBRATION_SAMPLES];
    calibration_distribution_t empty_cycles;
    calibration_distribution_t empty_instructions;
    calibration_distribution_t empty_timer;
    calibration_distribution_t loop_cycles;
    calibration_distribution_t loop_instructions;
    calibration_distribution_t loop_timer;
    unsigned int counter_overflows = 0;
    unsigned int timer_overflows = 0;
    unsigned int failures = 0;
    unsigned int i;
    uint32_t expected_body_instructions =
        CALIBRATION_ITERATIONS * CALIBRATION_BODY_INSNS_PER_ITERATION;

    init_scr();
    scr_printf("R5900 PERFORMANCE COUNTER CALIBRATION\n");
    scr_printf("No HDD/APA access. Standalone EE benchmark.\n\n");

    /* Warm the reference kernel and its instruction footprint before sampling. */
    for (i = 0; i < 4; ++i)
        calibration_sink ^= calibration_integer_loop(CALIBRATION_ITERATIONS);

    /* Pair samples and alternate order to reduce systematic first/second bias. */
    for (i = 0; i < CALIBRATION_SAMPLES; ++i) {
        int empty_status;
        int loop_status;

        if ((i & 1u) == 0) {
            empty_status = measure_empty(&empty[i]);
            loop_status = measure_loop(&loop[i], i);
        } else {
            loop_status = measure_loop(&loop[i], i);
            empty_status = measure_empty(&empty[i]);
        }

        if (empty_status != 0 || loop_status != 0)
            ++failures;
        if (empty[i].counter_overflow || loop[i].counter_overflow)
            ++counter_overflows;
        if (empty[i].timer_overflow || loop[i].timer_overflow)
            ++timer_overflows;
    }

    empty_cycles = distribution(empty, 0);
    empty_instructions = distribution(empty, 1);
    empty_timer = distribution(empty, 2);
    loop_cycles = distribution(loop, 0);
    loop_instructions = distribution(loop, 1);
    loop_timer = distribution(loop, 2);

    scr_printf("samples=%u iterations=%u\n",
               CALIBRATION_SAMPLES, CALIBRATION_ITERATIONS);
    scr_printf("PCR0=processor cycles PCR1=instructions completed\n");
    scr_printf("timer=GetTimerSystemTime raw BUSCLK units\n\n");

    scr_printf("EMPTY SCOPE\n");
    print_distribution("cycles", &empty_cycles);
    print_distribution("instructions", &empty_instructions);
    print_distribution("timer", &empty_timer);

    scr_printf("\nDETERMINISTIC LOOP\n");
    print_distribution("cycles", &loop_cycles);
    print_distribution("instructions", &loop_instructions);
    print_distribution("timer", &loop_timer);

    scr_printf("\nP50 LOOP - EMPTY\n");
    scr_printf("cycles=%lu instructions=%lu timer=%lu\n",
               (unsigned long)subtract_floor(loop_cycles.p50, empty_cycles.p50),
               (unsigned long)subtract_floor(loop_instructions.p50,
                                             empty_instructions.p50),
               (unsigned long)subtract_floor(loop_timer.p50, empty_timer.p50));
    scr_printf("loop-body instruction floor=%lu\n",
               (unsigned long)expected_body_instructions);
    scr_printf("overflows counter=%u timer=%u failures=%u\n",
               counter_overflows, timer_overflows, failures);
    scr_printf("sink=%08x\n", (unsigned int)calibration_sink);

    if (counter_overflows != 0 || timer_overflows != 0 || failures != 0 ||
        subtract_floor(loop_instructions.p50, empty_instructions.p50) <
            expected_body_instructions) {
        scr_printf("\nRESULT: INVALID - do not use counter claims\n");
    } else {
        scr_printf("\nRESULT: CALIBRATION STRUCTURALLY VALID\n");
        scr_printf("Record screen + SCPH/revision/toolchain provenance.\n");
    }

    SleepThread();
    return 0;
}
