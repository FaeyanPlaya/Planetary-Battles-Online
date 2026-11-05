// MatrixGame - licensed under GPLv2 or any later version.
#include "Lobby.hpp"
#include "BattleRules.hpp"
#include "Mods.hpp"
#include "PlayerName.hpp"

#include "utils.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <iterator>

namespace Lobby {
namespace {

bool parse_int(std::string_view text, int &out) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
    return error == std::errc{} && end == text.data() + text.size();
}

// A PlayerName in UTF-8; empty when the text is not one.
std::wstring decode_name(std::string_view text) {
    std::wstring name;
    try {
        name = utils::to_wstring(text);
    }
    catch (...) {
        return {};
    }
    return PlayerName::valid(name) ? name : std::wstring{};
}

std::string encode_name(const std::wstring &name) {
    return PlayerName::valid(name) ? utils::from_wstring(name) : std::string("-");
}

std::optional<std::vector<ModRef>> parse_mods(std::string_view list, char separator = ',') {
    std::vector<ModRef> mods;
    if (list == "-") return mods;
    while (!list.empty()) {
        const auto comma = list.find(separator);
        const auto item = list.substr(0, comma);
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        const auto colon = item.find(':');
        if (colon == std::string_view::npos) return std::nullopt;
        ModRef mod{std::string(item.substr(0, colon)), std::string(item.substr(colon + 1))};
        if (Mods::id_of(std::wstring(mod.id.begin(), mod.id.end())) != mod.id || mod.hash.empty()) return std::nullopt;
        mods.push_back(std::move(mod));
    }
    return mods;
}

std::string mods_text(const std::vector<ModRef> &mods, char separator) {
    std::string text;
    for (const ModRef &m : mods) text += (text.empty() ? "" : std::string(1, separator)) + m.id + ":" + m.hash;
    return text.empty() ? std::string("-") : text;
}

std::optional<std::vector<Person>> parse_people(std::string_view list) {
    std::vector<Person> people;
    while (!list.empty()) {
        const auto comma = list.find(',');
        const auto item = list.substr(0, comma);
        const auto colon = item.find(':');
        if (colon == std::string_view::npos) return std::nullopt;
        const auto bonus = BattleRules::parse_bonus(item.substr(0, colon));
        auto ready = item.substr(colon + 1);
        int side = 0;
        std::wstring name;
        if (const auto second = ready.find(':'); second != std::string_view::npos) {
            auto chosen = ready.substr(second + 1);
            ready = ready.substr(0, second);
            if (const auto third = chosen.find(':'); third != std::string_view::npos) {
                const auto nick = chosen.substr(third + 1);
                chosen = chosen.substr(0, third);
                if (nick != "-") {
                    name = decode_name(nick);
                    if (name.empty()) return std::nullopt;
                }
            }
            if (!parse_int(chosen, side) || side < 0 || side > BattleRules::side_count) return std::nullopt;
        }
        if (!bonus || (ready != "0" && ready != "1")) return std::nullopt;
        people.push_back({*bonus, ready == "1", side, std::move(name)});
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
    }
    return people;
}

}  // namespace

std::string to_line(const State &state, int you) {
    std::string people, pings, personal;
    for (const Person &p : state.people) {
        people += std::format("{}{}:{}:{}:{}", people.empty() ? "" : ",", BattleRules::bonus_name(p.bonus),
                              p.ready ? 1 : 0, p.side, encode_name(p.name));
        pings += (pings.empty() ? "" : ",") + (p.ping >= 0 ? std::to_string(p.ping) : std::string("-"));
        personal += (personal.empty() ? "" : "|") + mods_text(p.personal, ';');
    }
    return std::format("lobby you={} ai={} manual={} manual_bonus={} delay={} dropped={} resync={} auto={} people={} pings={} maphash={} mods={} mappack={} verified={} personal={} map={}",
                       you, state.ai_resources, state.manual_control ? 1 : 0, state.manual_bonus ? 1 : 0, state.delay,
                       state.dropped_ai ? "ai" : "idle", state.auto_resync ? "on" : "off", state.auto_delay, people, pings,
                       state.map_hash.empty() ? "-" : state.map_hash, mods_text(state.mods, ','),
                       state.map_pack.empty() ? "-" : state.map_pack, state.verified_only ? 1 : 0,
                       personal.empty() ? "-" : personal, utils::from_wstring(state.map));
}

std::vector<ModRef> unverified(const Person &person) {
    std::vector<ModRef> out;
    for (const ModRef &m : person.personal)
        if (!Mods::verified(m.id, m.hash)) out.push_back(m);
    return out;
}

int auto_delay_for(int slowest, int turn_ms) {
    if (turn_ms <= 0) return 200;
    const int needed = std::max(slowest, 0) + turn_ms + 50;
    return std::clamp((needed + turn_ms - 1) / turn_ms, 2, 8) * turn_ms;
}

int typical_round_trip(std::vector<int> sample) {
    if (sample.empty()) return 0;
    std::sort(sample.begin(), sample.end());
    return sample[sample.size() / 2];
}

int delay_turns(const State &state, int turn_ms) {
    if (turn_ms <= 0) return 2;
    if (state.delay > 0) return std::max(2, (state.delay + turn_ms / 2) / turn_ms);
    if (state.auto_delay > 0) return std::max(2, (state.auto_delay + turn_ms / 2) / turn_ms);
    int slowest = 0;
    for (const Person &p : state.people) slowest = std::max(slowest, p.ping);
    return auto_delay_for(slowest, turn_ms) / turn_ms;
}

int next_delay(int delay, int step) {
    constexpr int count = int(std::size(delay_choices));
    int at = 0;
    while (at < count && delay_choices[at] != delay) ++at;
    if (at == count) at = 0;
    return delay_choices[std::clamp(at + step, 0, count - 1)];
}

std::optional<Received> from_line(std::string_view line) {
    constexpr std::string_view head = "lobby ";
    if (!line.starts_with(head)) return std::nullopt;
    const auto map_at = line.find(" map=");
    if (map_at == std::string_view::npos) return std::nullopt;
    Received r;
    r.state.map = utils::to_wstring(line.substr(map_at + 5));
    std::string_view fields = line.substr(head.size(), map_at - head.size());
    std::string_view pings, personal;
    int seen = 0;
    while (!fields.empty()) {
        const auto space = fields.find(' ');
        const auto pair = fields.substr(0, space);
        fields = space == std::string_view::npos ? std::string_view{} : fields.substr(space + 1);
        const auto eq = pair.find('=');
        if (eq == std::string_view::npos) return std::nullopt;
        const auto key = pair.substr(0, eq), value = pair.substr(eq + 1);
        if (key == "you") {
            if (!parse_int(value, r.you)) return std::nullopt;
        }
        else if (key == "ai") {
            if (!parse_int(value, r.state.ai_resources)) return std::nullopt;
        }
        else if (key == "manual" || key == "manual_bonus") {
            if (value != "0" && value != "1") return std::nullopt;
            (key == "manual" ? r.state.manual_control : r.state.manual_bonus) = value == "1";
            continue;  // optional
        }
        else if (key == "delay") {
            if (!parse_int(value, r.state.delay) || r.state.delay < 0 || r.state.delay > 1000) return std::nullopt;
            continue;  // optional
        }
        else if (key == "auto") {
            if (!parse_int(value, r.state.auto_delay) || r.state.auto_delay < 0 || r.state.auto_delay > 1000) return std::nullopt;
            continue;  // optional
        }
        else if (key == "dropped") {
            if (value != "ai" && value != "idle") return std::nullopt;
            r.state.dropped_ai = value == "ai";
            continue;  // optional
        }
        else if (key == "resync") {
            if (value != "on" && value != "off") return std::nullopt;
            r.state.auto_resync = value == "on";
            continue;  // optional
        }
        else if (key == "pings") {
            pings = value;
            continue;  // optional
        }
        else if (key == "maphash") {
            r.state.map_hash = value == "-" ? std::string() : std::string(value);
            continue;  // optional
        }
        else if (key == "mappack") {
            if (value != "-" && (value.find(':') == std::string_view::npos ||
                                 Mods::id_of(std::wstring(value.begin(), value.begin() + value.find(':'))).empty()))
                return std::nullopt;
            r.state.map_pack = value == "-" ? std::string() : std::string(value);
            continue;  // optional
        }
        else if (key == "mods") {
            auto mods = parse_mods(value);
            if (!mods) return std::nullopt;
            r.state.mods = std::move(*mods);
            continue;  // optional
        }
        else if (key == "verified") {
            if (value != "0" && value != "1") return std::nullopt;
            r.state.verified_only = value == "1";
            continue;  // optional
        }
        else if (key == "personal") {
            personal = value;
            continue;  // optional
        }
        else if (key == "people") {
            auto people = parse_people(value);
            if (!people) return std::nullopt;
            r.state.people = std::move(*people);
        }
        else continue;
        ++seen;
    }
    for (Person &p : r.state.people) {
        if (personal.empty()) break;
        const auto bar = personal.find('|');
        auto mods = parse_mods(personal.substr(0, bar), ';');
        if (!mods) return std::nullopt;
        p.personal = std::move(*mods);
        personal = bar == std::string_view::npos ? std::string_view{} : personal.substr(bar + 1);
    }
    for (Person &p : r.state.people) {
        if (pings.empty()) break;
        const auto comma = pings.find(',');
        const auto item = pings.substr(0, comma);
        if (item != "-" && (!parse_int(item, p.ping) || p.ping < 0)) return std::nullopt;
        pings = comma == std::string_view::npos ? std::string_view{} : pings.substr(comma + 1);
    }
    const int count = int(r.state.people.size());
    if (seen != 3 || count < 1 || count > max_people || r.you < 0 || r.you >= count ||
        r.state.ai_resources < 0 || r.state.ai_resources > BattleRules::max_ai_resources)
        return std::nullopt;
    return r;
}

std::string hello_line(BattleChoice::Bonus bonus, const std::string &data, const std::string &build,
                       const std::wstring &name, const std::string &rejoin, const std::vector<ModRef> &personal) {
    return std::format("hello pbo {} bonus={} data={} build={} name={}{}{}", protocol_version, BattleRules::bonus_name(bonus),
                       data, build, encode_name(name), rejoin.empty() ? std::string() : " rejoin=" + rejoin,
                       personal.empty() ? std::string() : " personal=" + mods_text(personal, ';'));
}

std::optional<Hello> parse_hello(std::string_view line) {
    constexpr std::string_view head = "hello pbo ";
    if (!line.starts_with(head)) return std::nullopt;
    line.remove_prefix(head.size());
    const auto space = line.find(' ');
    Hello hello;
    if (!parse_int(line.substr(0, space), hello.version)) return std::nullopt;
    if (space == std::string_view::npos) return hello;  // another version may say less
    std::string_view rest = line.substr(space + 1);
    if (!rest.starts_with("bonus=")) return std::nullopt;
    while (!rest.empty()) {
        const auto end = rest.find(' ');
        const auto pair = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (pair.starts_with("bonus=")) hello.bonus = BattleRules::parse_bonus(pair.substr(6)).value_or(BattleChoice::Bonus::None);
        else if (pair.starts_with("data=")) hello.data = pair.substr(5);
        else if (pair.starts_with("build=")) hello.build = pair.substr(6);
        else if (pair.starts_with("name=")) hello.name = decode_name(pair.substr(5));
        else if (pair.starts_with("rejoin=")) hello.rejoin = pair.substr(7);
        else if (pair.starts_with("personal=")) {
            if (auto mods = parse_mods(pair.substr(9), ';')) hello.personal = std::move(*mods);
        }
    }
    return hello;
}

bool name_taken(const State &state, const std::wstring &name) {
    return !name.empty() &&
           std::any_of(state.people.begin(), state.people.end(), [&](const Person &p) { return PlayerName::same(p.name, name); });
}

bool clients_ready(const State &state) {
    return std::all_of(state.people.begin() + std::min<std::size_t>(1, state.people.size()), state.people.end(),
                       [](const Person &p) { return p.ready; });
}

}  // namespace Lobby
