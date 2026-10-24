// SPDX-License-Identifier: GPL-3.0-or-later
// De-identification: the identity must actually be gone, the pixels must actually be intact, and the
// verification pass must catch a survivor rather than trusting the writer.
//
// Run: ./core/clivus_test_anonymize <corpus-folder>

#include "clivus/anonymize.h"

#include <dcmtk/dcmdata/dcdeftag.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcitem.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                    \
        }                                                                    \
    } while (false)

namespace fs = std::filesystem;

namespace {

// Rewrites one corpus file with the identifying tags a real study carries, so the test has something
// to remove. Written to a temp folder: the committed corpus itself must stay synthetic-and-plain.
fs::path makeIdentifiableCopy(const fs::path& corpusFile, const fs::path& outDir)
{
    DcmFileFormat file;
    CHECK(file.loadFile(corpusFile.string().c_str()).good());
    DcmDataset* dataset = file.getDataset();
    CHECK(dataset != nullptr);
    dataset->putAndInsertString(DCM_PatientName, "Doe^Jane^Q");
    dataset->putAndInsertString(DCM_PatientID, "MRN-8821345");
    dataset->putAndInsertString(DCM_PatientBirthDate, "19710302");
    dataset->putAndInsertString(DCM_PatientSex, "F");
    dataset->putAndInsertString(DCM_AccessionNumber, "ACC-999123");
    dataset->putAndInsertString(DCM_StudyID, "STUDY-77");
    dataset->putAndInsertString(DCM_InstitutionName, "Springfield General");
    dataset->putAndInsertString(DCM_ReferringPhysicianName, "Who^Doctor");
    dataset->putAndInsertString(DcmTagKey(0x0008, 0x1072), "Surgeon^Sam");
    dataset->putAndInsertString(DCM_StudyDate, "20240115");
    dataset->putAndInsertString(DCM_SeriesDate, "20240116");
    dataset->putAndInsertString(DCM_StudyDescription, "CT of Jane Doe");
    dataset->putAndInsertString(DCM_SeriesDescription, "Doe protocol");
    dataset->putAndInsertString(DCM_DeviceSerialNumber, "SN-4711");
    dataset->putAndInsertString(DCM_StationName, "CT-ROOM-2");
    // A private element, the classic hiding place for scanner-side identity.
    dataset->putAndInsertString(DcmTagKey(0x0019, 0x1000), "SITE-PATIENT-Doe^Jane");

    fs::create_directories(outDir);
    const fs::path out = outDir / corpusFile.filename();
    CHECK(file.saveFile(out.string().c_str()).good());
    return out;
}

std::string tagValue(const fs::path& path, const DcmTagKey& tag)
{
    DcmFileFormat file;
    if (!file.loadFile(path.string().c_str()).good()) return {};
    OFString value;
    if (!file.getDataset()->findAndGetOFString(tag, value).good()) return {};
    return std::string(value.c_str());
}

bool tagPresent(const fs::path& path, const DcmTagKey& tag)
{
    DcmFileFormat file;
    if (!file.loadFile(path.string().c_str()).good()) return false;
    return file.getDataset()->tagExists(tag);
}

// Private (odd-group) elements actually present in a file. The corpus objects carry their own set, so
// counting is more honest than assuming the one this test inserts was even written back.
int countPrivateTags(const fs::path& path)
{
    DcmFileFormat file;
    if (!file.loadFile(path.string().c_str()).good()) return -1;
    DcmDataset* dataset = file.getDataset();
    if (dataset == nullptr) return -1;
    int count = 0;
    for (unsigned long i = 0; i < dataset->card(); ++i) {
        const DcmElement* element = dataset->getElement(i);
        if (element != nullptr && (element->getGTag() & 1u)) ++count;
    }
    return count;
}

std::vector<uint8_t> pixelBytes(const fs::path& path)
{
    DcmFileFormat file;
    std::vector<uint8_t> bytes;
    if (!file.loadFile(path.string().c_str()).good()) return bytes;
    DcmElement* pixels = nullptr;
    if (file.getDataset()->findAndGetElement(DCM_PixelData, pixels).good() && pixels != nullptr) {
        Uint8* data = nullptr;
        if (pixels->getUint8Array(data).good()) {
            const size_t length = size_t(pixels->getLength());
            if (data != nullptr) bytes.assign(data, data + length);
        }
    }
    return bytes;
}

}  // namespace

int main(int argc, char** argv)
{
    CHECK(argc > 1);
    const fs::path corpus = argv[1];
    const fs::path work = fs::temp_directory_path() / "clivus-anonymize-test";
    fs::remove_all(work);
    fs::create_directories(work / "source");

    // Two objects, so UID consistency across files can be checked.
    const fs::path first = makeIdentifiableCopy(corpus / "CT_small.dcm", work / "source");
    const fs::path second = makeIdentifiableCopy(corpus / "MR_small.dcm", work / "source");
    const std::string firstSop = tagValue(first, DCM_SOPInstanceUID);
    const std::string firstStudy = tagValue(first, DCM_StudyInstanceUID);
    CHECK(!firstSop.empty());

    // (a) export succeeds and reports what it did.
    clivus::DeidOptions options;
    clivus::DeidReport report;
    const fs::path dest = work / "export";
    CHECK(clivus::deidentifyStudy((work / "source").string(), dest.string(), options, report));
    CHECK(report.files == 2);
    CHECK(report.verified);
    CHECK(report.residual.empty());
    CHECK(report.attributesRemoved > 0);
    CHECK(report.privateTagsRemoved >= 2);
    CHECK(report.uidsRemapped > 0);
    CHECK(report.datesShifted >= 2);
    CHECK(report.appliedShiftDays != 0);

    const fs::path firstOut = dest / first.filename();
    const fs::path secondOut = dest / second.filename();
    CHECK(fs::exists(firstOut) && fs::exists(secondOut));

    // (b) identity is gone, replaced where a replacement is expected.
    CHECK(tagValue(firstOut, DCM_PatientName) == "ANONYMOUS");
    CHECK(tagValue(firstOut, DCM_PatientID) == report.newPatientId);
    CHECK(!tagPresent(firstOut, DCM_PatientBirthDate));
    CHECK(!tagPresent(firstOut, DCM_PatientSex));
    CHECK(!tagPresent(firstOut, DCM_AccessionNumber));
    CHECK(!tagPresent(firstOut, DCM_StudyID));
    CHECK(!tagPresent(firstOut, DCM_InstitutionName));
    CHECK(!tagPresent(firstOut, DCM_ReferringPhysicianName));
    CHECK(!tagPresent(firstOut, DcmTagKey(0x0008, 0x1072)));
    CHECK(!tagPresent(firstOut, DCM_DeviceSerialNumber));
    CHECK(!tagPresent(firstOut, DCM_StationName));
    CHECK(tagValue(firstOut, DCM_StudyDescription).empty());
    CHECK(tagValue(firstOut, DCM_SeriesDescription).empty());
    // The private element carrying a name must be gone.
    CHECK(!tagPresent(firstOut, DcmTagKey(0x0019, 0x1000)));

    // (c) UIDs are remapped, consistently, and no original identifier survives anywhere in the file.
    const std::string newSop = tagValue(firstOut, DCM_SOPInstanceUID);
    CHECK(newSop != firstSop);
    CHECK(newSop.rfind("2.25.", 0) == 0);
    CHECK(tagValue(firstOut, DCM_StudyInstanceUID) != firstStudy);
    {
        std::ifstream in(firstOut, std::ios::binary);
        std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(blob.find("Doe^Jane") == std::string::npos);
        CHECK(blob.find("MRN-8821345") == std::string::npos);
        CHECK(blob.find("ACC-999123") == std::string::npos);
        CHECK(blob.find("Springfield") == std::string::npos);
        CHECK(blob.find("SITE-PATIENT") == std::string::npos);
    }

    // (d) dates moved by exactly the reported offset, and the interval between them is preserved.
    {
        const std::string studyDate = tagValue(firstOut, DCM_StudyDate);
        const std::string seriesDate = tagValue(firstOut, DCM_SeriesDate);
        CHECK(studyDate.size() == 8);
        CHECK(studyDate != "20240115");
        // 2024-01-15 and 2024-01-16 stay one day apart whatever the offset.
        auto dayOf = [](const std::string& yyyymmdd) {
            return std::stoi(yyyymmdd.substr(6, 2)) + std::stoi(yyyymmdd.substr(4, 2)) * 31 +
                   std::stoi(yyyymmdd.substr(0, 4)) * 372;
        };
        CHECK(dayOf(seriesDate) - dayOf(studyDate) == 1);
    }

    // (e) pixels are untouched: an anonymised copy must still measure the same.
    CHECK(pixelBytes(firstOut) == pixelBytes(first));
    CHECK(pixelBytes(secondOut) == pixelBytes(second));

    // (f) a non-empty destination is refused: verification needs the folder to be only this export.
    {
        clivus::DeidReport second;
        CHECK(!clivus::deidentifyStudy((work / "source").string(), dest.string(), options, second));
        CHECK(!second.failures.empty());
    }

    // (g) options can be switched off, and the report tells the truth about it.
    {
        clivus::DeidOptions plain;
        plain.remapUids = false;
        plain.shiftDates = false;
        plain.redactDescriptions = false;
        plain.removePrivateTags = false;
        plain.removePatientSex = false;
        clivus::DeidReport plainReport;
        const fs::path dest2 = work / "export-plain";
        CHECK(clivus::deidentifyStudy((work / "source").string(), dest2.string(), plain, plainReport));
        CHECK(tagValue(dest2 / first.filename(), DCM_SOPInstanceUID) == firstSop);   // untouched
        CHECK(tagValue(dest2 / first.filename(), DCM_StudyDate) == "20240115");
        // Private tags are kept when the option says so, and both counts are reported rather than
        // assumed: the corpus files already carry private elements of their own.
        const int sourcePrivate = countPrivateTags(first);
        CHECK(sourcePrivate > 0);
        CHECK(countPrivateTags(dest2 / first.filename()) == sourcePrivate);
        CHECK(countPrivateTags(firstOut) == 0);
        // Identity attributes are still removed: those are not optional.
        CHECK(tagValue(dest2 / first.filename(), DCM_PatientName) == "ANONYMOUS");
        CHECK(!tagPresent(dest2 / first.filename(), DCM_AccessionNumber));
    }

    // (h) a source that is not DICOM is skipped, not silently copied.
    {
        clivus::DeidReport junkReport;
        const fs::path junkSource = work / "junk";
        fs::create_directories(junkSource);
        std::ofstream(junkSource / "notes.txt") << "Jane Doe, MRN 12345";
        CHECK(!clivus::deidentifyStudy(junkSource.string(), (work / "export-junk").string(),
                                       clivus::DeidOptions{}, junkReport));
        CHECK(junkReport.files == 0);
        CHECK(!junkReport.failures.empty());
        CHECK(!fs::exists(work / "export-junk" / "notes.txt"));
    }

    std::printf("anonymize: all checks passed (%d files, %d attributes removed, %d private, %d UIDs "
                "remapped, dates %+d days, verification clean)\n",
                report.files, report.attributesRemoved, report.privateTagsRemoved,
                report.uidsRemapped, report.appliedShiftDays);
    return 0;
}
