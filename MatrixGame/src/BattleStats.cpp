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
#include "Text/Font.hpp"

#include <chrono>
#include <format>
#include <memory>
#include <optional>

namespace BattleStats {
namespace {

constexpr auto refresh_every = std::chrono::milliseconds(500);
constexpr int player_width = 150;
constexpr int cell_width = 98, cell_height = 33;  // the template's number cells
constexpr const wchar_t *keys[] = {L"_p_", L"_r_", L"_g_", L"_b_"};

std::optional<std::array<std::wstring, side_count>> g_names;
std::array<bool, side_count> g_gave_up{};
std::array<int, side_count> g_left{};  // 1 left, 2 lost the connection, 3 removed, 4 kicked out
// The host's Kick buttons: the table's last column.
std::vector<Kickable> g_kickable;
int g_kick_armed = -1, g_kick_hover = -1;
std::vector<std::array<int, 4>> g_kick_rects;  // a person's each (0 when the person has no row)
CMatrixHint *g_hint = nullptr;
bool g_held = false;
unsigned g_seen = 0;    // sides that took part in the battle so far
unsigned g_people = 0;  // sides a person played
std::chrono::steady_clock::time_point g_next;
int g_screen_x = 0, g_screen_y = 0;
std::unique_ptr<CBitmap> g_wide_line;  // the table's row line, as long as the wider table
int g_wide_extra = -1;                 // how much longer it is

const wchar_t *text(bool russian, const wchar_t *english, const wchar_t *ru) { return russian ? ru : english; }

bool russian() {
    return SettingsModel::detect_language(g_MatrixData->BlockGet(PAR_REPLACE)->ParGetNE(L"setup")) ==
           SettingsModel::Language::Russian;
}

void release() {
    if (g_hint) g_hint->Release();
    g_hint = nullptr;
    g_kick_rects.clear();
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
                          : left == 4 ? Status::DefeatedKicked
                                      : Status::DefeatedWatching)
                   : gave_up     ? (left == 1   ? Status::GaveUpLeft
                                    : left == 2 ? Status::GaveUpLost
                                    : left == 3 ? Status::GaveUpRemoved
                                    : left == 4 ? Status::GaveUpKicked
                                                : Status::GaveUpWatching)
                   : left == 1   ? Status::Left
                   : left == 2   ? Status::Lost
                   : left == 3   ? Status::Removed
                   : left == 4   ? Status::Kicked
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

// The Kick column's width: a number cell and its line.
int kick_column_width() {
    const CBitmap *vl = CMatrixHint::FindBitmap(L"stat_vl");
    return cell_width + (vl ? vl->SizeX() : 0);
}

// stat_hlw: stat_hl with its middle drawn on for the player column (and the Kick column).
void prepare_wide_line(int extra) {
    if (g_wide_line && g_wide_extra == extra) return;
    const CBitmap *line = CMatrixHint::FindBitmap(L"stat_hl");
    if (!line) return;
    const int w = line->SizeX(), h = line->SizeY(), half = w / 2;
    g_wide_line = std::make_unique<CBitmap>();
    g_wide_line->CreateRGBA(w + extra, h);
    g_wide_line->Copy(CPoint(0, 0), CPoint(half, h), *line, CPoint(0, 0));
    for (int x = 0; x < extra; ++x) g_wide_line->Copy(CPoint(half + x, 0), CPoint(1, h), *line, CPoint(half, 0));
    g_wide_line->Copy(CPoint(half + extra, 0), CPoint(w - half, h), *line, CPoint(half, 0));
    g_wide_extra = extra;
    CMatrixHint::SetRuntimeBitmap(L"stat_hlw", g_wide_line.get());
}

void build() {
    const bool ru = russian();
    const std::wstring templ = stat_template();
    if (templ.empty()) return;
    g_MatrixMap->PrepareStatisticsCards();
    // The host's Kick buttons in their people's rows (by the rows' keys): the template lays them out, MARK says where.
    std::vector<std::wstring> kick;
    std::vector<int> person;  // a marked cell's person, in the template's order
    if (!g_kickable.empty()) {
        kick.resize(std::size(keys));
        const std::vector<std::wstring> cells = kick_cells(g_kickable, g_kick_armed, g_kick_hover, ru);
        const std::vector<Row> present = rows(ru);
        for (std::size_t k = 0; k < std::size(keys); ++k)
            for (std::size_t i = 0; i < g_kickable.size(); ++i)
                if (g_MatrixMap->StatisticsKey(g_kickable[i].side) == keys[k] &&
                    std::any_of(present.begin(), present.end(), [&](const Row &r) { return r.key == keys[k]; })) {
                    kick[k] = cells[i];
                    person.push_back(int(i));
                }
    }
    prepare_wide_line(player_width + (kick.empty() ? 0 : kick_column_width()));
    CMatrixHint *built = CMatrixHint::Build(fill_template(templ, rows(ru), ru, kick), g_MatrixData->BlockGet(PAR_REPLACE),
                                            TEMPLATE_DIALOG_STATISTICS);
    const auto centre = ScreenLayout::centred_dialog(g_ScreenX, g_ScreenY, built->m_Width, built->m_Height);
    built->Show(centre.x, centre.y);
    release();
    g_hint = built;
    g_screen_x = g_ScreenX;
    g_screen_y = g_ScreenY;
    g_kick_rects.assign(g_kickable.size(), {0, 0, 0, 0});
    for (std::size_t m = 0; m < person.size() && int(m) < built->GetCopyPosCnt(); ++m)
        g_kick_rects[std::size_t(person[m])] = {built->m_PosX + built->GetCopyPos(int(m)).x,
                                                built->m_PosY + built->GetCopyPos(int(m)).y, cell_width, cell_height};
    g_next = std::chrono::steady_clock::now() + refresh_every;
}

}  // namespace

std::wstring fill_template(std::wstring templ, const std::vector<Row> &table, bool ru, const std::vector<std::wstring> &kick) {
    // No OK button: the table goes when Tab does.
    utils::replace(templ, L"|_MOD:COPY|_BITMAP:bhole0:C", L"");
    // The rows' lines and the map's name at the right reach over the player column too.
    utils::replace(templ, L"_BITMAP:stat_hl:", L"_BITMAP:stat_hlw:");
    if (const auto planet = templ.find(L"[_planet]"); planet != std::wstring::npos) {
        const auto pos = templ.rfind(L"|_POS:", planet);
        const auto colon = pos == std::wstring::npos ? pos : templ.find(L':', pos + 6);
        if (colon != std::wstring::npos && colon < planet)
            templ.replace(pos + 6, colon - pos - 6, std::to_wstring(std::stoi(templ.substr(pos + 6, colon - pos - 6)) + player_width +
                                                              (kick.empty() ? 0 : kick_column_width())));
    }
    // The player column, right of the side's card, in the header and every row.
    if (const auto c1 = templ.find(L"|_TEXT:[stat_c1]"); c1 != std::wstring::npos) {
        const auto width = templ.rfind(L"|_WIDTH:", c1);
        if (width != std::wstring::npos)
            templ.insert(width, std::format(L"|_WIDTH:{}|_HEIGHT:33|_FONT:Font.2Normal|_TEXTP:B|_TEXT:{}|_BITMAP:stat_vl:B",
                                            player_width, text(ru, L"Player", L"\u0418\u0433\u0440\u043e\u043a")));
    }
    // The host's column of Kick buttons after the status, a cell as the others: the status no longer ends the line.
    const std::wstring status_heading = text(ru, L"Status", L"\u0421\u0442\u0430\u0442\u0443\u0441");
    if (!kick.empty())
        utils::replace(templ, L"|_TEXTP:L|_TEXT:[stat_c6]",
                       std::format(L"|_TEXTP:B|_TEXT:{}|_BITMAP:stat_vl:B|_TEXTP:L|_TEXT:{}", status_heading,
                                   text(ru, L"Kick out?", L"Выгнать?")));
    utils::replace(templ, L"[stat_c6]", status_heading);
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
                case Status::Kicked: status = text(ru, L"kicked out", L"выгнан"); break;
                case Status::GaveUpKicked: status = text(ru, L"gave up<br>(kicked out)", L"сдался<br>(выгнан)"); break;
                case Status::DefeatedKicked: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"разбит") + L"</color><br>" + text(ru, L"(kicked out)", L"(выгнан)"); break;
            }
        }
        if (!kick.empty()) {
            const std::wstring &button = kick[std::size_t(std::find(std::begin(keys), std::end(keys), key) - std::begin(keys))];
            utils::replace(templ, L"|_TEXT:" + cell + L"6]|_MOD:L",
                           L"|_TEXT:" + cell + L"6]|_BITMAP:stat_vl:B" + (button.empty() ? L"" : L"|_MOD:MARK") + L"|_TEXT:" +
                               button + L"|_MOD:L");
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
    else if (!g_left[side - 1] || how == 4) g_left[side - 1] = how == 5 ? 2 : how;  // a kick after leaving still shows
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
    g_kick_armed = g_kick_hover = -1;  // a kick asked again is forgotten with the table
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
    g_kickable.clear();
    g_kick_armed = g_kick_hover = -1;
}

std::vector<std::wstring> kick_cells(const std::vector<Kickable> &people, int armed, int hover, bool ru) {
    std::vector<std::wstring> cells;
    for (std::size_t i = 0; i < people.size(); ++i) {
        const std::wstring label = int(i) == armed ? text(ru, L"Sure?<br>Again", L"Точно?<br>Ещё раз")
                                                   : text(ru, L"Kick out", L"Выгнать");
        const std::uint32_t c = people[i].kicked ? 0xFF808080 : int(i) == hover || int(i) == armed ? 0xFFFF9A8A : 0xFFE05050;
        cells.push_back(colour_tag(c) + label + L"</color>");
    }
    return cells;
}

void set_kickable(std::vector<Kickable> people) {
    if (people == g_kickable) return;
    g_kickable = std::move(people);
    g_kick_armed = g_kick_hover = -1;
    if (g_held && g_MatrixMap) build();
}

bool kick_shown() { return g_held && g_hint && !g_kick_rects.empty(); }

static int kick_index(int x, int y) {
    for (std::size_t i = 0; i < g_kick_rects.size(); ++i) {
        const auto &r = g_kick_rects[i];
        if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]) return int(i);
    }
    return -1;
}

void kick_hover(int x, int y) {
    int now = kick_index(x, y);
    if (now >= 0 && now < int(g_kickable.size()) && g_kickable[std::size_t(now)].kicked) now = -1;
    if (now == g_kick_hover) return;
    g_kick_hover = now;
    if (kick_shown()) build();
}

int kick_click(int x, int y) {
    const int i = kick_index(x, y);
    if (i < 0 || i >= int(g_kickable.size())) return -1;
    if (g_kickable[std::size_t(i)].kicked) return -2;  // (grey: the click is the button's, nothing happens)
    if (g_kick_armed != i) {
        g_kick_armed = i;  // asks again
        build();
        return -2;
    }
    g_kick_armed = -1;
    return g_kickable[std::size_t(i)].peer;
}

std::vector<std::array<int, 4>> kick_rects() { return g_kick_rects; }

}  // namespace BattleStats
