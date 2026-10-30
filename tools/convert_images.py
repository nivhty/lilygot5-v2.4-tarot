#!/usr/bin/env python3
"""
Tarot Card Image Converter for LilyGo E-Ink boards
(T-Echo 1.54" 200x200, T5 V2.4 2.13" 250x122)
==============================================================================

Converts tarot card images (PNG, JPG, BMP, WEBP) to C header files containing
1-bit monochrome bitmap arrays and a pointer lookup table for the 200x200
monochrome e-paper display.

Bitmap Format:
--------------
- Dimensions: 128x160 pixels (default, centered on canvas maintaining aspect ratio)
              or 200x200 pixels (when using --full flag)
- 1-bit monochrome with Floyd-Steinberg error diffusion dithering
- Packed MSB first (bit 7 = leftmost pixel, bit 0 = rightmost pixel in byte)
- Byte order: row by row, left-to-right, top-to-bottom
- Polarity: 1 = black pixel (drawn ink), 0 = white pixel (screen background)
- Arrays stored in Flash with `const uint8_t PROGMEM`

Deck Mapping (78 Cards):
------------------------
- Indices  0-21: Major Arcana (The Fool through The World)
- Indices 22-35: Minor Arcana - Wands (Ace through King)
- Indices 36-49: Minor Arcana - Cups (Ace through King)
- Indices 50-63: Minor Arcana - Swords (Ace through King)
- Indices 64-77: Minor Arcana - Pentacles/Coins (Ace through King)

Card Identification:
--------------------
Filenames in the input directory are automatically matched to card indices
using any of the following conventions:
1. Standard names: fool.png, the_magician.jpg, ace_of_wands.png, 2_of_cups.png
2. Prefixed names: 00_fool.png, 01_magician.png, wands_01.png, c01.png
3. Sequential indices: 0.png through 77.png, or 00.png through 77.png
4. Aliases: coins/disks for pentacles, bateleur, papess, fortitude, etc.

Usage:
------
    python convert_images.py <input_dir> <output_header.h> [options]

Options:
    --full       Resize/pad to 200x200 pixels (full screen) instead of 128x160
    --landscape  Rotate each card 90° clockwise → 160x128 px (horizontal layout)
    --size WxH   Explicit output size, e.g. 72x122 for the T5 2.13" (overrides --full/--landscape)
    --invert     Invert black/white bits (default: 1=black, 0=white)
    --no-dither  Use thresholding instead of Floyd-Steinberg dithering

Example:
    python tools/convert_images.py artwork/ include/tarot_bitmaps.h
    python tools/convert_images.py artwork/ include/tarot_bitmaps.h --landscape
    python tools/convert_images.py artwork/ include/tarot_bitmaps.h --full
    python tools/convert_images.py ../t-echo-tarot/cards include/tarot_bitmaps.h --size 72x122
"""

import sys
import os
import re
import argparse
from pathlib import Path

try:
    from PIL import Image, ImageOps
except ImportError:
    sys.exit("Error: Pillow library not found. Install it with: pip install Pillow")

# -----------------------------------------------------------------------------
# Tarot Deck Definitions (78 Cards)
# -----------------------------------------------------------------------------

MAJOR_ARCANA = [
    (0, "The Fool", ["fool", "the_fool", "thefool", "le_mat", "mat"]),
    (1, "The Magician", ["magician", "the_magician", "themagician", "juggler", "le_bateleur", "bateleur"]),
    (2, "The High Priestess", ["high_priestess", "the_high_priestess", "highpriestess", "priestess", "papess", "la_papesse"]),
    (3, "The Empress", ["empress", "the_empress", "theempress"]),
    (4, "The Emperor", ["emperor", "the_emperor", "theemperor"]),
    (5, "The Hierophant", ["hierophant", "the_hierophant", "thehierophant", "pope", "the_pope"]),
    (6, "The Lovers", ["lovers", "the_lovers", "thelovers", "lover"]),
    (7, "The Chariot", ["chariot", "the_chariot", "thechariot"]),
    (8, "Strength", ["strength", "fortitude", "force"]),
    (9, "The Hermit", ["hermit", "the_hermit", "thehermit"]),
    (10, "Wheel of Fortune", ["wheel_of_fortune", "the_wheel_of_fortune", "wheel", "the_wheel", "fortune"]),
    (11, "Justice", ["justice"]),
    (12, "The Hanged Man", ["hanged_man", "the_hanged_man", "hangedman", "the_hangedman"]),
    (13, "Death", ["death"]),
    (14, "Temperance", ["temperance"]),
    (15, "The Devil", ["devil", "the_devil", "thedevil"]),
    (16, "The Tower", ["tower", "the_tower", "thetower"]),
    (17, "The Star", ["star", "the_star", "thestar"]),
    (18, "The Moon", ["moon", "the_moon", "themoon"]),
    (19, "The Sun", ["sun", "the_sun", "thesun"]),
    (20, "Judgement", ["judgement", "judgment", "the_judgement", "the_judgment"]),
    (21, "The World", ["world", "the_world", "theworld"]),
]

SUITS = {
    "wands": (22, ["wands", "wand", "batons", "rods", "staves", "w"]),
    "cups": (36, ["cups", "cup", "chalices", "c"]),
    "swords": (50, ["swords", "sword", "blades", "s"]),
    "pentacles": (64, ["pentacles", "pentacle", "coins", "coin", "disks", "discs", "p"]),
}

RANKS = [
    ("ace", 0, ["ace", "1", "01", "a", "one"]),
    ("two", 1, ["two", "2", "02"]),
    ("three", 2, ["three", "3", "03"]),
    ("four", 3, ["four", "4", "04"]),
    ("five", 4, ["five", "5", "05"]),
    ("six", 5, ["six", "6", "06"]),
    ("seven", 6, ["seven", "7", "07"]),
    ("eight", 7, ["eight", "8", "08"]),
    ("nine", 8, ["nine", "9", "09"]),
    ("ten", 9, ["ten", "10"]),
    ("page", 10, ["page", "11", "knave", "princess", "p"]),
    ("knight", 11, ["knight", "12", "prince", "kn"]),
    ("queen", 12, ["queen", "13", "q"]),
    ("king", 13, ["king", "14", "k"]),
]

# Canonical card names for all 78 cards
CARD_NAMES = {}
for idx, name, _ in MAJOR_ARCANA:
    CARD_NAMES[idx] = name

for suit_name, (base_idx, _) in SUITS.items():
    suit_title = suit_name.capitalize()
    for rank_name, r_offset, _ in RANKS:
        CARD_NAMES[base_idx + r_offset] = f"{rank_name.capitalize()} of {suit_title}"


def identify_card_index(filename: str) -> int | None:
    """
    Identifies the 0-77 tarot card index from a filename.
    Returns None if the filename cannot be identified.
    """
    stem = Path(filename).stem.lower()
    cleaned = re.sub(r'[^a-z0-9]', '_', stem)
    cleaned = re.sub(r'_+', '_', cleaned).strip('_')

    # 1. Match Minor Arcana by suit and rank combinations
    for suit_name, (base_idx, suit_aliases) in SUITS.items():
        for sa in suit_aliases:
            for rank_name, r_offset, rank_aliases in RANKS:
                for ra in rank_aliases:
                    patterns = [
                        f"{ra}_of_{sa}",
                        f"{ra}_{sa}",
                        f"{sa}_{ra}",
                        f"{sa}{ra}",
                    ]
                    if cleaned in patterns:
                        return base_idx + r_offset

    # 2. Match Major Arcana by name / aliases
    # Strip optional leading prefixes like card_, tarot_, m_, maj_
    no_prefix = re.sub(r'^(card|tarot|major|maj|m)_', '', cleaned)
    for idx, name, aliases in MAJOR_ARCANA:
        if no_prefix in aliases or cleaned in aliases:
            return idx
        # Also check with numeric prefix stripped: e.g. "00_fool" -> "fool"
        sub = re.sub(r'^[0-9]+_', '', no_prefix)
        if sub in aliases:
            return idx

    # 3. Match explicit deck index (0-77)
    # Handles: "0.png", "00.png", "card_05.png", "77_king.png", etc.
    # Also handles long numeric prefixes like "0912190455860_00_0_fool" -> index 00
    m = re.match(r'^(?:card_|tarot_)?(\d{1,2})(?:_.*)?$', cleaned)
    if m:
        val = int(m.group(1))
        if 0 <= val <= 77:
            return val

    # Strip a long leading numeric prefix (3+ digits) then retry
    stripped = re.sub(r'^\d{3,}_', '', cleaned)
    m2 = re.match(r'^(\d{1,2})(?:_.*)?$', stripped)
    if m2:
        val = int(m2.group(1))
        if 0 <= val <= 77:
            return val

    # 4. Check for Major Arcana codes like m00..m21
    m_code = re.match(r'^m(\d{1,2})$', cleaned)
    if m_code:
        val = int(m_code.group(1))
        if 0 <= val <= 21:
            return val

    return None


def sanitize_array_name(filename: str) -> str:
    """
    Derives a valid C identifier from the filename stem:
    e.g. 'fool.png' -> 'tarot_bmp_fool'
    """
    stem = Path(filename).stem
    safe_stem = re.sub(r'[^a-zA-Z0-9_]', '_', stem).lower()
    safe_stem = re.sub(r'_+', '_', safe_stem).strip('_')
    return f"tarot_bmp_{safe_stem}"


def process_image(image_path: Path, target_w: int, target_h: int,
                  invert: bool = False, dither: bool = True,
                  landscape: bool = False) -> bytes:
    """
    Loads, resizes, dithers, and packs an image into monochrome bytes.
    - Fits within target_w x target_h maintaining aspect ratio
    - Centers on a white background canvas of exactly target_w x target_h
    - Dithers using Floyd-Steinberg
    - Packs 8 pixels per byte, MSB first, row by row
    """
    with Image.open(image_path) as img:
        # Handle alpha channels (composite over white background)
        if img.mode in ('RGBA', 'LA') or (img.mode == 'P' and 'transparency' in img.info):
            img = img.convert('RGBA')
            bg = Image.new('RGBA', img.size, (255, 255, 255, 255))
            img = Image.alpha_composite(bg, img).convert('RGB')
        else:
            img = img.convert('RGB')

        # 1. Optionally rotate source image 90° CW for landscape layout
        if landscape:
            img = img.rotate(-90, expand=True)

        # 2. Auto-crop scanner white borders around the card frame
        gray_orig = img.convert('L')
        mask = gray_orig.point(lambda p: 255 if p < 180 else 0)
        bbox = mask.getbbox()
        if bbox:
            bx0 = max(0, bbox[0] - 1)
            by0 = max(0, bbox[1] - 1)
            bx1 = min(img.width, bbox[2] + 1)
            by1 = min(img.height, bbox[3] + 1)
            img = img.crop((bx0, by0, bx1, by1))

        # 3. Resize edge-to-edge to target dimensions (no letterboxing or indentation)
        canvas = img.resize((target_w, target_h), Image.Resampling.LANCZOS)

        # 2. Convert to 1-bit monochrome (Floyd-Steinberg dithering or threshold)
        gray = canvas.convert('L')
        if dither:
            dither_mode = getattr(Image.Dither, 'FLOYDSTEINBERG', getattr(Image, 'FLOYDSTEINBERG', 3))
            mono = gray.convert('1', dither=dither_mode)
        else:
            mono = gray.convert('1', dither=Image.Dither.NONE)

        # 3. Pack into bytes (MSB first, 8 pixels per byte, left-to-right, top-to-bottom)
        # Note: in PIL mode '1', 0 is black, 255 is white.
        # In Adafruit_GFX/GxEPD drawBitmap, 1 bit = drawn ink (black), 0 bit = background (white).
        bytes_data = bytearray()
        for y in range(target_h):
            byte_val = 0
            bit_pos = 7  # MSB first
            for x in range(target_w):
                pixel = mono.getpixel((x, y))
                is_black = (pixel == 0)
                if invert:
                    is_black = not is_black

                if is_black:
                    byte_val |= (1 << bit_pos)

                bit_pos -= 1
                if bit_pos < 0:
                    bytes_data.append(byte_val)
                    byte_val = 0
                    bit_pos = 7

            if bit_pos != 7:
                bytes_data.append(byte_val)

        return bytes(bytes_data)


def format_c_array(array_name: str, byte_data: bytes, comment: str) -> str:
    """Formats a byte array as a const uint8_t PROGMEM C array."""
    lines = [f"// {comment}", f"const uint8_t {array_name}[] PROGMEM = {{"]
    bytes_per_line = 16
    for i in range(0, len(byte_data), bytes_per_line):
        chunk = byte_data[i:i + bytes_per_line]
        hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
        if i + bytes_per_line < len(byte_data):
            lines.append(f"    {hex_str},")
        else:
            lines.append(f"    {hex_str}")
    lines.append("};\n")
    return "\n".join(lines)


def convert_images(input_dir: Path, output_file: Path, full: bool = False,
                   landscape: bool = False, invert: bool = False, dither: bool = True,
                   size: tuple[int, int] | None = None):
    """
    Main conversion routine: scans input_dir for images, converts each to
    monochrome bitmap array, and generates the complete C header file.
    """
    if size:
        target_w, target_h = size
    elif full:
        target_w, target_h = 200, 200
    elif landscape:
        target_w, target_h = 160, 128  # rotated: wide × short
    else:
        target_w, target_h = 128, 160
    expected_bytes = ((target_w + 7) // 8) * target_h

    # Find image files
    valid_exts = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tiff", ".gif"}
    image_paths = sorted([
        p for p in input_dir.iterdir()
        if p.is_file() and p.suffix.lower() in valid_exts
    ])

    if not image_paths:
        print(f"Warning: No valid image files found in {input_dir}")

    converted_bitmaps = []
    # 78 cards mapped to array names (or None)
    card_mapping = [None] * 78
    total_byte_size = 0
    used_array_names = set()

    print(f"Scanning directory: {input_dir}")
    print(f"Target size: {target_w}x{target_h} ({expected_bytes} bytes per card)")
    print(f"Dithering: {'Floyd-Steinberg' if dither else 'None (threshold)'}")
    print(f"Polarity: {'1=White, 0=Black (inverted)' if invert else '1=Black, 0=White (standard)'}\n")

    for p in image_paths:
        try:
            raw_bytes = process_image(p, target_w, target_h, invert=invert, dither=dither, landscape=landscape)
            array_name = sanitize_array_name(p.name)

            # Ensure unique array names
            base_name = array_name
            counter = 1
            while array_name in used_array_names:
                array_name = f"{base_name}_{counter}"
                counter += 1
            used_array_names.add(array_name)

            card_idx = identify_card_index(p.name)
            card_desc = f"{p.name} [{target_w}x{target_h}, {len(raw_bytes)} bytes]"
            if card_idx is not None:
                card_title = CARD_NAMES.get(card_idx, f"Card #{card_idx}")
                card_desc = f"Card [{card_idx:02d}]: {card_title} ({card_desc})"
                if card_mapping[card_idx] is not None:
                    print(f"  [!] Note: Overwriting mapping for index {card_idx} ({card_title}) with {p.name}")
                card_mapping[card_idx] = array_name
                print(f"  [+] {p.name} -> Index {card_idx:02d} ({card_title}) -> {array_name}")
            else:
                print(f"  [?] {p.name} -> Unmapped card index -> {array_name}")

            converted_bitmaps.append((array_name, raw_bytes, card_desc))
            total_byte_size += len(raw_bytes)

        except Exception as e:
            print(f"  [ERROR] Failed to process {p.name}: {e}", file=sys.stderr)

    # Generate Header Content
    header_lines = [
        "// =============================================================================",
        f"// Tarot Card Bitmaps ({target_w}x{target_h}, 1-bit monochrome E-Paper)",
        "// Auto-generated by tools/convert_images.py - DO NOT EDIT MANUALLY",
        "// =============================================================================",
        "",
        "#ifndef TAROT_BITMAPS_H",
        "#define TAROT_BITMAPS_H",
        "",
        "#include <Arduino.h>",
        "",
        f"#define TAROT_BMP_WIDTH    {target_w}",
        f"#define TAROT_BMP_HEIGHT   {target_h}",
        f"#define TAROT_BMP_BYTES    {expected_bytes}",
        "#define TAROT_TOTAL_CARDS  78",
        "",
        "// -----------------------------------------------------------------------------",
        "// Individual Card Bitmaps (1-bit monochrome, MSB-first, PROGMEM)",
        "// -----------------------------------------------------------------------------",
        "",
    ]

    for array_name, raw_bytes, desc in converted_bitmaps:
        header_lines.append(format_c_array(array_name, raw_bytes, desc))

    header_lines.extend([
        "// -----------------------------------------------------------------------------",
        "// Card Deck Pointer Mapping Array",
        "// Maps card index (0-77) to its bitmap array in PROGMEM (nullptr if missing).",
        "// -----------------------------------------------------------------------------",
        "const uint8_t* const tarotBitmaps[78] = {",
    ])

    mapped_count = 0
    for idx in range(78):
        c_name = CARD_NAMES.get(idx, f"Card #{idx}")
        bmp_ptr = card_mapping[idx]
        if bmp_ptr:
            mapped_count += 1
            header_lines.append(f"    {bmp_ptr},  // [{idx:02d}] {c_name}")
        else:
            header_lines.append(f"    nullptr,  // [{idx:02d}] {c_name} (missing)")

    header_lines.append("};")
    header_lines.append("")
    header_lines.append("#endif // TAROT_BITMAPS_H")
    header_lines.append("")

    # Ensure output parent directory exists
    output_file.parent.mkdir(parents=True, exist_ok=True)
    with open(output_file, "w", encoding="utf-8") as f:
        f.write("\n".join(header_lines))

    # Print Summary
    print("\n" + "=" * 60)
    print("TAROT BITMAP CONVERSION SUMMARY")
    print("=" * 60)
    print(f"  Target Resolution  : {target_w}x{target_h} px")
    print(f"  Total Images Found : {len(image_paths)}")
    print(f"  Converted Bitmaps  : {len(converted_bitmaps)}")
    print(f"  Deck Cards Mapped  : {mapped_count} / 78 cards")
    print(f"  Missing Cards      : {78 - mapped_count}")
    print(f"  Total Bitmap Size  : {total_byte_size:,} bytes ({total_byte_size / 1024:.1f} KB)")
    print(f"  Output Header File : {output_file.resolve()}")
    print("=" * 60 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Convert tarot card images to C header file for LilyGo e-ink displays."
    )
    parser.add_argument("input_dir", type=Path, help="Directory containing tarot card images")
    parser.add_argument("output_header", type=Path, help="Output C header file (e.g., tarot_bitmaps.h)")
    parser.add_argument(
        "--full",
        action="store_true",
        help="Resize/pad to 200x200 (full screen) instead of default 128x160",
    )
    parser.add_argument(
        "--landscape",
        action="store_true",
        help="Rotate each card 90° CW and output at 160x128 px for horizontal display",
    )
    parser.add_argument(
        "--size",
        type=str,
        default=None,
        help="Explicit output size WxH (e.g. 72x122). Overrides --full/--landscape",
    )
    parser.add_argument(
        "--invert",
        action="store_true",
        help="Invert black and white pixels (default: 1=black ink, 0=white background)",
    )
    parser.add_argument(
        "--no-dither",
        action="store_true",
        help="Disable Floyd-Steinberg dithering and use simple thresholding",
    )

    args = parser.parse_args()

    if not args.input_dir.exists():
        sys.exit(f"Error: Input directory '{args.input_dir}' does not exist.")
    if not args.input_dir.is_dir():
        sys.exit(f"Error: '{args.input_dir}' is not a directory.")

    size = None
    if args.size:
        try:
            w_str, h_str = args.size.lower().split("x")
            size = (int(w_str), int(h_str))
        except ValueError:
            sys.exit(f"Error: invalid --size '{args.size}', expected WxH (e.g. 72x122)")

    convert_images(
        input_dir=args.input_dir,
        output_file=args.output_header,
        full=args.full,
        landscape=args.landscape,
        invert=args.invert,
        dither=not args.no_dither,
        size=size,
    )


if __name__ == "__main__":
    main()
