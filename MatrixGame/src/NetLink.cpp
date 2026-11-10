// MatrixGame - licensed under GPLv2 or any later version.
// Winsock first: windows.h would otherwise bring the old winsock.h.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "NetLink.hpp"
#include "ExecutablePath.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <thread>

namespace NetLink {
namespace {

SOCKET sock(std::uintptr_t s) { return static_cast<SOCKET>(s); }

void non_blocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    BOOL nodelay = TRUE;  // turns are small and late ones stall every peer
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay), sizeof(nodelay));
}

bool address_of(const std::string &address, int port, sockaddr_in &out) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<u_short>(port));
    out.sin_addr.s_addr = inet_addr(address.c_str());  // inet_pton needs a newer Windows target
    return out.sin_addr.s_addr != INADDR_NONE;
}

// Waits until the socket can be read (or written); false on timeout.
bool wait_socket(SOCKET s, bool write, int timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv) > 0;
}

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// The bridge's local port: PBO_EOS_PORT (a test's second bridge on the same PC), else 47710.
int bridge_port() {
    const char *text = std::getenv("PBO_EOS_PORT");
    const int port = text ? std::atoi(text) : 0;
    return port > 0 && port < 65536 ? port : 47710;
}

// Starts eos-bridge.exe (at most every 5 s; a second one on a taken port ends by itself). PBO_EOS_FRESH=1: a test's
// second identity on this PC.
void start_bridge() {
    static long long last = -10000;
    if (now_ms() - last < 5000) return;
    last = now_ms();
    std::wstring command = L"\"" + (Startup::ExecutablePath().parent_path() / L"eos-bridge.exe").wstring() +
                           L"\" --port " + std::to_wstring(bridge_port());
    if (const char *fresh = std::getenv("PBO_EOS_FRESH"); fresh && *fresh == '1') command += L" --fresh-identity";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr,
                       nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// The answer line to a bridge command, without waiting; true once it came (`line`) or the stream closed (empty).
bool bridge_answer(Connection &link, std::string &line) {
    line.clear();
    if (link.next_line(line)) return true;
    const bool open = link.pump();  // what came with a close is still read
    return link.next_line(line) || !open;
}

}  // namespace

bool eos::is(const std::string &address) {
    if (!address.starts_with(prefix) || address.size() != prefix.size() + 32) return false;
    return std::all_of(address.begin() + prefix.size(), address.end(), [](char ch) { return std::isxdigit(ch); });
}

bool eos::available() {
    static const bool yes = [] {
        try {
            const auto folder = Startup::ExecutablePath().parent_path();
            return std::filesystem::exists(folder / L"eos-bridge.exe") && std::filesystem::exists(folder / L"CFG" / L"eos.txt");
        }
        catch (...) {
            return false;
        }
    }();
    return yes;
}

namespace {

std::string utf8(const std::wstring &text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring from_utf8(const std::string &text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}

// A room's text field: no separators or line ends in it.
std::string field(const std::wstring &text) {
    std::string out = utf8(text);
    for (char &ch : out)
        if (ch == '\x1f' || ch == '\n' || ch == '\r') ch = ' ';
    return out;
}

}  // namespace

std::string eos::own_address() {
    static std::string known;
    static std::unique_ptr<Connector> connecting;
    static std::unique_ptr<Connection> link;
    static long long retry = 0;
    if (!known.empty() || !available()) return known;
    if (link) {
        std::string line;
        if (!bridge_answer(*link, line)) return {};
        link.reset();
        if (line.starts_with("ME ") && is(std::string(prefix) + line.substr(3))) known = std::string(prefix) + line.substr(3);
        else retry = now_ms() + 5000;  // FAIL login: the bridge tries again
        return known;
    }
    if (!connecting) {
        if (now_ms() < retry) return {};
        connecting = std::make_unique<Connector>("127.0.0.1", bridge_port());
    }
    if (auto l = connecting->poll()) {
        link = std::move(l);
        link->send_line("WHO");
        link->pump();
        connecting.reset();
    } else if (connecting->failed()) {
        connecting.reset();
        start_bridge();
        retry = now_ms() + 1000;
    }
    return {};
}

eos::Room::Room(int port, std::string build) : m_Port(port), m_Created(std::time(nullptr)), m_Build(std::move(build)) {}

eos::Room::~Room() = default;  // closing the stream takes the room away

void eos::Room::set(const std::wstring &host, const std::wstring &map, const std::wstring &people, bool locked) {
    m_Text = field(host) + "\x1f" + field(map) + "\x1f" + field(people) + "\x1f" + (locked ? "1" : "0") + "\x1f" +
             std::to_string(m_Created);
}

void eos::Room::pump() {
    if (m_Link) {
        if (!m_Link->pump()) {  // the bridge ended: the room again with the next one
            m_Link.reset();
            m_Opened = false;
            m_Retry = now_ms() + 1000;
            return;
        }
        if (!m_Opened) {
            m_Link->send_line("ROOM " + std::to_string(m_Port) + " " + m_Build + " " + m_Text);
            m_Opened = true;
            m_Sent = m_Text;
        } else if (m_Sent != m_Text) {
            m_Link->send_line("DESC " + m_Text);
            m_Sent = m_Text;
        }
        return;
    }
    if (!m_Connecting) {
        if (now_ms() < m_Retry) return;
        m_Connecting = std::make_unique<Connector>("127.0.0.1", bridge_port());
    }
    if (auto link = m_Connecting->poll()) {
        m_Link = std::move(link);
        m_Connecting.reset();
    } else if (m_Connecting->failed()) {
        m_Connecting.reset();
        start_bridge();
        m_Retry = now_ms() + 1000;
    }
}

eos::RoomSearch::RoomSearch(std::string build) : m_Build(std::move(build)), m_Until(now_ms() + 30000) {}

eos::RoomSearch::~RoomSearch() = default;

bool eos::RoomSearch::poll() {
    if (now_ms() > m_Until) return true;
    if (m_Link) {
        std::string line;
        bool closed = false;  // the bridge closes right after END: what came with it is still read
        while (true) {
            if (!m_Link->next_line(line)) {
                if (closed) return true;
                closed = !m_Link->pump();
                if (!m_Link->next_line(line)) {
                    if (closed) return true;
                    return false;
                }
            }
            if (line == "END") return true;
            // ROOM <id> <port> <host>\x1f<map>\x1f<people>\x1f<1: a password>\x1f<made: UTC seconds>
            if (!line.starts_with("ROOM ")) continue;
            const auto a = line.find(' ', 5), b = a == std::string::npos ? a : line.find(' ', a + 1);
            if (b == std::string::npos) continue;
            RoomInfo room;
            room.address = std::string(prefix) + line.substr(5, a - 5);
            room.port = std::atoi(line.substr(a + 1, b - a - 1).c_str());
            const std::string text = line.substr(b + 1);
            const auto x = text.find('\x1f'), y = x == std::string::npos ? x : text.find('\x1f', x + 1);
            room.host = from_utf8(text.substr(0, x));
            if (x != std::string::npos) room.map = from_utf8(text.substr(x + 1, y == std::string::npos ? y : y - x - 1));
            const auto z = y == std::string::npos ? y : text.find('\x1f', y + 1);
            if (y != std::string::npos) room.people = from_utf8(text.substr(y + 1, z == std::string::npos ? z : z - y - 1));
            const auto w = z == std::string::npos ? z : text.find('\x1f', z + 1);
            room.locked = z != std::string::npos && text.substr(z + 1, w == std::string::npos ? w : w - z - 1) == "1";
            if (w != std::string::npos) room.created = std::atoll(text.c_str() + w + 1);
            if (is(room.address) && room.port > 0) m_Rooms.push_back(std::move(room));
        }
    }
    if (!m_Connecting) {
        if (now_ms() < m_Retry) return false;
        m_Connecting = std::make_unique<Connector>("127.0.0.1", bridge_port());
    }
    if (auto link = m_Connecting->poll()) {
        m_Link = std::move(link);
        m_Link->send_line("ROOMS " + m_Build);
        m_Link->pump();
        m_Connecting.reset();
    } else if (m_Connecting->failed()) {  // no bridge yet: started, and tried again in a while
        m_Connecting.reset();
        start_bridge();
        m_Retry = now_ms() + 1000;
        if (++m_Tries > 8) return true;
    }
    return false;
}

bool startup() {
    static const bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

Connection::Connection(std::uintptr_t socket) : m_Socket(socket) { non_blocking(sock(m_Socket)); }

Connection::~Connection() { closesocket(sock(m_Socket)); }

std::string Connection::remote_address() const {
    if (!m_Remote.empty()) return m_Remote;
    sockaddr_in addr{};
    int length = sizeof(addr);
    if (getpeername(sock(m_Socket), reinterpret_cast<sockaddr *>(&addr), &length) != 0 || addr.sin_family != AF_INET) return {};
    const char *text = inet_ntoa(addr.sin_addr);
    return text ? text : "";
}

void Connection::send_line(const std::string &line) {
    m_Out += line;
    m_Out += '\n';
}

bool Connection::pump() {
    if (!m_Open) return false;
    // Read first: writing to a connection the other side has closed brings back a reset, and a reset
    // throws away what had arrived unread (the host's last line to a person it removed).
    char buffer[16384];
    while (true) {
        const int n = ::recv(sock(m_Socket), buffer, sizeof(buffer), 0);
        if (n > 0) {
            m_In.append(buffer, std::size_t(n));
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
        m_Open = false;  // 0: closed by the other side
        return false;
    }
    while (!m_Out.empty()) {
        const int n = ::send(sock(m_Socket), m_Out.data(), int(m_Out.size()), 0);
        if (n > 0) {
            m_Out.erase(0, std::size_t(n));
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
        m_Open = false;
        return false;
    }
    return m_Open;
}

bool Connection::next_line(std::string &line) {
    const auto end = m_In.find('\n');
    if (end == std::string::npos) return false;
    line.assign(m_In, 0, end);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    m_In.erase(0, end + 1);
    return true;
}

bool Connection::wait_line(std::string &line, int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (next_line(line)) return true;
        if (!pump()) return next_line(line);
        if (next_line(line)) return true;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        if (left.count() <= 0) return false;
        wait_socket(sock(m_Socket), false, int(std::min<long long>(left.count(), 100)));
    }
}

void Connection::flush(int timeout_ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (m_Open && !m_Out.empty() && std::chrono::steady_clock::now() < until) {
        pump();
        if (!m_Out.empty()) wait_socket(sock(m_Socket), true, 10);
    }
}

Listener::Listener(const std::string &address, int port) : m_Socket(INVALID_SOCKET), m_Port(port) {
    if (address == "0.0.0.0" && eos::available()) m_EosRetry = now_ms();  // EOS people too (reached in accept)
    sockaddr_in addr;
    if (!startup() || !address_of(address, port, addr)) return;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return;
    BOOL exclusive = TRUE;  // a second host on the same port fails instead of sharing it
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive), sizeof(exclusive));
    if (bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return;
    }
    m_Socket = s;
}

Listener::~Listener() {
    if (sock(m_Socket) != INVALID_SOCKET) closesocket(sock(m_Socket));
}

bool Listener::listening() const { return sock(m_Socket) != INVALID_SOCKET; }

std::unique_ptr<Connection> Listener::accept(int timeout_ms) {
    if (!listening()) return nullptr;
    const long long until = now_ms() + timeout_ms;
    while (true) {
        if (auto link = accept_eos()) return link;
        const long long left = until - now_ms();
        // While EOS is on, the bridge's lines are looked at every 20 ms.
        const bool eos = m_Eos || m_EosConnecting || m_EosRetry;
        if (wait_socket(sock(m_Socket), false, int(std::max(0LL, eos ? std::min(left, 20LL) : left)))) {
            SOCKET s = ::accept(sock(m_Socket), nullptr, nullptr);
            if (s == INVALID_SOCKET) return nullptr;
            return std::make_unique<Connection>(static_cast<std::uintptr_t>(s));
        }
        if (now_ms() >= until) return accept_eos();
    }
}

std::unique_ptr<Connection> Listener::accept_eos() {
    if (!m_Eos && !m_EosConnecting) {
        if (!m_EosRetry || now_ms() < m_EosRetry) return nullptr;
        m_EosConnecting = std::make_unique<Connector>("127.0.0.1", bridge_port());
    }
    if (m_EosConnecting) {
        if (auto link = m_EosConnecting->poll()) {
            m_Eos = std::move(link);
            m_Eos->send_line("LISTEN " + std::to_string(m_Port));
            m_EosConnecting.reset();
        } else if (m_EosConnecting->failed()) {
            m_EosConnecting.reset();
            start_bridge();
            m_EosRetry = now_ms() + 1000;
        }
        return nullptr;
    }
    if (!m_Eos->pump()) {  // the bridge ended: start it again
        m_Eos.reset();
        m_EosRetry = now_ms() + 1000;
        return nullptr;
    }
    std::string line;
    while (m_Eos->next_line(line)) {
        if (!line.starts_with("ACCEPT ")) continue;  // OK <own id>
        const auto space = line.find(' ', 7);
        if (space == std::string::npos) continue;
        auto link = connect("127.0.0.1", bridge_port(), 2000);
        if (!link) continue;
        link->send_line("TAKE " + line.substr(7, space - 7));
        std::string answer;
        if (!link->wait_line(answer, 5000) || !answer.starts_with("OK ")) continue;
        link->set_remote(std::string(eos::prefix) + answer.substr(3));
        return link;
    }
    return nullptr;
}

std::unique_ptr<Connection> connect(const std::string &address, int port, int timeout_ms) {
    if (eos::is(address)) {
        const long long until = now_ms() + timeout_ms;
        do {
            Connector connector(address, port);
            while (!connector.failed()) {
                if (auto link = connector.poll()) return link;
                if (now_ms() >= until) return nullptr;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        } while (now_ms() < until);
        return nullptr;
    }
    sockaddr_in addr;
    if (!startup() || !address_of(address, port, addr)) return nullptr;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    do {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return nullptr;
        if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0)
            return std::make_unique<Connection>(static_cast<std::uintptr_t>(s));
        closesocket(s);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    } while (std::chrono::steady_clock::now() < until);
    return nullptr;
}

Connector::Connector(const std::string &address, int port) : m_Socket(INVALID_SOCKET) {
    if (eos::is(address)) {  // through the bridge: reached in poll
        m_Eos = address.substr(eos::prefix.size());
        m_Port = port;
        return;
    }
    sockaddr_in addr;
    if (!startup() || !address_of(address, port, addr)) {
        m_Failed = true;
        return;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        m_Failed = true;
        return;
    }
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
    if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        m_Failed = true;
        return;
    }
    m_Socket = s;
}

Connector::~Connector() {
    if (sock(m_Socket) != INVALID_SOCKET) closesocket(sock(m_Socket));
}

std::unique_ptr<Connection> Connector::poll() {
    if (!m_Eos.empty() && !m_Failed) {
        if (m_Link) {  // CONNECT sent: OK <who>, then the stream; FAIL: nobody there
            std::string line;
            if (!bridge_answer(*m_Link, line)) return nullptr;
            if (!line.starts_with("OK ")) {
                m_Link.reset();
                m_Failed = true;
                return nullptr;
            }
            m_Link->set_remote(std::string(eos::prefix) + m_Eos);
            return std::move(m_Link);
        }
        if (!m_Local) {
            if (now_ms() < m_Retry) return nullptr;
            m_Local = std::make_unique<Connector>("127.0.0.1", bridge_port());
        }
        if (auto link = m_Local->poll()) {
            m_Local.reset();
            m_Link = std::move(link);
            m_Link->send_line("CONNECT " + m_Eos + " " + std::to_string(m_Port));
            m_Link->pump();
        } else if (m_Local->failed()) {  // no bridge yet: start it and try again
            m_Local.reset();
            start_bridge();
            m_Retry = now_ms() + 500;
        }
        return nullptr;
    }
    const SOCKET s = sock(m_Socket);
    if (m_Failed || s == INVALID_SOCKET) return nullptr;
    fd_set writable, failed;
    FD_ZERO(&writable);
    FD_ZERO(&failed);
    FD_SET(s, &writable);
    FD_SET(s, &failed);
    timeval now{0, 0};
    const int n = select(0, nullptr, &writable, &failed, &now);
    if (n == 0) return nullptr;
    int error = 0;
    int length = sizeof(error);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length);
    if (n < 0 || FD_ISSET(s, &failed) || error != 0) {
        closesocket(s);
        m_Socket = INVALID_SOCKET;
        m_Failed = true;
        return nullptr;
    }
    m_Socket = INVALID_SOCKET;
    return std::make_unique<Connection>(static_cast<std::uintptr_t>(s));
}

bool valid_address(const std::string &address) {
    sockaddr_in addr;
    return std::count(address.begin(), address.end(), '.') == 3 && address_of(address, 1, addr);
}

std::vector<std::string> local_addresses() {
    std::vector<std::string> out;
    char name[256] = {};
    if (!startup() || gethostname(name, sizeof(name)) != 0) return out;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo *found = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &found) != 0) return out;
    for (addrinfo *a = found; a; a = a->ai_next) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(a->ai_addr);
        const std::string text = inet_ntoa(in->sin_addr);
        if (!text.starts_with("127.") && std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
    }
    freeaddrinfo(found);
    return out;
}

}  // namespace NetLink
