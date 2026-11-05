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
#include "UiTheme.hpp"
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
constexpr const wchar_t *keys[] = {L"_p_", L"_r_", L"_g_", L"_b_"};

std::optional<std::array<std::wstring, side_count>> g_names;
std::array<bool, side_count> g_gave_up{};
std::array<int, side_count> g_left{};  // 1 left, 2 lost the connection, 3 removed, 4 kicked out
// The host's Kick buttons under the table.
constexpr int kick_row = 22, kick_pad_x = 64, kick_pad_top = 22, kick_pad_bottom = 24, kick_min_width = 340;
const std::wstring kick_title_font = L"Font.2Ranger", kick_line_font = L"Font.2Normal";
std::vector<Kickable> g_kickable;
int g_kick_armed = -1, g_kick_hover = -1;
CMatrixHint *g_kick_frame = nullptr;
std::vector<CMatrixHint *> g_kick_buttons;
std::vector<std::array<int, 4>> g_kick_rects;
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

void release_kick() {
    if (g_kick_frame) g_kick_frame->Release();
    g_kick_frame = nullptr;
    for (CMatrixHint *b : g_kick_buttons) b->Release();
    g_kick_buttons.clear();
    g_kick_rects.clear();
}

void release() {
    if (g_hint) g_hint->Release();
    g_hint = nullptr;
    release_kick();
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
    if (g_kickable.empty()) return;
    // The host's Kick buttons in a frame of their own under the table (above it when the screen is too short).
    const KickTemplates t = kick_compose(g_kickable, g_kick_armed, g_kick_hover, ru, [](const std::wstring &line, const std::wstring &font) {
        return int(Text::GetFont(g_D3DD, font).CalcTextWidth(line));
    });
    g_kick_frame = CMatrixHint::Build(t.window, static_cast<CBlockPar *>(nullptr));
    const int x = (g_ScreenX - g_kick_frame->m_Width) / 2;
    int y = centre.y + built->m_Height + 8;
    if (y + g_kick_frame->m_Height > g_ScreenY) y = std::max(0, centre.y - g_kick_frame->m_Height - 8);
    g_kick_frame->Show(x, y);
    for (std::size_t i = 0; i < t.buttons.size(); ++i) {
        CMatrixHint *b = CMatrixHint::Build(t.buttons[i], static_cast<CBlockPar *>(nullptr));
        const int bx = x + kick_pad_x, by = y + t.buttons_y + int(i) * kick_row;
        b->Show(bx, by);
        g_kick_buttons.push_back(b);
        g_kick_rects.push_back({bx, by, t.width - 2 * kick_pad_x, kick_row});
    }
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
                case Status::Kicked: status = text(ru, L"kicked out", L"выгнан"); break;
                case Status::GaveUpKicked: status = text(ru, L"gave up<br>(kicked out)", L"сдался<br>(выгнан)"); break;
                case Status::DefeatedKicked: status = colour_tag(0xFFFF6060) + text(ru, L"defeated", L"разбит") + L"</color><br>" + text(ru, L"(kicked out)", L"(выгнан)"); break;
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

KickTemplates kick_compose(const std::vector<Kickable> &people, int armed, int hover, bool ru,
                           const std::function<int(const std::wstring &text, const std::wstring &font)> &text_width) {
    const std::wstring title = text(ru, L"Kick out of the battle", L"Выгнать из боя");
    const std::wstring line = text(ru, L"For good: the AI plays their side to the end", L"Без возврата: его сторону до конца боя ведёт ИИ");
    std::vector<std::wstring> buttons;
    for (std::size_t i = 0; i < people.size(); ++i) {
        const Kickable &p = people[i];
        const std::wstring who = p.name.empty() ? std::wstring(Factions::name(p.side, ru)) : p.name;
        buttons.push_back(int(i) == armed ? text(ru, L"Kick ", L"Выгнать ") + who + text(ru, L" for sure? Click again", L"? Щёлкните ещё раз")
                                          : text(ru, L"Kick ", L"Выгнать ") + who);
    }
    int inner = std::max(text_width(title, kick_title_font), text_width(line, kick_line_font));
    for (const std::wstring &b : buttons) inner = std::max(inner, text_width(b, kick_line_font));
    KickTemplates t;
    t.width = std::max(kick_min_width, inner + 16 + 2 * kick_pad_x);
    inner = t.width - 2 * kick_pad_x;
    const std::uint32_t colour = UiTheme::pick(0xFF000000, UiTheme::Dark::text);
    t.window = std::format(L"{}|_BORDER:{}:{}:{}:{}|_WIDTH:{}|_HEIGHT:{}|_ALIGN:1:1|_TEXTP:L|_COLOR:{:08X}|_FONT:{}|_TEXT:{}",
                           UiTheme::border(), kick_pad_x, kick_pad_top, kick_pad_x, kick_pad_bottom, inner, kick_row + 6, colour,
                           kick_title_font, title);
    t.window += std::format(L"|_HEIGHT:{}|_FONT:{}|_TEXT:{}", kick_row, kick_line_font, line);
    t.window += std::format(L"|_DOWN:{}", 6 + int(buttons.size()) * kick_row);
    t.buttons_y = kick_pad_top + kick_row + 6 + kick_row + 6;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const std::uint32_t c = int(i) == hover || int(i) == armed ? UiTheme::pick(0xFFE00000, 0xFFFF9A8A)
                                                                   : UiTheme::pick(0xFF8E0000, UiTheme::Dark::error);
        t.buttons.push_back(std::format(L"-1|_FONT:{}|_WIDTH:{}|_HEIGHT:{}|_ALIGN:1:1|_COLOR:{:08X}|_TEXT:{}", kick_line_font, inner,
                                        kick_row, c, buttons[i]));
    }
    return t;
}

void set_kickable(std::vector<Kickable> people) {
    if (people == g_kickable) return;
    g_kickable = std::move(people);
    g_kick_armed = g_kick_hover = -1;
    if (g_held && g_MatrixMap) build();
}

bool kick_shown() { return g_held && g_kick_frame != nullptr; }

static int kick_index(int x, int y) {
    for (std::size_t i = 0; i < g_kick_rects.size(); ++i) {
        const auto &r = g_kick_rects[i];
        if (x >= r[0] && x < r[0] + r[2] && y >= r[1] && y < r[1] + r[3]) return int(i);
    }
    return -1;
}

void kick_hover(int x, int y) {
    const int now = kick_index(x, y);
    if (now == g_kick_hover) return;
    g_kick_hover = now;
    if (kick_shown()) build();
}

int kick_click(int x, int y) {
    const int i = kick_index(x, y);
    if (i < 0 || i >= int(g_kickable.size())) return -1;
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
