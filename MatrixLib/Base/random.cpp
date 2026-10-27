#include "random.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <random>
#include <thread>

namespace {

// Same 16807/2147483647 generator used by the original map simulation.
std::minstd_rand0 generator;
random::Mode active_mode = random::Mode::ParkMiller;
std::uint64_t raw_draw_count = 0;

// Cosmetic stream: never shared between peers; seeding a battle restarts it from that seed too, so
// a battle repeated on one PC also looks the same (camera, effects), while peers that show other
// sides draw it differently without touching the simulation stream.
std::minstd_rand0 cosmetic_generator;
int cosmetic_depth = 0;
std::uint64_t cosmetic_draws = 0;

// The simulation runs on the thread that seeded it. Any other thread (audio,
// loading) is never allowed to advance the simulation stream.
std::thread::id simulation_thread;
std::atomic<std::uint64_t> foreign_draws{0};
std::mutex cosmetic_mutex;
random::DrawObserver draw_observer = nullptr;

int active_maximum() {
    return active_mode == random::Mode::LegacyCRT ? RAND_MAX : random::maximum;
}

}  // namespace

namespace random
{

void seed(unsigned int val)
{
    seed(val, active_mode);
}

void seed(unsigned int val, Mode selected_mode)
{
    active_mode = selected_mode;
    raw_draw_count = 0;
    simulation_thread = std::this_thread::get_id();
    if (active_mode == Mode::LegacyCRT) {
        std::srand(val);
    } else {
        generator.seed(val);
    }
    std::lock_guard<std::mutex> lock(cosmetic_mutex);
    cosmetic_generator.seed(val ^ 0x9E3779B9u);
}

Mode mode()
{
    return active_mode;
}

std::uint64_t draw_count()
{
    return raw_draw_count;
}

CosmeticScope::CosmeticScope()
{
    ++cosmetic_depth;
}

CosmeticScope::~CosmeticScope()
{
    --cosmetic_depth;
}

CosmeticScopeIf::CosmeticScopeIf(bool enabled) : enabled_(enabled)
{
    if (enabled_) ++cosmetic_depth;
}

CosmeticScopeIf::~CosmeticScopeIf()
{
    if (enabled_) --cosmetic_depth;
}

bool cosmetic()
{
    return cosmetic_depth > 0;
}

std::uint64_t cosmetic_draw_count()
{
    return cosmetic_draws;
}

void set_draw_observer(DrawObserver observer)
{
    draw_observer = observer;
}

std::uint64_t foreign_thread_draw_count()
{
    return foreign_draws.load();
}

int Rnd()
{
    if (simulation_thread != std::thread::id{} && std::this_thread::get_id() != simulation_thread) {
        ++foreign_draws;
        std::lock_guard<std::mutex> lock(cosmetic_mutex);
        const auto value = static_cast<int>(cosmetic_generator() - 1);
        return active_mode == Mode::LegacyCRT ? value % (RAND_MAX + 1) : value;
    }
    if (cosmetic_depth > 0) {
        std::lock_guard<std::mutex> lock(cosmetic_mutex);
        ++cosmetic_draws;
        // Keep the active mode's output range so range helpers stay valid.
        const auto value = static_cast<int>(cosmetic_generator() - 1);
        return active_mode == Mode::LegacyCRT ? value % (RAND_MAX + 1) : value;
    }
    ++raw_draw_count;
    if (draw_observer) draw_observer(raw_draw_count);
    return active_mode == Mode::LegacyCRT ? std::rand() : static_cast<int>(generator() - 1);
}

double RndFloat()
{
    return static_cast<double>(Rnd()) / active_maximum();
}

int Rnd(int zmin, int zmax)
{
    if (zmin > zmax) {
        std::swap(zmin, zmax);
    }
    const auto width = static_cast<uint64_t>(static_cast<int64_t>(zmax) - zmin) + 1;
    const uint64_t source_width = static_cast<uint64_t>(active_maximum()) + 1;
    uint64_t value = static_cast<uint64_t>(Rnd());
    uint64_t capacity = source_width;
    while (width > capacity) {
        value = value * source_width + static_cast<uint64_t>(Rnd());
        capacity *= source_width;
    }
    return static_cast<int>(static_cast<int64_t>(zmin) + static_cast<int64_t>(value % width));
}

double RndFloat(double zmin, double zmax)
{
    if (zmin > zmax) {
        std::swap(zmin, zmax);
    }
    return std::lerp(zmin, zmax, RndFloat());
}

} // namespace random

double RND(double from, double to)
{
    return random::RndFloat(from, to);
}

float FRND(double x)
{
    return static_cast<float>(RND(0, x));
}

float FSRND(double x)
{
    return static_cast<float>(RND(-std::abs(x), std::abs(x)));
}

int IRND(int n)
{
    if (random::mode() == random::Mode::LegacyCRT) {
        const uint64_t draw = static_cast<uint64_t>(random::Rnd());
        if (n <= 0) return 0;
        // Positive rounding without floating conversion or overflowing n - 1.
        return static_cast<int>((draw * static_cast<uint64_t>(n - 1) + RAND_MAX / 2) / RAND_MAX);
    }
    return random::Rnd(0, n > 0 ? n - 1 : 0);
}
