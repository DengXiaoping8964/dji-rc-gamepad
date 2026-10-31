#pragma once

namespace dji {

// Discovers the libusb0 nodes DJI's driver created for MI_00/MI_01 and tries every
// plausible bulk endpoint pair. Returns 0 when at least one endpoint answered.
int probe_bulk(int timeout_ms);

// Same transport, driven through libusb-1.0 (opaque handles, no ABI guessing).
int probe_libusb(int timeout_ms, unsigned short vendor, unsigned short product);

}  // namespace dji
