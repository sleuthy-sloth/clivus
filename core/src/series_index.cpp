// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/series_index.h"

#include "clivus/codecs.h"

#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmdata/dcspchrs.h>    // declared-charset text conversion
#include <dcmtk/dcmimage/diregist.h>   // registers the colour image classes with DicomImage
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmimgle/dimopx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>

namespace clivus {
namespace fs = std::filesystem;

namespace {

using Vec3 = std::array<double, 3>;

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

// Read doubles from a DCMTK element into a vector
bool readDoubles(DcmItem* ds, const DcmTagKey& key, std::vector<double>& out, size_t n)
{
    DcmElement* elem = nullptr;
    ds->findAndGetElement(key, elem);
    if (!elem) return false;
    out.assign(n, 0.0);
    for (size_t i = 0; i < n; ++i) {
        Float64 v = 0.0;
        if (elem->getFloat64(v, static_cast<unsigned long>(i)).bad()) return false;
        out[i] = v;
    }
    return true;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

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

// A dataset states its text encoding in SpecificCharacterSet (0008,0005), and everything downstream
// decodes bytes as UTF-8. Convert once, here, where the bytes are read: without it a dataset that
// declares ISO_IR 100 (Latin-1) reaches the UI as U+FFFD replacement characters, so a patient or
// series name with an accent in it reads as a broken file. Older European exports commonly declare
// Latin-1, and the fix belongs at this one point rather than at each call site in the UI.
// Best effort by design: when this DCMTK build has no conversion support, or the declared set cannot
// be selected, the bytes are left exactly as they were rather than the file being refused.
void normaliseCharacterSet(DcmDataset* ds)
{
    if (!ds || !ds->tagExistsWithValue(DCM_SpecificCharacterSet)) return;
    if (!DcmSpecificCharacterSet::isConversionAvailable()) return;
    DcmSpecificCharacterSet converter;
    if (converter.selectCharacterSet(*ds).good()) ds->convertCharacterSet(converter);
}

// Reads one object's header, or nothing when the object has no pixel data to show. The first read
// caps element values at 4096 bytes, which is what keeps a folder scan cheap; if it reports no Pixel
// Data the object is read once more with no cap before being written off, because that cap is the
// only thing sitting between a value-heavy object and a normal parse. Only a would-be miss pays for
// the second read.
// Storage classes that carry no Pixel Data by definition. Used only to skip the unbounded second read:
// the list is a deny-list on purpose, so an object it does not recognise still gets the full parse and
// is judged on whether it actually has pixels. Missing one of these costs a re-read; listing an image
// class here would hide a series, which is the worse mistake.
bool isNonImageStorage(DcmDataset* ds)
{
    static const char* const kNonImage[] = {
        "1.2.840.10008.1.3.10",             // Media Storage Directory (DICOMDIR)
        "1.2.840.10008.5.1.4.1.1.11.1",     // Grayscale Softcopy Presentation State
        "1.2.840.10008.5.1.4.1.1.11.2",     // Color Softcopy Presentation State
        "1.2.840.10008.5.1.4.1.1.11.3",     // Pseudo-Color Softcopy Presentation State
        "1.2.840.10008.5.1.4.1.1.11.4",     // Blending Softcopy Presentation State
        "1.2.840.10008.5.1.4.1.1.66.4",     // Segmentation
        "1.2.840.10008.5.1.4.1.1.88.11",    // Basic Text SR
        "1.2.840.10008.5.1.4.1.1.88.22",    // Enhanced SR
        "1.2.840.10008.5.1.4.1.1.88.33",    // Comprehensive SR
        "1.2.840.10008.5.1.4.1.1.88.34",    // Comprehensive SR Instance
        "1.2.840.10008.5.1.4.1.1.88.59",    // Key Object Selection Document
        "1.2.840.10008.5.1.4.1.1.88.88",    // Acquisition Context SR
        "1.2.840.10008.5.1.4.1.1.104.1",    // Encapsulated PDF
        "1.2.840.10008.5.1.4.1.1.104.2",    // Encapsulated CDA
        "1.2.840.10008.5.1.4.1.1.128",      // Basic Structured Display
        "1.2.840.10008.5.1.4.1.1.481.2",    // RT Structure Set
        "1.2.840.10008.5.1.4.1.1.481.5",    // RT Plan
        "1.2.840.10008.5.1.4.1.1.481.8",    // RT Ion Plan
    };
    OFString sopClass;
    if (ds->findAndGetOFString(DCM_SOPClassUID, sopClass).bad()) return false;
    for (const char* known : kNonImage) {
        if (sopClass == known) return true;
    }
    return false;
}

// Reads one object's header, or nothing when the object has no pixel data to show. The first read
// caps element values at 4096 bytes, which is what keeps a folder scan cheap; if it reports no Pixel
// Data the object is read once more with no cap before being written off, because that cap is the
// only thing sitting between a value-heavy object and a normal parse. Only a would-be miss pays for
// the second read — and a known non-image storage class does not pay it at all, which is what a
// folder full of structured reports and RT structure sets used to cost.
std::unique_ptr<DcmFileFormat> loadHeader(const std::string& file, bool* skippedNonImage = nullptr)
{
    if (skippedNonImage) *skippedNonImage = false;
    std::unique_ptr<DcmFileFormat> chosen;

    auto header = std::make_unique<DcmFileFormat>();
    if (header->loadFile(file.c_str(), EXS_Unknown, EGL_noChange, 4096).bad()) return nullptr;
    DcmDataset* ds = header->getDataset();
    if (ds && ds->tagExistsWithValue(DCM_PixelData)) {
        chosen = std::move(header);
    } else {
        if (ds && isNonImageStorage(ds)) {
            if (skippedNonImage) *skippedNonImage = true;
            return nullptr;   // a report or a structure set: no pixels to look for
        }
        auto full = std::make_unique<DcmFileFormat>();
        if (full->loadFile(file.c_str(), EXS_Unknown, EGL_noChange, 0xFFFFFFFFu).bad()) return nullptr;
        DcmDataset* fullDs = full->getDataset();
        if (!fullDs || !fullDs->tagExistsWithValue(DCM_PixelData)) return nullptr;
        chosen = std::move(full);
    }
    normaliseCharacterSet(chosen->getDataset());
    return chosen;
}

// Read per-slice geometry from a dataset
SeriesInfo::SliceGeometry readSliceGeometry(DcmDataset* ds)
{
    SeriesInfo::SliceGeometry geom;
    
    // ImagePositionPatient (3 doubles)
    std::vector<double> ipp;
    if (readDoubles(ds, DCM_ImagePositionPatient, ipp, 3)) {
        geom.originX = ipp[0];
        geom.originY = ipp[1];
        geom.originZ = ipp[2];
    }
    
    // ImageOrientationPatient (6 doubles - row then column cosines)
    std::vector<double> iop;
    if (readDoubles(ds, DCM_ImageOrientationPatient, iop, 6)) {
        geom.orientRowX = iop[0];
        geom.orientRowY = iop[1];
        geom.orientRowZ = iop[2];
        geom.orientColX = iop[3];
        geom.orientColY = iop[4];
        geom.orientColZ = iop[5];
    }
    
    // SliceLocation (optional)
    Float64 sl = 0.0;
    if (ds->findAndGetFloat64(DCM_SliceLocation, sl).good()) {
        geom.sliceLocation = sl;
    }
    
    // InstanceNumber
    geom.instanceNumber = instanceNumber(ds);
    
    // Check if we have valid geometry (both IPP and IOP present)
    double iopNorm = std::sqrt(iop.empty() ? 0.0 : 
        iop[0]*iop[0] + iop[1]*iop[1] + iop[2]*iop[2] + 
        iop[3]*iop[3] + iop[4]*iop[4] + iop[5]*iop[5]);
    geom.hasGeometry = !ipp.empty() && iopNorm > 1e-9;
    
    return geom;
}

}  // namespace

namespace {

// One parsed object, kept until the walk ends. Grouping is decided after the walk in a sorted order,
// so which file supplies a series' metadata is no longer whatever the filesystem happened to hand
// over first: the same folder now indexes the same way twice.
struct FileRecord {
    std::string path;
    long instance = 0;
    int frames = 1;
    SeriesInfo::SliceGeometry geometry;
    std::string seriesUid;
    std::string studyUid;
    std::string patientId;
    std::string patientName;
    std::string modality;
    std::string studyDescription;
    std::string seriesDescription;
    int width = 0;
    int height = 0;
};

// The identity two objects must share to belong to one series. An absent value is not evidence of a
// different patient, so it never conflicts; only two different, present values do. PatientID is
// compared before PatientName, because an ID that matches settles the question and a name that was
// merely corrected should not split a series.
bool identityDiffers(const FileRecord& a, const FileRecord& b)
{
    if (a.studyUid != b.studyUid) return true;
    if (!a.patientId.empty() && !b.patientId.empty()) return a.patientId != b.patientId;
    if (!a.patientName.empty() && !b.patientName.empty()) return a.patientName != b.patientName;
    return false;
}

// What identifies the patient inside a split group. Only used to keep one conflicting patient's
// files together instead of giving every file a series of its own.
std::string patientKey(const FileRecord& r)
{
    if (!r.patientId.empty()) return "id:" + r.patientId;
    if (!r.patientName.empty()) return "name:" + r.patientName;
    return "unidentified";
}

struct GroupKey {
    std::string studyUid;
    std::string seriesUid;
    std::string identity;   // empty for the first identity seen under a UID, patientKey() for a split
    bool operator<(const GroupKey& o) const
    {
        if (studyUid != o.studyUid) return studyUid < o.studyUid;
        if (seriesUid != o.seriesUid) return seriesUid < o.seriesUid;
        return identity < o.identity;
    }
};

// Objects with no SeriesInstanceUID are held here rather than dropped: one group per scanned root, so
// they stay reachable and can never be mistaken for a series the sender actually named.
constexpr const char* kQuarantineSeriesUid = "quarantine";

struct Group {
    SeriesInfo info;
    std::vector<const FileRecord*> records;
};

// How many objects are parsed between progress snapshots. Small enough that a big folder starts
// showing series quickly, large enough that the regrouping below is noise next to the file reads.
constexpr size_t kProgressInterval = 64;

// Turns the objects parsed so far into series. Called once at the end, and again at intervals while
// the walk is still running so the browser can show what it has instead of nothing.
// Records are read, never consumed, which is what makes repeated calls possible.
std::vector<SeriesInfo> buildSeries(const std::vector<FileRecord>& records,
                                    ScanDiagnostics* diagnostics = nullptr)
{
    std::vector<SeriesInfo> out;

    std::vector<const FileRecord*> ordered;
    ordered.reserve(records.size());
    for (const FileRecord& r : records) ordered.push_back(&r);
    std::sort(ordered.begin(), ordered.end(), [](const FileRecord* a, const FileRecord* b) {
        if (a->studyUid != b->studyUid) return a->studyUid < b->studyUid;
        if (a->seriesUid != b->seriesUid) return a->seriesUid < b->seriesUid;
        return a->path < b->path;
    });

    std::map<GroupKey, Group> groups;
    std::map<std::string, const FileRecord*> firstIdentity;   // series UID -> first object seen
    int splitIndex = 0;                                      // names split groups in scan order, so a
                                                             // rescan produces the same names

    for (const FileRecord* rp : ordered) {
        const FileRecord& r = *rp;
        GroupKey key{r.studyUid, r.seriesUid, std::string()};
        if (r.seriesUid.empty()) {
            key.studyUid.clear();
            key.seriesUid = kQuarantineSeriesUid;
            if (diagnostics) ++diagnostics->quarantinedObjects;
        } else {
            const auto first = firstIdentity.find(r.seriesUid);
            if (first == firstIdentity.end()) {
                firstIdentity.emplace(r.seriesUid, rp);
            } else if (identityDiffers(*first->second, r)) {
                // Same series UID, different patient identity: a separate group, and a count so the
                // caller can report it instead of the rows quietly merging.
                key.identity = patientKey(r);
                if (groups.find(key) == groups.end() && diagnostics) ++diagnostics->conflictSplits;
            }
        }

        Group& g = groups[key];
        if (g.records.empty()) {
            g.info.seriesUid = r.seriesUid;
            g.info.studyUid = r.studyUid;
            g.info.modality = r.modality;
            g.info.patientName = r.patientName;
            g.info.patientId = r.patientId;
            g.info.studyDescription = r.studyDescription;
            g.info.seriesDescription = r.seriesDescription;
            g.info.width = r.width;
            g.info.height = r.height;
            g.info.quarantined = r.seriesUid.empty();
            // A split group cannot keep the shared UID: the cache and the UI both address a series by
            // (study, series), so two groups under one pair would overwrite each other.
            if (!key.identity.empty())
                g.info.seriesUid += "#split" + std::to_string(++splitIndex);
        }
        g.records.push_back(rp);
    }

    out.reserve(groups.size());
    for (auto& [key, g] : groups) {
        // The sort key carries the frame count, so files and frameCounts come out of one sort and
        // cannot drift apart.
        std::stable_sort(g.records.begin(), g.records.end(),
                         [](const FileRecord* a, const FileRecord* b) {
                             if (a->instance != b->instance) return a->instance < b->instance;
                             return a->path < b->path;
                         });
        SeriesInfo s = std::move(g.info);
        for (const FileRecord* r : g.records) {
            s.files.push_back(r->path);
            s.frameCounts.push_back(r->frames);
            s.sliceGeometry.push_back(r->geometry);
        }
        out.push_back(std::move(s));
    }
    std::sort(out.begin(), out.end(), [](const SeriesInfo& a, const SeriesInfo& b) {
        // The quarantine group goes last: objects with no series identity are worth showing, but they
        // are not a study and must not lead the list a person reads.
        if (a.quarantined != b.quarantined) return !a.quarantined;
        if (a.studyDescription != b.studyDescription) return a.studyDescription < b.studyDescription;
        // Series UID last keeps the order total (split groups carry their own #splitN suffix). The
        // display ordering itself is the browser's job, not the scan's: changing it here reorders
        // every caller's list, which is not this change's business.
        return a.seriesUid < b.seriesUid;
    });
    return out;
}

}  // namespace

std::vector<SeriesInfo> scanDirectory(const std::string& dir, ScanControl* control,
                                      ScanDiagnostics* diagnostics, const ScanProgress& progress)
{
    registerCodecs();
    if (diagnostics) *diagnostics = ScanDiagnostics{};

    if (control && control->cancel.load(std::memory_order_relaxed)) return {};

    std::vector<FileRecord> records;

    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return {};

    // Per-entry errors (a denied subfolder, a file that vanished mid-walk, an unreadable junction)
    // must not end the whole walk: quietly skipping the bad entry and continuing is what the scan
    // promise means. Breaking on the first error walked only the entries before it — on Windows
    // that read as "subfolders are never scanned" when one bad directory sat early in the order.
    // (A walk that still errors on increment after a pop is exhausted by the iterator, not us.)
    for (; it != fs::recursive_directory_iterator();) {
        if (control) {
            control->examined.fetch_add(1, std::memory_order_relaxed);
            // Checked per entry: the walk is the slow part of a big folder, and one header parse is
            // short enough that stopping between entries is all the responsiveness anyone needs.
            if (control->cancel.load(std::memory_order_relaxed)) return {};
        }
        if (!it->is_regular_file(ec)) { it.increment(ec); continue; }
        const fs::path p = it->path();
        bool nonImage = false;
        auto header = loadHeader(p.string(), &nonImage);
        if (!header) {
            if (nonImage && diagnostics) ++diagnostics->skippedNonImage;
            it.increment(ec);
            continue;
        }
        DcmDataset* ds = header->getDataset();

        FileRecord r;
        r.path = p.string();
        r.instance = instanceNumber(ds);
        r.frames = frameCountOf(ds);
        r.geometry = readSliceGeometry(ds);
        // anyVRString throughout: the RT Dose in the corpus carries its Series Instance UID, Modality
        // and Patient Name as VR=UN, and the escaped string the getter hands back would group it under
        // a mangled UID and show it with a blank modality.
        r.seriesUid = anyVRString(ds, DCM_SeriesInstanceUID);
        r.studyUid = anyVRString(ds, DCM_StudyInstanceUID);
        r.modality = anyVRString(ds, DCM_Modality);
        r.patientName = anyVRString(ds, DCM_PatientName);
        r.patientId = anyVRString(ds, DCM_PatientID);
        r.studyDescription = anyVRString(ds, DCM_StudyDescription);
        r.seriesDescription = anyVRString(ds, DCM_SeriesDescription);
        Uint16 rows = 0, cols = 0;
        ds->findAndGetUint16(DCM_Rows, rows);
        ds->findAndGetUint16(DCM_Columns, cols);
        r.width = int(cols);
        r.height = int(rows);
        records.push_back(std::move(r));
        it.increment(ec);
        // Mid-walk snapshot: the browser can show what has been found so far instead of an empty pane
        // for the whole walk. Cheap relative to the file reads it sits between, and the last call
        // below is the one that counts.
        if (progress && records.size() % kProgressInterval == 0) progress(buildSeries(records));
    }

    // The final list is authoritative; any snapshot taken during the walk is superseded here.
    return buildSeries(records, diagnostics);
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
    // Physical pixel size (PS3.3 C.7.6.3.1.1): Pixel Spacing carries [row, column] spacing in mm.
    // Values stay on the frame; measurements decide what an unknown spacing is allowed to mean
    // (nothing) — this only records what the header said. Read as TEXT and split, not through
    // findAndGetFloat64Array: the RT Dose in the wild (and in this corpus) carries it as VR=UN,
    // which the numeric accessor refuses while the values themselves are perfectly good.
    {
        OFString spacingText;
        if (ds->findAndGetOFStringArray(DCM_PixelSpacing, spacingText).good() &&
            !spacingText.empty()) {
            const std::string text(spacingText.c_str());
            const size_t slash = text.find('\\');
            const auto toDouble = [](const std::string& part) {
                try { return std::stod(part); } catch (...) { return 0.0; }
            };
            if (slash != std::string::npos) {
                const double row = toDouble(text.substr(0, slash));
                const double col = toDouble(text.substr(slash + 1));
                if (row > 0.0 && col > 0.0) {
                    out.pixelSpacingRow = row;
                    out.pixelSpacingCol = col;
                }
            }
        }
    }
    return out.valid();
}

// SeriesInfo member functions for geometry

bool SeriesInfo::hasGeometry() const
{
    for (const auto& geom : sliceGeometry) {
        if (geom.hasGeometry) return true;
    }
    return false;
}

bool SeriesInfo::hasFullGeometry() const
{
    if (sliceGeometry.empty() || sliceGeometry.size() != files.size()) return false;
    for (const auto& geom : sliceGeometry) {
        if (!geom.hasGeometry) return false;
    }
    return true;
}

std::array<double, 3> SeriesInfo::normal() const
{
    for (const auto& geom : sliceGeometry) {
        if (geom.hasGeometry) {
            Vec3 row{geom.orientRowX, geom.orientRowY, geom.orientRowZ};
            Vec3 col{geom.orientColX, geom.orientColY, geom.orientColZ};
            Vec3 n = cross(row, col);
            double nNorm = norm(n);
            if (nNorm > 1e-9) {
                return {n[0] / nNorm, n[1] / nNorm, n[2] / nNorm};
            }
        }
    }
    return {0.0, 0.0, 1.0};  // default to axial when no geometry
}

std::vector<double> SeriesInfo::alongDistances() const
{
    std::vector<double> result;
    if (!hasGeometry()) return result;
    
    auto n = normal();
    for (const auto& geom : sliceGeometry) {
        if (geom.hasGeometry) {
            Vec3 ipp{geom.originX, geom.originY, geom.originZ};
            result.push_back(dot(ipp, n));
        }
    }
    return result;
}

// Orientation label from ImageOrientationPatient row/col cosines.
// Returns "Axial", "Sagittal", "Coronal", or "Oblique" based on which anatomical plane
// the slice normal is closest to. No R/L/A/P markers are added.
// Returns empty string when orientation cosines are not valid (all zeros or near-zero).
std::string orientationLabel(const std::array<double, 6>& iop)
{
    Vec3 row{iop[0], iop[1], iop[2]};
    Vec3 col{iop[3], iop[4], iop[5]};
    
    double rowNorm = norm(row);
    double colNorm = norm(col);
    if (rowNorm < 1e-9 || colNorm < 1e-9) return "";
    
    Vec3 n = cross(row, col);
    double nNorm = norm(n);
    if (nNorm < 1e-9) return "";
    
    n = {n[0] / nNorm, n[1] / nNorm, n[2] / nNorm};
    
    // Check which anatomical axis the normal is closest to
    // Axial: normal ~ (0, 0, ±1)
    // Sagittal: normal ~ (±1, 0, 0)
    // Coronal: normal ~ (0, ±1, 0)
    double axAxial = std::abs(n[2]);
    double axSagittal = std::abs(n[0]);
    double axCoronal = std::abs(n[1]);
    
    // Threshold for considering it "aligned" with an axis (cosine of 45° ≈ 0.707)
    // Use > threshold, not >=, so exactly 45° counts as oblique
        // Stricter threshold for alignment to a primary anatomical plane (e.g., within ~18 degrees of an axis)
    const double alignedThreshold = 0.95; 
    
    if (axAxial > alignedThreshold) {
        return "Axial";
    } else if (axSagittal > alignedThreshold) {
        return "Sagittal";
    } else if (axCoronal > alignedThreshold) {
        return "Coronal";
    }
    
    return "Oblique";
}

}  // namespace clivus
