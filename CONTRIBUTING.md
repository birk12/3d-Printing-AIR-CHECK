# Contributing

## Ground rules

1. **No invented numbers.** Every electrical value, timing and register
   address must be traceable to a manufacturer document, and the source goes
   in the comment. If you cannot find it, say so in the comment rather than
   guessing.
2. **The generated files are generated.** `docs/BOM.md`,
   `docs/BATTERY_LIFE.md`, `electronics/schematic/NETLIST.md`,
   `electronics/schematic/aircheck.net`, `electronics/bom/bom.csv`,
   `firmware/components/ac_core/include/ac_core/ac_font.h` and
   `ac_font_data.c` are all produced by scripts. Edit the script.
3. **`ac_core` must not learn about ESP-IDF.** No `esp_*` include ever goes
   into `firmware/components/ac_core`. That boundary is what makes half of
   this firmware testable.
4. **Changing a measurement profile means re-running the energy model.** The
   profiles in `ac_config.c` and in `tools/battery_calculator/model.py` are
   the same table and the host tests check they agree.

## Before you open a pull request

```bash
# measurement core: 455 checks
cc -std=c99 -Wall -Wextra -Werror -O1 -Ifirmware/components/ac_core/include \
   firmware/components/ac_core/src/*.c firmware/test/host/test_ac_core.c \
   -lm -o /tmp/ac_test && /tmp/ac_test

# electrical rule check: 371 checks
python3 electronics/schematic/design.py --emit

# enclosure
openscad -o /tmp/f.stl --export-format binstl \
         cad/openscad/aircheck_case.scad -D 'part="front"'
python3 tools/diagnostics/stl_check.py /tmp/f.stl

# energy model
python3 tools/battery_calculator/model.py

# firmware
cd firmware && idf.py build
```

All of these run in CI.

## Changing the enclosure

Dimensions live in `cad/openscad/aircheck_params.scad` and nowhere else. If
you find a bare number in `aircheck_case.scad` that is not derived from a
parameter, that is a bug.

Add an `assert()` for any new clearance you rely on. The existing ones have
caught real collisions.

Render and **look at** the result before you push - iso, from the bed side,
and a section through anything you changed. `tools/diagnostics/stl_check.py`
catches what a render hides: overhangs, unclosed meshes, a part that no longer
fits the bed.

## Changing the display

Every screen renders to a PNG without hardware:

```bash
cc -std=c99 -Ifirmware/components/ac_core/include \
   firmware/components/ac_core/src/*.c tools/diagnostics/screen_preview.c \
   -lm -o /tmp/preview && /tmp/preview /tmp/screens
```

Look at all ten. Text that collides or runs off the panel is obvious in the
image and invisible in the code - three separate layout collisions were found
this way and none of them by reading.

## Reporting a hardware result

This is the most valuable contribution available right now, because **nothing
in this repository has been verified on an assembled device**. If you build
one, please open an issue with:

* which build variant, and any substitutions
* measured idle current (a PPK II trace is ideal)
* measured battery life, and in which mode
* whether commissioning worked, on which iOS version and which border router
* which of the bench tests in `docs/TESTING.md` passed

Negative results are more useful than positive ones.

## Style

* C99 for `ac_core`, C for `ac_hal`, C++17 where esp-matter requires it
* four spaces, no tabs, 90 columns
* comments explain *why*, not *what*; the datasheet reference is the *why*
* no `TODO` without an issue number
