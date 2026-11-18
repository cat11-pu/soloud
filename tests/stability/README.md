# Stability tests

Run from the repository root:

```sh
./tests/stability/run_stability_tests.sh
```

The script builds two ASan/UBSan binaries:

- `stability_test_asan` covers illegal initialization parameters, repeated
  `deinit`, repeated voice-group destruction, out-of-range voice handles,
  illegal per-voice sample rates, reconnect cycles and voice churn.
- `capi_destroy_test_asan` creates and destroys a C API `Soloud` handle twice;
  the second destroy must return `INVALID_PARAMETER` instead of freeing twice.

Successful output ends with:

```text
307 checks, 0 failures
STABILITY TEST PASSED
first=0 second=1 null=1
```

Expected error codes:

- Sample rates outside `8000..384000`, channel counts outside `1/2/4/6/8`, and
  buffer sizes outside `16..1048576` return `INVALID_PARAMETER`.
- Handles whose 12-bit voice slot is outside `VOICE_COUNT` are rejected before
  indexing `mVoice[]`.
- Non-positive, NaN or infinite per-voice sample rates/play speeds return
  `INVALID_PARAMETER`.
- A second `destroyVoiceGroup` or C API `Soloud_destroy` returns
  `INVALID_PARAMETER`.
