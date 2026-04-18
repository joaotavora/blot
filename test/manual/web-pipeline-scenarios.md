# Web pipeline manual test scenarios

Fixture: `test/fixture/gcc-deep-hierarchy` (two TUs: source-1.cpp, source-2.cpp)

Launch:
```
./build-Debug/blot --web \
  --ccj test/fixture/gcc-deep-hierarchy/compile_commands.json \
  test/fixture/gcc-deep-hierarchy
```

---

## Scenario 1 — Abandon mid-cycle

```
click source-1.cpp
while compile badge is spinning:
    click source-2.cpp
wait for source-2 cycle to complete
assert source panel shows source-2 content
assert assembly panel shows source-2 assembly (not source-1's)
assert compile badge reached "done" or "cached" (not stuck)
```

---

## Scenario 2 — Return-to-file cache hit

```
click source-1.cpp
wait for all three phase badges to reach done/cached
click source-2.cpp
wait for all three phase badges to reach done/cached
click source-1.cpp
assert compile badge goes directly to cached (never shows "running")
assert assembly panel shows source-1 assembly
```

---

## Scenario 3 — Options change re-annotates correctly

```
click source-1.cpp
wait for annotate badge to reach done/cached
note the current assembly content
toggle demangle checkbox
assert annotate badge briefly goes idle then running then done (green, NOT blue)
assert assembly panel updates (content differs from before if symbols were mangled)
toggle demangle checkbox back
assert annotate badge reaches cached (blue) — original result is already in cache
assert assembly panel reverts to original content

Variant: navigate away first
click source-2.cpp; wait for done/cached
click source-1.cpp; wait for annotate cached (blue)
toggle demangle checkbox
assert annotate badge becomes done (green) — first time with these opts
```
