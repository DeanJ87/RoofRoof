#pragma once

// The roof's latched fault in NVS (spec 003 FR-011, FR-012, research R7), so
// a restart never clears it (constitution I). The record's format and its
// checks are lib/RoofIoLogic's (FaultRecord.h).

#include "FaultRecord.h"
#include "SwitchWatch.h"

namespace SQM
{
    namespace RoofFaultStore
    {
        // What was stored: None, Fault (in `out`), or Unreadable (`out` is the
        // storedUnreadable fault to latch).
        RoofIo::Decoded load(RoofIo::FaultRecord &out);
        // Saves `record`, or removes it when its kind is None. False if NVS refused.
        bool save(const RoofIo::FaultRecord &record);
        // Spec 024: the park switches' stuck-switch watch (two counts). A
        // missing or unreadable one loads as nothing counted yet.
        Safety::SwitchWatch loadWatch();
        bool saveWatch(const Safety::SwitchWatch &watch);
    } // namespace RoofFaultStore
} // namespace SQM
