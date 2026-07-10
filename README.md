# Multithreaded Image Edge Detection (C++)

A C++17 program that applies Sobel edge detection to an image, comparing
single-threaded vs. multi-threaded (`std::thread`) performance.

## What it does

1. Generates a synthetic 4096×4096 RGB test image (no external image
   dependencies needed to run it standalone).
2. Applies a Sobel edge detection filter:
   - **Single-threaded**: processes the whole image on one thread.
   - **Multi-threaded**: splits the image into row ranges, one per thread.
     Each thread only reads the shared source image and writes to its own
     disjoint output rows — no locks needed, and no data races.
3. Verifies correctness: checks that multi-threaded output is byte-for-byte
   identical to single-threaded output.
4. Prints timing and speedup for 2, 4, and 8 threads.
5. Writes `.ppm` images so you can visually inspect the input and edge output.

## Build

```bash
g++ -O2 -std=c++17 -pthread edge_detect.cpp -o edge_detect
```

## Run

```bash
./edge_detect
```

Example output (numbers will vary by machine — run this yourself and use
your own numbers, don't copy these):

```
Generating 4096x4096 test image...
Detected hardware threads: 8

Single-threaded : 803.4 ms
2 threads      : 420.1 ms (speedup: 1.91x, correctness: PASS)
4 threads      : 230.6 ms (speedup: 3.48x, correctness: PASS)
8 threads      : 145.2 ms (speedup: 5.53x, correctness: PASS)
```

> Note: on a single-core machine (e.g. some CI/sandbox environments) you
> will NOT see speedup — thread creation overhead can even make it slower.
> Run this on your own multi-core laptop/desktop to get real numbers.

## Why this design is thread-safe

The image is split by **row range**, and:
- The source image (`src`) is only ever **read**, never written, by worker
  threads — concurrent reads are always safe.
- The destination image (`dst`) is only ever written by a **single** thread
  per row range, and row ranges are disjoint — no two threads ever write to
  the same memory location.

This means no mutexes or atomics are needed — the parallelism is
"embarrassingly parallel" by construction.

## Files

- `edge_detect.cpp` — the full implementation
- `input.ppm` — generated test image
- `edges_single_thread.ppm` / `edges_multi_thread.ppm` — output for visual comparison
