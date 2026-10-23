# Privacy and the local index cache

Clivus reads DICOM files from a folder you choose and sends nothing over the network. It has no
accounts, no telemetry, no crash reporting and no update checker. This page describes the one thing
it writes to disk besides your settings: the series index cache.

## What the cache is for

Opening a folder means reading one header per file. On a 500-slice CT that is a second or two —
tolerable once, wasteful on every start. The cache remembers the scan, keyed to a fingerprint of the
folder, so a second open of the same files reads the cache instead.

## Where it lives

Never inside the DICOM folder. An older build put a `.clivus-index.sqlite` beside the images, which
meant copying a study to a colleague copied its metadata too; this build no longer writes that file
and deletes it on sight when you open such a folder.

| Platform | Location |
| --- | --- |
| Windows | `%LOCALAPPDATA%\Clivus\index\` |
| macOS | `~/Library/Caches/Clivus/index/` |
| Linux/BSD | `$XDG_CACHE_HOME/clivus/index/` (else `~/.cache/clivus/index/`) |

Each folder gets one file named from an FNV-1a hash of its canonical path, e.g.
`1aeed5d9415097ea.sqlite`. The filename does not spell out where the study lives. **That is not
anonymity** — folder paths come from a small, guessable space, so a hash can be correlated by anyone
who can already enumerate candidate paths. It keeps the path out of a casual directory listing, and
nothing more.

## What it stores

- Series UID, study UID
- Modality, study description, series description
- Image rows/columns
- The file paths the scan found, and each file's frame count
- Per-slice geometry (image position and orientation) for the MPR eligibility check
- A folder fingerprint: file count, newest modification time, total bytes

## What it does not store

- Patient names
- Patient identifiers or dates of birth
- Pixel data
- Anything from outside the folder you opened

The browser shows patient details only from the current session's in-memory scan of the files, never
from the cache. After a restart those details come back from the files themselves.

## When it is refreshed

Every open compares the folder fingerprint with the cached one. Count, newest timestamp or total
bytes differing makes the entry a miss, and the folder is rescanned. A cache written by a different
schema version is dropped at open rather than migrated, so fields a new schema no longer allows — a
patient name, for example — do not linger on disk.

**Known limitation:** a file replaced by one with the same size *and* the same modification time is
not detected. Detecting that would mean reading every byte of every file on every open, which is the
work the cache exists to avoid. If you replace files in place while preserving timestamps, use
*View ▸ Clear index cache* first.

## Clearing it

- *View ▸ Clear index cache* forgets the currently open folder.
- Deleting the cache directory above removes every folder's entry.

Clearing costs one rescan and nothing else. The viewer works with no cache at all: if the cache
directory is unwritable, full of rubbish, or on read-only media, Clivus rescans and moves on.

Clearing the cache does not reach into filesystem backups, snapshots, or any other application's
files. It removes Clivus's own copy only.

## Failure behaviour

A cache that cannot be opened is deleted and rebuilt once; if it still cannot be used, the folder is
scanned uncached. A failed write is rolled back, so the cache is never "half updated". None of this
touches your DICOM files, and none of it blocks viewing.
