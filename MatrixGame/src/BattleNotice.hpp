// MatrixGame - licensed under GPLv2 or any later version.
#pragma once

#include <cstdint>
#include <string>

// Short messages at the top right of the battle screen, each for a few seconds; newer ones below the
// older. For the people of a shared battle: who left, gave up or lost the connection, a desync.
namespace BattleNotice {

void show(const std::wstring &text, std::uint32_t colour = 0xFFFFFFFF);
// The same for an answer to this person's own action (a rally point set, auto detonation on): not in the battle's
// history (Right Ctrl), which keeps the chat and what happened to the people.
void show_own(const std::wstring &text, std::uint32_t colour = 0xFFFFFFFF);
// Every frame: drops the expired messages and keeps the rest at the screen's right edge.
void update();
// Releases the messages before the session's hints are destroyed.
void clear();

}  // namespace BattleNotice
