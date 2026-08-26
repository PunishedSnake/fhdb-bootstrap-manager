#ifndef PS2_HDD_BOOTSTRAP_MANAGER_HDL_PROFILE_H
#define PS2_HDD_BOOTSTRAP_MANAGER_HDL_PROFILE_H

/*
 * Same-source Phase-0 instrumentation switch.
 *
 * PROFILE=1 is the diagnostic build used to collect EE/IOP latency and
 * transport telemetry. PROFILE=0 compiles that telemetry out while preserving
 * the exact HDL pump, prefetch, DMA, cache-coherency and durability paths so a
 * real-console A/B measures profiler perturbation instead of another code path.
 */
#ifndef HDL_PROFILE_ENABLED
#define HDL_PROFILE_ENABLED 1
#endif

#if HDL_PROFILE_ENABLED != 0 && HDL_PROFILE_ENABLED != 1
#error "HDL_PROFILE_ENABLED must be 0 or 1"
#endif

#endif
