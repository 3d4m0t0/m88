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

enum class LetterShiftAdjust { None, AddShift, RemoveShift };

// Caps Lock ON: AddShift for uppercase without Shift; RemoveShift for lowercase with Shift
// (Caps+Shift -> uppercase like Shift alone).
LetterShiftAdjust LetterShiftAdjustFor(const QKeyEvent& ev);

}  // namespace QtInput
