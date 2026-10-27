#pragma once

#include <cstdint>

namespace random
{

enum class Mode { ParkMiller, LegacyCRT };

// Reseeding preserves the selected mode; a new battle selects it explicitly.
void seed(unsigned int val);
void seed(unsigned int val, Mode selected_mode);
Mode mode();
// Raw Rnd() calls since the last seed(); observing does not advance the stream.
// Direct CRT rand()/srand() calls are outside this counter. Like the generator,
// it is intended for the simulation thread, not concurrent access.
std::uint64_t draw_count();

// Presentation work (drawing, sound, camera, interface and effects that the
// engine creates only near the camera) draws from a separate cosmetic stream
// while a CosmeticScope is alive, so the simulation stream and draw_count()
// do not depend on what a particular viewer sees or hears. Scopes nest.
class CosmeticScope {
public:
    CosmeticScope();
    ~CosmeticScope();
    CosmeticScope(const CosmeticScope &) = delete;
    CosmeticScope &operator=(const CosmeticScope &) = delete;
};
// Enters the cosmetic stream only when `enabled`; for work on a cosmetic object.
class CosmeticScopeIf {
public:
    explicit CosmeticScopeIf(bool enabled);
    ~CosmeticScopeIf();
    CosmeticScopeIf(const CosmeticScopeIf &) = delete;
    CosmeticScopeIf &operator=(const CosmeticScopeIf &) = delete;

private:
    bool enabled_;
};
bool cosmetic();
std::uint64_t cosmetic_draw_count();
// Draws requested from threads other than the one that last seeded the stream;
// they are served from the cosmetic stream.
std::uint64_t foreign_thread_draw_count();

// Diagnostic hook called on every simulation-stream draw; null disables it.
using DrawObserver = void (*)(std::uint64_t index);
void set_draw_observer(DrawObserver observer);

// Park-Miller's maximum. Legacy CRT output uses RAND_MAX instead.
inline constexpr int maximum = 2147483645;
int Rnd();
// Floating ranges include both endpoints; reversed bounds are normalized.
double RndFloat();
// Inclusive integer range. Ordinary ranges retain the legacy modulo mapping.
int Rnd(int zmin, int zmax);
double RndFloat(double zmin, double zmax);

} // namespace random

double RND(double from, double to);
float FRND(double x);
float FSRND(double x);
// Select an index in [0, n). Legacy CRT retains rounded endpoint weighting.
// Non-positive counts return zero. Every call consumes one raw draw.
int IRND(int n);
