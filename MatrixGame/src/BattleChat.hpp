// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// The people of a shared battle write to each other: Enter opens a line at the bottom left, Enter sends it
// (Esc drops it); messages stay above it for a while, each in its sender's side colour. Nothing here touches
// the battle: the lines travel beside the lockstep turns (Lockstep::Session::say).
namespace BattleChat {

constexpr std::size_t max_length = 100;  // characters of one message

// The text as a message may carry it: one line, no control characters, no hint markup ('|', tags), cut to
// max_length; spaces at its ends dropped.
std::wstring clean(std::wstring_view text);
// "Rover (Люди): hello" (the faction alone when there is no name).
std::wstring line(int side, const std::wstring &name, const std::wstring &text, bool russian);

// The line being typed.
bool typing();
void open(bool russian);
void close();
void type(wchar_t ch);
void erase_last();
void paste(std::wstring_view text);
// Enter: the typed message (empty: nothing to send) and the line closes.
std::wstring finish();

// A message from a side's person (this PC's own too).
void show(int side, const std::wstring &name, const std::wstring &text, bool russian);
// Every frame: drops old messages, keeps the rest in place.
void update();
// Releases the hints (the battle's end).
void clear();

}  // namespace BattleChat
