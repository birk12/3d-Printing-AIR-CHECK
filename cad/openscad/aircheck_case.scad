// =====================================================================
// 3D Printing AIR CHECK - enclosure (v1.2)
// ---------------------------------------------------------------------
//   openscad -D 'part="front"'    -o front_shell.stl aircheck_case.scad
//   openscad -D 'part="back"'     -o lid.stl         aircheck_case.scad
//   openscad -D 'part="button"' / "batcover" / "stand" / "assembly" / "exploded"
//   openscad -D 'part="sec_x"' / "sec_y" / "sec_z"   (section views)
//
// Both shells are modelled in the device frame (see aircheck_params.scad) and
// the lid is flipped into place for the assembly.  Print direction is Z for
// both, so every silhouette corner radius stands perpendicular to the bed and
// no radius ever becomes an overhang.  The side-wall port slots are
// horizontal holes whose roofs are bridges of at most VENT_SLOT_W / 9.5 mm.
// =====================================================================

include <aircheck_params.scad>

part = "assembly";

// ---------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------

module rrect(w, h, r) {
    hull() for (x = [r, w - r], y = [r, h - r]) translate([x, y]) circle(r = r);
}

// Rounded slab extruded along Z with a chamfer on the z=0 face (the face that
// sits on the print bed).
module slab(w, h, d, r, cham_bed = CHAMFER_BED) {
    hull() {
        translate([0, 0, cham_bed]) linear_extrude(max(d - cham_bed, 0.01))
            rrect(w, h, r);
        if (cham_bed > 0)
            linear_extrude(0.01) translate([cham_bed, cham_bed])
                rrect(w - 2 * cham_bed, h - 2 * cham_bed, max(r - cham_bed, 0.1));
    }
}

module box_at(x, y, z, w, h, d) { translate([x, y, z]) cube([w, h, d]); }

module post(x, y, z0, z1, od = POST_D) {
    translate([x, y, z0]) cylinder(d = od, h = z1 - z0);
}

// PCB outline with the corner notch at the top of the USB end
module pcb_outline() {
    difference() {
        square([PCB_W, PCB_H]);
        translate([-0.01, PCB_H - PCB_NOTCH_H]) square([PCB_NOTCH_W + 0.01, PCB_NOTCH_H + 0.01]);
    }
}

// the port face of the SEN63C sees these windows, sensor-local length -> Y
function sen_inlet_y0()  = SEN_Y + SEN_INLET_L0;
function sen_inlet_y1()  = SEN_Y + SEN_INLET_L1;
function sen_outlet_y0() = SEN_Y + SEN_OUTLET_C - SEN_OUTLET_D / 2;
function sen_outlet_y1() = SEN_Y + SEN_OUTLET_C + SEN_OUTLET_D / 2;
OUTLET_COL = (SEN_OUTLET_D - PORT_BAR) / 2;
INLET_AREA  = len(PORT_ROWS_Z) * PORT_SLOT_H * (SEN_INLET_L1 - SEN_INLET_L0);
OUTLET_AREA = len(PORT_ROWS_Z) * PORT_SLOT_H * 2 * OUTLET_COL;

// battery bay outline on the lid (device X/Y)
BAY_W  = BAT_W + 2 * (BAT_CLEAR + BAT_BAY_T);
BAY_H  = BAT_H + 2 * (BAT_CLEAR + BAT_BAY_T);
BAY_X0 = BAT_BAY_X0;
BAY_Y0 = BAT_Y - BAT_CLEAR - BAT_BAY_T;
COVER_SKIRT_T = 0.8;
COVER_SKIRT_H = 3.0;

// ---------------------------------------------------------------------
// component mock-ups (for the assembly view and the collision checks)
// ---------------------------------------------------------------------

module mock_led() {
    color("White") translate([LED_CX, LED_CY, LED_SKIN + 0.1])
        cylinder(d = 5.0, h = LED_BODY_H);
}

module mock_pcb() {
    color("DarkGreen") translate([PCB_X, PCB_Y, PCB_Z])
        linear_extrude(PCB_T) pcb_outline();
    // FireBeetle soldered on its headers, component side facing the back
    fx = PCB_X + FB_ON_PCB[0];
    fy = PCB_Y + FB_ON_PCB[1];
    fz = PCB_Z + PCB_T + HEADER_H;
    color("DimGray") box_at(fx, fy, fz, FB_W, FB_H, FB_PCB_T);
    color("Black") box_at(fx + 4, fy + 2, fz + FB_PCB_T, FB_W - 8, FB_H - 4, 1.2);
    // USB-C at the X = 0 end, JST PH battery socket beside it
    color("Silver") box_at(fx - 0.5, fy + (FB_H - USBC_W) / 2, fz + FB_PCB_T, 7.5, USBC_W, USBC_H);
    color("Ivory") box_at(fx + 5, fy + 0.5, fz + FB_PCB_T, 8, 6, FB_TOP_PARTS);
    // button
    color("Black")
        box_at(BTN_CX - BTN_SW_BODY / 2, BTN_CY - BTN_SW_BODY / 2,
               PCB_Z - BTN_SW_H, BTN_SW_BODY, BTN_SW_BODY, BTN_SW_H);
}

module mock_sen63c() {
    color("DarkSlateGray") box_at(SEN_X, SEN_Y, SEN_Z, SEN_HGT, SEN_LEN, SEN_WID);
    // the keep-out behind the connector face
    color("Orange", 0.35)
        box_at(SEN_X - SEN_PLUG_L, SEN_Y + SEN_PLUG_L0, SEN_Z,
               SEN_PLUG_L, SEN_PLUG_L1 - SEN_PLUG_L0, SEN_WID);
}

module mock_gas() {
    color("Navy") box_at(SGP40_X, SGP40_Y, FACE + GAS_STANDOFF,
                         GAS_BRK_W, GAS_BRK_H, GAS_BRK_T);
}

module mock_battery() {
    color("SlateGray") box_at(BAT_X, BAT_Y, BAT_Z + BAT_PAD, BAT_W, BAT_H, BAT_T);
}

// ---------------------------------------------------------------------
// shared negatives
// ---------------------------------------------------------------------

// Blind pocket from the inside; the last LED_SKIN of front face stays, so the
// LED glows through the plastic without a hole.  Printed face down this is an
// open pocket, not a bridge.
module led_pocket() {
    // LED_SKIN = 0 gives a plain through-hole for dark filaments
    translate([LED_CX, LED_CY, LED_SKIN > 0 ? LED_SKIN : -0.5])
        cylinder(d = LED_POCKET_D, h = FACE + 1);
}

module button_hole() {
    translate([BTN_CX, BTN_CY, -0.5]) cylinder(d = BTN_HOLE_D, h = FACE + 1);
    translate([BTN_CX, BTN_CY, -0.01]) cylinder(d = BTN_CB_D, h = BTN_CB_DEPTH);
}

module usbc_opening() {
    // X = 0 wall, centred on the FireBeetle's receptacle
    z0 = PCB_Z + PCB_T + HEADER_H + FB_PCB_T - (USBC_CLEAR_H - USBC_H) / 2;
    y0 = PCB_Y + FB_ON_PCB[1] + (FB_H - USBC_CLEAR_W) / 2;
    translate([-1, y0, z0]) cube([WALL + 2, USBC_CLEAR_W, USBC_CLEAR_H]);
}

// SEN63C ports through the X = CASE_W wall: one window over both inlets, one
// two-column window over the outlet.  Each slot is PORT_SLOT_H tall, so every
// roof is a bridge of at most OUTLET_COL.
module sen_ports() {
    for (z = PORT_ROWS_Z) {
        translate([CASE_W - WALL - 1, sen_inlet_y0(), z])
            cube([WALL + 2, SEN_INLET_L1 - SEN_INLET_L0, PORT_SLOT_H]);
        for (c = [0, 1])
            translate([CASE_W - WALL - 1, sen_outlet_y0() + c * (OUTLET_COL + PORT_BAR), z])
                cube([WALL + 2, OUTLET_COL, PORT_SLOT_H]);
    }
}

// Gas bay vents: bottom wall and the X = 0 side wall, below the divider.
module gas_bay_vents() {
    x0 = 12.0;
    x1 = GAS_PARTITION_X - 1.0;
    ncol = ceil((x1 - x0 + 1.6) / (VENT_SLOT_W + 1.6));
    colw = (x1 - x0 - (ncol - 1) * 1.6) / ncol;
    for (c = [0 : ncol - 1], r = [0 : 3])
        translate([x0 + c * (colw + 1.6), -1, FACE + 3 + r * 4.0])
            cube([colw, WALL + 2, VENT_SLOT_H]);
    for (c = [0 : 2], r = [0 : 3])
        translate([-1, 13 + c * 11.6, FACE + 3 + r * 4.0])
            cube([WALL + 2, VENT_SLOT_W, VENT_SLOT_H]);
}

// ---------------------------------------------------------------------
// FRONT SHELL
// ---------------------------------------------------------------------

module front_shell_solid() {
    difference() {
        slab(CASE_W, CASE_H, FRONT_SHELL_D, CORNER_R, CHAMFER_BED);
        // hollow
        translate([WALL, WALL, FACE])
            linear_extrude(FRONT_SHELL_D) rrect(INNER_W, INNER_H, max(CORNER_R - WALL, 0.5));
        // rebate for the lid's lip
        translate([WALL - LAP_WALL, WALL - LAP_WALL, FRONT_SHELL_D - LAP_DEPTH])
            linear_extrude(LAP_DEPTH + 1)
                rrect(INNER_W + 2 * LAP_WALL, INNER_H + 2 * LAP_WALL,
                      max(CORNER_R - WALL + LAP_WALL, 0.5));
    }
}

// The SEN63C stands on its side in a cradle: rails above and below it, stops
// behind its connector face at both ends (the middle stays open for the
// plug), and two low ribs under it so it does not rest on the front face.
module sen63c_cradle() {
    w = 2.0;
    ztop = FRONT_SHELL_D - LAP_DEPTH;
    // rails, from the connector face to the wall
    // (0.4 mm into the wall so the union never meets it edge-on)
    box_at(SEN_X, SEN_Y - FIT_SLIDE - w, FACE, CASE_W - WALL - SEN_X + 0.4, w, ztop - FACE);
    box_at(SEN_X, SEN_Y + SEN_LEN + FIT_SLIDE, FACE, CASE_W - WALL - SEN_X + 0.4, w, ztop - FACE);
    // stops behind the connector face, clear of the plug stretch
    for (yy = [[SEN_Y - FIT_SLIDE - w, SEN_Y + SEN_PLUG_L0 - 1],
               [SEN_Y + SEN_PLUG_L1 + 1, SEN_Y + SEN_LEN + FIT_SLIDE + w]])
        box_at(SEN_X - FIT_SLIDE - w, yy[0], FACE, w, yy[1] - yy[0], RIB_TOP_Z - FACE);
    // stand-off ribs under the module
    for (x = [SEN_X + 4, SEN_X + SEN_HGT - 6])
        box_at(x, SEN_Y + 4, FACE, 2, SEN_LEN - 8, SEN_Z - FACE);
    // gasket frames on the inner wall around each window
    xg = CASE_W - WALL - GASKET_RIB_H;
    zlo = PORT_ROWS_Z[0] - 1.5;
    zhi = PORT_ROWS_Z[len(PORT_ROWS_Z) - 1] + PORT_SLOT_H + 1.5;
    for (yy = [[sen_inlet_y0() - 1.5, sen_inlet_y1() + 1.5],
               [sen_outlet_y0() - 1.5, sen_outlet_y1() + 1.5]])
        difference() {
            box_at(xg, yy[0] - GASKET_RIB_W, zlo - GASKET_RIB_W, GASKET_RIB_H + 0.4,
                   yy[1] - yy[0] + 2 * GASKET_RIB_W,
                   min(zhi + GASKET_RIB_W, FRONT_SHELL_D - LAP_DEPTH) - (zlo - GASKET_RIB_W));
            box_at(xg - 0.1, yy[0], zlo, GASKET_RIB_H + 0.3, yy[1] - yy[0], zhi - zlo);
        }
}

module front_shell() {
    difference() {
        union() {
            front_shell_solid();
            // --- screw posts -------------------------------------------
            for (p = POST_XY) post(p[0], p[1], FACE, FRONT_SHELL_D - LAP_DEPTH);
            // --- gas bay: top rib and inner side wall, both sealed ---------
            box_at(WALL, SENSOR_BAY_TOP, FACE,
                   GAS_PARTITION_X + GAS_PARTITION_T - WALL, DIVIDER_T, RIB_TOP_Z - FACE);
            box_at(GAS_PARTITION_X, WALL, FACE,
                   GAS_PARTITION_T, SENSOR_BAY_TOP - WALL, RIB_TOP_Z - FACE);
            // --- SEN63C cradle --------------------------------------------
            sen63c_cradle();
            // --- LED guide: a short tube that centres the 5 mm LED ------------
            difference() {
                translate([LED_CX, LED_CY, FACE - 0.01])
                    cylinder(d = LED_POCKET_D + 2.4, h = 3.0);
                translate([LED_CX, LED_CY, FACE - 0.1])
                    cylinder(d = LED_POCKET_D, h = 3.2);
            }
            // --- carrier board standoffs ---------------------------------
            for (h = PCB_HOLES) post(PCB_X + h[0], PCB_Y + h[1], FACE, PCB_Z, 6.0);
            // --- gas sensor standoffs ------------------------------------
            for (dx = [2.5, GAS_BRK_W - 2.5], dy = [2.5, GAS_BRK_H - 2.5])
                post(SGP40_X + dx, SGP40_Y + dy, FACE, FACE + GAS_STANDOFF, 5.0);
        }
        // ---- negatives -------------------------------------------------
        led_pocket();
        button_hole();
        usbc_opening();
        sen_ports();
        gas_bay_vents();
        // heat-set inserts, entered from the seam side
        for (p = POST_XY)
            translate([p[0], p[1], FRONT_SHELL_D - LAP_DEPTH - INSERT_DEPTH])
                cylinder(d = INSERT_D, h = INSERT_DEPTH + 0.1);
        // self-tapping cores for the carrier board and the breakout
        for (h = PCB_HOLES)
            translate([PCB_X + h[0], PCB_Y + h[1], PCB_Z - 6.5])
                cylinder(d = SELFTAP_CORE_D, h = 7.0);
        for (dx = [2.5, GAS_BRK_W - 2.5], dy = [2.5, GAS_BRK_H - 2.5])
            translate([SGP40_X + dx, SGP40_Y + dy, FACE + GAS_STANDOFF - 5.0])
                cylinder(d = SELFTAP_CORE_D, h = 5.5);
        // cable pass-through from the gas bay to the carrier; sealed with the
        // foam strip after the cable is in (ASSEMBLY.md)
        box_at(GAS_PARTITION_X - 16, SENSOR_BAY_TOP - 0.1, FACE + 6,
               12, DIVIDER_T + 0.2, 6);
        // branding, engraved 0.6 mm deep and 1.2 mm wide strokes
        // mirrored: the front face is seen from -Z, see aircheck_params.scad
        translate([CASE_W / 2, 11.5, -0.01]) mirror([1, 0, 0]) linear_extrude(0.7)
            text("AIR CHECK", size = 4.2, halign = "center", valign = "center",
                 font = "Helvetica:style=Bold", spacing = 1.25);
    }
}

// ---------------------------------------------------------------------
// LID (back shell).  Modelled with device X and Y but z = 0 at the outer back
// face: mirrored in Z it drops straight into the assembly.  The printable part
// is that same solid turned over, which is a mirror in X - the export
// dispatcher does it.
// ---------------------------------------------------------------------

// device Z  ->  lid-local z
function lidz(z) = CASE_D - z;

module back_shell() {
    difference() {
        union() {
            // lid body and its lip in one solid, cavity cut afterwards, so no
            // two coincident interior faces are ever unioned together
            difference() {
                union() {
                    slab(CASE_W, CASE_H, BACK_SHELL_D, CORNER_R, CHAMFER_BED);
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
            // posts reach down to the front shell's posts (0.2 mm short, so the
            // rim, not the posts, sets the seam)
            for (p = POST_XY)
                post(p[0], p[1], FACE, lidz(FRONT_SHELL_D - LAP_DEPTH) - 0.2, POST_D);
        }
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
            translate([BAY_X0 + 8 + i * 13, CASE_H - WALL - 1.2, -1])
                cube([8, WALL + 2, 1.6]);
    }
}

// Battery bay: walls standing up from the lid face to the front of the cell
// pad; the cover plate closes it and its skirt grips the two long walls.
module bat_bay() {
    t = BAT_BAY_T;
    zh = lidz(BAT_Z + BATCOVER_T) - FACE;
    difference() {
        box_at(BAY_X0, BAY_Y0, FACE, BAY_W, BAY_H, zh);
        box_at(BAY_X0 + t, BAY_Y0 + t, FACE - 0.1, BAY_W - 2 * t, BAY_H - 2 * t, zh + 0.2);
        // lead exit, at the top, toward the carrier
        box_at(BAY_X0 + 8, BAY_Y0 + BAY_H - t - 0.1, FACE + 1, 12, t + 0.2, zh);
    }
}

module keyholes() {
    for (i = [0, 1]) {
        x = KEYHOLE_X;
        y = KEYHOLE_Y0 + i * KEYHOLE_SPACING;
        translate([x, y, -1]) cylinder(d = KEYHOLE_D_BIG, h = FACE + 2);
        translate([x, y - KEYHOLE_LEN, -1]) cylinder(d = KEYHOLE_D_SMALL, h = FACE + 2);
        translate([x - KEYHOLE_D_SMALL / 2, y - KEYHOLE_LEN, -1])
            cube([KEYHOLE_D_SMALL, KEYHOLE_LEN, FACE + 2]);
        // pocket on the inside so the screw head clears
        hull() for (dy = [0, -KEYHOLE_LEN])
            translate([x, y + dy, FACE - 1.4]) cylinder(d = KEYHOLE_CSK_D, h = 1.5);
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

// Plate over the battery bay; printed plate side down.  Keeps the cell in and
// puts plastic between the pouch and the FireBeetle above it.  The skirt grips
// the two long bay walls (FIT_SLIDE), nothing is screwed.
module battery_cover() {
    t = BATCOVER_T;
    ww = BAY_W + 2 * (FIT_SLIDE + COVER_SKIRT_T);
    difference() {
        union() {
            cube([ww, BAY_H, t]);
            for (x = [0, ww - COVER_SKIRT_T])
                translate([x, 0, 0]) cube([COVER_SKIRT_T, BAY_H, t + COVER_SKIRT_H]);
        }
        // slots save plastic and let a swelling cell be seen without opening it
        for (i = [0 : 3], j = [0 : 5])
            translate([COVER_SKIRT_T + 10 + i * 13, 8 + j * 15, -1]) cube([6, 9, t + 2]);
        // skirt relief around the two screw posts it passes
        for (p = POST_XY)
            translate([p[0] - (BAY_X0 - FIT_SLIDE - COVER_SKIRT_T), p[1] - BAY_Y0, -1])
                cylinder(d = POST_D + 0.8, h = t + COVER_SKIRT_H + 2);
    }
}

module desk_stand() {
    // cradle that holds the case at 15 deg; printed on its side
    tilt = 15;
    w = 70;
    d = 52;
    difference() {
        linear_extrude(w)
            polygon([[0, 0], [d, 0], [d, 6], [10, 6 + (d - 10) * tan(tilt)],
                     [0, 6 + (d - 10) * tan(tilt)]]);
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

module lid_in_place() {
    translate([0, 0, CASE_D]) mirror([0, 0, 1]) back_shell();
}

module cover_in_place() {
    translate([BAY_X0 - FIT_SLIDE - COVER_SKIRT_T, BAY_Y0, BAT_Z]) battery_cover();
}

module assembly() {
    color("Gainsboro", 0.55) front_shell();
    color("Gainsboro", 0.55) lid_in_place();
    color("Wheat", 0.8) cover_in_place();
    mock_led();
    mock_pcb();
    mock_sen63c();
    mock_gas();
    mock_battery();
    translate([BTN_CX, BTN_CY, BTN_CB_DEPTH + BTN_CAP_T])
        rotate([180, 0, 0]) color("Black") button_cap();
}

// front shell with everything that goes in it, lid and battery off
module open_view() {
    color("Gainsboro") front_shell();
    mock_led();
    mock_pcb();
    mock_sen63c();
    mock_gas();
}

module exploded(gap = 26) {
    color("Gainsboro") front_shell();
    translate([0, 0, gap * 0.4]) mock_led();
    translate([0, 0, gap * 0.55]) mock_pcb();
    translate([0, 0, gap * 0.25]) mock_gas();
    translate([gap * 0.6, 0, gap * 0.9]) mock_sen63c();
    translate([0, 0, gap * 1.3]) color("Wheat") cover_in_place();
    translate([0, 0, gap * 1.7]) mock_battery();
    translate([0, 0, gap * 2.4]) color("Gainsboro") lid_in_place();
    translate([BTN_CX, BTN_CY, -gap * 1.4]) rotate([180, 0, 0])
        color("Black") button_cap();
}

// ---------------------------------------------------------------------
// numeric self-checks - these run on every render
// ---------------------------------------------------------------------

echo(str("CASE  ", CASE_W, " x ", CASE_H, " x ", CASE_D, " mm"));
echo(str("PCB front face Z = ", PCB_Z, "   stack top Z = ", STACK_Z,
         "   battery cover Z = ", BAT_Z));
echo(str("SEN63C X ", SEN_X, " .. ", SEN_X + SEN_HGT, "  Y ", SEN_Y, " .. ", SEN_Y + SEN_LEN,
         "  Z ", SEN_Z, " .. ", SEN_Z + SEN_WID));
echo(str("port open area: inlets ", INLET_AREA, " mm2 (>= 56), outlet ", OUTLET_AREA,
         " mm2 (>= 148)"));
echo(str("battery bay X ", BAY_X0, " .. ", BAY_X0 + BAY_W, "   plug keep-out from X ",
         SEN_X - SEN_PLUG_L));

function hits(p, x0, y0, x1, y1, r = POST_D / 2) =
    p[0] + r > x0 && p[0] - r < x1 && p[1] + r > y0 && p[1] - r < y1;

// stack and battery
assert(STACK_Z < BAT_Z - 0.5, "the FireBeetle stack collides with the battery cover");
assert(RIB_TOP_Z < BAT_Z, "the front-shell ribs collide with the battery cover");
// SEN63C
assert(SEN_Z > FACE && SEN_Z + SEN_WID < CASE_D - FACE, "the SEN63C does not fit the depth");
assert(SEN_PLUG_L0 < 17.2 && SEN_PLUG_L1 > 29.2 && SEN_CONN_W0 >= 0 && SEN_CONN_W1 <= SEN_WID,
       "the plug keep-out does not cover the connector (STEP: 17.2..29.2 x 16.3..21.8)");
assert(SEN_X + SEN_HGT + SEN_SEAL_T <= CASE_W - WALL + 0.001, "the SEN63C gasket does not fit");
assert(SEN_Y - FIT_SLIDE - 2 > POST_XY[1][1] + POST_D / 2, "the SEN63C cradle hits the bottom post");
assert(SEN_Y + SEN_LEN + FIT_SLIDE + 2 < POST_XY[3][1] - POST_D / 2, "the SEN63C cradle hits the top post");
assert(INLET_AREA >= 56 && OUTLET_AREA >= 148,
       "port open area below Sensirion's minimum (design-in guide 2.1)");
assert(sen_inlet_y1() + 3 < sen_outlet_y0(), "inlet and outlet windows too close to seal apart");
assert(PORT_ROWS_Z[len(PORT_ROWS_Z) - 1] + PORT_SLOT_H < FRONT_SHELL_D - LAP_DEPTH,
       "a port slot runs into the lap joint");
assert(OUTLET_COL <= 12, "outlet slot roof is too long a bridge");
// every port row must fall on the port face
for (z = PORT_ROWS_Z)
    assert(z >= SEN_Z && z + PORT_SLOT_H <= SEN_Z + SEN_WID, "a port row misses the port face");
// battery bay against the SEN63C and its plug
assert(BAY_X0 + BAY_W + FIT_SLIDE + COVER_SKIRT_T < SEN_X - SEN_PLUG_L,
       "the battery bay or its cover skirt reaches into the SEN63C plug keep-out");
assert(BAY_X0 > POST_XY[0][0] + POST_D / 2, "the battery bay hits the left screw posts");
// (the cover skirt is relieved around those posts in battery_cover())
assert(BAY_Y0 > WALL && BAY_Y0 + BAY_H < CASE_H - WALL, "the battery bay does not fit up the case");
// carrier board
assert(PCB_X > WALL && PCB_X + PCB_W < SEN_X - SEN_PLUG_L,
       "the carrier board runs into the SEN63C plug keep-out");
assert(PCB_Y > SENSOR_BAY_TOP + DIVIDER_T && PCB_Y + PCB_H < CASE_H - WALL,
       "the carrier board does not fit between the gas bay and the top wall");
assert(PCB_X + PCB_NOTCH_W > POST_XY[2][0] + POST_D / 2 + 0.5,
       "the PCB notch is not wide enough for the top screw post");
assert(PCB_Y + FB_ON_PCB[1] + FB_H < PCB_Y + PCB_H - PCB_NOTCH_H,
       "the FireBeetle runs into the PCB notch");
assert(POST_XY[2][1] - POST_D / 2 > PCB_Y + PCB_H - PCB_NOTCH_H,
       "the top screw post is not inside the notch band");
for (h = PCB_HOLES)
    assert(!(h[0] < PCB_NOTCH_W + 3 && h[1] > PCB_H - PCB_NOTCH_H - 3),
           "a PCB hole sits in the notch");
// gas bay
assert(SGP40_X + GAS_BRK_W < GAS_PARTITION_X, "the SGP40 breakout hits the partition");
assert(SGP40_Y + GAS_BRK_H < SENSOR_BAY_TOP, "the SGP40 breakout pokes past the top rib");
assert(GAS_PARTITION_X + GAS_PARTITION_T < SEN_X - SEN_PLUG_L, "the gas bay reaches the SEN63C");
assert(PCB_Y + FB_ON_PCB[1] + (FB_H - USBC_CLEAR_W) / 2 > SENSOR_BAY_TOP + DIVIDER_T,
       "the USB-C opening cuts into the gas bay");
// posts against everything
for (p = POST_XY) {
    assert(!hits(p, PCB_X + PCB_NOTCH_W, PCB_Y, PCB_X + PCB_W, PCB_Y + PCB_H) &&
           !hits(p, PCB_X, PCB_Y, PCB_X + PCB_W, PCB_Y + PCB_H - PCB_NOTCH_H),
           str("screw post ", p, " hits the carrier board"));
    assert(!hits(p, SEN_X, SEN_Y, SEN_X + SEN_HGT, SEN_Y + SEN_LEN),
           str("screw post ", p, " hits the SEN63C"));
    assert(!hits(p, SGP40_X, SGP40_Y, SGP40_X + GAS_BRK_W, SGP40_Y + GAS_BRK_H),
           str("screw post ", p, " hits the gas breakout"));
    assert(!hits(p, BAY_X0, BAY_Y0, BAY_X0 + BAY_W, BAY_Y0 + BAY_H),
           str("screw post ", p, " hits the battery bay"));
}
// keyholes
assert(KEYHOLE_X + KEYHOLE_CSK_D / 2 < BAY_X0 - 0.2, "a keyhole pocket runs into the battery bay");
assert(KEYHOLE_X - KEYHOLE_D_BIG / 2 > WALL - 0.5, "a keyhole runs into the side wall");
assert(KEYHOLE_Y0 - KEYHOLE_LEN - KEYHOLE_CSK_D / 2 > POST_XY[0][1] + POST_D / 2,
       "the lower keyhole runs into the bottom screw post");
assert(KEYHOLE_Y0 + KEYHOLE_SPACING + KEYHOLE_CSK_D / 2 < POST_XY[2][1] - POST_D / 2,
       "the upper keyhole runs into the top screw post");
// front-face features
assert(LED_CX > PCB_X + 3 && LED_CX < PCB_X + PCB_W - 3 &&
       LED_CY > PCB_Y + FB_ON_PCB[1] + FB_H && LED_CY < PCB_Y + PCB_H - 3,
       "the LED is not over the carrier, clear of the FireBeetle");
assert(BTN_CX > PCB_X + PCB_NOTCH_W + 4 && BTN_CX < PCB_X + PCB_W - 3 &&
       BTN_CY > PCB_Y + FB_ON_PCB[1] + FB_H && BTN_CY < PCB_Y + PCB_H - 3,
       "the button is not over the carrier, clear of the FireBeetle and the notch");
assert(norm([LED_CX - BTN_CX, LED_CY - BTN_CY]) > (LED_POCKET_D + BTN_CB_D) / 2 + 4,
       "LED and button too close");
assert(WALL >= 4 * NOZZLE, "wall thinner than four extrusions");
assert(VENT_SLOT_W <= 12, "vent slot roof is too long a bridge");
assert(INSERT_D > 2.5 && INSERT_DEPTH >= 5, "heat-set insert pocket does not match an M2.5 insert");
assert(FRONT_SHELL_D - LAP_DEPTH - INSERT_DEPTH > FACE, "insert pocket breaks through the front face");

// ---------------------------------------------------------------------
// export dispatcher
// ---------------------------------------------------------------------

if (part == "front")         front_shell();
else if (part == "back")     translate([CASE_W, 0, 0]) mirror([1, 0, 0]) back_shell();
else if (part == "button")   button_cap();
else if (part == "batcover") battery_cover();
else if (part == "stand")    desk_stand();
else if (part == "assembly") assembly();
else if (part == "exploded") exploded();
else if (part == "open")     open_view();
else if (part == "sec_z")
    projection(cut = true) translate([0, 0, -(PCB_Z + 4)]) assembly();
else if (part == "sec_y")
    projection(cut = true) rotate([90, 0, 0]) translate([0, -(SEN_Y + 20), 0]) assembly();
else if (part == "sec_x")
    projection(cut = true) rotate([0, 90, 0]) translate([-(SEN_X + 10), 0, 0]) assembly();
