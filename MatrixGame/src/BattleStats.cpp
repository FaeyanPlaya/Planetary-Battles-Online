// MatrixGame - licensed under GPLv2 or any later version.
#include "BattleStats.hpp"
#include "Factions.hpp"
#include "PlayerName.hpp"
#include "SettingsModel.hpp"
#include "ScreenLayout.hpp"
#include "MatrixMap.hpp"
#include "MatrixSide.hpp"
#include "StringConstants.hpp"
#include "Interface/MatrixHint.hpp"
#include "CBlockPar.hpp"
#include "3g.hpp"
#include "utils.hpp"

#include <chrono>
#include <format>
#include <memory>
#include <optional>

namespace BattleStats {
namespace {

constexpr auto refresh_every = std::chrono::milliseconds(500);
constexpr int player_width = 150;
constexpr const wchar_t *keys[] = {L"_p_", L"_r_", L"_g_", L"_b_"};

std::optional<std::array<std::wstring, side_count>> g_names;
std::array<bool, side_count> g_gave_up{};
std::array<int, side_count> g_left{};  // 1 left, 2 lost the connection
CMatrixHint *g_hint = nullptr;
bool g_held = false;
unsigned g_seen = 0;    // sides that took part in the battle so far
unsigned g_people = 0;  // sides a person played
std::chrono::steady_clock::time_point g_next;
int g_screen_x = 0, g_screen_y = 0;
std::unique_ptr<CBitmap> g_wide_line;  // the table's row line, as long as the wider table

const wchar_t *text(bool russian, const wchar_t *english, const wchar_t *ru) { return russian ? ru : english; }

bool russian() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
           SettingsModel::Language::Russian;
}

void release() {
    if (g_hint) g_hint->Release();
    g_hint = nullptr;
}

// GetSideById stops the game on a side the map lacks.
CMatrixSideUnit *find_side(int id) {
    for (int i = 0; i < g_MatrixMap->m_SideCnt; ++i)
        if (g_MatrixMap->m_Side[i].m_Id == id) return &g_MatrixMap->m_Side[i];
    return nullptr;
}

void note_sides() {
    if (!g_MatrixMap || !g_MatrixMap->m_Side) return;
    for (int side = 1; side <= side_count; ++side) {
        CMatrixSideUnit *unit = find_side(side);
        if (unit && unit->GetStatus() == SS_ACTIVE) g_seen |= 1u << side;
        if (unit && unit->GetStatus() == SS_ACTIVE && g_MatrixMap->IsHumanSide(side)) g_people |= 1u << side;
    }
}

std::vector<Row> rows(bool ru) {
    std::vector<Row> out;
    const int you = g_MatrixMap->GetPlayerSide() ? g_MatrixMap->GetPlayerSide()->m_Id : 0;
    for (int side = 1; side <= side_count; ++side) {
        CMatrixSideUnit *unit = find_side(side);
        if (!unit || !(g_seen & (1u << side))) continue;
        Row r;
        r.key = g_MatrixMap->StatisticsKey(side);
        r.side = side;
        r.you = side == you;
        const bool gave_up = g_gave_up[side - 1];
        const int left = g_left[side - 1];
        const bool person = (g_people & (1u << side)) != 0;
        r.status = unit->GetStatus() != SS_ACTIVE
                       ? (!person      ? Status::Defeated
                          : left == 1 ? Status::DefeatedLeft
                          : left == 2 ? Status::DefeatedLost
                          : left == 3 ? Status::DefeatedRemoved
                                      : Status::DefeatedWatching)
                   : gave_up     ? (left == 1   ? Status::GaveUpLeft
                                    : left == 2 ? Status::GaveUpLost
                                    : left == 3 ? Status::GaveUpRemoved
                                                : Status::GaveUpWatching)
                   : left == 1   ? Status::Left
                   : left == 2   ? Status::Lost
                   : left == 3   ? Status::Removed
                                 : Status::Fighting;
        if (g_names && !(*g_names)[side - 1].empty()) r.player = (*g_names)[side - 1];
        else if (!g_names && r.you) r.player = PlayerName::load();
        if (r.player.empty())
            r.player = g_MatrixMap->IsHumanSide(side) ? text(ru, L"Player", L"\u0418\u0433\u0440\u043e\u043a") : text(ru, L"AI", L"\u0418\u0418");
        r.numbers = {unit->GetStatValue(STAT_ROBOT_BUILD), unit->GetStatValue(STAT_ROBOT_KILL),
                     unit->GetStatValue(STAT_TURRET_BUILD), unit->GetStatValue(STAT_TURRET_KILL),
                     unit->GetStatValue(STAT_BUILDING_KILL)};
        out.push_back(std::move(r));
    }
    return out;
}

// The parts named Stat of the game's templates, as the end of the battle joins them.
std::wstring stat_template() {
    CBlockPar *bp = g_MatrixData->BlockGet(PAR_TEMPLATES);
    std::wstring templ;
    for (int i = 0; i < bp->ParCount(); ++i) {
        if (bp->ParGetName(i) != TEMPLATE_DIALOG_STATISTICS) continue;
        std::wstring part(bp->ParGet(i));
        if (templ.empty() == (!part.empty() && part[0] == L'|')) continue;
        templ += part;
    }
    return templ;
}

std::wstring colour_tag(std::uint32_t c) {
    return std::format(L"<color={},{},{}>", (c >> 16) & 255, (c >> 8) & 255, c & 255);
}

// stat_hlw: stat_hl with its middle drawn on for the player column.
void prepare_wide_line() {
    if (g_wide_line) return;
    const CBitmap *line = CMatrixHint::FindBitmap(L"stat_hl");
    if (!line) return;
    const int w = line->SizeX(), h = line->SizeY(), half = w / 2;
    g_wide_line = std::make_unique<CBitmap>();
    g_wide_line->CreateRGBA(w + player_width, h);
    g_wide_line->Copy(CPoint(0, 0), CPoint(half, h), *line, CPoint(0, 0));
    for (int x = 0; x < player_width; ++x) g_wide_line->Copy(CPoint(half + x, 0), CPoint(1, h), *line, CPoint(half, 0));
    g_wide_line->Copy(CPoint(half + player_width, 0), CPoint(w - half, h), *line, CPoint(half, 0));
    CMatrixHint::SetRuntimeBitmap(L"stat_hlw", g_wide_line.get());
}

void build() {
    const bool ru = russian();
    const std::wstring templ = stat_template();
    if (templ.empty()) return;
    g_MatrixMap->PrepareStatisticsCards();
    prepare_wide_line();
    CMatrixHint *built = CMatrixHint::Build(fill_template(templ, rows(ru), ru), g_MatrixData->BlockGet(PAR_REPLACE),
                                            TEMPLATE_DIALOG_STATISTICS);
    const auto centre = ScreenLayout::centred_dialog(g_ScreenX, g_ScreenY, built->m_Width, built->m_Height);
    built->Show(centre.x, centre.y);
    release();
    g_hint = built;
    g_screen_x = g_ScreenX;
    g_screen_y = g_ScreenY;
    g_next = std::chrono::steady_clock::now() + refresh_every;
}

}  // namespace

std::wstring fill_template(std::wstring templ, const std::vector<Row> &table, bool ru) {
    // No OK button: the table goes when Tab does.
    utils::replace(templ, L"|_MOD:COPY|_BITMAP:bhole0:C", L"");
    // The rows' lines and the map's name at the right reach over the player column too.
    utils::replace(templ, L"_BITMAP:stat_hl:", L"_BITMAP:stat_hlw:");
    if (const auto planet = templ.find(L"[_planet]"); planet != std::wstring::npos) {
        const auto pos = templ.rfind(L"|_POS:", planet);
        const auto colon = pos == std::wstring::npos ? pos : templ.find(L':', pos + 6);
        if (colon != std::wstring::npos && colon < planet)
            templ.replace(pos + 6, colon - pos - 6, std::to_wstring(std::stoi(templ.substr(pos + 6, colon - pos - 6)) + player_width));
    }
    // The player column, right of the side's card, in the header and every row.
    if (const auto c1 = templ.find(L"|_TEXT:[stat_c1]"); c1 != std::wstring::npos) {
        const auto width = templ.rfind(L"|_WIDTH:", c1);
        if (width != std::wstring::npos)
            templ.insert(width, std::format(L"|_WIDTH:{}|_HEIGHT:33|_FONT:Font.2Normal|_TEXTP:B|_TEXT:{}|_BITMAP:stat_vl:B",
                                            player_width, text(ru, L"Player", L"\u0418\u0433\u0440\u043e\u043a")));
    }
    utils::replace(templ, L"[stat_c6]", text(ru, L"Status", L"\u0421\u0442\u0430\u0442\u0443\u0441"));
    for (const wchar_t *key : keys) {
        const Row *row = nullptr;
        for (const Row &r : table)
            if (r.key == key) row = &r;
        const std::wstring cell = std::wstring(L"[") + key + L"c";
        if (const auto c1 = templ.find(L"|_TEXT:" + cell + L"1]"); c1 != std::wstring::npos && row) {
            // The name, and the faction under it, in the side's colour; then the template's own colour.
            const auto colour_at = templ.rfind(L"|_COLOR:", c1);
            const std::wstring own = colour_at == std::wstring::npos
                                         ? std::wstring(L"FFFFFFFF")
                                         : templ.substr(colour_at + 8, templ.find(L'|', colour_at + 1) - colour_at - 8);
            const std::wstring faction =
                std::wstring(Factions::name(row->side, ru)) + (row->you ? text(ru, L" (you)", L" (\u0432\u044b)") : L"");
            templ.insert(c1, std::format(L"|_WIDTH:{}|_COLOR:{:08X}|_TEXT:{}<br>{}|_BITMAP:stat_vl:B|_WIDTH:98|_COLOR:{}",
                                         player_width, Factions::bright_colour(row->side), row->player, faction, own));
        }
        for (int n = 0; n < 5; ++n)
            utils::replace(templ, cell + std::to_wstring(n + 1) + L"]", row ? std::to_wstring(row->numbers[std::size_t(n)]) : L"");
        std::wstring status;
        if (row) {
            switch (row->status) {
                case Status::Fighting: status = colour_tag(0xFF80E080) + text(ru, L"fighting", L"\u0432 \u0431\u043e\u044e") + L"</color>"; break;
                case Status::GaveUpWatching: status = text(ru, L"gave up<br>(watching)", L"\u0441\u0434\u0430\u043b\u0441\u044f<br>(\u043d\u0430\u0431\u043b\u044e\u0434\u0430\u0435\u0442)"); break;
                case Status::GaveUpLeft: status = text(ru, L"gave up<br>(left)", L"\u0441\u0434\u0430\u043b\u0441\u044f<br>(\u0432\u044b\u0448\u0435\u043b)"); break;
                case Status::GaveUpLost: status = text(ru, L"gave up<br>(connection lost)", L"\u0441\u0434\u0430\u043b\u0441\u044f<br>(\u043d\u0435\u0442 \u0441\u0432\u044f\u0437\u0438)"); break;
                case Status::GaveUpRemoved: status = text(ru, L"gave up<br>(not waited for)", L"\u0441\u0434\u0430\u043b\u0441\u044f<br>(\u043d\u0435 \u0434\u043e\u0436\u0434\u0430\u043b\u0438\u0441\u044c)"); break;
                case Status::Left: status = text(ru, L"left", L"\u0432\u044b\u0448\u0435\u043b"); break;
                case Status::Lost: status = text(ru, L"connection lost", L"\u043d\u0435\u0442 \u0441\u0432\u044f\u0437\u0438"); break;
                case Status::Removed: status = text(ru, L"not waited for", L"\u043d\u0435 \u0434\u043e\u0436\u0434\u0430\u043b\u0438\u0441\u044c"); break;
                case Status::Defeated: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"\u0440\u0430\u0437\u0431\u0438\u0442") + L"</color>"; break;
                case Status::DefeatedWatching: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"\u0440\u0430\u0437\u0431\u0438\u0442") + L"</color><br>" + text(ru, L"(watching)", L"(\u043d\u0430\u0431\u043b\u044e\u0434\u0430\u0435\u0442)"); break;
                case Status::DefeatedLeft: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"\u0440\u0430\u0437\u0431\u0438\u0442") + L"</color><br>" + text(ru, L"(left)", L"(\u0432\u044b\u0448\u0435\u043b)"); break;
                case Status::DefeatedLost: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"\u0440\u0430\u0437\u0431\u0438\u0442") + L"</color><br>" + text(ru, L"(connection lost)", L"(\u043d\u0435\u0442 \u0441\u0432\u044f\u0437\u0438)"); break;
                case Status::DefeatedRemoved: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"\u0440\u0430\u0437\u0431\u0438\u0442") + L"</color><br>" + text(ru, L"(not waited for)", L"(\u043d\u0435 \u0434\u043e\u0436\u0434\u0430\u043b\u0438\u0441\u044c)"); break;
            }
        }
        utils::replace(templ, cell + L"6]", status);
    }
    return templ;
}

void set_names(const std::array<std::wstring, side_count> &names) { g_names = names; }
void clear_names() { g_names.reset(); }

void set_gone(int side, int how) {
    if (side < 1 || side > side_count) return;
    if (how == 0) g_gave_up[side - 1] = true;
    else if (!g_left[side - 1]) g_left[side - 1] = how;
}

void set_back(int side) {
    if (side >= 1 && side <= side_count) g_left[side - 1] = 0;
}

// The battle's end shows the game's own statistics: Tab's would cover it.
bool end_statistics_shown() {
    const wchar *name = g_MatrixMap->m_DialogModeName;
    return FLAG(g_MatrixMap->m_Flags, MMFLAG_DIALOG_MODE | MMFLAG_STAT_DIALOG | MMFLAG_STAT_DIALOG_D) &&
           (FLAG(g_MatrixMap->m_Flags, MMFLAG_STAT_DIALOG | MMFLAG_STAT_DIALOG_D) ||
            (name && (wcscmp(name, TEMPLATE_DIALOG_STATISTICS) == 0 || wcscmp(name, TEMPLATE_DIALOG_STATISTICS_D) == 0)));
}

void hold(bool down) {
    if (down && g_MatrixMap && end_statistics_shown()) down = false;
    if (down == g_held) return;
    g_held = down;
    if (!down) release();
    else if (g_MatrixMap) {
        note_sides();
        build();
    }
}

void update() {
    note_sides();
    if (!g_held || !g_MatrixMap) return;
    if (end_statistics_shown()) {
        hold(false);
        return;
    }
    if (!g_hint || std::chrono::steady_clock::now() >= g_next || g_screen_x != g_ScreenX || g_screen_y != g_ScreenY) build();
}

void clear() {
    release();
    CMatrixHint::SetRuntimeBitmap(L"stat_hlw", nullptr);
    g_wide_line.reset();
    g_held = false;
    g_seen = 0;
    g_people = 0;
    g_gave_up.fill(false);
    g_left.fill(0);
}

}  // namespace BattleStats
