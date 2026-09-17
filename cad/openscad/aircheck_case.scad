// =====================================================================
// 3D Printing AIR CHECK - enclosure
// ---------------------------------------------------------------------
//   openscad -D 'part="front"' -o front_shell.stl aircheck_case.scad
//   openscad -D 'part="back"'  -o back_shell.stl  aircheck_case.scad
//   openscad -D 'part="button"' / "stand" / "batclip" / "assembly"
//   openscad -D 'part="sec_x"' / "sec_y" / "sec_z"   (section views)
//
// Both shells are modelled in the device frame (X right, Y up, Z from the
// front face inward) and rotated into the print orientation by the export
// dispatcher at the bottom of this file.  Print direction is Z, so every
// silhouette corner radius stands perpendicular to the bed and no radius
// ever becomes an overhang.
// =====================================================================

include <aircheck_params.scad>

part = "assembly";

// ---------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------

module rrect(w, h, r) {
    hull() for (x = [r, w - r], y = [r, h - r]) translate([x, y]) circle(r = r);
}

// Rounded slab extruded along Z with a chamfer on the z=z0 face (the face
// that sits on the print bed) and on the z=z1 face.
module slab(w, h, d, r, cham_bed = CHAMFER_BED, cham_top = 0) {
    hull() {
        translate([0, 0, cham_bed]) linear_extrude(max(d - cham_bed - cham_top, 0.01))
            rrect(w, h, r);
        if (cham_bed > 0)
            linear_extrude(0.01) translate([cham_bed, cham_bed])
                rrect(w - 2 * cham_bed, h - 2 * cham_bed, max(r - cham_bed, 0.1));
        if (cham_top > 0)
            translate([0, 0, d - 0.01]) linear_extrude(0.01)
                translate([cham_top, cham_top])
                    rrect(w - 2 * cham_top, h - 2 * cham_top, max(r - cham_top, 0.1));
    }
}

module box_at(x, y, z, w, h, d) { translate([x, y, z]) cube([w, h, d]); }

// A slot whose roof is a printable bridge: width <= VENT_SLOT_W.
module vent_slots(x, y, z, total_w, n, depth, slot_h = VENT_SLOT_H,
                  gap = VENT_SLOT_GAP) {
    pitch = slot_h + gap;
    for (i = [0 : n - 1])
        box_at(x, y + i * pitch, z, total_w, slot_h, depth);
}

module vent_field(x, y, z, w, h, depth) {
    // fills w x h with slots of at most VENT_SLOT_W width
    ncol = ceil(w / (VENT_SLOT_W + 1.6));
    colw = (w - (ncol - 1) * 1.6) / ncol;
    nrow = floor((h + VENT_SLOT_GAP) / (VENT_SLOT_H + VENT_SLOT_GAP));
    for (c = [0 : ncol - 1], r = [0 : nrow - 1])
        box_at(x + c * (colw + 1.6), y + r * (VENT_SLOT_H + VENT_SLOT_GAP), z,
               colw, VENT_SLOT_H, depth);
}

module post(x, y, z0, z1, od = POST_D) {
    translate([x, y, z0]) cylinder(d = od, h = z1 - z0);
}

// ---------------------------------------------------------------------
// component mock-ups (for the assembly view and the collision check)
// ---------------------------------------------------------------------

module mock_epd() {
    color("DimGray")
        box_at(EPD_CX - EPD_MOD_W / 2, EPD_CY - EPD_MOD_H / 2, FACE,
               EPD_MOD_W, EPD_MOD_H, EPD_MOD_T);
    color("White")
        box_at(EPD_CX - EPD_ACTIVE / 2 + EPD_ACTIVE_DX,
               EPD_CY - EPD_ACTIVE / 2 + EPD_ACTIVE_DY, FACE - 0.2,
               EPD_ACTIVE, EPD_ACTIVE, 0.4);
}

module mock_pcb() {
    color("DarkGreen") box_at(PCB_X, PCB_Y, PCB_Z, PCB_W, PCB_H, PCB_T);
    // Feather on short headers, component side facing the back of the case
    color("MidnightBlue")
        box_at(PCB_X + 2, PCB_Y + 3, PCB_Z + PCB_T + HEADER_H,
               FEATHER_W, FEATHER_H, FEATHER_PCB_T + FEATHER_TOP_PARTS);
    // USB-C receptacle on the left end of the Feather
    color("Silver")
        box_at(PCB_X + 2 - 1.5, PCB_Y + 3 + (FEATHER_H - USBC_W) / 2,
               PCB_Z + PCB_T + HEADER_H + FEATHER_PCB_T, 7.5, USBC_W, USBC_H);
    // button
    color("Black")
        box_at(BTN_CX - BTN_SW_BODY / 2, BTN_CY - BTN_SW_BODY / 2,
               PCB_Z - BTN_SW_H, BTN_SW_BODY, BTN_SW_BODY, BTN_SW_H);
}

module mock_sps30() {
    color("ForestGreen")
        box_at(SPS30_X, SPS30_Y, SPS30_Z, SPS30_W, SPS30_H, SPS30_T);
}

module mock_gas() {
    color("Navy") box_at(SGP40_X, SGP40_Y, FACE + GAS_STANDOFF,
                         GAS_BRK_W, GAS_BRK_H, GAS_BRK_T);
    color("Navy") box_at(SCD41_X, SCD41_Y, FACE + GAS_STANDOFF,
                         GAS_BRK_W, GAS_BRK_H, GAS_BRK_T);
}

module mock_battery() {
    color("DarkSlateGray")
        box_at(BAT_X, BAT_Y, BAT_Z + BAT_PAD, BAT_W, BAT_H, BAT_T);
}

// ---------------------------------------------------------------------
// shared negatives
// ---------------------------------------------------------------------

module epd_window() {
    // straight opening at the outer face, opening out to 45 deg inwards so the
    // chamfer prints without support when the shell lies face down.
    translate([EPD_CX - EPD_WIN / 2 + EPD_ACTIVE_DX,
               EPD_CY - EPD_WIN / 2 + EPD_ACTIVE_DY, -0.5])
        cube([EPD_WIN, EPD_WIN, FACE + 1]);
    hull() {
        translate([EPD_CX - EPD_WIN / 2 + EPD_ACTIVE_DX,
                   EPD_CY - EPD_WIN / 2 + EPD_ACTIVE_DY, FACE - EPD_BEZEL_CHAMFER])
            cube([EPD_WIN, EPD_WIN, 0.01]);
        translate([EPD_CX - EPD_WIN / 2 - EPD_BEZEL_CHAMFER + EPD_ACTIVE_DX,
                   EPD_CY - EPD_WIN / 2 - EPD_BEZEL_CHAMFER + EPD_ACTIVE_DY, FACE])
            cube([EPD_WIN + 2 * EPD_BEZEL_CHAMFER,
                  EPD_WIN + 2 * EPD_BEZEL_CHAMFER, 0.01]);
    }
}

module button_hole() {
    translate([BTN_CX, BTN_CY, -0.5]) cylinder(d = BTN_HOLE_D, h = FACE + 1);
    translate([BTN_CX, BTN_CY, -0.01]) cylinder(d = BTN_CB_D, h = BTN_CB_DEPTH);
}

module usbc_opening() {
    // left side wall, aligned with the Feather's receptacle
    z0 = PCB_Z + PCB_T + HEADER_H + FEATHER_PCB_T - (USBC_CLEAR_H - USBC_H) / 2;
    y0 = PCB_Y + 3 + (FEATHER_H - USBC_CLEAR_W) / 2;
    translate([-1, y0, z0]) cube([WALL + 6, USBC_CLEAR_W, USBC_CLEAR_H]);
}

// ---------------------------------------------------------------------
// FRONT SHELL
// ---------------------------------------------------------------------

module front_shell_solid() {
    difference() {
        slab(CASE_W, CASE_H, FRONT_SHELL_D, CORNER_R, CHAMFER_BED, 0);
        // hollow
        translate([WALL, WALL, FACE])
            linear_extrude(FRONT_SHELL_D) rrect(INNER_W, INNER_H, max(CORNER_R - WALL, 0.5));
        // rebate for the back shell lip
        translate([WALL - LAP_WALL, WALL - LAP_WALL, FRONT_SHELL_D - LAP_DEPTH])
            linear_extrude(LAP_DEPTH + 1)
                rrect(INNER_W + 2 * LAP_WALL, INNER_H + 2 * LAP_WALL,
                      max(CORNER_R - WALL + LAP_WALL, 0.5));
    }
}

module front_shell() {
    difference() {
        union() {
            front_shell_solid();
            // --- screw posts -------------------------------------------
            for (p = POST_XY) post(p[0], p[1], FACE, FRONT_SHELL_D - LAP_DEPTH);
            // --- sealed rib between the sensor bay and the electronics ---
            box_at(WALL, SENSOR_BAY_TOP, FACE,
                   INNER_W, DIVIDER_T, FRONT_SHELL_D - FACE - LAP_DEPTH);
            // --- sealed wall between the gas bay and the SPS30 bay -------
            box_at(GAS_PARTITION_X, WALL, FACE,
                   GAS_PARTITION_T, SENSOR_BAY_TOP - WALL,
                   FRONT_SHELL_D - FACE - LAP_DEPTH);
            // --- SPS30 cradle -------------------------------------------
            sps30_cradle();
            // --- display module retainers --------------------------------
            epd_retainers();
            // --- carrier board standoffs ---------------------------------
            for (dx = [PCB_HOLE_INSET, PCB_W - PCB_HOLE_INSET],
                 dy = [PCB_HOLE_INSET, PCB_H - PCB_HOLE_INSET])
                post(PCB_X + dx, PCB_Y + dy, FACE, PCB_Z, 6.0);
            // --- gas sensor standoffs ------------------------------------
            for (b = [[SGP40_X, SGP40_Y], [SCD41_X, SCD41_Y]])
                for (dx = [2.5, GAS_BRK_W - 2.5], dy = [2.5, GAS_BRK_H - 2.5])
                    post(b[0] + dx, b[1] + dy, FACE, FACE + GAS_STANDOFF, 5.0);
        }
        // ---- negatives -------------------------------------------------
        epd_window();
        button_hole();
        usbc_opening();
        sps30_ducts();
        gas_bay_vents();
        // heat-set inserts, entered from the seam side
        for (p = POST_XY)
            translate([p[0], p[1], FRONT_SHELL_D - LAP_DEPTH - INSERT_DEPTH])
                cylinder(d = INSERT_D, h = INSERT_DEPTH + 0.1);
        // self-tapping cores for the carrier board
        for (dx = [PCB_HOLE_INSET, PCB_W - PCB_HOLE_INSET],
             dy = [PCB_HOLE_INSET, PCB_H - PCB_HOLE_INSET])
            translate([PCB_X + dx, PCB_Y + dy, PCB_Z - 6.5])
                cylinder(d = SELFTAP_CORE_D, h = 7.0);
        // and for the two gas breakouts
        for (b = [[SGP40_X, SGP40_Y], [SCD41_X, SCD41_Y]])
            for (dx = [2.5, GAS_BRK_W - 2.5], dy = [2.5, GAS_BRK_H - 2.5])
                translate([b[0] + dx, b[1] + dy, FACE + GAS_STANDOFF - 5.0])
                    cylinder(d = SELFTAP_CORE_D, h = 5.5);
        // cable pass-through from the sensor bay to the electronics
        box_at(GAS_PARTITION_X - 16, SENSOR_BAY_TOP - 0.1, FACE + 6,
               12, DIVIDER_T + 0.2, 6);
        // branding, engraved 0.6 mm deep and 1.2 mm wide strokes
        translate([EPD_CX, 11.5, -0.01]) linear_extrude(0.7)
            text("AIR CHECK", size = 4.2, halign = "center", valign = "center",
                 font = "Helvetica:style=Bold", spacing = 1.25);
    }
}

// SPS30 sits in a pocket whose bottom wall carries the two ducts.
module sps30_cradle() {
    w = 2.0;
    // side rails, they only touch the sensor's plastic fixation elements
    box_at(SPS30_X - w - FIT_SLIDE, SPS30_Y - FIT_SLIDE, FACE,
           w, SPS30_H + 2 * FIT_SLIDE, SPS30_Z + SPS30_T - FACE);
    box_at(SPS30_X + SPS30_W + FIT_SLIDE, SPS30_Y - FIT_SLIDE, FACE,
           w, SPS30_H + 2 * FIT_SLIDE, SPS30_Z + SPS30_T - FACE);
    // top stop
    box_at(SPS30_X - w, SPS30_Y + SPS30_H + FIT_SLIDE, FACE,
           SPS30_W + 2 * w, w, SPS30_Z + SPS30_T - FACE);
    // front stand-off ribs so the sensor floats on foam, not on the shell
    for (x = [SPS30_X + 6, SPS30_X + SPS30_W - 8])
        box_at(x, SPS30_Y + 4, FACE, 2, SPS30_H - 8, SPS30_Z - FACE);
    // duct walls: a sealed box under the port face, split by a rib
    y0 = WALL;
    y1 = SPS30_Y;
    box_at(SPS30_X - w, y0, SPS30_Z - 1.0, w, y1 - y0, SPS30_T + 2.0);
    box_at(SPS30_X + SPS30_W, y0, SPS30_Z - 1.0, w, y1 - y0, SPS30_T + 2.0);
    // the separating rib between outlet and inlets
    box_at(SPS30_X + SPS30_OUTLET_X1 + (SPS30_INLET_X0 - SPS30_OUTLET_X1) / 2
           - SPS30_DUCT_RIB / 2, y0, SPS30_Z - 1.0,
           SPS30_DUCT_RIB, y1 - y0 + 1.0, SPS30_T + 2.0);
    // duct roof, so air cannot escape sideways into the bay
    box_at(SPS30_X - w, y0, SPS30_Z + SPS30_T,
           SPS30_W + 2 * w, y1 - y0, 1.0);
}

// The two openings in the bottom wall.  They are far apart and separated by
// an external lip so exhaust air cannot be drawn straight back in.
module sps30_ducts() {
    ow = SPS30_OUTLET_X1 - SPS30_OUTLET_X0;
    iw = SPS30_INLET_X1 - SPS30_INLET_X0;
    // outlet: two slots so neither roof is a long bridge
    for (c = [0 : 1])
        translate([SPS30_X + SPS30_OUTLET_X0 + c * (ow / 2 + 0.8), -1,
                   SPS30_Z + 1.0])
            cube([ow / 2 - 0.8, WALL + 2, SPS30_T - 2.0]);
    // inlets
    translate([SPS30_X + SPS30_INLET_X0, -1, SPS30_Z + 1.0])
        cube([iw, WALL + 2, SPS30_T - 2.0]);
}

module gas_bay_vents() {
    // bottom wall
    for (c = [0 : 2], r = [0 : 3])
        translate([GAS_BAY_X + 8 + c * 11.6, -1, FACE + 3 + r * 4.0])
            cube([10, WALL + 2, 2.4]);
    // left wall
    for (c = [0 : 2], r = [0 : 3])
        translate([-1, GAS_BAY_Y + 6 + c * 11.6, FACE + 3 + r * 4.0])
            cube([WALL + 2, 10, 2.4]);
}

module epd_retainers() {
    // four L shaped tabs that trap the module against the inside of the face
    x0 = EPD_CX - EPD_MOD_W / 2 - 1.2;
    y0 = EPD_CY - EPD_MOD_H / 2 - 1.2;
    w = EPD_MOD_W + 2.4;
    h = EPD_MOD_H + 2.4;
    for (c = [[x0, y0], [x0 + w - 6, y0], [x0, y0 + h - 6], [x0 + w - 6, y0 + h - 6]])
        difference() {
            translate([c[0], c[1], FACE]) cube([6, 6, EPD_MOD_T + 1.2]);
            translate([c[0] - 0.1 + (c[0] > EPD_CX ? -1.2 : 1.2),
                       c[1] - 0.1 + (c[1] > EPD_CY ? -1.2 : 1.2), FACE - 0.1])
                cube([6, 6, EPD_MOD_T + FIT_LOOSE + 0.1]);
        }
}

// ---------------------------------------------------------------------
// BACK SHELL
// ---------------------------------------------------------------------

module back_shell() {
    difference() {
        union() {
            // shell body and the lip in one solid, cavity cut afterwards, so
            // no two coincident interior faces are ever unioned together
            difference() {
                union() {
                    slab(CASE_W, CASE_H, BACK_SHELL_D, CORNER_R, CHAMFER_BED, 0);
                    translate([WALL - LAP_WALL, WALL - LAP_WALL, BACK_SHELL_D - 1.0])
                        linear_extrude(LAP_DEPTH + 1.0)
                            rrect(INNER_W + 2 * LAP_WALL, INNER_H + 2 * LAP_WALL,
                                  max(CORNER_R - WALL + LAP_WALL, 0.5));
                }
                translate([WALL, WALL, FACE])
                    linear_extrude(BACK_SHELL_D + LAP_DEPTH + 2)
                        rrect(INNER_W, INNER_H, max(CORNER_R - WALL, 0.5));
            }
            bat_bay();
            for (p = POST_XY) post(p[0], p[1], FACE, BACK_SHELL_D, POST_D);
        }
        // screw clearance + head counterbore in the outer face
        for (p = POST_XY) {
            translate([p[0], p[1], -1])
                cylinder(d = SCREW_D, h = BACK_SHELL_D + LAP_DEPTH + 2);
            translate([p[0], p[1], -0.01])
                cylinder(d = SCREW_HEAD_D, h = SCREW_HEAD_DEPTH);
        }
        keyholes();
        // slow-leak vent for the battery bay: not an air channel, just a path
        // for any gas a failing cell might produce
        for (i = [0 : 3])
            translate([CASE_W / 2 - 20 + i * 12, CASE_H - WALL - 1.2, -1])
                cube([8, WALL + 2, 1.6]);
    }
}

module bat_bay() {
    t = 1.6;
    x0 = BAT_X - BAT_CLEAR - t;
    y0 = BAT_Y - BAT_CLEAR - t;
    w = BAT_W + 2 * (BAT_CLEAR + t);
    h = BAT_H + 2 * (BAT_CLEAR + t);
    zh = BACK_SHELL_D - FACE;
    difference() {
        box_at(x0, y0, FACE, w, h, zh);
        box_at(x0 + t, y0 + t, FACE - 0.1, w - 2 * t, h - 2 * t, zh + 0.2);
        // lead exit
        box_at(x0 + w / 2 - 6, y0 - 0.1, FACE + 1, 12, t + 0.2, zh);
    }
}

module keyholes() {
    for (i = [-1, 1]) {
        x = CASE_W / 2 + i * KEYHOLE_SPACING / 2;
        y = CASE_H - 12;
        translate([x, y, -1]) cylinder(d = KEYHOLE_D_BIG, h = FACE + 2);
        translate([x, y - KEYHOLE_LEN, -1]) cylinder(d = KEYHOLE_D_SMALL, h = FACE + 2);
        translate([x - KEYHOLE_D_SMALL / 2, y - KEYHOLE_LEN, -1])
            cube([KEYHOLE_D_SMALL, KEYHOLE_LEN, FACE + 2]);
        // countersunk pocket on the inside so the screw head clears
        translate([x, y, FACE - 1.4]) cylinder(d = KEYHOLE_D_BIG + 3, h = 1.5);
    }
}

// ---------------------------------------------------------------------
// small parts
// ---------------------------------------------------------------------

module button_cap() {
    // printed disc side down: the visible face is the bed face
    cylinder(d = BTN_CAP_D, h = BTN_CAP_T);
    translate([0, 0, BTN_CAP_T - 0.01])
        cylinder(d = BTN_HOLE_D - 2 * FIT_SLIDE, h = BTN_CAP_POST);
}

module battery_clip() {
    // bridges the battery bay and holds the cell down; screws into the
    // two bosses cast into the bay walls
    l = BAT_W + 2 * BAT_CLEAR + 16;
    difference() {
        union() {
            cube([l, 12, 2.4]);
            for (x = [4, l - 4]) translate([x - 4, 0, 0]) cube([8, 12, 5.0]);
        }
        for (x = [5, l - 5]) translate([x, 6, -1]) cylinder(d = SCREW_D, h = 10);
    }
}

module desk_stand() {
    // cradle that holds the case at 15 deg; printed on its back face
    tilt = 15;
    w = 70;
    d = 52;
    t = 3.0;
    difference() {
        union() {
            linear_extrude(w)
                polygon([[0, 0], [d, 0], [d, 6], [10, 6 + (d - 10) * tan(tilt)],
                         [0, 6 + (d - 10) * tan(tilt)]]);
        }
        // groove that the case's bottom edge drops into
        rotate([0, -tilt, 0]) translate([14, 6, -1])
            cube([CASE_D + 2 * FIT_LOOSE, 40, w + 2]);
        // material saving
        translate([8, -1, 10]) cube([d - 18, 4, w - 20]);
    }
}

// ---------------------------------------------------------------------
// assembly + sections
// ---------------------------------------------------------------------

module assembly() {
    color("Gainsboro", 0.55) front_shell();
    color("Gainsboro", 0.55) translate([0, 0, CASE_D])
        rotate([0, 180, 0]) translate([-CASE_W, 0, 0]) back_shell();
    mock_epd();
    mock_pcb();
    mock_sps30();
    mock_gas();
    mock_battery();
    translate([BTN_CX, BTN_CY, BTN_CB_DEPTH + BTN_CAP_T])
        rotate([180, 0, 0]) color("Black") button_cap();
}

module exploded(gap = 26) {
    color("Gainsboro") front_shell();
    translate([0, 0, -gap]) mock_epd();
    translate([0, 0, gap * 0.55]) mock_pcb();
    translate([0, 0, gap * 0.25]) mock_sps30();
    translate([0, 0, gap * 0.25]) mock_gas();
    translate([0, 0, gap * 1.3]) mock_battery();
    translate([0, 0, CASE_D + gap * 1.8]) rotate([0, 180, 0])
        translate([-CASE_W, 0, 0]) color("Gainsboro") back_shell();
    translate([BTN_CX, BTN_CY, -gap * 1.4]) rotate([180, 0, 0])
        color("Black") button_cap();
}

// ---------------------------------------------------------------------
// numeric self-checks - these run on every render
// ---------------------------------------------------------------------

echo(str("CASE  ", CASE_W, " x ", CASE_H, " x ", CASE_D, " mm"));
echo(str("PCB front face Z = ", PCB_Z, "   stack top Z = ", STACK_Z));
echo(str("battery front face Z = ", BAT_Z + BAT_PAD,
         "   clearance to the stack = ", BAT_Z + BAT_PAD - STACK_Z, " mm"));
echo(str("SPS30 occupies Z ", SPS30_Z, " .. ", SPS30_Z + SPS30_T,
         "  (front shell is ", FRONT_SHELL_D, " deep)"));
echo(str("display window ", EPD_WIN, " x ", EPD_WIN, " mm, active area ",
         EPD_ACTIVE, " mm"));
echo(str("sensor bay: Y ", WALL, " .. ", SENSOR_BAY_TOP,
         "   electronics: Y ", SENSOR_BAY_TOP + DIVIDER_T, " .. ", CASE_H - WALL));

assert(STACK_Z < BAT_Z + BAT_PAD,
       "the Feather stack collides with the battery");
assert(SPS30_Z + SPS30_T <= FRONT_SHELL_D - LAP_DEPTH,
       "the SPS30 crosses the shell seam");
assert(SPS30_Y + SPS30_H < SENSOR_BAY_TOP,
       "the SPS30 pokes through the sensor bay divider");
assert(PCB_Y > SENSOR_BAY_TOP + DIVIDER_T,
       "the carrier board sits inside the sensor bay");
assert(PCB_X + PCB_W < CASE_W - WALL && PCB_Y + PCB_H < CASE_H - WALL,
       "the carrier board does not fit inside the case");
assert(BAT_Y + BAT_H <= CASE_H - WALL,
       "the battery does not fit inside the case");
assert(BAT_Y > SENSOR_BAY_TOP,
       "the battery overlaps the sensor bay");
assert(EPD_CY + EPD_MOD_H / 2 < CASE_H - WALL,
       "the display module hits the top wall");
assert(EPD_CY - EPD_MOD_H / 2 > BTN_CY + BTN_CAP_D / 2,
       "the button overlaps the display module");
assert(SGP40_Y + GAS_BRK_H < SCD41_Y, "the two gas breakouts overlap");
assert(SCD41_Y + GAS_BRK_H < SENSOR_BAY_TOP, "the SCD41 pokes past the divider");
assert(SGP40_X + GAS_BRK_W < GAS_PARTITION_X,
       "the gas breakouts hit the partition to the SPS30 bay");
assert(SPS30_X > GAS_PARTITION_X + GAS_PARTITION_T,
       "the SPS30 overlaps the gas bay partition");
assert(WALL >= 4 * NOZZLE, "wall thinner than four extrusions");
assert(VENT_SLOT_W <= 12, "vent slot roof is too long a bridge");
assert(INSERT_D > 2.5 && INSERT_DEPTH >= 5,
       "heat-set insert pocket does not match an M2.5 insert");

// ---------------------------------------------------------------------
// export dispatcher
// ---------------------------------------------------------------------

if (part == "front")        rotate([0, 0, 0]) front_shell();
else if (part == "back")    back_shell();
else if (part == "button")  button_cap();
else if (part == "batclip") battery_clip();
else if (part == "stand")   desk_stand();
else if (part == "assembly") assembly();
else if (part == "exploded") exploded();
else if (part == "sec_z")
    projection(cut = true) translate([0, 0, -(FACE + EPD_MOD_T + 1)]) assembly();
else if (part == "sec_y")
    projection(cut = true) rotate([90, 0, 0]) translate([0, -24, 0]) assembly();
else if (part == "sec_x")
    projection(cut = true) rotate([0, 90, 0]) translate([-EPD_CX, 0, 0]) assembly();
