#include "hw/series_index.h"

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmimgle/dimopx.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>

namespace hw {
namespace fs = std::filesystem;

namespace {

std::string tagString(DcmItem* ds, const DcmTagKey& key)
{
    OFString v;
    if (ds && ds->findAndGetOFStringArray(key, v).good()) return std::string(v.c_str());
    return {};
}

long instanceNumber(DcmItem* ds)
{
    OFString v;
    if (ds && ds->findAndGetOFStringArray(DCM_InstanceNumber, v).good())
        return std::strtol(v.c_str(), nullptr, 10);
    return 0;
}

// Internal representation -> float, without any modality transform (we apply slope/intercept).
bool interDataToFloat(const DiMonoPixel* px, std::vector<float>& out)
{
    if (!px || !px->getData()) return false;
    const size_t n = size_t(px->getCount());
    const void* d = px->getData();
    out.resize(n);
    switch (px->getRepresentation()) {
    case EPR_Uint8:  { auto p = static_cast<const Uint8*>(d); for (size_t i = 0; i < n; ++i) out[i] = float(p[i]); } break;
    case EPR_Sint8:  { auto p = static_cast<const Sint8*>(d); for (size_t i = 0; i < n; ++i) out[i] = float(p[i]); } break;
    case EPR_Uint16: { auto p = static_cast<const Uint16*>(d); for (size_t i = 0; i < n; ++i) out[i] = float(p[i]); } break;
    case EPR_Sint16: { auto p = static_cast<const Sint16*>(d); for (size_t i = 0; i < n; ++i) out[i] = float(p[i]); } break;
    case EPR_Uint32: { auto p = static_cast<const Uint32*>(d); for (size_t i = 0; i < n; ++i) out[i] = float(p[i]); } break;
    case EPR_Sint32: { auto p = static_cast<const Sint32*>(d); for (size_t i = 0; i < n; ++i) out[i] = float(p[i]); } break;
    default: return false;
    }
    return true;
}

}  // namespace

std::vector<SeriesInfo> scanDirectory(const std::string& dir)
{
    std::map<std::string, SeriesInfo> bySeries;
    std::map<std::string, std::vector<std::pair<long, std::string>>> sortKeys;

    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return {};

    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path p = it->path();

        DcmFileFormat ff;
        // 4096-byte cap: reads the header only, so scanning a big folder stays cheap.
        if (ff.loadFile(p.string().c_str(), EXS_Unknown, EGL_noChange, 4096).bad()) continue;
        DcmDataset* ds = ff.getDataset();
        if (!ds || !ds->tagExistsWithValue(DCM_PixelData)) continue;

        SeriesInfo s;
        s.seriesUid = tagString(ds, DCM_SeriesInstanceUID);
        if (s.seriesUid.empty()) continue;
        s.studyUid = tagString(ds, DCM_StudyInstanceUID);
        s.modality = tagString(ds, DCM_Modality);
        s.patientName = tagString(ds, DCM_PatientName);
        s.studyDescription = tagString(ds, DCM_StudyDescription);
        s.seriesDescription = tagString(ds, DCM_SeriesDescription);
        Uint16 rows = 0, cols = 0;
        ds->findAndGetUint16(DCM_Rows, rows);
        ds->findAndGetUint16(DCM_Columns, cols);
        s.width = int(cols);
        s.height = int(rows);

        auto& known = bySeries[s.seriesUid];
        if (known.files.empty()) known = s;                       // first file wins the metadata
        known.files.push_back(p.string());
        sortKeys[s.seriesUid].emplace_back(instanceNumber(ds), p.string());
    }

    std::vector<SeriesInfo> out;
    out.reserve(bySeries.size());
    for (auto& [uid, s] : bySeries) {
        auto& keys = sortKeys[uid];
        std::stable_sort(keys.begin(), keys.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        s.files.clear();
        for (auto& k : keys) s.files.push_back(std::move(k.second));
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const SeriesInfo& a, const SeriesInfo& b) {
        if (a.studyDescription != b.studyDescription) return a.studyDescription < b.studyDescription;
        return a.seriesUid < b.seriesUid;
    });
    return out;
}

bool loadFrame(const std::string& file, ImageFrame& out)
{
    DcmFileFormat ff;
    if (ff.loadFile(file.c_str()).bad()) return false;
    DcmDataset* ds = ff.getDataset();
    if (!ds) return false;

    // CIF_IgnoreModalityTransformation: keep stored values and apply slope/intercept ourselves,
    // so the window/level numbers match what Horos shows for the same object.
    DicomImage img(ds, ds->getOriginalXfer(), CIF_IgnoreModalityTransformation);
    if (img.getStatus() != EIS_Normal || img.getFrameCount() < 1) return false;
    if (!img.isMonochrome()) return false;   // colour images: P1 ticket (planar config / PALETTE COLOR)

    // DicomImage::getInterData() is a DiPixel*; for a monochrome image it is a DiMonoPixel,
    // which is what carries the representation (Uint8/16/32, signed or not).
    const DiMonoPixel* mono = static_cast<const DiMonoPixel*>(img.getInterData());
    if (!mono) return false;
    // shortcut: frame 0 only; multi-frame objects (US cine, enhanced CT) need per-frame access.
    std::vector<float> raw;
    if (!interDataToFloat(mono, raw)) return false;

    float slope = 1.0f;
    float intercept = 0.0f;
    Float64 v = 0;
    if (ds->findAndGetFloat64(DCM_RescaleSlope, v).good()) slope = float(v);
    if (ds->findAndGetFloat64(DCM_RescaleIntercept, v).good()) intercept = float(v);

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

    out = makeFrame(std::move(raw), int(img.getWidth()), int(img.getHeight()),
                    slope, intercept, hasPadding, paddingValue);
    return out.valid();
}

}  // namespace hw
