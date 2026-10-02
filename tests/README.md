# Tests

Regression tests for the processing (detection, gain, Peak Ceiling, export).
They drive the real processor code through a small command-line harness and
check the exported audio numerically.

```bash
bash build.sh                 # Debug build; also produces the Shared Code library
bash tests/build_harness.sh   # builds tests/build/harness (Apple Silicon)
python3 tests/run_tests.py    # all tests; or name some: basic touching metadata ...
```

Needs Python 3 with numpy. The exit status is the number of failed checks.
Results and generated audio go to `tests/build/`, which is not tracked.

The key property checked is that **no sample ever receives more gain than the
clip (or the silence, at 1.0) it belongs to**. That is what keeps the output
under Peak Ceiling through the 10 ms ramps at clip boundaries.

Lines marked `KNOWN` are documented limitations rather than failures.
