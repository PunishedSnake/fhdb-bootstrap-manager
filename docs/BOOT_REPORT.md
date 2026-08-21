# BOOTCHAIN.TXT rendering contract

`BOOTCHAIN.TXT` is a human-readable snapshot of evidence collected by PS2 HDD Bootstrap Manager. In the `0.4.0-dev` Michishirube line, formatting is deliberately separated from evidence acquisition and persistence so the diagnostic text can be regression-tested on an ordinary host.

## Ownership boundary

The report pipeline has three distinct responsibilities:

1. **Evidence acquisition** — `boot_chain_ps2.c` reads FMCB configuration, memory-card modules, `__sysconf`, `__system`, and related filesystem evidence. `main.c` still acquires the active raw payload and its hashes because that crosses the raw-HDD transport boundary.
2. **Rendering** — `boot_report.c` receives a completed `boot_chain_info_t`, explicit `osdStart`/`osdSize`, and application identity strings. It performs no device access and returns one bounded NUL-terminated text image.
3. **Persistence/presentation** — `main.c` writes the rendered bytes to `BOOTCHAIN.TXT`, records the result in `HDDMAN.LOG`, and shows the short console summary.

This separation is intentional. A formatter must never gain the ability to mount PFS, inspect a memory card, change an APA pointer, or decide whether a write is safe.

## Public API

```c
unsigned int boot_report_render(char *buffer, unsigned int capacity,
                                const boot_chain_info_t *info,
                                unsigned int start, unsigned int sectors,
                                const char *application_name,
                                const char *application_version);
```

The function returns the number of report bytes stored, excluding the trailing NUL. For any non-zero output capacity, the destination remains NUL-terminated even if the report is truncated. Invalid required pointers or zero capacity return `0`.

`BOOT_REPORT_SIZE` is currently 16384 bytes. The limit is a memory-safety policy, not part of the on-disk rescue format and not an invitation to silently drop important evidence. If the normal report ever approaches the bound, the sections should be reviewed instead of merely increasing the number.

## Section order

The text format intentionally keeps a stable top-to-bottom order:

1. generator identity;
2. console ROMVER and expected regional FMCB folder;
3. APA `osdStart` / `osdSize` pointer state;
4. active-payload read/KELF results and SHA-256 fingerprints;
5. probable family, confidence, next stage, and identification-method disclaimer;
6. FMCB `Skip_HDD` evidence;
7. memory-card HDD modules grouped by regional folder;
8. downstream HDD/PFS evidence;
9. final assessment and any additional FMCB HDD-module warning.

The report remains advisory diagnostics. Family labels are evidence-based classifications; they are not cryptographic identification of encrypted KELF contents.

## Assessment precedence

Assessment messages retain the Torii ordering:

1. inconsistent `osdStart`/`osdSize` is **critical**;
2. an enabled pointer whose payload cannot be read safely is **critical**;
3. a readable active image without a structurally valid KELF is a **warning**;
4. a valid-looking payload with no recognized downstream environment is a **warning**;
5. otherwise the report states that no structural contradiction was found.

An additional note is appended when external HDD modules are present on a memory card while `Skip_HDD` is enabled, because that combination can explain an apparently ignored FMCB setting.

## Portable regression fixtures

`tests/test_boot_report.c` covers:

- a complete golden report for a disabled bootstrap;
- active payload byte counts and both SHA-256 fingerprints;
- OSDMenu/PSBBN and memory-card module evidence;
- all high-level assessment branches;
- the external-HDD-module/`Skip_HDD` note;
- bounded-output truncation and guaranteed NUL termination.

The golden fixture intentionally checks the complete report, not only selected substrings. Changes to headings, section order, labels, or line breaks therefore require an explicit test update and review instead of quietly changing the diagnostics users paste into bug reports.

## Non-goals

`boot_report.c` must not:

- read or write `hdd0:` sectors;
- mount or unmount PFS;
- read memory cards or USB storage;
- save `BOOTCHAIN.TXT` or append `HDDMAN.LOG`;
- classify a boot chain independently of `boot_chain.c`;
- validate/decrypt/sign KELFs;
- decide whether a rescue/install operation is permitted.

Those boundaries keep diagnostic presentation portable and prevent a seemingly harmless text-format change from acquiring storage side effects.
