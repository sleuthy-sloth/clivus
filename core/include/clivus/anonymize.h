// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace clivus {

// De-identification for export: write a copy of a study with patient identity removed.
//
// What this is: a deliberate, inspectable implementation of the identity-stripping part of DICOM
// PS3.15 Annex E (basic profile). It removes or replaces the attributes that carry identity, removes
// every private tag (private elements are where a lot of accidental PHI lives — vendor annotations,
// scanner-side names, protocol strings), optionally remaps UIDs and shifts dates so a study cannot be
// joined back to its source, and then RE-READS every file it wrote to check that the original values
// are gone. A copy that fails that check is reported as a failure, not as an export.
//
// What this is NOT, and the UI must say so:
//   - It is not a guarantee of anonymity. Burned-in pixel annotations, secondary captures, structure
//     reports, and the image content itself can identify a person. Clivus performs no OCR and cannot
//     detect burned-in text. Free-text fields that are kept (descriptions, if you choose to keep
//     them) can contain names.
//   - It is not a regulatory certification. Nothing here has been assessed against HIPAA/GDPR or any
//     national de-identification standard, and "de-identified" in the legal sense is a claim only a
//     qualified person can make for a given dataset.
//   - It does not touch pixel data. Values, spacing and geometry are preserved byte for byte so the
//     copy remains measurable; only metadata changes.
struct DeidOptions {
    bool remapUids = true;             // Study/Series/SOP Instance UIDs + references, consistently
    bool shiftDates = true;            // Study/Series/Acquisition/Content dates by one random offset
    int dateShiftDays = 0;             // 0 = pick a random offset within +/- 365 days
    bool redactDescriptions = true;    // clear Study/Series Description (free text often holds names)
    bool removePrivateTags = true;     // remove every private (odd-group) element
    bool removePatientSex = true;      // quasi-identifier; keeping it is a choice, not a default
};

struct DeidReport {
    int files = 0;
    int skipped = 0;                   // not DICOM, or unreadable
    int attributesRemoved = 0;
    int attributesReplaced = 0;
    int privateTagsRemoved = 0;
    int uidsRemapped = 0;
    int datesShifted = 0;
    int appliedShiftDays = 0;
    std::string newPatientId;
    // Post-write verification: the original identifying values were looked for in every output file.
    bool verified = false;
    std::vector<std::string> residual;   // where an original value survived (should be empty)
    std::vector<std::string> warnings;   // e.g. a file that could not be re-read
    std::vector<std::string> failures;   // files that could not be de-identified or written
};

// Copies sourceFolder (recursively) into destFolder, de-identifying every DICOM object it finds.
// destFolder must not exist or be empty: an export that mixes with existing files cannot be verified.
// Non-DICOM files are skipped and counted — they are not copied, because an unknown file cannot be
// shown to be free of identifiers.
bool deidentifyStudy(const std::string& sourceFolder, const std::string& destFolder,
                     const DeidOptions& options, DeidReport& report);

// Exposed for tests and for the report UI: the identity-bearing attributes this build handles.
const std::vector<std::string>& deidentifiedAttributeNames();

}  // namespace clivus
