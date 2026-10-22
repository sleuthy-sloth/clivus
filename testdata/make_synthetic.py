#!/usr/bin/env python3
"""Write a synthetic 8-slice signed 12-bit CT series (explicit VR little endian).

No DICOM library needed: enough of the standard to exercise series scanning, the
modality transform (slope/intercept), PixelPaddingValue and 16-bit decoding.
Usage: python3 make_synthetic.py <outdir> [size] [slices] [--zfill] [--reverse]

--zfill    fill slice k with the constant k (slope 1, no padding). Gives the volume tests a value
           that identifies which slice ended up at which depth.
--reverse  write the files in descending z, so anything that trusts filename order gets it wrong.
"""
import os
import struct
import sys

TS_EXPLICIT_LE = "1.2.840.10008.1.2.1"
CT_IMAGE_STORAGE = "1.2.840.10008.5.1.4.1.1.2"


def pad(value: str, even=True):
    b = value.encode("ascii")
    if even and len(b) % 2:
        b += b"\x00" if False else b" "   # UI pads with NUL, text pads with space (fixed by caller)
    return b


def elem(tag, vr, data: bytes):
    group, element = tag
    out = struct.pack("<HH", group, element) + vr.encode("ascii")
    if vr in ("OB", "OW", "SQ", "UN", "UT", "OF"):
        out += b"\x00\x00" + struct.pack("<I", len(data))
    else:
        out += struct.pack("<H", len(data))
    return out + data


def txt(tag, vr, value):
    b = value.encode("ascii")
    if len(b) % 2:
        b += b"\x00" if vr == "UI" else b" "
    return elem(tag, vr, b)


def us(tag, v):
    return elem(tag, "US", struct.pack("<H", v))


def ss(tag, v):
    return elem(tag, "SS", struct.pack("<h", v))


def ds(tag, v):
    return txt(tag, "DS", v)


def is_(tag, v):
    return txt(tag, "IS", str(v))


def uid(root: str, n: int):
    return f"{root}.{n}"


def make_slice(path, series_uid, study_uid, sop_uid, instance, z, size=512, fill_value=None):
    px = bytearray()
    for y in range(size):
        for x in range(size):
            if fill_value is not None:
                px += struct.pack("<h", fill_value)
                continue
            dx, dy = x - size // 2, y - size // 2
            r = (dx * dx + dy * dy) ** 0.5
            v = 900.0 - abs(r - 60 - instance * 8) * 4.0          # ring shrinking with z
            v += 200 + instance * 20                              # soft-tissue-ish background
            stored = max(-2000, min(2047, int(v)))
            px += struct.pack("<h", stored)
    if fill_value is None:
        for i in range(0, len(px), 194):                         # every 97th pixel: padding value
                                                                 # (even stride: never split a pixel)
            px[i:i + 2] = struct.pack("<h", -2000)

    items = [
        txt((0x0008, 0x0016), "UI", CT_IMAGE_STORAGE),
        txt((0x0008, 0x0018), "UI", sop_uid),
        txt((0x0008, 0x0060), "CS", "CT"),
        txt((0x0008, 0x1030), "LO", "Synthetic phantom"),
        txt((0x0008, 0x103E), "LO", "Synthetic CT series"),
        txt((0x0010, 0x0010), "PN", "SPIKE^SYNTHETIC"),
        txt((0x0010, 0x0020), "LO", "SYN-001"),
        txt((0x0020, 0x000D), "UI", study_uid),
        txt((0x0020, 0x000E), "UI", series_uid),
        is_((0x0020, 0x0011), 1),
        is_((0x0020, 0x0013), instance),
        txt((0x0020, 0x0032), "DS", f"0\\0\\{z}"),
        txt((0x0020, 0x0037), "DS", "1\\0\\0\\0\\1\\0"),      # axial: normal is +z
        ds((0x0018, 0x0050), "2.5"),
        ds((0x0018, 0x0088), "2.5"),
        us((0x0028, 0x0002), 1),
        txt((0x0028, 0x0004), "CS", "MONOCHROME2"),
        us((0x0028, 0x0010), size),
        us((0x0028, 0x0011), size),
        ds((0x0028, 0x0030), "1\\1"),
        us((0x0028, 0x0100), 16),
        us((0x0028, 0x0101), 12),
        us((0x0028, 0x0102), 11),
        us((0x0028, 0x0103), 1),                                  # signed
        ds((0x0028, 0x1050), "40"),
        ds((0x0028, 0x1051), "400"),
        ds((0x0028, 0x1052), "0" if fill_value is not None else "-1024"),
        ds((0x0028, 0x1053), "1"),
    ]
    if fill_value is None:
        items.append(ss((0x0028, 0x0120), -2000))
    items.append(elem((0x7FE0, 0x0010), "OW", bytes(px)))
    ds_body = b"".join(sorted(items, key=lambda b: struct.unpack("<HH", b[0:4])))   # ascending tag order

    meta_group_len = 0
    meta_parts = [
        txt((0x0002, 0x0002), "UI", CT_IMAGE_STORAGE),
        txt((0x0002, 0x0003), "UI", sop_uid),
        txt((0x0002, 0x0010), "UI", TS_EXPLICIT_LE),
        txt((0x0002, 0x0012), "UI", "1.2.826.0.1.3680043.8.1055.1"),
    ]
    meta = elem((0x0002, 0x0001), "OB", b"\x00\x01") + b"".join(meta_parts)
    preamble = b"\x00" * 128 + b"DICM"
    with open(path, "wb") as f:
        f.write(preamble + elem((0x0002, 0x0000), "UL", struct.pack("<I", len(meta))) + meta + ds_body)


def main():
    flags = {a for a in sys.argv[1:] if a.startswith("--")}
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    outdir = os.path.abspath(args[0] if len(args) > 0 else ".")
    size = int(args[1]) if len(args) > 1 else 512
    slices = int(args[2]) if len(args) > 2 else 8
    zfill = "--zfill" in flags
    reverse = "--reverse" in flags
    os.makedirs(outdir, exist_ok=True)
    root = "1.2.826.0.1.3680043.8.1055.1"
    series_uid = uid(root, 100)
    study_uid = uid(root, 101)
    order = list(range(slices))
    if reverse:
        order.reverse()
    for pos, i in enumerate(order):
        # filename advances with write order, z with slice index: with --reverse the two disagree.
        # --reverse deliberately makes InstanceNumber disagree with position, so a test can tell which
        # of the two the loader believed.
        make_slice(os.path.join(outdir, f"syn-{pos:02d}.dcm"), series_uid, study_uid,
                   uid(root, 200 + i), pos + 1 if reverse else i + 1, z=i * 2.5, size=size, fill_value=i if zfill else None)
    print(f"wrote {slices} slices ({size}x{size}) to {outdir}")


if __name__ == "__main__":
    main()
