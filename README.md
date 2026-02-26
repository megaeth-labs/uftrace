# motrace

motrace is a function tracing tool with its runtime library (libmcount). It records
function entry/exit data and provides report and graph views similar to uftrace.

## Build

1. Configure:
   `./configure`
   Optional out-of-tree build: `./configure --objdir=/path/to/objdir`
2. Build:
   `make`

## Install

`make install` (use `DESTDIR=...` and `prefix=...` if needed).

## Record Examples

Basic function tracing:

```
motrace record --no-libcall -P 'main' -P 'fixed_routine' -P 'cpu_task' -P 'off_cpu_task' ./test_uftrace
```

With Off-CPU tracing:

```
motrace record --no-libcall --offcpu -P 'main' -P 'fixed_routine' -P 'cpu_task' -P 'off_cpu_task' ./test_uftrace
```

Strip binary workflow with external `.sym`:

```
motrace nm --format=sym --output-dir ./test ./test_uftrace_strip
motrace record --with-syms=./test --no-libcall --offcpu -P 'main' -P 'fixed_routine' -P 'cpu_task' -P 'off_cpu_task' ./test_uftrace_strip
```

## Attach Examples

Start target with on-demand attach:

```
MOTRACE_ATTACH=1 LD_PRELOAD=/usr/local/lib/motrace/libmcount.so ./test_uftrace
```

Attach to a running process:

```
motrace attach -p PID --no-libcall --offcpu -P 'main' -P 'fixed_routine' -P 'cpu_task' -P 'off_cpu_task'
```

Example with a concrete PID:

```
motrace attach -p 202963 --no-libcall --offcpu -P 'main' -P 'fixed_routine' -P 'cpu_task' -P 'off_cpu_task'
```

Strip binary attach with external `.sym`:

```
motrace attach -p 245732 --with-syms=./test --no-libcall --offcpu -P 'main' -P 'fixed_routine' -P 'cpu_task' -P 'off_cpu_task'
```

## Report (uftrace-style)

Report summary:

```
motrace report
```

Report with off-CPU fields (requires record with `--offcpu`):

```
motrace report --output-fields=total,self,call,offcpu,offcpu-self
```

Report with external `.sym`:

```
motrace report --with-syms=./test
```

Report fields:
- `Total time`: inclusive wall-clock time spent in the function.
- `Self time`: exclusive wall-clock time (total minus children).
- `Calls`: number of calls.
- `Offcpu time`: total off-CPU time (total minus CPU time).
- `OffcpuSelf`: exclusive off-CPU time (self minus self CPU time).
- `Function`: function name (optional `[Source]` is shown when `--srcline` is set).

## Graph (uftrace-style)

Call graph view:

```
motrace graph
```

Graph with extra fields:

```
motrace graph --output-fields=total,self,offcpu,offcpu-self,addr
```

Graph fields:
- `TOTAL TIME`: inclusive wall-clock time for the node.
- `SELF TIME`: exclusive wall-clock time for the node.
- `OFFCPU`: inclusive off-CPU time (requires record with `--offcpu`).
- `OFFCPU SELF`: exclusive off-CPU time.
- `ADDRESS`: function address (48-bit truncated).
- `FUNCTION`: function name prefixed by `(CALLS)`, indentation shows call depth.
- `[SOURCE]`: optional source location when `--srcline` is set.

Per-function per-TID latency stats (p50/p90/p99/min/max):

```
motrace stats
```

If `--offcpu` was recorded, the output includes a separate `offcpu` section per function.

## Clean

`make clean`

## Tests

`make test`
`make unittest`
`make runtest`
