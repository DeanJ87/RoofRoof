#pragma once

// The partition layout and build this image is for (SQMeter spec 027). Both go
// into the image marker (src/FirmwareMarker.cpp), so an upload only installs an
// image built for this device's partition table. RoofRoof has one board
// (ESP32 DevKit, 4 MB) and one layout.
#define SQM_LAYOUT_ID "l2"

