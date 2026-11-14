# Audio engine abnormal-input tests

Run:

```sh
./tests/run_abnormal_tests.sh
```

The test binary uses the null backend and is compiled with AddressSanitizer,
LeakSanitizer and UndefinedBehaviorSanitizer. It covers:

- Zero-length audio sources, zero-length mix requests and zero-length WAV data.
- Sample rates above `SOLOUD_MAX_SAMPLERATE`, including `UINT_MAX`.
- Channel counts outside the supported 1/2/4/6/8 set, including zero.
- Buffer sizes that would overflow internal size arithmetic.
- 200 consecutive `Soloud::init` calls and invalid calls between legal ones.
- A deterministic legal-input mix whose FNV-1a hash is fixed at
  `deeb2a71a2e4470e`.

Malformed engine initialization returns `INVALID_PARAMETER`; malformed WAV
input returns `INVALID_PARAMETER`. Any sanitizer report or hash change fails
the run.
