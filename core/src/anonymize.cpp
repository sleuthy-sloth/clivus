// SPDX-License-Identifier: GPL-3.0-or-later
#include "clivus/anonymize.h"

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcitem.h>
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcsequen.h>
#include <dcmtk/dcmdata/dctagkey.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace clivus {
namespace {

// Attributes deleted outright. Patient name and ID are handled separately because they are replaced
// rather than removed (an empty PatientName makes some viewers show the field as missing, which reads
// as a broken file rather than an anonymised one).
const DcmTagKey kRemovedTags[] = {
    DCM_PatientBirthDate,          DCM_PatientBirthTime,
    DCM_OtherPatientNames,
    DCM_PatientAddress,            DCM_PatientTelephoneNumbers,
    DCM_PatientComments,           DcmTagKey(0x0010, 0x2160),   // Ethnic Group: DCMTK 3.7 calls this
                                                                 // DCM_EthnicGroups, 3.6 only has the
                                                                 // singular DCM_EthnicGroup
    DCM_InstitutionName,           DCM_InstitutionAddress,
    DCM_InstitutionalDepartmentName,
    DCM_ReferringPhysicianName,    DCM_PerformingPhysicianName,
    DCM_PhysiciansOfRecord,        DCM_NameOfPhysiciansReadingStudy,
    DCM_OperatorsName,
    DCM_AccessionNumber,           DCM_StudyID,
    DCM_ImageComments,             DcmTagKey(0x0032, 0x4000),   // StudyComments (retired)
    DCM_DeviceSerialNumber,        DCM_StationName,
    DCM_IssuerOfPatientID,         DCM_PatientInsurancePlanCodeSequence,
    DCM_ResponsiblePerson,         DCM_ResponsibleOrganization,
    DcmTagKey(0x0008, 0x1072),     // OperatingPhysicianName
    DcmTagKey(0x0008, 0x1048),     // PhysicianOfRecord
    DcmTagKey(0x0008, 0x1049),     // PhysicianOfRecordIdentificationSequence
};

// Date attributes shifted by a constant offset (the interval structure of the study is preserved;
// the absolute dates are not). Time-of-day is left alone: it is not identifying on its own, and
// shifting it would silently change acquisition timing that the images depend on.
const DcmTagKey kDateTags[] = {
    DCM_StudyDate, DCM_SeriesDate, DCM_AcquisitionDate, DCM_ContentDate,
};

const DcmTagKey kDescriptionTags[] = {
    DCM_StudyDescription, DCM_SeriesDescription, DCM_ProtocolName,
};

// UIDs that must be remapped consistently so the copy cannot be joined to the source.
const DcmTagKey kUidTags[] = {
    DCM_StudyInstanceUID, DCM_SeriesInstanceUID, DCM_SOPInstanceUID,
    DCM_ReferencedSOPInstanceUID, DCM_FrameOfReferenceUID, DCM_MediaStorageSOPInstanceUID,
};

std::mt19937_64& rng()
{
    static std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

// A UID under the 2.25 root: a 128-bit random number, unique per call. Not derived from the original,
// so the copy cannot be reversed by hashing.
std::string freshUid()
{
    std::uniform_int_distribution<unsigned long long> dist;
    const unsigned long long high = dist(rng());
    const unsigned long long low = dist(rng());
    return "2.25." + std::to_string(high) + std::to_string(low);
}

std::string freshPatientId()
{
    static const char* hex = "0123456789abcdef";
    std::string id = "CLIVUS-";
    for (int i = 0; i < 10; ++i) id.push_back(hex[rng()() % 16]);
    return id;
}

// YYYYMMDD -> days since epoch (civil calendar, Howard Hinnant's algorithm; no locale, no timezone).
long long daysFromCivil(const int year, const unsigned month, const unsigned day)
{
    long long y = year;
    const unsigned m = month;
    const unsigned d = day;
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

void civilFromDays(long long z, int& year, unsigned& month, unsigned& day)
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(y + (month <= 2));
}

std::string shiftDate(const std::string& value, const int days)
{
    if (value.size() < 8) return value;   // empty, partial or a range: left alone rather than guessed
    for (int i = 0; i < 8; ++i)
        if (value[size_t(i)] < '0' || value[size_t(i)] > '9') return value;
    const int year = std::stoi(value.substr(0, 4));
    const unsigned month = unsigned(std::stoi(value.substr(4, 2)));
    const unsigned day = unsigned(std::stoi(value.substr(6, 2)));
    if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return value;
    long long epochDays = daysFromCivil(year, month, day) + days;
    int newYear = 0;
    unsigned newMonth = 1, newDay = 1;
    civilFromDays(epochDays, newYear, newMonth, newDay);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d%02u%02u", newYear, newMonth, newDay);
    return std::string(buffer);
}

// Walk a dataset (and every nested sequence) for reading only. Nothing may be modified here: an
// element replaced while the list is being iterated frees the object the loop still holds, which is a
// use-after-free (it segfaulted the first version of this file). Callers record what they want and
// apply it afterwards.
template <typename Visit>
void walkReadOnly(DcmItem& item, Visit&& visit)
{
    const unsigned long count = item.card();
    for (unsigned long i = 0; i < count; ++i) {
        DcmElement* element = item.getElement(i);
        if (element == nullptr) continue;
        const bool isSequence = element->ident() == EVR_SQ;
        visit(item, *element);
        if (isSequence) {
            auto* sequence = OFstatic_cast(DcmSequenceOfItems*, element);
            for (unsigned long j = 0; j < sequence->card(); ++j)
                if (DcmItem* nested = sequence->getItem(j)) walkReadOnly(*nested, visit);
        }
    }
}

std::string valueOf(DcmItem& item, const DcmTagKey& tag)
{
    OFString value;
    if (item.findAndGetOFString(tag, value).good()) return std::string(value.c_str());
    return {};
}

bool readFile(const fs::path& path, DcmFileFormat& file)
{
    return file.loadFile(path.string().c_str()).good();
}

}  // namespace

const std::vector<std::string>& deidentifiedAttributeNames()
{
    static const std::vector<std::string> names = {
        "(0010,0010) PatientName            replaced with ANONYMOUS",
        "(0010,0020) PatientID             replaced with a generated ID",
        "(0010,0030) PatientBirthDate       removed",
        "(0010,0032) PatientBirthTime       removed",
        "(0010,0040) PatientSex             removed by default (quasi-identifier)",
        "(0010,1000) OtherPatientIDs        removed",
        "(0010,1001) OtherPatientNames      removed",
        "(0010,1040) PatientAddress         removed",
        "(0010,2154) PatientTelephoneNumbers removed",
        "(0010,4000) PatientComments        removed",
        "(0008,0050) AccessionNumber        removed",
        "(0020,0010) StudyID                removed",
        "(0008,0080) InstitutionName        removed",
        "(0008,0090) ReferringPhysicianName removed",
        "(0008,1050) PerformingPhysicianName removed",
        "(0008,1060) NameOfPhysiciansReadingStudy removed",
        "(0008,1070) OperatorsName          removed",
        "(0018,1000) DeviceSerialNumber     removed",
        "(0018,1030) ProtocolName           cleared (free text)",
        "(0008,1030) StudyDescription       cleared (free text)",
        "(0008,103E) SeriesDescription      cleared (free text)",
        "(0020,000D) StudyInstanceUID       remapped",
        "(0020,000E) SeriesInstanceUID      remapped",
        "(0008,0018) SOPInstanceUID         remapped",
        "(0020,0052) FrameOfReferenceUID    remapped",
        "(0008,0020)-(0008,0023) dates       shifted by one constant offset",
        "every private (odd-group) element   removed",
    };
    return names;
}

bool deidentifyStudy(const std::string& sourceFolder, const std::string& destFolder,
                     const DeidOptions& options, DeidReport& report)
{
    report = DeidReport{};
    std::error_code ec;
    const fs::path source(sourceFolder);
    const fs::path dest(destFolder);
    if (!fs::is_directory(source, ec)) {
        report.failures.push_back("The source folder does not exist.");
        return false;
    }
    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        // Mixing new output into an existing folder would defeat the post-write verification, which
        // reads back what it wrote and expects to find only de-identified objects.
        report.failures.push_back("The destination folder must be empty (an export is verified as a whole).");
        return false;
    }

    const int shiftDays = options.shiftDates
                              ? (options.dateShiftDays != 0
                                     ? options.dateShiftDays
                                     : int(std::uniform_int_distribution<int>(-365, 365)(rng())))
                              : 0;
    report.appliedShiftDays = shiftDays;
    report.newPatientId = freshPatientId();

    std::map<std::string, std::string> uidMap;
    std::set<std::string> originalValues;   // what verification will look for afterwards

    auto remapUid = [&uidMap](const std::string& original) -> std::string {
        if (original.empty()) return original;
        auto found = uidMap.find(original);
        if (found != uidMap.end()) return found->second;
        const std::string replacement = freshUid();
        uidMap.emplace(original, replacement);
        return replacement;
    };

    for (const auto& entry : fs::recursive_directory_iterator(source, ec)) {
        if (ec) {
            report.warnings.push_back("Could not list part of the source folder: " + ec.message());
            ec.clear();
            break;
        }
        if (!entry.is_regular_file()) continue;
        DcmFileFormat file;
        if (!readFile(entry.path(), file)) {
            ++report.skipped;   // not a DICOM object: not copied, cannot be shown to be clean
            continue;
        }
        DcmDataset* dataset = file.getDataset();
        if (dataset == nullptr) {
            ++report.skipped;
            continue;
        }

        // Collect the original identity before touching anything: these are the strings the
        // verification pass must not find afterwards.
        for (const DcmTagKey& tag : {DCM_PatientName, DCM_PatientID, DCM_AccessionNumber}) {
            const std::string value = valueOf(*dataset, tag);
            if (!value.empty() && value != "ANONYMOUS") originalValues.insert(value);
        }

        // Patient name and ID: replaced, not removed.
        if (dataset->putAndInsertString(DCM_PatientName, "ANONYMOUS").good())
            ++report.attributesReplaced;
        if (dataset->putAndInsertString(DCM_PatientID, report.newPatientId.c_str()).good())
            ++report.attributesReplaced;
        if (options.removePatientSex && dataset->findAndDeleteElement(DCM_PatientSex).good())
            ++report.attributesRemoved;

        for (const DcmTagKey& tag : kRemovedTags) {
            if (dataset->findAndDeleteElement(tag).good()) ++report.attributesRemoved;
        }
        if (options.redactDescriptions) {
            for (const DcmTagKey& tag : kDescriptionTags) {
                OFString existing;
                if (dataset->findAndGetOFString(tag, existing).good()) {
                    if (dataset->putAndInsertString(tag, "").good()) ++report.attributesReplaced;
                }
            }
        }
        if (options.shiftDates) {
            for (const DcmTagKey& tag : kDateTags) {
                OFString value;
                if (!dataset->findAndGetOFString(tag, value).good()) continue;
                const std::string shifted = shiftDate(std::string(value.c_str()), shiftDays);
                if (shifted != value.c_str() && dataset->putAndInsertString(tag, shifted.c_str()).good())
                    ++report.datesShifted;
            }
        }
        if (options.remapUids) {
            // Collect first, apply after: see walkReadOnly's note.
            struct Replacement { DcmItem* owner; DcmTagKey tag; std::string value; };
            std::vector<Replacement> replacements;
            walkReadOnly(*dataset, [&](DcmItem& owner, DcmElement& element) {
                const DcmTagKey tag = element.getTag();
                const bool isUid = std::any_of(std::begin(kUidTags), std::end(kUidTags),
                                               [&tag](const DcmTagKey& candidate) { return candidate == tag; });
                if (!isUid) return;
                OFString value;
                if (!element.getOFString(value, 0).good() || value.empty()) return;
                replacements.push_back({&owner, tag, remapUid(std::string(value.c_str()))});
            });
            for (const Replacement& replacement : replacements) {
                if (replacement.owner->putAndInsertString(replacement.tag,
                                                          replacement.value.c_str()).good())
                    ++report.uidsRemapped;
            }
            // The file meta header repeats the SOP Instance UID; leaving it would make the copy
            // inconsistent with its own dataset.
            OFString metaUid;
            if (file.getMetaInfo()->findAndGetOFString(DCM_MediaStorageSOPInstanceUID, metaUid).good()) {
                const std::string replacement = remapUid(std::string(metaUid.c_str()));
                file.getMetaInfo()->putAndInsertString(DCM_MediaStorageSOPInstanceUID,
                                                       replacement.c_str());
            }
        }
        if (options.removePrivateTags) {
            // Collect odd-group tags first: deleting while iterating would shift the element list.
            std::vector<DcmTagKey> privateTags;
            const unsigned long count = dataset->card();
            for (unsigned long i = 0; i < count; ++i) {
                DcmElement* element = dataset->getElement(i);
                if (element != nullptr && (element->getGTag() & 1u)) privateTags.push_back(element->getTag());
            }
            for (const DcmTagKey& tag : privateTags)
                if (dataset->findAndDeleteElement(tag).good()) ++report.privateTagsRemoved;
        }

        // Mirror the destination layout: a subfolder per source subfolder, same file names.
        const fs::path relative = fs::relative(entry.path(), source, ec);
        const fs::path output = dest / (ec ? entry.path().filename() : relative);
        fs::create_directories(output.parent_path(), ec);
        const E_TransferSyntax transferSyntax = file.getDataset()->getOriginalXfer();
        if (!file.saveFile(output.string().c_str(), transferSyntax, EET_ExplicitLength).good()) {
            report.failures.push_back("Could not write " + output.string());
            continue;
        }
        ++report.files;
    }

    if (report.files == 0) {
        report.failures.push_back("No DICOM objects were found in the source folder.");
        return false;
    }

    // --- verification: re-read every written file and look for what was supposed to disappear.
    for (const auto& entry : fs::recursive_directory_iterator(dest, ec)) {
        if (ec || !entry.is_regular_file()) continue;
        DcmFileFormat file;
        if (!readFile(entry.path(), file) || file.getDataset() == nullptr) {
            report.warnings.push_back("Could not re-read " + entry.path().string() + " to verify it.");
            continue;
        }
        DcmDataset* dataset = file.getDataset();
        auto scan = [&](DcmItem& item, auto&& self) -> void {
            for (unsigned long i = 0; i < item.card(); ++i) {
                DcmElement* element = item.getElement(i);
                if (element == nullptr) continue;
                if (options.removePrivateTags && (element->getGTag() & 1u))
                    report.residual.push_back("private element in " + entry.path().filename().string());
                OFString value;
                if (element->getOFStringArray(value).good()) {
                    const std::string text(value.c_str());
                    for (const std::string& original : originalValues) {
                        if (!original.empty() && text == original)
                            report.residual.push_back(entry.path().filename().string() + ": " +
                                                      original);
                    }
                }
                if (element->ident() == EVR_SQ) {
                    auto* sequence = OFstatic_cast(DcmSequenceOfItems*, element);
                    for (unsigned long j = 0; j < sequence->card(); ++j)
                        if (DcmItem* nested = sequence->getItem(j)) self(*nested, self);
                }
            }
        };
        scan(*dataset, scan);
    }
    report.verified = report.residual.empty();

    report.warnings.push_back(
        "Pixel data is unchanged, so burned-in annotations in the image itself (if any) are still "
        "there. Clivus cannot detect burned-in text; review the images before sharing them.");
    report.warnings.push_back(
        "De-identification removes identity from metadata. It is not a legal determination that the "
        "data are anonymous; that judgement belongs to whoever is accountable for the dataset.");
    return report.failures.empty() && report.verified;
}

}  // namespace clivus
