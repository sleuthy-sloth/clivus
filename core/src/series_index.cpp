// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/series_index.h"

#include "clivus/codecs.h"

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimage/diregist.h>   // registers the colour image classes with DicomImage
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmimgle/dimopx.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>

namespace clivus {
namespace fs = std::filesystem;

namespace {

std::string tagString(DcmItem* ds, const DcmTagKey& key)
{
    OFString v;
    if (ds && ds->findAndGetOFStringArray(key, v).good()) return std::string(v.c_str());
    return {};
}

// A text element's value, whatever VR the sender used. Two things make this more than a wrapper:
//
//  - The RT Dose in our corpus carries Modality, Series Instance UID, Patient Name, Pixel Spacing and
//    DoseGridScaling with VR=UN. For those, findAndGetOFStringArray *succeeds* but answers with a
//    backslash-escaped octal dump of the bytes ("52\54\44\4f\53\45" for "RTDOSE"), so a VR-strict
//    read is not the problem — a silently wrong string is. Reading the bytes gives the ASCII back.
//  - Everything else takes the normal string path.
std::string anyVRString(DcmItem* ds, const DcmTagKey& key)
{
    DcmElement* elem = nullptr;
    if (!ds || ds->findAndGetElement(key, elem).bad() || !elem) return {};
    const DcmEVR vr = elem->getVR();
    if (vr == EVR_UN || vr == EVR_OB || vr == EVR_OW) {
        Uint8* raw = nullptr;
        if (elem->getUint8Array(raw).bad() || !raw) return {};
        std::string s(reinterpret_cast<const char*>(raw), size_t(elem->getLength()));
        while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
        return s;
    }
    OFString v;
    if (ds->findAndGetOFStringArray(key, v).good()) return std::string(v.c_str());
    return {};
}

long instanceNumber(DcmItem* ds)
{
    OFString v;
    if (ds && ds->findAndGetOFStringArray(DCM_InstanceNumber, v).good())
        return std::strtol(v.c_str(), nullptr, 10);
    return 0;
}

// NumberOfFrames, or 1 when the object does not say (a plain single-frame image omits it). An object
// that claims 0 frames is treated as 1: it still has a pixel buffer worth showing.
int frameCountOf(DcmItem* ds)
{
    if (!ds || !ds->tagExistsWithValue(DCM_NumberOfFrames)) return 1;
    const long n = std::strtol(anyVRString(ds, DCM_NumberOfFrames).c_str(), nullptr, 10);
    return n > 1 ? int(n) : 1;
}

// Internal representation -> float, without any modality transform (we apply slope/intercept).
// offset/count pick one frame out of the buffer: for some pixel types DicomImage hands back the whole
// stack (a 15-frame 32-bit RT Dose reports 1500 pixels for a 10x10 image), and frames are contiguous.
// Taking exactly width*height values from the frame's own offset is what keeps a 32-bit object from
// reading past one frame.
bool interDataToFloat(const DiMonoPixel* px, size_t offset, size_t count, std::vector<float>& out)
{
    if (!px || !px->getData() || count == 0) return false;
    if (offset + count > size_t(px->getCount())) return false;
    const void* d = px->getData();
    out.resize(count);
    switch (px->getRepresentation()) {
    case EPR_Uint8:  { auto p = static_cast<const Uint8*>(d); for (size_t i = 0; i < count; ++i) out[i] = float(p[offset + i]); } break;
    case EPR_Sint8:  { auto p = static_cast<const Sint8*>(d); for (size_t i = 0; i < count; ++i) out[i] = float(p[offset + i]); } break;
    case EPR_Uint16: { auto p = static_cast<const Uint16*>(d); for (size_t i = 0; i < count; ++i) out[i] = float(p[offset + i]); } break;
    case EPR_Sint16: { auto p = static_cast<const Sint16*>(d); for (size_t i = 0; i < count; ++i) out[i] = float(p[offset + i]); } break;
    case EPR_Uint32: { auto p = static_cast<const Uint32*>(d); for (size_t i = 0; i < count; ++i) out[i] = float(p[offset + i]); } break;
    case EPR_Sint32: { auto p = static_cast<const Sint32*>(d); for (size_t i = 0; i < count; ++i) out[i] = float(p[offset + i]); } break;
    default: return false;
    }
    return true;
}

// Reads one object's header, or nothing when the object has no pixel data to show. The first read
// caps element values at 4096 bytes, which is what keeps a folder scan cheap; if it reports no Pixel
// Data the object is read once more with no cap before being written off, because that cap is the
// only thing sitting between a value-heavy object and a normal parse. Only a would-be miss pays for
// the second read.
std::unique_ptr<DcmFileFormat> loadHeader(const std::string& file)
{
    auto header = std::make_unique<DcmFileFormat>();
    if (header->loadFile(file.c_str(), EXS_Unknown, EGL_noChange, 4096).bad()) return nullptr;
    DcmDataset* ds = header->getDataset();
    if (ds && ds->tagExistsWithValue(DCM_PixelData)) return header;

    auto full = std::make_unique<DcmFileFormat>();
    if (full->loadFile(file.c_str(), EXS_Unknown, EGL_noChange, 0xFFFFFFFFu).bad()) return nullptr;
    DcmDataset* fullDs = full->getDataset();
    if (!fullDs || !fullDs->tagExistsWithValue(DCM_PixelData)) return nullptr;
    return full;
}

}  // namespace

std::vector<SeriesInfo> scanDirectory(const std::string& dir, ScanControl* control)
{
    registerCodecs();
    std::map<std::string, SeriesInfo> bySeries;
    // The sort key carries the frame count, so files and frameCounts come out of one sort and cannot
    // drift apart.
    struct Entry {
        long instance = 0;
        std::string path;
        int frames = 1;
    };
    std::map<std::string, std::vector<Entry>> sortKeys;

    if (control && control->cancel.load(std::memory_order_relaxed)) return {};

    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return {};

    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (control) {
            control->examined.fetch_add(1, std::memory_order_relaxed);
            // Checked per entry: the walk is the slow part of a big folder, and one header parse is
            // short enough that stopping between entries is all the responsiveness anyone needs.
            if (control->cancel.load(std::memory_order_relaxed)) return {};
        }
        if (!it->is_regular_file(ec)) continue;
        const fs::path p = it->path();

        auto header = loadHeader(p.string());
        if (!header) continue;
        DcmDataset* ds = header->getDataset();

        SeriesInfo s;
        // anyVRString throughout: the RT Dose in the corpus carries its Series Instance UID, Modality
        // and Patient Name as VR=UN, and the escaped string the getter hands back would group it under
        // a mangled UID and show it with a blank modality.
        s.seriesUid = anyVRString(ds, DCM_SeriesInstanceUID);
        if (s.seriesUid.empty()) continue;
        s.studyUid = anyVRString(ds, DCM_StudyInstanceUID);
        s.modality = anyVRString(ds, DCM_Modality);
        s.patientName = anyVRString(ds, DCM_PatientName);
        s.studyDescription = anyVRString(ds, DCM_StudyDescription);
        s.seriesDescription = anyVRString(ds, DCM_SeriesDescription);
        Uint16 rows = 0, cols = 0;
        ds->findAndGetUint16(DCM_Rows, rows);
        ds->findAndGetUint16(DCM_Columns, cols);
        s.width = int(cols);
        s.height = int(rows);

        auto& known = bySeries[s.seriesUid];
        if (known.files.empty()) known = s;                       // first file wins the metadata
        known.files.push_back(p.string());
        sortKeys[s.seriesUid].push_back(Entry{instanceNumber(ds), p.string(), frameCountOf(ds)});
    }

    std::vector<SeriesInfo> out;
    out.reserve(bySeries.size());
    for (auto& [uid, s] : bySeries) {
        auto& keys = sortKeys[uid];
        std::stable_sort(keys.begin(), keys.end(),
                         [](const auto& a, const auto& b) { return a.instance < b.instance; });
        s.files.clear();
        s.frameCounts.clear();
        for (auto& k : keys) {
            s.files.push_back(std::move(k.path));
            s.frameCounts.push_back(k.frames);
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const SeriesInfo& a, const SeriesInfo& b) {
        if (a.studyDescription != b.studyDescription) return a.studyDescription < b.studyDescription;
        return a.seriesUid < b.seriesUid;
    });
    return out;
}

int SeriesInfo::totalFrames() const
{
    if (frameCounts.size() != files.size()) return int(files.size());
    int total = 0;
    for (int n : frameCounts) total += n > 0 ? n : 1;
    return total;
}

bool SeriesInfo::locate(int flat, int& fileIndex, int& frameIndex) const
{
    fileIndex = -1;
    frameIndex = -1;
    if (flat < 0) return false;
    if (frameCounts.size() != files.size()) {   // counts unknown: one frame per object
        if (flat >= int(files.size())) return false;
        fileIndex = flat;
        frameIndex = 0;
        return true;
    }
    int remaining = flat;
    for (size_t i = 0; i < files.size(); ++i) {
        const int n = frameCounts[i] > 0 ? frameCounts[i] : 1;
        if (remaining < n) {
            fileIndex = int(i);
            frameIndex = remaining;
            return true;
        }
        remaining -= n;
    }
    return false;
}

bool loadFrame(const std::string& file, ImageFrame& out, int frameIndex)
{
    registerCodecs();
    DcmFileFormat ff;
    if (ff.loadFile(file.c_str()).bad()) return false;
    DcmDataset* ds = ff.getDataset();
    if (!ds) return false;

    // Decompress encapsulated pixel data before DicomImage sees it. DicomImage's own encapsulated
    // path refuses objects that are perfectly valid through the codecs (32-bit RLE RT Dose is the
    // case we have), while chooseRepresentation goes straight through them. One path for every file:
    // normalize to little-endian explicit, then hand DicomImage plain pixel data.
    if (ds->chooseRepresentation(EXS_LittleEndianExplicit, nullptr).bad()) return false;

    // CIF_IgnoreModalityTransformation: keep stored values and apply slope/intercept ourselves,
    // so the window/level numbers match what Horos shows for the same object.
    DicomImage img(ds, EXS_LittleEndianExplicit, CIF_IgnoreModalityTransformation);
    if (img.getStatus() != EIS_Normal) return false;
    const size_t frames = size_t(img.getFrameCount());
    const size_t perFrame = size_t(img.getWidth()) * size_t(img.getHeight());
    if (frames < 1 || perFrame == 0) return false;
    // Clamped, not rejected: a caller holding an index from a stale scan gets the nearest frame.
    const size_t want = frameIndex < 0 ? 0 : std::min(size_t(frameIndex), frames - 1);

    // Colour first, because it needs a different DCMTK accessor than greyscale.
    //
    // PALETTE COLOR: SamplesPerPixel is 1 but the stored values are INDICES into per-channel LUTs
    // the file carries (PS3.3 C.7.9). DCMTK tolerates palette objects but its DicomImage palette
    // support is too partial to trust, so the mapping is done here instead: indexes come out of
    // the mono pixel data path (they are ordinary 8/16-bit values), and each channel's LUT
    // (descriptor + data, read as Uint16 whatever VR the writer chose) turns them into display
    // RGB. Output is exactly the values the file's tables define — no scaling, no window.
    {
        OFString pi;
        if (ds->findAndGetOFStringArray(DCM_PhotometricInterpretation, pi).good() &&
            pi == "PALETTE COLOR") {
            // The indexes are the object's own pixel bytes (after chooseRepresentation the data
            // is uncompressed explicit-LE whatever the file shipped), so read them straight from
            // the dataset rather than through DicomImage: getInterData() on a palette object is
            // representation-dependent and not trustworthy here.
            Uint16 bitsAllocated = 8;
            (void)ds->findAndGetUint16(DCM_BitsAllocated, bitsAllocated);
            DcmElement* pd = nullptr;
            ds->findAndGetElement(DCM_PixelData, pd);
            if (!pd) return false;
            std::vector<float> index;
            index.reserve(perFrame);
            if (bitsAllocated <= 8) {
                Uint8* raw8 = nullptr;
                if (pd->getUint8Array(raw8).bad() || !raw8) return false;
                const size_t n = size_t(pd->getLength());
                const size_t base = size_t(want) * perFrame;
                if (n < base + perFrame) return false;
                for (size_t i = 0; i < perFrame; ++i) index.push_back(float(raw8[base + i]));
            } else {
                Uint16* raw16 = nullptr;
                if (pd->getUint16Array(raw16).bad() || !raw16) return false;
                const size_t n = size_t(pd->getLength()) / 2;
                const size_t base = size_t(want) * perFrame;
                if (n < base + perFrame) return false;
                for (size_t i = 0; i < perFrame; ++i) index.push_back(float(raw16[base + i]));
            }
            struct Lut { const DcmTagKey desc, data; };
            static const Lut luts[3] = {
                {DCM_RedPaletteColorLookupTableDescriptor,
                 DCM_RedPaletteColorLookupTableData},
                {DCM_GreenPaletteColorLookupTableDescriptor,
                 DCM_GreenPaletteColorLookupTableData},
                {DCM_BluePaletteColorLookupTableDescriptor,
                 DCM_BluePaletteColorLookupTableData},
            };
            std::vector<float> rgb(index.size() * 3, 0.0f);
            for (int c = 0; c < 3; ++c) {
                const Uint16* desc = nullptr;
                unsigned long descCount = 0;
                if (ds->findAndGetUint16Array(luts[c].desc, desc, &descCount).bad() ||
                    !desc || descCount < 2)
                    return false;
                const unsigned long entries = desc[0];
                const unsigned long first = desc[1];
                if (entries == 0) return false;
                const Uint16* data = nullptr;
                unsigned long dataCount = 0;
                if (ds->findAndGetUint16Array(luts[c].data, data, &dataCount).bad() ||
                    !data)
                    return false;
                const unsigned long usable = std::min(dataCount, entries);
                const unsigned long bits = descCount > 2 ? desc[2] : 16;
                for (size_t i = 0; i < index.size(); ++i) {
                    long idx = std::lroundf(index[i]) - long(first);
                    const long last = long(usable) - 1;
                    idx = std::clamp(idx, 0L, last);
                    float value = float(data[size_t(idx)]);
                    // 8-bit entries hold 0..255 directly; 16-bit tables scale to display units so
                    // the canvas gets the same 0..255 range either way.
                    if (bits >= 16) value = std::min(value, 255.0f);
                    rgb[i * 3 + size_t(c)] = value;
                }
            }
            out = makeColourFrame(std::move(rgb), int(img.getWidth()), int(img.getHeight()));
            out.frameIndex = int(want);
            out.frameCount = int(frames);
            return out.valid();
        }
    }

    // getOutputData() is the one call that returns interleaved 8-bit RGB whatever the file holds:
    // DCMTK converts from RGB, YBR_FULL, YBR_FULL_422 / YBR_PARTIAL_422 (including the subsampling
    // those imply) and honours PlanarConfiguration for us, and scales 16-bit samples down to 8. That
    // scaling is normally something we refuse to let DCMTK do, because it would bake in a VOI choice;
    // a colour image has no VOI transform, so there is nothing to lose here.
    if (!img.isMonochrome()) {
        const void* rgb = img.getOutputData(8, static_cast<unsigned long>(want));
        if (!rgb) return false;
        const auto* p = static_cast<const Uint8*>(rgb);
        std::vector<float> values(perFrame * 3);
        for (size_t i = 0; i < values.size(); ++i) values[i] = float(p[i]);
        out = makeColourFrame(std::move(values), int(img.getWidth()), int(img.getHeight()));
        out.frameIndex = int(want);
        out.frameCount = int(frames);
        return out.valid();
    }

    // Pick this frame out of the pixel buffer. DicomImage usually hands back the whole stack, so the
    // frames are contiguous and the frame's own offset is all that is needed; when the buffer holds
    // fewer frames than NumberOfFrames claims, a single-frame sub-image is built rather than trusting
    // an offset that is not there.
    const DiPixel* inter = img.getInterData();
    if (!inter) return false;
    size_t offset = want * perFrame;
    std::unique_ptr<DicomImage> single;
    if (inter->getCount() < offset + perFrame) {
        single.reset(img.createDicomImage(static_cast<unsigned long>(want), 1));
        if (!single || single->getStatus() != EIS_Normal) return false;
        inter = single->getInterData();
        offset = 0;
        if (!inter || inter->getCount() < perFrame) return false;
    }

    // DicomImage::getInterData() is a DiPixel*; for a monochrome image it is a DiMonoPixel,
    // which is what carries the representation (Uint8/16/32, signed or not).
    const DiMonoPixel* mono = static_cast<const DiMonoPixel*>(inter);
    if (!mono) return false;
    std::vector<float> raw;
    if (!interDataToFloat(mono, offset, perFrame, raw)) return false;

    float slope = 1.0f;
    float intercept = 0.0f;
    Float64 v = 0;
    if (ds->findAndGetFloat64(DCM_RescaleSlope, v).good()) slope = float(v);
    if (ds->findAndGetFloat64(DCM_RescaleIntercept, v).good()) intercept = float(v);


    if (!ds->tagExistsWithValue(DCM_RescaleSlope)) {
        // RT Dose carries no rescale tags: the modality transform is DoseGridScaling alone, a
        // multiplier from stored values to Gy. anyVRString because the corpus object (and real RT
        // Dose from some writers) carries it as VR=UN. Units land in Gy, which is what the window
        // numbers then show.
        const std::string dose = anyVRString(ds, DCM_DoseGridScaling);
        if (!dose.empty()) {
            const double d = std::strtod(dose.c_str(), nullptr);
            if (d > 0) slope = float(d);
        }
    }

    // PixelPaddingValue is US or SS; DCMTK's accessors are VR-strict (Sint32 refuses SS),
    // so branch on the VR actually present in the file.
    bool hasPadding = false;
    float paddingValue = 0.0f;
    {
        DcmElement* elem = nullptr;
        ds->findAndGetElement(DCM_PixelPaddingValue, elem);
        if (elem && elem->getVR() == EVR_US) {
            Uint16 v = 0;
            hasPadding = ds->findAndGetUint16(DCM_PixelPaddingValue, v).good();
            paddingValue = float(v);
        } else if (elem && elem->getVR() == EVR_SS) {
            Sint16 v = 0;
            hasPadding = ds->findAndGetSint16(DCM_PixelPaddingValue, v).good();
            paddingValue = float(v);
        }
    }

    // PhotometricInterpretation decides which way round the grey ramp runs. MONOCHROME1 means high
    // stored values are displayed dark — the flip belongs at presentation, so it is recorded on the
    // frame and applied by render(), leaving the stored values (and the window numbers) untouched.
    bool invert = false;
    {
        OFString pi;
        if (ds->findAndGetOFStringArray(DCM_PhotometricInterpretation, pi).good())
            invert = (pi == "MONOCHROME1");
    }

    // Non-linear Modality LUT (PS3.3 C.11.2.1.3): a Modality LUT Sequence with LUT Descriptor +
    // LUT Data. When it is present the standard forbids Rescale tags, and the transform is a
    // lookup, not the linear slope/intercept: stored value x maps to LUTData[clamp(x - first)].
    // The LUT is applied HERE, before makeFrame, so pixels/min/max/window numbers are all in
    // post-LUT modality units and the window maths stays exactly the C.11.2.1.2 formula it already
    // is. (Linear objects skip this branch entirely: their numbers keep using slope/intercept.)
    if (ds->tagExistsWithValue(DCM_ModalityLUTSequence)) {
        DcmSequenceOfItems* sq = nullptr;
        if (ds->findAndGetSequence(DCM_ModalityLUTSequence, sq).bad() || !sq ||
            sq->card() < 1)
            return false;
        DcmItem* item = sq->getItem(0);
        if (!item) return false;

        const Uint16* desc = nullptr;
        unsigned long descCount = 0;
        if (item->findAndGetUint16Array(DCM_LUTDescriptor, desc, &descCount).bad() || !desc ||
            descCount < 2)
            return false;
        // Descriptor order (C.11.2.5.1): number of entries, first mapped value, bits per entry.
        // DCMTK writes the first two always; the bits field is optional (default 16).
        const unsigned long entries = desc[0];
        const unsigned long first = desc[1];
        if (entries == 0) return false;
        const Uint16* data = nullptr;
        unsigned long dataCount = 0;
        if (item->findAndGetUint16Array(DCM_LUTData, data, &dataCount).bad() || !data ||
            dataCount == 0)
            return false;
        const unsigned long usable = std::min(dataCount, entries);
        for (size_t i = 0; i < raw.size(); ++i) {
            long idx = std::lroundf(raw[i]) - long(first);
            idx = std::clamp(idx, 0L, long(usable) - 1);
            raw[i] = float(data[size_t(idx)]);
        }
        // Padding is expressed in STORED units and must still be filtered after the value has
        // been mapped, so the padding marker is mapped through the LUT too.
        if (hasPadding) {
            long padIdx = std::lroundf(paddingValue) - long(first);
            paddingValue = float(data[size_t(std::clamp(padIdx, 0L, long(usable) - 1))]);
        }
        slope = 1.0f;
        intercept = 0.0f;   // the LUT already did the work
    }

    out = makeFrame(std::move(raw), int(img.getWidth()), int(img.getHeight()),
                    slope, intercept, hasPadding, paddingValue);
    out.invert = invert;
    out.frameIndex = int(want);
    out.frameCount = int(frames);
    return out.valid();
}

}  // namespace clivus
