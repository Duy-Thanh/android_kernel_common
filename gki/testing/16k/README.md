# Android GKI 16KB Release Testing Plans & Known Failures

This directory contains the Tradefed argument files (`@flag-file`) used by the
TPE GKI Release team (TVCs and Kernel SWEs) to validate `kernel_aarch64_16k`
monthly releases without being blocked by non-kernel userspace test noise.

* **Tracking Bug:** http://b/559889030
* **Design Doc:** https://docs.google.com/document/d/1nadaDZiTdmzF_zKX9TqKjCjc4kHqMdRk_Un684QAeqU/edit

---

## Files

1. `gki_16k_kernel_plan.args` (**Include List for TVC**)
   * Dedicated positive test plan targeting core kernel VTS compliance, LTP,
     filesystem (`f2fs_test`), virtualization (`Microdroid`/`VmMultiTenancy`),
     KeyMint, and OS page-size boundary suites (~12 core modules).
2. `gki_16k_known_failures.args` (**Exclude / Waive List**)
   * Tracks both **Part A (Known Kernel/Architectural Limitations)** and
     **Part B (Non-Kernel Userspace/Framework Noise)** to prevent known
     issues from blocking monthly GKI release SLOs.

---

## Standard Operating Procedure (SOP)

### 1. For TVC (Test Execution)

* **Platform Build (`-pb`):** Always flash a current release platform build
  (e.g., September `tokay_16k` build `16302520` or newer) rather than older
  pinned June builds (`15635118` / `15660275`), which cause false-positive
  Microdroid/AVF failures (http://b/562751750).

Before running CTS/VTS on a 16KB GKI build, sync the latest test lists from
`common` (or `kernel/tests`):

```bash
git pull
```

#### Mode A: Run Dedicated 16K Kernel Release Test Plan (Recommended Monthly Gate)
Pass both the include plan and the known-failures waive list via Tradefed's
native `@file` syntax:

```bash
# For VTS-on-GKI (16KB):
vts-tradefed run commandAndExit vts \
  @gki/testing/16k/gki_16k_kernel_plan.args \
  @gki/testing/16k/gki_16k_known_failures.args

# For CTS-on-GKI (16KB kernel boundary modules):
cts-tradefed run commandAndExit cts \
  @gki/testing/16k/gki_16k_kernel_plan.args \
  @gki/testing/16k/gki_16k_known_failures.args
```

#### Mode B: Run Full CTS/VTS Suite with Known Failures Filtered Out
If executing a full suite run on MTT or Terminal, pass only the exclude list
to eliminate >95% of known userspace/framework failures:

```bash
cts-tradefed run commandAndExit cts \
  @gki/testing/16k/gki_16k_known_failures.args
```

---

### 2. For Kernel SWE (Monthly 4-Hour Triage Time-Box)

When a monthly 16KB test run completes, any remaining failures in the report
represent **new, uncataloged issues**:

1. **Time-Boxed Review:** Spend up to **4 hours (0.5 person-day)** per release
   cycle reviewing newly failed items from the `gki_16k_kernel_plan.args` modules.
2. **Triage & Route:**
   * If a failure is a **genuine new GKI kernel regression**, file/assign a bug
     to the corresponding kernel subsystem owner (e.g. MM/pKVM/Driver).
   * If a failure is a **known architectural limitation or hardcoded 4KB test
     assumption**, file a tracking bug to the test owner.
3. **Update the Waive List:**
   * Add an `--exclude-filter` entry to `gki_16k_known_failures.args` with the
     mandatory comment header:
     `# [b/XXXXXXXXX] [Category] Brief explanation`
   * Upload a CL for Gerrit review (`git push aosp HEAD:refs/for/android17-6.18`).
