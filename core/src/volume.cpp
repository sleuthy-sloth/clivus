// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/volume.h"

#include "clivus/codecs.h"

#include <dcmtk/dcmdata/dctk.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace clivus {
namespace {

using Vec3 = std::array<double, 3>;

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

double median(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

struct SliceRef {
    std::string path;
    double along = 0.0;    // distance along the slice normal
    long instance = 0;
    bool hasGeometry = false;
};

}  // namespace

bool loadVolume(const SeriesInfo& series, Volume& out)
{
    registerCodecs();
    if (series.files.empty()) return false;

    // Pass 1: geometry. Orientation is shared by the whole series, so it is read from the first slice
    // that has it; each slice then only contributes its own position.
    std::vector<SliceRef> slices;
    slices.reserve(series.files.size());
    Vec3 normal{0.0, 0.0, 1.0};
    bool haveNormal = false;
    double spacingX = 1.0, spacingY = 1.0, sliceThickness = 0.0;
    
    // Store first slice's geometry for Volume
    Vec3 firstRow{1.0, 0.0, 0.0};
    Vec3 firstCol{0.0, 1.0, 0.0};
    Vec3 firstIPP{0.0, 0.0, 0.0};
    bool haveFirstGeometry = false;

    for (const std::string& file : series.files) {
        DcmFileFormat ff;
        if (ff.loadFile(file.c_str(), EXS_Unknown, EGL_noChange, 4096).bad()) continue;
        DcmDataset* ds = ff.getDataset();
        if (!ds) continue;

        SliceRef ref;
        ref.path = file;
        OFString num;
        if (ds->findAndGetOFStringArray(DCM_InstanceNumber, num).good())
            ref.instance = std::strtol(num.c_str(), nullptr, 10);

        if (!haveNormal) {
            std::vector<double> iop;
            if (readDoubles(ds, DCM_ImageOrientationPatient, iop, 6)) {
                const Vec3 row{iop[0], iop[1], iop[2]};
                const Vec3 col{iop[3], iop[4], iop[5]};
                normal = cross(row, col);
                haveNormal = dot(normal, normal) > 1e-9;
                if (haveNormal) {
                    firstRow = row;
                    firstCol = col;
                }
            }
            std::vector<double> ps;
            if (readDoubles(ds, DCM_PixelSpacing, ps, 2)) {
                spacingY = ps[0];   // PixelSpacing is row spacing then column spacing
                spacingX = ps[1];
            }
            Float64 thick = 0.0;
            if (ds->findAndGetFloat64(DCM_SliceThickness, thick).good()) sliceThickness = thick;
        }

        std::vector<double> ipp;
        if (haveNormal && readDoubles(ds, DCM_ImagePositionPatient, ipp, 3)) {
            ref.along = dot(Vec3{ipp[0], ipp[1], ipp[2]}, normal);
            ref.hasGeometry = true;
            if (!haveFirstGeometry) {
                firstIPP = Vec3{ipp[0], ipp[1], ipp[2]};
                haveFirstGeometry = true;
            }
        }
        slices.push_back(std::move(ref));
    }
    if (slices.empty()) return false;

    // If any slice is missing a position, the geometric order is unusable for the whole series: mixing
    // sorted and unsorted slices would interleave them wrongly. Fall back to InstanceNumber, which is
    // at least monotonic for the ASCII-keyed sources that also omit positions.
    const bool geometric = haveNormal
        && std::all_of(slices.begin(), slices.end(), [](const SliceRef& s) { return s.hasGeometry; });
    std::stable_sort(slices.begin(), slices.end(), [geometric](const SliceRef& a, const SliceRef& b) {
        if (geometric) return a.along < b.along;
        return a.instance < b.instance;
    });

    // Pass 2: pixels.
    Volume vol;
    for (size_t k = 0; k < slices.size(); ++k) {
        ImageFrame frame;
        // A stack with a hole in it is a bug, not a feature: fail loudly instead of leaving zeros.
        if (!loadFrame(slices[k].path, frame)) return false;
        if (vol.width == 0) {
            vol.width = frame.width;
            vol.height = frame.height;
            // shortcut: voxels are float32. A 1000-slice 512^2 study is 1 GB; switch to int16 with a
            // separate slope when that hurts, but it keeps the maths honest for now.
            const size_t bytes = size_t(frame.width) * size_t(frame.height) * slices.size() * sizeof(float);
            if (frame.width <= 0 || frame.height <= 0 || bytes > (4ull << 30)) return false;
            vol.voxels.assign(size_t(frame.width) * size_t(frame.height) * slices.size(), 0.0f);
            vol.minValue = frame.minValue;
            vol.maxValue = frame.maxValue;
        }
        if (frame.width != vol.width || frame.height != vol.height) return false;   // mixed sizes
        std::copy(frame.pixels.begin(), frame.pixels.end(),
                  vol.voxels.begin() + static_cast<std::ptrdiff_t>(k) * vol.width * vol.height);
        vol.minValue = std::min(vol.minValue, frame.minValue);
        vol.maxValue = std::max(vol.maxValue, frame.maxValue);
    }
    vol.depth = int(slices.size());
    if (!vol.valid()) return false;

    vol.spacingX = spacingX > 0.0 ? spacingX : 1.0;
    vol.spacingY = spacingY > 0.0 ? spacingY : 1.0;
    if (geometric && slices.size() > 1) {
        std::vector<double> gaps;
        for (size_t k = 1; k < slices.size(); ++k) gaps.push_back(std::fabs(slices[k].along - slices[k - 1].along));
        const double gap = median(std::move(gaps));
        vol.spacingZ = gap > 1e-6 ? gap : (sliceThickness > 0.0 ? sliceThickness : 1.0);
    } else {
        vol.spacingZ = sliceThickness > 0.0 ? sliceThickness : 1.0;
    }
    
    // Propagate geometry to Volume
    if (haveFirstGeometry) {
        vol.originX = firstIPP[0];
        vol.originY = firstIPP[1];
        vol.originZ = firstIPP[2];
        vol.orientRowX = firstRow[0];
        vol.orientRowY = firstRow[1];
        vol.orientRowZ = firstRow[2];
        vol.orientColX = firstCol[0];
        vol.orientColY = firstCol[1];
        vol.orientColZ = firstCol[2];
        const Vec3 n = cross(firstRow, firstCol);
        const double len = std::sqrt(dot(n, n));
        if (len > 1e-9) { vol.normalX = n[0] / len; vol.normalY = n[1] / len; vol.normalZ = n[2] / len; }
        vol.hasGeometry = true;
    }
    
    out = std::move(vol);
    return true;
}

bool hasUsableGeometry(const SeriesInfo& series)
{
    if (series.files.empty()) return false;
    const size_t n = std::min(series.sliceGeometry.size(), series.files.size());
    if (n < series.files.size()) return false;   // geometry must parallel the file list
    for (size_t i = 0; i < n; ++i)
        if (!series.sliceGeometry[i].hasGeometry) return false;
    return true;
}

bool resliceVolume(const Volume& volume, const MprAxis axis, const int position, ImageFrame& out)
{
    return resliceVolume(volume, axis, position, ResliceOptions{}, out);
}

namespace {

// The three planes differ only in which axis is "depth" and how (u,w) map to the volume. u indexes
// the output's width, w its height; p is the depth position. One sampler keeps the arithmetic
// identical between the three planes (and keeps the row-major output layout the slice browser and
// the tests expect).
double voxelAt(const Volume& v, const MprAxis axis, const int u, const int w, const int p)
{
    switch (axis) {
    case MprAxis::Axial:    return v.at(u, w, p);            // width = x, height = y, depth = z
    case MprAxis::Coronal:  return v.at(u, p, w);            // width = x, height = z, depth = y
    case MprAxis::Sagittal: return v.at(p, w, u);            // width = z, height = y, depth = x
    }
    return 0.0;
}

int depthExtent(const Volume& v, const MprAxis axis)
{
    switch (axis) {
    case MprAxis::Axial:    return v.depth;
    case MprAxis::Coronal:  return v.height;
    case MprAxis::Sagittal: return v.width;
    }
    return 0;
}

// Output size: the two remaining axes.
void planeSize(const Volume& v, const MprAxis axis, int& width, int& height)
{
    switch (axis) {
    case MprAxis::Axial:    width = v.width;  height = v.height; break;
    case MprAxis::Coronal:  width = v.width;  height = v.depth;  break;
    case MprAxis::Sagittal: width = v.depth;  height = v.height; break;
    }
}

}  // namespace

bool resliceVolume(const Volume& volume, const MprAxis axis, const int position,
                   const ResliceOptions& options, ImageFrame& out)
{
    return resliceVolume(volume, axis, double(position), options, out);
}

bool resliceVolume(const Volume& volume, const MprAxis axis, const double position,
                   const ResliceOptions& options, ImageFrame& out)
{
    if (!volume.valid()) return false;
    const int extent = depthExtent(volume, axis);
    if (extent <= 0) return false;
    if (position < 0.0 || position > double(extent - 1) + 0.999999) return false;

    int width = 0, height = 0;
    planeSize(volume, axis, width, height);
    if (width <= 0 || height <= 0) return false;

    const int nearest = std::max(0, std::min(int(std::lround(position)), extent - 1));
    const int low = std::max(0, std::min(int(std::floor(position)), extent - 1));
    const int high = std::min(low + 1, extent - 1);
    const double fraction = std::max(0.0, std::min(1.0, position - low));

    // Slab: how many planes the requested thickness covers. The slab is centred on the position and
    // clipped to the stack, so no value is invented at the edges.
    int slabLow = nearest, slabHigh = nearest;
    if (options.projection != SlabProjection::None && options.slabThicknessMm > 0.0 &&
        volume.spacingZ > 0.0) {
        const int half = std::max(0, int(std::lround(options.slabThicknessMm / volume.spacingZ / 2.0)));
        slabLow = std::max(0, nearest - half);
        slabHigh = std::min(extent - 1, nearest + half);
    }

    ImageFrame frame;
    frame.width = width;
    frame.height = height;
    frame.channels = 1;
    frame.pixels.resize(size_t(width) * size_t(height));
    // Physical spacing of the reformatted image: in-plane axes keep the volume's spacing; the
    // "height" axis of a coronal/sagittal reformat runs along the slice normal, so it gets spacingZ.
    switch (axis) {
    case MprAxis::Axial:
        frame.pixelSpacingRow = volume.spacingY;
        frame.pixelSpacingCol = volume.spacingX;
        break;
    case MprAxis::Coronal:
        frame.pixelSpacingRow = volume.spacingZ;
        frame.pixelSpacingCol = volume.spacingX;
        break;
    case MprAxis::Sagittal:
        // width runs through slices (spacingZ), height along rows (spacingY)
        frame.pixelSpacingRow = volume.spacingY;
        frame.pixelSpacingCol = volume.spacingZ;
        break;
    }

    for (int w = 0; w < height; ++w) {
        for (int u = 0; u < width; ++u) {
            double value = 0.0;
            if (options.projection != SlabProjection::None) {
                bool first = true;
                for (int p = slabLow; p <= slabHigh; ++p) {
                    const double sample = voxelAt(volume, axis, u, w, p);
                    if (first) {
                        value = sample;
                        first = false;
                    } else if (options.projection == SlabProjection::Maximum) {
                        value = std::max(value, sample);
                    } else {
                        value = std::min(value, sample);
                    }
                }
            } else if (options.interpolation == ResliceInterpolation::Linear && low != high) {
                value = (1.0 - fraction) * voxelAt(volume, axis, u, w, low) +
                        fraction * voxelAt(volume, axis, u, w, high);
            } else {
                value = voxelAt(volume, axis, u, w, nearest);
            }
            frame.pixels[size_t(w) * size_t(width) + size_t(u)] = float(value);
        }
    }

    frame.minValue = volume.minValue;
    frame.maxValue = volume.maxValue;
    out = std::move(frame);
    return out.valid();
}

}  // namespace clivus

