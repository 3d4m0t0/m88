#pragma once

#include <QKeyEvent>

#include "types.h"

namespace QtInput {

uint VkFromKeyEvent(const QKeyEvent& ev);
uint32 KeyExtended(const QKeyEvent& ev);
uint32 KeyDataFromEvent(const QKeyEvent& ev);

// Host Alt/Meta (IME); must not map to PC-88 GRPH (VK_MENU).
bool IsHostImeModifierKey(int qt_key);
bool IsHostModifierVk(uint vk);
void SetSuppressMenu(bool enabled);

// US/101 host: prefer evdev scancodes for the top digit/punctuation row (layout-independent).
void SetHostAt101(bool enabled);

uint VkFromAsciiChar(QChar ch);
uint32 KeyDataForAsciiChar(QChar ch);

enum class LetterShiftAdjust { None, AddShift, RemoveShift };

// Caps Lock ON: AddShift for uppercase without Shift; RemoveShift for lowercase with Shift
// (Caps+Shift -> uppercase like Shift alone).
LetterShiftAdjust LetterShiftAdjustFor(const QKeyEvent& ev);

}  // namespace QtInput
