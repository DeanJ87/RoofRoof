#pragma once

// The pin rules' English texts (src/PinText.cpp, the device catalogue's
// "pins" area).

#include <string>

#include "PinRules.h"

namespace SQM
{
    namespace Board
    {
        std::string issueText(const Issue &issue);
        std::string unreadableText();
        std::string unsetText();
    } // namespace Board
} // namespace SQM
