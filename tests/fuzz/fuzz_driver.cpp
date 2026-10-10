// Self-contained coverage-guided mutation fuzzer for the PKTool parsers.
//
//   pktool_fuzz <target|all> [--seconds N] [--runs N] [--seed S]
//                [--corpus DIR] [--crash-dir DIR] [--timeout S]
//
// Targets: see fuzz_targets.h. Build with -DPKTOOL_FUZZ=ON (the core is then
// instrumented with -fsanitize-coverage=trace-pc, GCC or clang) and normally
// -DPKTOOL_SANITIZE=ON. Inputs that reach new code are kept; a crash, a failed
// invariant or a hang writes the exact input to <crash-dir>/<kind>-<target>.bin
// first. PKTOOL_FUZZ_SEEDS=<dir> adds extra seed files (for example saves
// that must never be committed).
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/io/file_io.h"
#include "tests/fuzz/fuzz_targets.h"

extern "C" uint8_t pktool_cov_map[1 << 16];
extern "C" void __sanitizer_set_death_callback(void (*)(void)) __attribute__((weak));

namespace {

using namespace pktool;

const std::vector<uint8_t>* g_current = nullptr;
std::string g_crashDir = ".";
std::string g_targetName;

void dumpCurrent(const char* kind) {
    if (!g_current) return;
    std::string path = g_crashDir + "/" + kind + "-" + g_targetName + ".bin";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    const uint8_t* p = g_current->data();
    size_t left = g_current->size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n <= 0) break;
        p += n;
        left -= static_cast<size_t>(n);
    }
    ::close(fd);
    const char msg[] = "input saved as ";
    (void)!::write(2, msg, sizeof msg - 1);
    (void)!::write(2, path.c_str(), path.size());
    (void)!::write(2, "\n", 1);
}

void onDeath() { dumpCurrent("crash"); }

extern "C" void __sanitizer_print_stack_trace(void) __attribute__((weak));

void onSignal(int sig) {
    if (sig == SIGALRM && __sanitizer_print_stack_trace) __sanitizer_print_stack_trace();
    dumpCurrent(sig == SIGALRM ? "timeout" : "crash");
    _exit(sig == SIGALRM ? 3 : 1);
}

bool arg(int argc, char** argv, const char* name, std::string& value) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            value = argv[i + 1];
            return true;
        }
    }
    return false;
}

size_t maxLenFor(const std::string& target) {
    if (target == "save_load" || target == "save_probe") return 0x80000 + 0x200;
    if (target == "bank") return 20 * 30 * 144 + 20 * 16 + 64;
    if (target == "language_pack" || target == "cjk_font") return 200000;
    return 4096;
}

// Returns the number of new edges found.
int runOne(const fuzz::Target& t, const std::vector<uint8_t>& input, uint8_t* seen, int timeoutSeconds) {
    std::memset(pktool_cov_map, 0, sizeof pktool_cov_map);
    g_current = &input;
    ::alarm(static_cast<unsigned>(timeoutSeconds));
    t.fn(input.data(), input.size());
    ::alarm(0);
    int fresh = 0;
    for (size_t i = 0; i < sizeof pktool_cov_map; ++i) {
        if (pktool_cov_map[i] && !seen[i]) {
            seen[i] = 1;
            ++fresh;
        }
    }
    return fresh;
}

int fuzzTarget(const fuzz::Target& t, double seconds, long maxRuns, uint64_t seed, const std::string& corpusDir,
               int timeoutSeconds) {
    g_targetName = t.name;
    std::vector<std::vector<uint8_t>> corpus = fuzz::seeds(t.name);
    std::vector<std::string> extra;
    if (!corpusDir.empty()) {
        for (const std::string& f : io::listFiles(corpusDir, "")) extra.push_back(f);
    }
    if (const char* dir = std::getenv("PKTOOL_FUZZ_SEEDS")) {
        for (const std::string& f : io::listFiles(dir, "")) extra.push_back(f);
    }
    for (const std::string& f : extra) {
        std::vector<uint8_t> bytes;
        if (io::readFile(f, bytes) && bytes.size() <= maxLenFor(t.name)) corpus.push_back(std::move(bytes));
    }
    if (corpus.empty()) corpus.push_back(std::vector<uint8_t>(16, 0));

    std::vector<uint8_t> seen(1 << 16, 0);
    size_t edges = 0;
    long execs = 0;
    for (auto& e : corpus) {
        edges += static_cast<size_t>(runOne(t, e, seen.data(), timeoutSeconds));
        ++execs;
    }
    std::printf("[%s] %zu seeds, %zu edges\n", t.name, corpus.size(), edges);

    fuzz::Rng rng(seed);
    auto start = std::chrono::steady_clock::now();
    auto lastReport = start;
    const size_t maxLen = maxLenFor(t.name);
    std::vector<uint8_t> candidate;
    for (;;) {
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if ((seconds > 0 && elapsed >= seconds) || (maxRuns > 0 && execs >= maxRuns)) break;
        const auto& base = corpus[rng.below(static_cast<uint32_t>(corpus.size()))];
        const auto& other = corpus[rng.below(static_cast<uint32_t>(corpus.size()))];
        candidate = fuzz::mutate(rng, base, &other, maxLen);
        int fresh = runOne(t, candidate, seen.data(), timeoutSeconds);
        ++execs;
        if (fresh > 0 && corpus.size() < 4000) {
            edges += static_cast<size_t>(fresh);
            corpus.push_back(candidate);
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastReport).count() >= 10) {
            lastReport = now;
            std::printf("[%s] %6.0fs execs=%ld (%.0f/s) corpus=%zu edges=%zu\n", t.name, elapsed, execs,
                        static_cast<double>(execs) / (elapsed > 0 ? elapsed : 1), corpus.size(), edges);
            std::fflush(stdout);
        }
    }
    double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("[%s] done: %ld execs in %.1fs, corpus=%zu, edges=%zu, no crash\n", t.name, execs, total,
                corpus.size(), edges);
    if (!corpusDir.empty()) {
        // Keep the small interesting inputs (only ever pass synthetic corpora here).
        io::makeDirectories(corpusDir);
        size_t kept = 0;
        for (size_t i = 0; i < corpus.size(); ++i) {
            if (corpus[i].size() > 64 * 1024) continue;
            std::string path = io::joinPath(corpusDir, std::string(t.name) + "-" + std::to_string(i) + ".bin");
            io::safeWriteFile(path, corpus[i]);
            ++kept;
        }
        std::printf("[%s] saved %zu small corpus files to %s\n", t.name, kept, corpusDir.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <target|all> [--seconds N] [--runs N] [--seed S] [--corpus DIR] [--crash-dir DIR] "
                     "[--timeout S]\ntargets:",
                     argv[0]);
        for (const auto& t : fuzz::targets()) std::fprintf(stderr, " %s", t.name);
        std::fprintf(stderr, "\n");
        return 2;
    }
    std::string v;
    double seconds = arg(argc, argv, "--seconds", v) ? std::atof(v.c_str()) : 60;
    long runs = arg(argc, argv, "--runs", v) ? std::atol(v.c_str()) : 0;
    uint64_t seed = arg(argc, argv, "--seed", v) ? std::strtoull(v.c_str(), nullptr, 10) : 1;
    std::string corpus;
    arg(argc, argv, "--corpus", corpus);
    if (arg(argc, argv, "--crash-dir", v)) g_crashDir = v;
    int timeout = arg(argc, argv, "--timeout", v) ? std::atoi(v.c_str()) : 20;

    if (__sanitizer_set_death_callback) __sanitizer_set_death_callback(onDeath);
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL, SIGALRM}) ::signal(sig, onSignal);

    std::string which = argv[1];
    if (which == "all") {
        for (const auto& t : fuzz::targets()) fuzzTarget(t, seconds, runs, seed, "", timeout);
        return 0;
    }
    const fuzz::Target* t = fuzz::findTarget(which);
    if (!t) {
        std::fprintf(stderr, "unknown target %s\n", which.c_str());
        return 2;
    }
    if (arg(argc, argv, "--replay", v)) {
        // Runs one saved input (a crash-*.bin or timeout-*.bin file) and exits.
        std::vector<uint8_t> input;
        if (!io::readFile(v, input)) {
            std::fprintf(stderr, "cannot read %s\n", v.c_str());
            return 2;
        }
        g_targetName = t->name;
        g_current = &input;
        ::alarm(static_cast<unsigned>(timeout));
        t->fn(input.data(), input.size());
        ::alarm(0);
        std::printf("[%s] replayed %zu bytes, no crash\n", t->name, input.size());
        return 0;
    }
    return fuzzTarget(*t, seconds, runs, seed, corpus, timeout);
}
