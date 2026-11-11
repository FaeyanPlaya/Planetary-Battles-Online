// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Line-based TCP connections for battles shared over the network (Winsock, no extra libraries).
// Every message is one line of text; sending and receiving never block once connected.
// An address "eos:<id>" is a person reached over Epic Online Services: eos-bridge.exe beside the EXE (64-bit, as the
// EOS SDK is) carries the stream over Epic's P2P, no VPN or open port needed; it is started when first needed and
// outlives the game's restarts. A Listener on 0.0.0.0 takes such people too when the build has CFG\eos.txt.
namespace NetLink {

bool startup();  // WSAStartup; false when Winsock is unavailable

// The longest line a connection takes (a map file's line is 64 KiB); one that sends more without a line end is closed.
inline constexpr std::size_t max_line = 1u << 20;

class Connection {
public:
    Connection(std::uintptr_t socket);
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    void send_line(const std::string &line);
    // Sends what is queued and reads what has arrived; false once the connection is gone.
    bool pump();
    // The next complete received line (without the line end), if any.
    bool next_line(std::string &line);
    // Waits up to timeout_ms for a complete line; false on timeout or a closed connection.
    bool wait_line(std::string &line, int timeout_ms);
    // Sends everything queued, waiting up to timeout_ms (before closing).
    void flush(int timeout_ms);
    bool open() const { return m_Open; }
    // Bytes queued and not yet sent.
    std::size_t queued() const { return m_Out.size(); }
    // The other end's IPv4 address or "eos:<id>"; empty: unknown.
    std::string remote_address() const;
    void set_remote(std::string address) { m_Remote = std::move(address); }

private:
    std::uintptr_t m_Socket;
    bool m_Open = true;
    std::string m_In, m_Out;
    std::size_t m_InAt = 0;  // where the next line begins in m_In
    std::string m_Remote;  // set: a stream through the EOS bridge, and who is at its other end
};

class Connector;

class Listener {
public:
    // Listens on address:port; address 127.0.0.1 keeps it to this PC.
    Listener(const std::string &address, int port);
    ~Listener();
    bool listening() const;
    // Waits up to timeout_ms for a connection.
    std::unique_ptr<Connection> accept(int timeout_ms);

private:
    std::unique_ptr<Connection> accept_eos();

    std::uintptr_t m_Socket;
    int m_Port = 0;
    // EOS: the bridge's LISTEN stream (its "ACCEPT <id> <who>" lines), while it is being reached, the next try.
    std::unique_ptr<Connection> m_Eos;
    std::unique_ptr<Connector> m_EosConnecting;
    long long m_EosRetry = 0;
};

// Connects to address:port, retrying until timeout_ms passes (the host may still be starting).
std::unique_ptr<Connection> connect(const std::string &address, int port, int timeout_ms);

// One connection attempt that never blocks: poll it every frame until it connects or fails.
class Connector {
public:
    Connector(const std::string &address, int port);
    ~Connector();
    Connector(const Connector &) = delete;
    Connector &operator=(const Connector &) = delete;
    // The connection once it is made (once), else nullptr.
    std::unique_ptr<Connection> poll();
    bool failed() const { return m_Failed; }

private:
    std::uintptr_t m_Socket;
    bool m_Failed = false;
    // An "eos:" address: the person and port asked for, the bridge's stream until it answers OK, the next try.
    std::string m_Eos;
    int m_Port = 0;
    std::unique_ptr<Connector> m_Local;
    std::unique_ptr<Connection> m_Link;
    long long m_Retry = 0;
};

// Whether the text is an IPv4 address that connect and Listener take.
bool valid_address(const std::string &address);

namespace eos {
inline constexpr std::string_view prefix = "eos:";
// "eos:" and 32 hex digits.
bool is(const std::string &address);
// Whether this build can reach people over EOS (CFG\eos.txt and eos-bridge.exe beside the EXE).
bool available();
// This PC's "eos:<id>" once the bridge told it (asked in the background, the bridge started if needed: call it every
// frame until it is not empty); empty without EOS.
std::string own_address();

// A host's room in the list on the internet (an EOS lobby nobody joins) while this object lives; build: only the
// same build's join screens list it.
class Room {
public:
    Room(int port, std::string build);
    ~Room();
    Room(const Room &) = delete;
    Room &operator=(const Room &) = delete;
    // What the list shows; sent when it changes (the bridge sends it on at most every 2 s).
    void set(const std::wstring &host, const std::wstring &map, const std::wstring &people, bool locked);
    // Every frame: reaches the bridge, sends the text.
    void pump();

private:
    int m_Port;
    long long m_Created;  // when the room was made: UTC seconds
    std::string m_Build, m_Text, m_Sent;
    std::unique_ptr<Connector> m_Connecting;
    std::unique_ptr<Connection> m_Link;
    bool m_Opened = false;
    long long m_Retry = 0;
};

struct RoomInfo {
    std::string address;  // eos:<id>
    int port = 0;
    std::wstring host, map, people;
    bool locked = false;  // the lobby has a password
    long long created = 0;  // when the host made it: UTC seconds (0: not said)
};

// One look for the rooms of a build: poll every frame until it is done (some seconds; nothing without a bridge).
class RoomSearch {
public:
    explicit RoomSearch(std::string build);
    ~RoomSearch();
    RoomSearch(const RoomSearch &) = delete;
    RoomSearch &operator=(const RoomSearch &) = delete;
    bool poll();  // true once done: rooms() then
    const std::vector<RoomInfo> &rooms() const { return m_Rooms; }

private:
    std::string m_Build;
    std::unique_ptr<Connector> m_Connecting;
    std::unique_ptr<Connection> m_Link;
    std::vector<RoomInfo> m_Rooms;
    long long m_Until, m_Retry = 0;
    int m_Tries = 0;
};
}  // namespace eos

// IPv4 addresses of this PC that other PCs may reach (not 127.x), for showing to people who join.
std::vector<std::string> local_addresses();

}  // namespace NetLink
