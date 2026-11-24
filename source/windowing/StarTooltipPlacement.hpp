#pragma once

#include "StarRect.hpp"
#include "StarList.hpp"
#include "StarMaybe.hpp"

namespace Star {

// Where a tooltip and the sections scripts attach beside it go on screen. All
// values are in interface pixels with y up; nothing here touches panes, so the
// rules can be tested on their own.

enum class TooltipSide { Left, Right, Below, Above };

}
