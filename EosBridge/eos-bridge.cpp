// eos-bridge - Epic Online Services P2P for Planetary Battles Online.
// The game is 32-bit and the EOS SDK is 64-bit only, so this separate program runs beside the EXE (and outlives the
// game's restarts). The game opens local TCP streams to 127.0.0.1:<port>; each begins with one command line, and the
// bridge carries the stream over EOS P2P (NAT punch-through, else Epic's relays). Commands and answers:
//   WHO                    -> ME <puid> | FAIL <why>                        (then closed)
//   LISTEN <port>          -> OK <puid>, then "ACCEPT <id> <puid>" per stream coming in; closing it stops listening
//   TAKE <id>              -> OK <puid>, then the stream                     (an ACCEPT's stream, within 10 s)
//   CONNECT <puid> <port>  -> OK <puid>, then the stream | FAIL <why>
//   ROOM <port> <build> <text>  a host's room in the list (an EOS lobby) while this stream is open; "DESC <text>"
//                          lines change its text
//   ROOMS <build>          -> "ROOM <puid> <port> <text>" per room of this build, then END (then closed)
// Over EOS (socket "PBO", channel 0, reliable ordered) each packet is a kind byte, the stream's id (4 bytes, given by
// the side that opened it; kind | 0x80 when the sender is that side) and the payload: OPEN port, ACCEPTED, REFUSED,
// DATA bytes, CLOSE.
// MIT License (EosBridge/LICENSE), not the engine's GPL: it links the EOS SDK, which is Epic Games' and is not in
// the repository.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <eos_connect.h>
#include <eos_lobby.h>
#include <eos_logging.h>
#include <eos_p2p.h>
#include <eos_sdk.h>

namespace {

using Clock = std::chrono::steady_clock;

enum Kind : std::uint8_t { Open = 1, Accepted, Refused, Data, Close };
constexpr std::uint8_t opener_bit = 0x80;
constexpr std::size_t header = 5, max_payload = EOS_P2P_MAX_PACKET_SIZE - header;
constexpr std::size_t read_limit = 256 * 1024;  // a stream's bytes waiting for EOS before its socket is read again
constexpr std::size_t write_limit = 64u << 20;  // a stream's bytes waiting for the game; more: the stream is closed
constexpr auto connect_wait = std::chrono::seconds(20), take_wait = std::chrono::seconds(10);
constexpr auto idle_exit = std::chrono::minutes(5);  // no game connected this long: the bridge ends

std::FILE *log_file;

void say(const char *format, ...) {
    char when[32];
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
    std::va_list args;
    va_start(args, format);
    char text[1024];
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (log_file) {
        std::fprintf(log_file, "%s %s\n", when, text);
        std::fflush(log_file);
    }
}

std::filesystem::path exe_folder() {
    wchar_t path[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    return std::filesystem::path(std::wstring(path, n)).parent_path();
}

// A local TCP connection from the game.
struct Local {
    SOCKET socket = INVALID_SOCKET;
    enum class Role { Command, Listen, Pending, Stream, Who, Room, Search } role = Role::Command;
    std::uint32_t key = 0;  // for callbacks (the object may be gone by then)
    int room = 0;           // Room: its key in Bridge::rooms
    std::string in, out;
    bool closing = false;  // close once `out` is sent
    int port = 0;          // Listen: the port
    // Pending / Stream:
    std::string peer;
    std::uint32_t id = 0;
    bool mine = false;  // this side opened it
    std::string to_peer;
    Clock::time_point until{};
};

struct Incoming {  // an OPEN waiting for the game's TAKE
    std::string peer;
    std::uint32_t id;
    Clock::time_point until;
};

// A host's room: an EOS lobby nobody joins, found by its attributes (build, port, desc).
struct Room {
    std::string id;  // the lobby, once made
    bool creating = false, updating = false, gone = false, dirty = true;
    std::string build, desc;
    int port = 0;
    Clock::time_point next{};
};

struct Bridge {
    EOS_HPlatform platform = nullptr;
    EOS_HConnect connect = nullptr;
    EOS_HP2P p2p = nullptr;
    EOS_HLobby lobby = nullptr;
    std::map<int, Room> rooms;
    int next_room = 1;
    std::uint32_t next_key = 1;
    EOS_ProductUserId me = nullptr;
    std::string me_text;
    enum class Login { Working, Done, Failed } login = Login::Working;
    bool fresh = false;
    Clock::time_point retry{};  // a failed login is tried again then
    EOS_P2P_SocketId socket_id{};
    std::vector<std::unique_ptr<Local>> locals;
    std::map<std::uint32_t, Incoming> incoming;  // by the id given in ACCEPT
    std::uint32_t next_id = 1;
    Clock::time_point last_game = Clock::now();
} b;

std::string puid_text(EOS_ProductUserId id) {
    char text[EOS_PRODUCTUSERID_MAX_LENGTH + 1] = {};
    std::int32_t length = sizeof(text);
    if (EOS_ProductUserId_ToString(id, text, &length) != EOS_EResult::EOS_Success) return {};
    return text;
}

bool send_packet(const std::string &peer, std::uint8_t kind, std::uint32_t id, bool mine, const char *data = nullptr,
                 std::size_t size = 0) {
    std::uint8_t packet[EOS_P2P_MAX_PACKET_SIZE];
    packet[0] = kind | (mine ? opener_bit : 0);
    std::memcpy(packet + 1, &id, 4);
    if (size) std::memcpy(packet + header, data, size);
    EOS_P2P_SendPacketOptions o{};
    o.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    o.LocalUserId = b.me;
    o.RemoteUserId = EOS_ProductUserId_FromString(peer.c_str());
    o.SocketId = &b.socket_id;
    o.Channel = 0;
    o.DataLengthBytes = std::uint32_t(header + size);
    o.Data = packet;
    o.bAllowDelayedDelivery = EOS_TRUE;
    o.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
    const EOS_EResult r = EOS_P2P_SendPacket(b.p2p, &o);
    if (r != EOS_EResult::EOS_Success && r != EOS_EResult::EOS_LimitExceeded)
        say("send to %s: %s", peer.c_str(), EOS_EResult_ToString(r));
    return r == EOS_EResult::EOS_Success;
}

void reply(Local &l, const std::string &line) { l.out += line + "\n"; }

Local *local_by_key(std::uint32_t key) {
    for (auto &l : b.locals)
        if (l->key == key) return l.get();
    return nullptr;
}

void *as_data(std::uint32_t key) { return reinterpret_cast<void *>(std::uintptr_t(key)); }
std::uint32_t data_key(void *data) { return std::uint32_t(reinterpret_cast<std::uintptr_t>(data)); }

EOS_Lobby_AttributeData text_attribute(const char *key, const std::string &value) {
    EOS_Lobby_AttributeData d{};
    d.ApiVersion = EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;
    d.Key = key;
    d.Value.AsUtf8 = value.c_str();
    d.ValueType = EOS_ELobbyAttributeType::EOS_AT_STRING;
    return d;
}

void EOS_CALL on_room_made(const EOS_Lobby_CreateLobbyCallbackInfo *i) {
    const auto it = b.rooms.find(int(data_key(i->ClientData)));
    if (it == b.rooms.end()) return;
    Room &r = it->second;
    r.creating = false;
    if (i->ResultCode == EOS_EResult::EOS_Success) {
        r.id = i->LobbyId;
        say("room %d made: %s", it->first, r.id.c_str());
    } else {
        say("room %d: %s", it->first, EOS_EResult_ToString(i->ResultCode));
        r.next = Clock::now() + std::chrono::seconds(10);
    }
}

void EOS_CALL on_room_updated(const EOS_Lobby_UpdateLobbyCallbackInfo *i) {
    const auto it = b.rooms.find(int(data_key(i->ClientData)));
    if (it == b.rooms.end()) return;
    it->second.updating = false;
    if (i->ResultCode != EOS_EResult::EOS_Success) {
        say("room %d update: %s", it->first, EOS_EResult_ToString(i->ResultCode));
        it->second.dirty = true;
    }
}

void EOS_CALL on_room_destroyed(const EOS_Lobby_DestroyLobbyCallbackInfo *i) {
    if (i->ResultCode != EOS_EResult::EOS_Success) say("room destroy: %s", EOS_EResult_ToString(i->ResultCode));
}

// Makes, updates (its text at most every 2 s) and destroys the rooms.
void rooms_tick() {
    if (!b.me) return;
    for (auto it = b.rooms.begin(); it != b.rooms.end();) {
        Room &r = it->second;
        const void *key = as_data(std::uint32_t(it->first));
        if (r.gone && !r.creating) {
            if (!r.id.empty()) {
                EOS_Lobby_DestroyLobbyOptions o{};
                o.ApiVersion = EOS_LOBBY_DESTROYLOBBY_API_LATEST;
                o.LocalUserId = b.me;
                o.LobbyId = r.id.c_str();
                EOS_Lobby_DestroyLobby(b.lobby, &o, nullptr, on_room_destroyed);
                say("room %d closed", it->first);
            }
            it = b.rooms.erase(it);
            continue;
        }
        if (r.id.empty() && !r.creating && Clock::now() >= r.next) {
            EOS_Lobby_CreateLobbyOptions o{};
            o.ApiVersion = EOS_LOBBY_CREATELOBBY_API_LATEST;
            o.LocalUserId = b.me;
            o.MaxLobbyMembers = 4;
            o.PermissionLevel = EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
            o.bPresenceEnabled = EOS_FALSE;
            o.bAllowInvites = EOS_FALSE;
            o.BucketId = "pbo";
            o.bDisableHostMigration = EOS_TRUE;
            r.creating = true;
            EOS_Lobby_CreateLobby(b.lobby, &o, const_cast<void *>(key), on_room_made);
        } else if (!r.id.empty() && r.dirty && !r.updating && Clock::now() >= r.next) {
            EOS_Lobby_UpdateLobbyModificationOptions mo{};
            mo.ApiVersion = EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;
            mo.LocalUserId = b.me;
            mo.LobbyId = r.id.c_str();
            EOS_HLobbyModification m = nullptr;
            if (EOS_Lobby_UpdateLobbyModification(b.lobby, &mo, &m) == EOS_EResult::EOS_Success) {
                const std::string port = std::to_string(r.port);
                for (const auto &[name, value] : {std::pair<const char *, const std::string *>{"build", &r.build},
                                                  {"port", &port}, {"desc", &r.desc}}) {
                    const EOS_Lobby_AttributeData d = text_attribute(name, *value);
                    EOS_LobbyModification_AddAttributeOptions a{};
                    a.ApiVersion = EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;
                    a.Attribute = &d;
                    a.Visibility = EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
                    EOS_LobbyModification_AddAttribute(m, &a);
                }
                EOS_Lobby_UpdateLobbyOptions u{};
                u.ApiVersion = EOS_LOBBY_UPDATELOBBY_API_LATEST;
                u.LobbyModificationHandle = m;
                r.updating = true;
                r.dirty = false;
                say("room %d: build %s, port %d, %s", it->first, r.build.c_str(), r.port, r.desc.c_str());
                r.next = Clock::now() + std::chrono::seconds(2);
                EOS_Lobby_UpdateLobby(b.lobby, &u, const_cast<void *>(key), on_room_updated);
                EOS_LobbyModification_Release(m);
            }
        }
        ++it;
    }
}

std::string room_attribute(EOS_HLobbyDetails details, const char *key) {
    EOS_LobbyDetails_CopyAttributeByKeyOptions o{};
    o.ApiVersion = EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;
    o.AttrKey = key;
    EOS_Lobby_Attribute *a = nullptr;
    if (EOS_LobbyDetails_CopyAttributeByKey(details, &o, &a) != EOS_EResult::EOS_Success || !a) return {};
    std::string value = a->Data && a->Data->ValueType == EOS_ELobbyAttributeType::EOS_AT_STRING && a->Data->Value.AsUtf8
                            ? a->Data->Value.AsUtf8
                            : "";
    EOS_Lobby_Attribute_Release(a);
    // Anybody's room: its text goes to the game as one line, so it holds no line end, and is no longer than ours are.
    if (value.size() > 512) value.resize(512);
    for (char &ch : value)
        if (ch == '\n' || ch == '\r') ch = ' ';
    return value;
}

std::map<std::uint32_t, EOS_HLobbySearch> searches;  // by the asking stream's key

void EOS_CALL on_found(const EOS_LobbySearch_FindCallbackInfo *i) {
    const std::uint32_t key = data_key(i->ClientData);
    const auto it = searches.find(key);
    if (it == searches.end()) return;
    Local *l = local_by_key(key);
    if (l && i->ResultCode == EOS_EResult::EOS_Success) {
        EOS_LobbySearch_GetSearchResultCountOptions c{};
        c.ApiVersion = EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
        const std::uint32_t count = EOS_LobbySearch_GetSearchResultCount(it->second, &c);
        say("rooms found: %u", count);
        for (std::uint32_t n = 0; n < count; ++n) {
            EOS_LobbySearch_CopySearchResultByIndexOptions o{};
            o.ApiVersion = EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
            o.LobbyIndex = n;
            EOS_HLobbyDetails details = nullptr;
            if (EOS_LobbySearch_CopySearchResultByIndex(it->second, &o, &details) != EOS_EResult::EOS_Success) continue;
            EOS_LobbyDetails_GetLobbyOwnerOptions w{};
            w.ApiVersion = EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
            const std::string owner = puid_text(EOS_LobbyDetails_GetLobbyOwner(details, &w));
            const std::string port = room_attribute(details, "port"), desc = room_attribute(details, "desc");
            if (!owner.empty() && !port.empty()) reply(*l, "ROOM " + owner + " " + port + " " + desc);
            EOS_LobbyDetails_Release(details);
        }
    } else if (i->ResultCode != EOS_EResult::EOS_NotFound)
        say("rooms: %s", EOS_EResult_ToString(i->ResultCode));
    if (l) {
        reply(*l, "END");
        l->closing = true;
    }
    EOS_LobbySearch_Release(it->second);
    searches.erase(it);
}

Local *stream(const std::string &peer, std::uint32_t id, bool mine) {
    for (auto &l : b.locals)
        if ((l->role == Local::Role::Stream || l->role == Local::Role::Pending) && l->peer == peer && l->id == id &&
            l->mine == mine)
            return l.get();
    return nullptr;
}

Local *listener(int port) {
    for (auto &l : b.locals)
        if (l->role == Local::Role::Listen && l->port == port && !l->closing) return l.get();
    return nullptr;
}

void drop(Local &l, const char *why) {
    if (l.role == Local::Role::Room)
        if (const auto it = b.rooms.find(l.room); it != b.rooms.end()) it->second.gone = true;
    if (l.role == Local::Role::Stream || l.role == Local::Role::Pending) {
        if (b.me) send_packet(l.peer, Close, l.id, l.mine);
        say("stream %s/%u%s closed: %s", l.peer.c_str(), l.id, l.mine ? " (ours)" : "", why);
    }
    l.closing = true;
    l.role = Local::Role::Who;  // nothing more is forwarded
}

void command(Local &l, const std::string &line) {
    char word[16] = {}, arg[64] = {};
    int number = 0;
    std::sscanf(line.c_str(), "%15s %63s %d", word, arg, &number);
    const std::string w = word;
    if (b.login == Bridge::Login::Failed) {
        reply(l, "FAIL login");
        l.closing = true;
    } else if (w == "WHO") {
        reply(l, "ME " + b.me_text);
        l.closing = true;
    } else if (w == "LISTEN") {
        l.role = Local::Role::Listen;
        l.port = std::atoi(arg);
        reply(l, "OK " + b.me_text);
        say("listening on %d", l.port);
    } else if (w == "TAKE") {
        const auto it = b.incoming.find(std::uint32_t(std::strtoul(arg, nullptr, 10)));
        if (it == b.incoming.end()) {
            reply(l, "FAIL gone");
            l.closing = true;
            return;
        }
        l.role = Local::Role::Stream;
        l.peer = it->second.peer;
        l.id = it->second.id;
        l.mine = false;
        b.incoming.erase(it);
        send_packet(l.peer, Accepted, l.id, false);
        reply(l, "OK " + l.peer);
        say("stream %s/%u taken", l.peer.c_str(), l.id);
    } else if (w == "ROOM") {
        l.role = Local::Role::Room;
        l.room = b.next_room++;
        Room &r = b.rooms[l.room];
        r.port = std::atoi(arg);
        char build[64] = {};
        int text_at = 0;
        std::sscanf(line.c_str(), "%*15s %*63s %63s %n", build, &text_at);
        r.build = build;
        r.desc = text_at > 0 ? line.substr(std::size_t(text_at)) : "";
        say("room %d for port %d", l.room, r.port);
    } else if (w == "ROOMS") {
        l.role = Local::Role::Search;
        EOS_Lobby_CreateLobbySearchOptions o{};
        o.ApiVersion = EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;
        o.MaxResults = 20;
        EOS_HLobbySearch search = nullptr;
        if (EOS_Lobby_CreateLobbySearch(b.lobby, &o, &search) != EOS_EResult::EOS_Success) {
            reply(l, "END");
            l.closing = true;
            return;
        }
        const std::string build = arg;
        say("looking for rooms of %s", build.c_str());
        const EOS_Lobby_AttributeData d = text_attribute("build", build);
        EOS_LobbySearch_SetParameterOptions p{};
        p.ApiVersion = EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;
        p.Parameter = &d;
        p.ComparisonOp = EOS_EComparisonOp::EOS_CO_EQUAL;
        EOS_LobbySearch_SetParameter(search, &p);
        EOS_LobbySearch_FindOptions f{};
        f.ApiVersion = EOS_LOBBYSEARCH_FIND_API_LATEST;
        f.LocalUserId = b.me;
        searches[l.key] = search;
        EOS_LobbySearch_Find(search, &f, as_data(l.key), on_found);
    } else if (w == "CONNECT") {
        l.role = Local::Role::Pending;
        l.peer = arg;
        l.id = b.next_id++;
        l.mine = true;
        l.port = number;
        l.until = Clock::now() + connect_wait;
        const std::uint32_t port = std::uint32_t(number);
        send_packet(l.peer, Open, l.id, true, reinterpret_cast<const char *>(&port), 4);
        say("connecting to %s port %d as %u", l.peer.c_str(), number, l.id);
    } else {
        reply(l, "FAIL command");
        l.closing = true;
    }
}

void packet(const std::string &peer, const std::uint8_t *data, std::uint32_t size) {
    if (size < header) return;
    const std::uint8_t kind = data[0] & ~opener_bit;
    const bool from_opener = data[0] & opener_bit;
    std::uint32_t id;
    std::memcpy(&id, data + 1, 4);
    const char *payload = reinterpret_cast<const char *>(data + header);
    const std::size_t length = size - header;
    if (kind == Open) {
        std::uint32_t port = 0;
        if (length >= 4) std::memcpy(&port, payload, 4);
        Local *l = listener(int(port));
        if (!l) {
            send_packet(peer, Refused, id, false);
            say("%s asked for port %u: nobody listens", peer.c_str(), port);
            return;
        }
        if (b.incoming.size() >= 64) {  // (the game takes each within moments: so many are a flood)
            send_packet(peer, Refused, id, false);
            return;
        }
        const std::uint32_t local_id = b.next_id++;
        b.incoming[local_id] = {peer, id, Clock::now() + take_wait};
        reply(*l, "ACCEPT " + std::to_string(local_id) + " " + peer);
        say("%s opens %u on port %u", peer.c_str(), id, port);
        return;
    }
    Local *l = stream(peer, id, !from_opener);
    if (!l) {
        if (kind != Close) send_packet(peer, Close, id, !from_opener);  // a stream this side no longer has
        return;
    }
    if (kind == Accepted && l->role == Local::Role::Pending) {
        l->role = Local::Role::Stream;
        reply(*l, "OK " + peer);
        say("stream %s/%u accepted", peer.c_str(), id);
    } else if (kind == Refused && l->role == Local::Role::Pending) {
        reply(*l, "FAIL refused");
        drop(*l, "refused");
    } else if (kind == Data && l->role == Local::Role::Stream) {
        l->out.append(payload, length);
        // The game reads its socket every frame: so much waiting for it is a peer filling this PC's memory.
        if (l->out.size() > write_limit) {
            l->out.clear();
            drop(*l, "more came than the game takes");
        }
    } else if (kind == Close) {
        l->role = Local::Role::Who;  // the other end closed it: no CLOSE back
        l->closing = true;
        say("stream %s/%u closed by the other end", peer.c_str(), id);
    }
}

void EOS_CALL on_log(const EOS_LogMessage *m) { say("[eos] %s: %s", m->Category, m->Message); }

void EOS_CALL on_request(const EOS_P2P_OnIncomingConnectionRequestInfo *info) {
    EOS_P2P_AcceptConnectionOptions o{};
    o.ApiVersion = EOS_P2P_ACCEPTCONNECTION_API_LATEST;
    o.LocalUserId = b.me;
    o.RemoteUserId = info->RemoteUserId;
    o.SocketId = &b.socket_id;
    EOS_P2P_AcceptConnection(b.p2p, &o);
}

void EOS_CALL on_closed(const EOS_P2P_OnRemoteConnectionClosedInfo *info) {
    const std::string peer = puid_text(info->RemoteUserId);
    say("connection with %s closed (%d)", peer.c_str(), int(info->Reason));
    for (auto &l : b.locals)
        if ((l->role == Local::Role::Stream || l->role == Local::Role::Pending) && l->peer == peer) {
            if (l->role == Local::Role::Pending) reply(*l, "FAIL closed");
            l->role = Local::Role::Who;
            l->closing = true;
        }
}

void logged_in(EOS_ProductUserId user) {
    b.me = user;
    b.me_text = puid_text(user);
    b.login = Bridge::Login::Done;
    say("logged in as %s", b.me_text.c_str());
    EOS_P2P_AddNotifyPeerConnectionRequestOptions r{};
    r.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;
    r.LocalUserId = b.me;
    r.SocketId = &b.socket_id;
    EOS_P2P_AddNotifyPeerConnectionRequest(b.p2p, &r, nullptr, on_request);
    EOS_P2P_AddNotifyPeerConnectionClosedOptions c{};
    c.ApiVersion = EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST;
    c.LocalUserId = b.me;
    c.SocketId = &b.socket_id;
    EOS_P2P_AddNotifyPeerConnectionClosed(b.p2p, &c, nullptr, on_closed);
    EOS_P2P_SetPacketQueueSizeOptions q{};
    q.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
    q.IncomingPacketQueueMaxSizeBytes = 16 * 1024 * 1024;
    q.OutgoingPacketQueueMaxSizeBytes = 16 * 1024 * 1024;
    EOS_P2P_SetPacketQueueSize(b.p2p, &q);
}

void EOS_CALL on_user(const EOS_Connect_CreateUserCallbackInfo *i) {
    if (i->ResultCode == EOS_EResult::EOS_Success) return logged_in(i->LocalUserId);
    say("create user: %s", EOS_EResult_ToString(i->ResultCode));
    b.login = Bridge::Login::Failed;
    b.retry = Clock::now() + std::chrono::seconds(15);
}

void EOS_CALL on_login(const EOS_Connect_LoginCallbackInfo *i) {
    if (i->ResultCode == EOS_EResult::EOS_Success) return logged_in(i->LocalUserId);
    if (i->ResultCode == EOS_EResult::EOS_InvalidUser) {
        EOS_Connect_CreateUserOptions o{};
        o.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
        o.ContinuanceToken = i->ContinuanceToken;
        EOS_Connect_CreateUser(b.connect, &o, nullptr, on_user);
        return;
    }
    say("login: %s", EOS_EResult_ToString(i->ResultCode));
    b.login = Bridge::Login::Failed;
    b.retry = Clock::now() + std::chrono::seconds(15);
}

void login() {
    EOS_Connect_Credentials c{};
    c.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    c.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_UserLoginInfo u{};
    u.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    u.DisplayName = "PBO";
    EOS_Connect_LoginOptions o{};
    o.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
    o.Credentials = &c;
    o.UserLoginInfo = &u;
    EOS_Connect_Login(b.connect, &o, nullptr, on_login);
}

void EOS_CALL on_device(const EOS_Connect_CreateDeviceIdCallbackInfo *i) {
    if (i->ResultCode == EOS_EResult::EOS_Success || i->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed) return login();
    say("device id: %s", EOS_EResult_ToString(i->ResultCode));
    b.login = Bridge::Login::Failed;
    b.retry = Clock::now() + std::chrono::seconds(15);
}

void create_device() {
    EOS_Connect_CreateDeviceIdOptions d{};
    d.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    d.DeviceModel = "PC";
    EOS_Connect_CreateDeviceId(b.connect, &d, nullptr, on_device);
}

void EOS_CALL on_deleted(const EOS_Connect_DeleteDeviceIdCallbackInfo *) { create_device(); }

bool start_eos(const std::filesystem::path &ids_file) {
    std::map<std::string, std::string> id;
    std::ifstream f(ids_file);
    for (std::string line; std::getline(f, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (const auto eq = line.find('='); eq != std::string::npos) id[line.substr(0, eq)] = line.substr(eq + 1);
    }
    if (id["ProductId"].empty() || id["ClientId"].empty()) {
        say("no EOS ids in %s", ids_file.string().c_str());
        return false;
    }
    EOS_InitializeOptions init{};
    init.ApiVersion = EOS_INITIALIZE_API_LATEST;
    init.ProductName = "Planetary Battles Online";
    init.ProductVersion = "1.0";
    if (const EOS_EResult r = EOS_Initialize(&init); r != EOS_EResult::EOS_Success) {
        say("initialize: %s", EOS_EResult_ToString(r));
        return false;
    }
    EOS_Logging_SetCallback(on_log);
    EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Warning);
    EOS_Platform_Options p{};
    p.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    p.ProductId = id["ProductId"].c_str();
    p.SandboxId = id["SandboxId"].c_str();
    p.DeploymentId = id["DeploymentId"].c_str();
    p.ClientCredentials.ClientId = id["ClientId"].c_str();
    p.ClientCredentials.ClientSecret = id["ClientSecret"].c_str();
    p.Flags = EOS_PF_DISABLE_OVERLAY;
    b.platform = EOS_Platform_Create(&p);
    if (!b.platform) {
        say("platform: not created");
        return false;
    }
    b.connect = EOS_Platform_GetConnectInterface(b.platform);
    b.p2p = EOS_Platform_GetP2PInterface(b.platform);
    b.lobby = EOS_Platform_GetLobbyInterface(b.platform);
    b.socket_id.ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    std::strcpy(b.socket_id.SocketName, "PBO");
    if (b.fresh) {  // a test's second identity on the same PC (the device id is per Windows user)
        EOS_Connect_DeleteDeviceIdOptions o{};
        o.ApiVersion = EOS_CONNECT_DELETEDEVICEID_API_LATEST;
        EOS_Connect_DeleteDeviceId(b.connect, &o, nullptr, on_deleted);
    } else
        create_device();
    return true;
}

void receive() {
    if (!b.me) return;
    EOS_P2P_ReceivePacketOptions o{};
    o.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    o.LocalUserId = b.me;
    o.MaxDataSizeBytes = EOS_P2P_MAX_PACKET_SIZE;
    std::uint8_t data[EOS_P2P_MAX_PACKET_SIZE];
    for (int n = 0; n < 4096; ++n) {
        EOS_ProductUserId from = nullptr;
        EOS_P2P_SocketId socket{};
        std::uint8_t channel = 0;
        std::uint32_t size = 0;
        if (EOS_P2P_ReceivePacket(b.p2p, &o, &from, &socket, &channel, data, &size) != EOS_EResult::EOS_Success) break;
        packet(puid_text(from), data, size);
    }
}

// Moves bytes between the game's sockets and EOS; false: the socket is gone.
bool pump(Local &l) {
    char buffer[16384];
    while (!l.closing && (l.role != Local::Role::Stream || l.to_peer.size() < read_limit)) {
        const int n = recv(l.socket, buffer, sizeof(buffer), 0);
        if (n == 0 || (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK)) {
            // What came before the close goes first (a host's "refused ..." line and then its close).
            if (l.role == Local::Role::Stream)
                while (!l.to_peer.empty()) {
                    const std::size_t size = std::min(l.to_peer.size(), max_payload);
                    if (!send_packet(l.peer, Data, l.id, l.mine, l.to_peer.data(), size)) break;
                    l.to_peer.erase(0, size);
                }
            drop(l, "the game closed it");
            l.out.clear();
            return false;
        }
        if (n < 0) break;
        if (l.role == Local::Role::Stream) {
            l.to_peer.append(buffer, n);
            continue;
        }
        l.in.append(buffer, n);
        if (l.role == Local::Role::Room) {
            for (std::size_t end; (end = l.in.find('\n')) != std::string::npos; l.in.erase(0, end + 1)) {
                std::string desc = l.in.substr(0, end);
                if (!desc.empty() && desc.back() == '\r') desc.pop_back();
                if (!desc.starts_with("DESC ")) continue;
                Room &r = b.rooms[l.room];
                if (r.desc != desc.substr(5)) r.desc = desc.substr(5), r.dirty = true;
            }
            continue;
        }
        if (l.role != Local::Role::Command) continue;  // a listener says nothing more
        if (const auto end = l.in.find('\n'); end != std::string::npos) {
            std::string line = l.in.substr(0, end), rest = l.in.substr(end + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            l.in.clear();
            if (b.login == Bridge::Login::Working) {  // answered once logged in
                l.in = line + "\n" + rest;
                break;
            }
            command(l, line);
            if (l.role == Local::Role::Stream || l.role == Local::Role::Pending) l.to_peer += rest;
        }
    }
    if (l.role == Local::Role::Command && b.login != Bridge::Login::Working && l.in.find('\n') != std::string::npos) {
        std::string line = l.in.substr(0, l.in.find('\n')), rest = l.in.substr(l.in.find('\n') + 1);
        l.in.clear();
        command(l, line);
        if (l.role == Local::Role::Stream || l.role == Local::Role::Pending) l.to_peer += rest;
    }
    if (l.role == Local::Role::Stream) {
        while (!l.to_peer.empty()) {
            const std::size_t n = std::min(l.to_peer.size(), max_payload);
            if (!send_packet(l.peer, Data, l.id, l.mine, l.to_peer.data(), n)) break;
            l.to_peer.erase(0, n);
        }
    }
    if (l.role == Local::Role::Pending && Clock::now() > l.until) {
        reply(l, "FAIL timeout");
        drop(l, "no answer");
    }
    while (!l.out.empty()) {
        const int n = send(l.socket, l.out.data(), int(std::min<std::size_t>(l.out.size(), 65536)), 0);
        if (n <= 0) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) break;
            drop(l, "the game's socket failed");
            return false;
        }
        l.out.erase(0, n);
    }
    return !(l.closing && l.out.empty());
}

}  // namespace

int main(int argc, char **argv) {
    int port = 47710;
    std::filesystem::path ids = exe_folder() / "CFG" / "eos.txt";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--ids" && i + 1 < argc) ids = argv[++i];
        else if (a == "--fresh-identity") b.fresh = true;
    }
    std::filesystem::create_directories(exe_folder() / "logs");
    log_file = _wfopen((exe_folder() / "logs" / ("eos-bridge-" + std::to_string(port) + ".log")).c_str(), L"a");

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    setsockopt(server, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(u_short(port));
    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(server, 16) != 0) {
        say("port %d is taken: another bridge runs", port);
        return 2;
    }
    u_long nonblocking = 1;
    ioctlsocket(server, FIONBIO, &nonblocking);
    say("started on port %d", port);
    if (!start_eos(ids)) b.login = Bridge::Login::Failed;

    while (true) {
        fd_set read{};
        FD_SET(server, &read);
        for (auto &l : b.locals) FD_SET(l->socket, &read);
        timeval wait{0, 5000};
        select(0, &read, nullptr, nullptr, &wait);
        while (true) {
            SOCKET s = accept(server, nullptr, nullptr);
            if (s == INVALID_SOCKET) break;
            ioctlsocket(s, FIONBIO, &nonblocking);
            BOOL nodelay = TRUE;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
            auto l = std::make_unique<Local>();
            l->socket = s;
            l->key = b.next_key++;
            b.locals.push_back(std::move(l));
        }
        if (b.platform) EOS_Platform_Tick(b.platform);
        if (b.platform && b.login == Bridge::Login::Failed && !b.me && Clock::now() > b.retry) {
            b.login = Bridge::Login::Working;
            say("trying to log in again");
            create_device();
        }
        receive();
        rooms_tick();
        for (auto it = b.locals.begin(); it != b.locals.end();) {
            if (pump(**it)) {
                ++it;
                continue;
            }
            closesocket((*it)->socket);
            it = b.locals.erase(it);
        }
        for (auto it = b.incoming.begin(); it != b.incoming.end();) {
            if (Clock::now() < it->second.until) {
                ++it;
                continue;
            }
            send_packet(it->second.peer, Refused, it->second.id, false);
            it = b.incoming.erase(it);
        }
        if (!b.locals.empty()) b.last_game = Clock::now();
        if (Clock::now() - b.last_game > idle_exit) break;
    }
    say("no game for 5 minutes: ending");
    if (b.platform) EOS_Platform_Release(b.platform);
    EOS_Shutdown();
    return 0;
}
