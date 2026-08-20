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

# After

Same machine. Each entry is `bench/run.sh` or the equivalent, medians or best
of 3, against a freshly started target every time.

## Narrowing scan, dense target

The dense case above, 128MB with every slot matching:

| | before | after |
|---|---|---|
| initial scan | 1.33s | 0.58s |
| narrow | 1.95s | 0.48s |
| read syscalls | 65,695 | ~1,100 |

Two changes account for it. `sm_peekdata` reads ahead while the scan walks
forwards instead of refilling 2048 bytes at a time, which is where the syscall
count went. Then `sm_peekdata` and `add_element` got inline fast paths for the
cases the per byte loop actually hits, a cache hit and a contiguous append,
because both were out of line calls being made once per scanned byte.

Worth noting the syscalls fell about 30x while the time fell about 4x. The
scan is limited by the comparison work, not by reading. That is also why
`process_vm_readv` is only worth a few percent end to end despite being 2.8x
faster than `pread` on its own.

## Initial scan, threads

Realistic shape this time, 1000 matches rather than every slot, because the
initial scan cost tracks region size and not match count:

| region | serial | threads=1 | threads=12 |
|---|---|---|---|
| 64MB | 204ms | 208ms | 84ms |
| 256MB | 734ms | 732ms | 208ms |
| 1GB | 2936ms | 2917ms | 702ms |

Short of linear, and it should be. Reading and comparing are shared across
cores but the stitch that turns per chunk records back into swaths is serial,
and so is everything before the scan starts.

A note on measuring this: the first attempt showed threads=1 coming out 10 to
20 percent behind the old serial path, which looked like real overhead from the
chunking. It was two orphaned scanmem processes from earlier timed out runs
sitting at 95% CPU. On an idle machine threads=1 is level with serial. Check
what else is running before believing a regression this size.
