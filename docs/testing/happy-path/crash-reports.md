# Crash reports

The Debug sandbox, started with `--scene=sdf_minimal`; its folder is `%TEMP%\sandbox\` (`$TMPDIR/sandbox/` elsewhere).

1. Start it: `logs\` has a new `<yyyyMMdd-HHmmss>-<pid>.log`, its first line naming the sandbox, the Debug build and the pid.
2. `/api/dev/crash?kind=nothing`: answered 400, and the sandbox runs on.
3. `/api/dev/crash?kind=segv`: the sandbox exits, stderr and the end of the log both show "Crash: EXCEPTION_ACCESS_VIOLATION" (SIGSEGV elsewhere) and a stack naming `crashOnPurpose` with its file and line, and `crashes\` has `<run>.txt` with the same, and `<run>.dmp` on Windows.
4. Start it again: that run's log has moved from `logs\` into `crashes\` beside its report.
5. Repeat 3 and 4 with `kind=abort`, `kind=throw` (the report reads "uncaught exception: thrown on purpose") and `kind=overflow` (EXCEPTION_STACK_OVERFLOW on Windows; its stack is 64 frames of `recurse`, cut off before `crashOnPurpose`).
