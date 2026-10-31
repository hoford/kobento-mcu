// docsize: report on-disk .rc size vs parsed in-RAM size for a set of RC documents.
//
//   docsize a.rc b.rc ...
//
// Loads each document with rccore's CoreDocument::initFromBuffer (the same parse the device
// runs) and measures the retained heap across the parse with macOS mstats(). The delta is the
// parsed object graph the engine holds in RAM while the document is shown. This is the "loaded
// size" the device's PSRAM holds (minus per-block heap_caps overhead, which the device adds on
// top). Each document is parsed and freed on its own so the numbers are independent.
#include "rccore/CoreDocument.h"
#include "rccore/WireBuffer.h"

#include <malloc/malloc.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
    // ESP-IDF multi_heap (TLSF) adds a per-allocation block header plus 8-byte alignment.
    // ~12 B/block is a fair estimate of the extra PSRAM each small parse object costs on device.
    constexpr size_t kDeviceBlockOverhead = 12;
    printf("%-30s %9s %11s %6s %8s %11s\n",
           "document", "file_B", "parsed_B", "x", "allocs", "est_dev_B");
    printf("%-30s %9s %11s %6s %8s %11s\n",
           "--------", "------", "--------", "-", "------", "---------");
    for (int i = 1; i < argc; i++) {
        auto data = readFile(argv[i]);
        if (data.empty()) { fprintf(stderr, "skip (empty/missing): %s\n", argv[i]); continue; }
        const size_t fileB = data.size();

        // Warm the allocator with a throwaway parse so first-touch arena growth doesn't land
        // in the measured delta, then measure a clean parse.
        { auto warm = std::make_unique<rccore::CoreDocument>();
          rccore::WireBuffer wb(data.data(), data.size()); warm->initFromBuffer(wb); }

        size_t parsedB = 0, allocs = 0;
        bool ok = false;
        {
            struct mstats m0 = mstats();
            auto doc = std::make_unique<rccore::CoreDocument>();
            rccore::WireBuffer buf(data.data(), data.size());
            ok = doc->initFromBuffer(buf);
            struct mstats m1 = mstats();
            parsedB = m1.bytes_used > m0.bytes_used ? m1.bytes_used - m0.bytes_used : 0;
            allocs = m1.chunks_used > m0.chunks_used ? m1.chunks_used - m0.chunks_used : 0;
        }

        const char* base = argv[i];
        for (const char* p = argv[i]; *p; p++) if (*p == '/') base = p + 1;
        if (!ok) { printf("%-30s %9zu %11s\n", base, fileB, "PARSE_FAIL"); continue; }
        size_t estDev = parsedB + allocs * kDeviceBlockOverhead;
        printf("%-30s %9zu %11zu %5.1fx %8zu %11zu\n",
               base, fileB, parsedB, (double) parsedB / fileB, allocs, estDev);
    }
    return 0;
}
