// Multithreaded Image Edge Detection (Sobel Operator)
//
// Generates a synthetic test image, then applies Sobel edge detection
// both single-threaded and multi-threaded (row-partitioned across
// std::thread workers) to compare performance.
//
// Build:  g++ -O2 -std=c++17 -pthread edge_detect.cpp -o edge_detect
// Run:    ./edge_detect

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

struct Image {
    int width, height;
    std::vector<uint8_t> data; // RGB, 3 bytes per pixel

    Image(int w, int h) : width(w), height(h), data(static_cast<size_t>(w) * h * 3, 0) {}

    uint8_t& at(int x, int y, int c) { return data[(static_cast<size_t>(y) * width + x) * 3 + c]; }
    uint8_t at(int x, int y, int c) const { return data[(static_cast<size_t>(y) * width + x) * 3 + c]; }
};

// Synthetic test pattern so the project runs standalone without needing an
// external image file. Swap this out for a real loaded photo if you want.
Image generateTestImage(int width, int height) {
    Image img(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            img.at(x, y, 0) = static_cast<uint8_t>(127 + 127 * std::sin(x * 0.02));
            img.at(x, y, 1) = static_cast<uint8_t>(127 + 127 * std::sin(y * 0.02));
            img.at(x, y, 2) = static_cast<uint8_t>(127 + 127 * std::sin((x + y) * 0.015));
        }
    }
    return img;
}

inline int grayAt(const Image& img, int x, int y) {
    x = std::clamp(x, 0, img.width - 1);
    y = std::clamp(y, 0, img.height - 1);
    int r = img.at(x, y, 0), g = img.at(x, y, 1), b = img.at(x, y, 2);
    return (r * 299 + g * 587 + b * 114) / 1000;
}

// Processes rows [startRow, endRow) of dst, reading from the shared src image.
// Safe to call from multiple threads concurrently: src is read-only, and each
// thread only ever writes to its own disjoint row range of dst, so there is
// no data race and no locking is required.
void sobelRowRange(const Image& src, Image& dst, int startRow, int endRow) {
    static const int gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    static const int gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};

    for (int y = startRow; y < endRow; ++y) {
        for (int x = 0; x < src.width; ++x) {
            int sx = 0, sy = 0;
            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    int g = grayAt(src, x + kx, y + ky);
                    sx += g * gx[ky + 1][kx + 1];
                    sy += g * gy[ky + 1][kx + 1];
                }
            }
            int magnitude = static_cast<int>(std::sqrt(static_cast<double>(sx * sx + sy * sy)));
            uint8_t val = static_cast<uint8_t>(std::clamp(magnitude, 0, 255));
            dst.at(x, y, 0) = dst.at(x, y, 1) = dst.at(x, y, 2) = val;
        }
    }
}

Image sobelSingleThreaded(const Image& src) {
    Image dst(src.width, src.height);
    sobelRowRange(src, dst, 0, src.height);
    return dst;
}

Image sobelMultiThreaded(const Image& src, int numThreads) {
    Image dst(src.width, src.height);
    std::vector<std::thread> threads;
    int rowsPerThread = src.height / numThreads;

    for (int t = 0; t < numThreads; ++t) {
        int startRow = t * rowsPerThread;
        int endRow = (t == numThreads - 1) ? src.height : startRow + rowsPerThread;
        threads.emplace_back(sobelRowRange, std::cref(src), std::ref(dst), startRow, endRow);
    }
    for (auto& th : threads) th.join();
    return dst;
}

void writePPM(const std::string& filename, const Image& img) {
    std::ofstream out(filename, std::ios::binary);
    out << "P6\n" << img.width << " " << img.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(img.data.data()), img.data.size());
}

// Sanity check: multi-threaded output must exactly match single-threaded
// output, since the algorithm is identical -- only the execution is parallel.
bool imagesEqual(const Image& a, const Image& b) {
    return a.width == b.width && a.height == b.height && a.data == b.data;
}

int main() {
    const int WIDTH = 4096;
    const int HEIGHT = 4096;
    unsigned int hwThreads = std::thread::hardware_concurrency();

    std::cout << "Generating " << WIDTH << "x" << HEIGHT << " test image...\n";
    std::cout << "Detected hardware threads: " << (hwThreads ? hwThreads : 1) << "\n\n";

    Image src = generateTestImage(WIDTH, HEIGHT);
    writePPM("input.ppm", src);

    auto t0 = std::chrono::high_resolution_clock::now();
    Image resultSingle = sobelSingleThreaded(src);
    auto t1 = std::chrono::high_resolution_clock::now();
    double singleMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    writePPM("edges_single_thread.ppm", resultSingle);
    std::cout << "Single-threaded : " << singleMs << " ms\n";

    for (int numThreads : {2, 4, 8}) {
        auto t2 = std::chrono::high_resolution_clock::now();
        Image resultMulti = sobelMultiThreaded(src, numThreads);
        auto t3 = std::chrono::high_resolution_clock::now();
        double multiMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
        double speedup = singleMs / multiMs;

        bool correct = imagesEqual(resultSingle, resultMulti);
        std::cout << numThreads << " threads      : " << multiMs << " ms (speedup: "
                   << speedup << "x, correctness: " << (correct ? "PASS" : "FAIL") << ")\n";

        if (numThreads == 8) writePPM("edges_multi_thread.ppm", resultMulti);
    }

    return 0;
}
