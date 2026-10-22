#include "hw/codecs.h"

#include <dcmtk/dcmdata/dcrledrg.h>

// Each of these headers only exists when DCMTK was built with that codec, so __has_include is the
// build-time probe — no CMake feature detection to keep in sync.
#if __has_include(<dcmtk/dcmjpeg/djdecode.h>)
#include <dcmtk/dcmjpeg/djdecode.h>
#define HW_HAVE_JPEG 1
#endif
#if __has_include(<dcmtk/dcmjpls/djdecode.h>)
#include <dcmtk/dcmjpls/djdecode.h>
#define HW_HAVE_JPEGLS 1
#endif
#if __has_include(<dcmtk/dcmjp2k/djdecode.h>)
#include <dcmtk/dcmjp2k/djdecode.h>
#define HW_HAVE_JPEG2000 1
#endif

#include <mutex>

namespace hw {

void registerCodecs()
{
    static std::once_flag once;
    std::call_once(once, [] {
        DcmRLEDecoderRegistration::registerCodecs();
#ifdef HW_HAVE_JPEG
        DJDecoderRegistration::registerCodecs();
#endif
#ifdef HW_HAVE_JPEGLS
        DJLSDecoderRegistration::registerCodecs();
#endif
#ifdef HW_HAVE_JPEG2000
        DJKDecoderRegistration::registerCodecs();
#endif
    });
}

std::string codecSummary()
{
    std::string s = "RLE";
#ifdef HW_HAVE_JPEG
    s += ", JPEG";
#endif
#ifdef HW_HAVE_JPEGLS
    s += ", JPEG-LS";
#endif
#ifdef HW_HAVE_JPEG2000
    s += ", JPEG 2000";
#endif
    return s;
}

}  // namespace hw
