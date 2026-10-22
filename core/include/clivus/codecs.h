// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace clivus {

// DCMTK compiles its decoders in but registers none of them: RLE, JPEG, JPEG-LS and JPEG 2000
// each need their own registerCodecs() call, otherwise a compressed file fails with
// "can't change to unencapsulated representation for pixel data". Idempotent, call before decoding.
void registerCodecs();

// The decoders this binary actually carries, e.g. "RLE, JPEG, JPEG-LS". Whether JPEG 2000 is in
// that list is a property of the DCMTK build, not of DCMTK itself, so it is worth printing.
std::string codecSummary();

}  // namespace clivus
