# Baseline, before any optimisation

Recorded at `6a81d1e` with `bench/run.sh`.

    CPU:    AMD Ryzen 5 3600X 6-Core Processor (12 threads)
    Kernel: 7.0.12+kali-amd64
    CC:     gcc (Debian 15.3.0-2) 15.3.0

Target is memfake with every slot in the buffer set to the search value, so
the match set is as large as the buffer allows. That is deliberately the worst
case for the narrowing scan.

| target | matches | initial scan | narrow | pread64 calls |
|---|---|---|---|---|
| 16MB | 2,097,152 | 0.19s | 0.22s | 8,233 |
| 128MB | 16,777,216 | 1.33s | 1.95s | 65,695 |

Medians of 3, each run against a freshly started target.

## What the syscall count says

The narrowing scan reads every match back through `sm_peekdata`, which
refills its cache `PEEKDATA_CHUNK` (2048) bytes at a time. 128MB of matches
divided by 2048 is 65,536, and we measure 65,695 including the initial scan's
1MB reads and a handful of setup. So the narrowing scan is spending one
syscall per 2KB of match data regardless of how the matches are laid out.

`process_vm_readv` accepts up to IOV_MAX (1024 here) remote iovecs per call,
so the same work should collapse to roughly 64 calls instead of 65,536.
