// =====================================================================
// 3D Printing AIR CHECK - enclosure (v1.4)
// ---------------------------------------------------------------------
//   openscad -D 'part="front"' -o front_shell.stl aircheck_case.scad
//   openscad -D 'part="back"'  -o lid.stl         aircheck_case.scad
//   openscad -D 'part="door"'  -o door.stl        aircheck_case.scad
//   openscad -D 'part="stand"' / "assembly" / "exploded" / "open"
//   openscad -D 'part="sec_x"' / "sec_y" / "sec_z"   (section views)
//
// All parts are modelled in the device frame (aircheck_params.scad).  The lid
// and the door are flipped into place for the assembly; the printable parts
// are the same solids turned over.  Print direction is Z for everything, so
// every silhouette corner radius stands perpendicular to the bed and no
// radius becomes an overhang.  Horizontal holes (side-wall slots, USB-C) have
// bridged roofs of at most VENT_SLOT_W.
//
// Nothing in the gas bay is glued, taped or foamed (EDR-17): every board sits
// in a fence or on screws, and the bay is closed by printed walls.
// =====================================================================

include <aircheck_params.scad>

part = "assembly";

// ---------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------

module rrect(w, h, r) {
    hull() for (x = [r, w - r], y = [r, h - r]) translate([x, y]) circle(r = r);
}

module slab(w, h, d, r, cham_bed = CHAMFER_BED) {
    hull() {
        translate([0, 0, cham_bed]) linear_extrude(max(d - cham_bed, 0.01)) rrect(w, h, r);
        if (cham_bed > 0)
            linear_extrude(0.01) translate([cham_bed, cham_bed])
                rrect(w - 2 * cham_bed, h - 2 * cham_bed, max(r - cham_bed, 0.1));
    }
}

module box_at(x, y, z, w, h, d) { translate([x, y, z]) cube([w, h, d]); }

module post(x, y, z0, z1, od = POST_D) {
    translate([x, y, z0]) cylinder(d = od, h = z1 - z0);
}

// open-topped rectangular fence around a board footprint
module fence(x, y, w, h, z0, z1, fit = FIT_SLIDE, t = POCKET_WALL) {
    difference() {
        box_at(x - fit - t, y - fit - t, z0, w + 2 * (fit + t), h + 2 * (fit + t), z1 - z0);
        box_at(x - fit, y - fit, z0 - 0.1, w + 2 * fit, h + 2 * fit, z1 - z0 + 0.2);
    }
}

module inner2d() { translate([WALL, WALL]) rrect(INNER_W, INNER_H, max(CORNER_R - WALL, 0.5)); }
module lip2d() {
    translate([WALL - LAP_WALL, WALL - LAP_WALL])
        rrect(INNER_W + 2 * LAP_WALL, INNER_H + 2 * LAP_WALL, max(CORNER_R - WALL + LAP_WALL, 0.5));
}
module band2d(y0, y1) { translate([-1, y0]) square([CASE_W + 2, y1 - y0]); }

// ---------------------------------------------------------------------
// derived positions
// ---------------------------------------------------------------------

function sen_inlet_y0()  = SEN_Y + SEN_INLET_L0;
function sen_inlet_y1()  = SEN_Y + SEN_INLET_L1;
function sen_outlet_y0() = SEN_Y + SEN_OUTLET_C - SEN_OUTLET_D / 2;
function sen_outlet_y1() = SEN_Y + SEN_OUTLET_C + SEN_OUTLET_D / 2;
OUTLET_COL  = (SEN_OUTLET_D - PORT_BAR) / 2;
INLET_AREA  = len(PORT_ROWS_Z) * PORT_SLOT_H * (SEN_INLET_L1 - SEN_INLET_L0);
OUTLET_AREA = len(PORT_ROWS_Z) * PORT_SLOT_H * 2 * OUTLET_COL;

KH_XS        = [KH1_X, KH2_X];
KH_SCREWS    = [for (x = KH_XS, dy = [-KH_SCREW_Y, KH_SCREW_Y]) [x + KH_W / 2, KH_Y + KH_L / 2 + dy]];
// support pads under each holder, clear of the contact pins at the ends
KH_PADS      = [for (x = KH_XS, dx = [-12, 12], dy = [-14, 14]) [x + KH_W / 2 + dx, KH_Y + KH_L / 2 + dy]];
// cell centres (X), and the cell the NTC sits on: holder 1's inner cell
CELL_XS      = [for (x = KH_XS, dx = [-KH_CELL_PITCH / 2, KH_CELL_PITCH / 2]) x + KH_W / 2 + dx];
// the NTC sits on holder 2's outer cell: next to the charger chamber, the
// warmest one (battery review P3)
NTC_X        = KH2_X + KH_W / 2 + KH_CELL_PITCH / 2;
NTC_Y        = KH_Y + KH_L / 2;
CHG_HOLES_XY = [for (h = CHG_HOLES) [CHG_X + h[1], CHG_Y + h[0]]];
DOOR_POSTS   = [[DOOR_POST_X, DOOR_POST_Y_L], [CASE_W - DOOR_POST_X, DOOR_POST_Y_R]];
FB_HOLES     = [for (dx = [FB_HOLE_IN, FB_W - FB_HOLE_IN], dy = [FB_HOLE_IN, FB_H - FB_HOLE_IN])
                [FB_X + dx, FB_Y + dy]];
SGP_HOLES    = [for (dx = [SGP_HOLE_IN, SGP_S - SGP_HOLE_IN], dy = [SGP_HOLE_IN, SGP_S - SGP_HOLE_IN])
                [SGP_X + dx, SGP_Y + dy]];
SR_BODY_X    = SR_X + (SR_L - SR_HOUSING_L) / 2;
SHT_Z        = FACE + GAS_STANDOFF;
SGP_Z        = FACE + GAS_STANDOFF;
USB_CY       = FB_Y + FB_H / 2;
USB_CZ       = FB_Z + FB_PCB_T + USBC_H / 2;

// Every solid that goes into the case, as [name, x, y, z, w, h, d].  The
// collision checks at the end run over this list.
BODIES = [
    ["holder 1 + cells", KH1_X, KH_Y, KH_Z, KH_W, KH_L, KH_H],
    ["holder 2 + cells", KH2_X, KH_Y, KH_Z, KH_W, KH_L, KH_H],
    ["holder 1 pins", KH1_X, KH_Y, FACE, KH_W, KH_L, KH_STANDOFF - 0.01],
    ["holder 2 pins", KH2_X, KH_Y, FACE, KH_W, KH_L, KH_STANDOFF - 0.01],
    ["NTC bead", NTC_X - 2.0, NTC_Y - 1.85, CELL_TOP_Z, 4.0, 3.7, 2.4],
    ["#6091 charger", CHG_X, CHG_Y, CHG_Z, CHG_W, CHG_L, 1.6 + CHG_PARTS],
    ["#6091 solder side", CHG_X, CHG_Y, CHG_Z - 2.0, CHG_W, CHG_L, 2.0],
    ["BMS", BMS_X, BMS_Y, FACE + 1.0, BMS_W, BMS_L, 2.5],
    ["SEN62", SEN_X, SEN_Y, SEN_Z, SEN_HGT, SEN_LEN, SEN_WID],
    ["SEN62 plug keep-out", SEN_X - SEN_PLUG_L, SEN_Y + SEN_PLUG_L0, SEN_Z,
        SEN_PLUG_L, SEN_PLUG_L1 - SEN_PLUG_L0, SEN_WID],
    ["SHT40", SHT_X, SHT_Y, SHT_Z, SHT_L, SHT_W, SHT_T + SHT_PARTS],
    ["SGP40", SGP_X, SGP_Y, SGP_Z, SGP_S, SGP_S, SGP_T + SGP_PARTS],
    ["Sunrise", SR_X, SR_Y, SR_Z, SR_L, SR_W, SR_H],
    ["Sunrise filter clearance", SR_BODY_X, SR_Y, SR_Z + SR_H, SR_HOUSING_L, SR_W, 1.5],
    ["FireBeetle", FB_X, FB_Y, FB_Z, FB_W, FB_H, FB_PCB_T + FB_TOP_PARTS],
    ["FireBeetle solder side", FB_X, FB_Y, FB_Z - 2.0, FB_W, FB_H, 2.0],
    ["SW1 Pololu 2810", SW1_X, SW1_Y, MOD_Z, SW1_S, SW1_S, 5.0],
    ["PS1 Pololu S9V11E2A", PS1_X, PS1_Y, MOD_Z, PS1_W, PS1_L, PS1_T + 2.0],
    ["D1 LM66200", D1_X, D1_Y, MOD_Z + 3.0, D1_L, D1_W, 2.2 + 2.0],
    ["J2 USB-C power socket", J2_X, J2_Y, J2_Z, J2_L, J2_W, J2_T],
    ["J2 socket", J2_X + J2_SOCKET_X0, J2_Y + J2_SOCKET_Y0, J2_Z + J2_T / 2 - J2_SOCKET_H / 2,
        J2_SOCKET_X1 - J2_SOCKET_X0, J2_W - J2_SOCKET_Y0, J2_SOCKET_H],
    ["J2 solder side", J2_X, J2_Y, J2_Z - 1.0, J2_L, J2_SOCKET_Y0, 1.0],
    ["LED holder", LED_CX - LED_NUT_D / 2, LED_CY - LED_NUT_D / 2, FACE,
        LED_NUT_D, LED_NUT_D, LED_BEHIND],
    ["button", BTN_CX - BTN_BODY_D / 2, BTN_CY - BTN_BODY_D / 2, FACE,
        BTN_BODY_D, BTN_BODY_D, BTN_BEHIND],
    ["USB-C plug", -8, USB_CY - USBC_CLEAR_W / 2, USB_CZ - USBC_CLEAR_H / 2,
        FB_X + 8 - 0.01, USBC_CLEAR_W, USBC_CLEAR_H],
];

// ---------------------------------------------------------------------
// component mock-ups
// ---------------------------------------------------------------------

module mock(name, c) {
    for (b = BODIES) if (b[0] == name) color(c) box_at(b[1], b[2], b[3], b[4], b[5], b[6]);
}

module mock_all() {
    mock("holder 1 + cells", "DimGray");
    mock("holder 2 + cells", "DimGray");
    for (x = CELL_XS)                    // the four cells, for the picture
        color("SteelBlue") translate([x, KH_Y + (KH_L - 64.95) / 2, CELL_TOP_Z - CELL_D / 2])
            rotate([-90, 0, 0]) cylinder(d = CELL_D, h = 64.95);
    mock("NTC bead", "Gold");
    mock("#6091 charger", "Purple");
    mock("BMS", "Teal");
    mock("SEN62", "DarkSlateGray");
    color("Orange", 0.35) mock("SEN62 plug keep-out", "Orange");
    mock("SHT40", "SeaGreen");
    mock("SGP40", "Crimson");
    mock("Sunrise", "Black");
    mock("FireBeetle", "RoyalBlue");
    mock("SW1 Pololu 2810", "Green");
    mock("PS1 Pololu S9V11E2A", "Green");
    mock("D1 LM66200", "MidnightBlue");
    mock("J2 USB-C power socket", "Black");
    mock("J2 socket", "Silver");
    mock("LED holder", "Silver");
    mock("button", "Black");
}

// ---------------------------------------------------------------------
// negatives
// ---------------------------------------------------------------------

J2_CX = J2_X + (J2_SOCKET_X0 + J2_SOCKET_X1) / 2;
J2_CZ = J2_Z + J2_T / 2;
module usbc_opening() {
    translate([-1, USB_CY - USBC_CLEAR_W / 2, USB_CZ - USBC_CLEAR_H / 2])
        cube([WALL + 2, USBC_CLEAR_W, USBC_CLEAR_H]);
    // the power socket, through the top wall
    translate([J2_CX - J2_OPEN_W / 2, CASE_H - WALL - 1, J2_CZ - J2_OPEN_H / 2])
        cube([J2_OPEN_W, WALL + 2, J2_OPEN_H]);
}

module sen_ports() {
    for (z = PORT_ROWS_Z) {
        translate([CASE_W - WALL - 1, sen_inlet_y0(), z])
            cube([WALL + 2, SEN_INLET_L1 - SEN_INLET_L0, PORT_SLOT_H]);
        for (c = [0, 1])
            translate([CASE_W - WALL - 1, sen_outlet_y0() + c * (OUTLET_COL + PORT_BAR), z])
                cube([WALL + 2, OUTLET_COL, PORT_SLOT_H]);
    }
}

// Gas bay: slots in the X = 0 wall, and a field of slots in the front face
// under the boards (open holes in the first layers - nothing to bridge).
// (above the lower lid post, which merges into this wall)
GAS_SIDE_SLOTS = [for (c = [0 : 2]) POST_XY[0][1] + POST_D / 2 + 1.2 + c * (VENT_SLOT_W + 1.6)];
GAS_SIDE_ROWS  = [6.0, 10.4, 14.8, 19.2];
GAS_FRONT_COLS = [for (c = [0 : 6]) WALL + 4 + c * (FRONT_VENT_W + 2.8)];
GAS_FRONT_ROWS = [for (r = [0 : 8]) UP_Y0 + 3 + r * 4.8];
module gas_bay_vents() {
    for (y = GAS_SIDE_SLOTS, z = GAS_SIDE_ROWS)
        translate([-1, y, z]) cube([WALL + 2, VENT_SLOT_W, VENT_SLOT_H]);
    for (x = GAS_FRONT_COLS, y = GAS_FRONT_ROWS)
        translate([x, y, -1]) cube([FRONT_VENT_W, FRONT_VENT_H, FACE + 2]);
}

module panel_holes() {
    for (h = [[LED_CX, LED_CY, LED_HOLE_D], [BTN_CX, BTN_CY, BTN_HOLE_D]]) {
        translate([h[0], h[1], -1]) cylinder(d = h[2], h = FACE + 2);
        // spot face from the inside, so the nut sees a flat PANEL_T
        translate([h[0], h[1], PANEL_T]) cylinder(d = PANEL_SPOT_D, h = FACE);
    }
}

// Compartment: pressure relief in the bottom wall (a venting cell must not
// pressurise a sealed box).  Charger chamber: air in through the bottom wall,
// out through the right-hand wall high up - a chimney past the #6091.
BATC_VENT_XS = [for (i = [0 : 3]) KH1_X + 6 + i * 20];
CHG_VENT_XS  = [for (i = [0 : 2]) CHG_WALL_X + CHG_WALL_T + 2.0 + i * (CHG_VENT_W + 2.2)];
CHG_VENT_YS  = [for (i = [0 : 3]) DOOR_POST_Y_R + DOOR_POST_D / 2 + 1.5 + i * (CHG_VENT_SW + 2.2)];
CHG_VENT_ZS  = [for (i = [0 : 5]) 5.0 + i * (CHG_VENT_H + 1.2)];
CHG_VENT_LOW  = len(CHG_VENT_XS) * len(CHG_VENT_ZS) * CHG_VENT_W * CHG_VENT_H;
CHG_VENT_HIGH = len(CHG_VENT_YS) * len(CHG_VENT_ZS) * CHG_VENT_SW * CHG_VENT_H;
module batc_vents() {
    for (x = BATC_VENT_XS)
        translate([x, -1, WALL_TOP_Z - 6]) cube([BATC_VENT_W, WALL + 2, BATC_VENT_H]);
    for (x = CHG_VENT_XS, z = CHG_VENT_ZS)
        translate([x, -1, z]) cube([CHG_VENT_W, WALL + 2, CHG_VENT_H]);
    for (y = CHG_VENT_YS, z = CHG_VENT_ZS)
        translate([CASE_W - WALL - 1, y, z]) cube([WALL + 2, CHG_VENT_SW, CHG_VENT_H]);
}

// ---------------------------------------------------------------------
// FRONT SHELL
// ---------------------------------------------------------------------

module front_shell_solid() {
    difference() {
        slab(CASE_W, CASE_H, FRONT_SHELL_D, CORNER_R, CHAMFER_BED);
        translate([0, 0, FACE]) linear_extrude(FRONT_SHELL_D) inner2d();
    }
}

module sen62_cradle() {
    w = 2.0;
    // rails above and below, 0.4 mm into the wall so the union is manifold
    box_at(SEN_X, SEN_Y - FIT_SLIDE - w, FACE, CASE_W - WALL - SEN_X + 0.4, w, WALL_TOP_Z - FACE);
    box_at(SEN_X, SEN_Y + SEN_LEN + FIT_SLIDE, FACE, CASE_W - WALL - SEN_X + 0.4, w, WALL_TOP_Z - FACE);
    // stops behind the connector face, clear of the plug
    for (yy = [[SEN_Y - FIT_SLIDE - w, SEN_Y + SEN_PLUG_L0 - 1],
               [SEN_Y + SEN_PLUG_L1 + 1, SEN_Y + SEN_LEN + FIT_SLIDE + w]])
        box_at(SEN_X - FIT_SLIDE - w, yy[0], FACE, w, yy[1] - yy[0], WALL_TOP_Z - FACE);
    for (x = [SEN_X + 4, SEN_X + SEN_HGT - 6])
        box_at(x, SEN_Y + 4, FACE, 2, SEN_LEN - 8, SEN_Z - FACE);
    // gasket frames around each window
    xg = CASE_W - WALL - GASKET_RIB_H;
    zlo = PORT_ROWS_Z[0] - 1.5;
    zhi = PORT_ROWS_Z[len(PORT_ROWS_Z) - 1] + PORT_SLOT_H + 1.5;
    for (yy = [[sen_inlet_y0() - 1.5, sen_inlet_y1() + 1.5],
               [sen_outlet_y0() - 1.5, sen_outlet_y1() + 1.5]])
        difference() {
            box_at(xg, yy[0] - GASKET_RIB_W, zlo - GASKET_RIB_W, GASKET_RIB_H + 0.4,
                   yy[1] - yy[0] + 2 * GASKET_RIB_W,
                   min(zhi + GASKET_RIB_W, WALL_TOP_Z) - (zlo - GASKET_RIB_W));
            box_at(xg - 0.1, yy[0], zlo, GASKET_RIB_H + 0.3, yy[1] - yy[0], zhi - zlo);
        }
}

module gas_bay_mounts() {
    // SHT40: four corner pads and a snug fence (Seeed publish no hole drawing)
    for (dx = [0, SHT_L - 4], dy = [0, SHT_W - 4])
        box_at(SHT_X + dx, SHT_Y + dy, FACE, 4, 4, GAS_STANDOFF);
    fence(SHT_X, SHT_Y, SHT_L, SHT_W, FACE, SHT_Z + SHT_T + 0.8);
    // SGP40: four posts for M2.5 self-tapping screws
    for (h = SGP_HOLES) post(h[0], h[1], FACE, SGP_Z, 5.0);
    // Sunrise: a pedestal under the housing (the pins at both ends hang
    // free), and four corner guides around the housing
    box_at(SR_BODY_X + 3, SR_Y + 2, FACE, SR_HOUSING_L - 6, SR_W - 4, SR_Z - FACE);
    for (dx = [-1.2 - FIT_LOOSE, SR_HOUSING_L + FIT_LOOSE], dy = [0, SR_W - 3])
        box_at(SR_BODY_X + dx, SR_Y + dy, FACE, 1.2, 3, SR_Z + 4 - FACE);
}

module electronics_mounts() {
    for (h = FB_HOLES) post(h[0], h[1], FACE, FB_Z, 4.6);
    // pads + fences for the two Pololu boards
    for (m = [[SW1_X, SW1_Y, SW1_S, SW1_S], [PS1_X, PS1_Y, PS1_W, PS1_L]]) {
        box_at(m[0] + 2, m[1] + 2, FACE, m[2] - 4, m[3] - 4, MOD_Z - FACE);
        fence(m[0], m[1], m[2], m[3], FACE, MOD_Z + POCKET_H);
    }
    for (h = D1_HOLES) post(D1_X + h[0], D1_Y + h[1], FACE, MOD_Z + 3.0, 5.0);
    for (h = J2_HOLES) post(J2_X + h[0], J2_Y + h[1], FACE, J2_Z, 5.0);
    // battery holders: two screw bosses and four pads each, pins hang free
    for (h = KH_SCREWS) post(h[0], h[1], FACE, KH_Z, 7.0);
    for (h = KH_PADS) post(h[0], h[1], FACE, KH_Z, 5.0);
    // charger chamber: its wall, the #6091 on four posts, the BMS on a pad
    box_at(CHG_WALL_X, KH_Y - 1.2 - 0.4, FACE, CHG_WALL_T, BATC_Y1 - KH_Y + 1.2 + 0.8,
           WALL_TOP_Z - FACE);
    for (h = CHG_HOLES_XY) post(h[0], h[1], FACE, CHG_Z, 5.0);
    box_at(BMS_X - 0.5, BMS_Y + 3, FACE, BMS_W + 1, BMS_L - 6, 1.0);
    fence(BMS_X, BMS_Y, BMS_W, BMS_L, FACE, FACE + 2.5, FIT_LOOSE);
}

module front_shell() {
    front_shell_body();
    // the partition's top between the lid's and the door's lips, up to the
    // seam, and into the outer wall so the rebate has no pocket there
    difference() {
        box_at(WALL - LAP_WALL - 0.3, BATC_Y1 + LAP_WALL, WALL_TOP_Z - 0.01,
               INNER_W + 2 * (LAP_WALL + 0.3), BATC_PART_T - 2 * LAP_WALL, LAP_DEPTH - 0.2);
        box_at(BATC_NOTCH_X0, BATC_Y1 - 0.1, WALL_TOP_Z - 1,
               BATC_NOTCH_W, BATC_PART_T + 0.2, LAP_DEPTH + 2);
    }
}

module front_shell_body() {
    difference() {
        union() {
            front_shell_solid();
            for (p = POST_XY) post(p[0], p[1], FACE, WALL_TOP_Z);
            for (p = DOOR_POSTS) post(p[0], p[1], FACE, WALL_TOP_Z, DOOR_POST_D);
            // battery partition: full height, its top kept as a 1.8 mm core
            // between the lid's and the door's lips
            box_at(WALL - 0.4, BATC_Y1, FACE, INNER_W + 0.8, BATC_PART_T, WALL_TOP_Z - FACE);
            // gas bay: inner side wall and top wall, up to the wall top
            box_at(GAS_X1, UP_Y0 - 0.4, FACE, GAS_WALL_T, GAS_Y1 - UP_Y0 + GAS_WALL_T + 0.4,
                   WALL_TOP_Z - FACE);
            box_at(WALL - 0.4, GAS_Y1, FACE, GAS_X1 - WALL + 0.4 + 0.01, GAS_WALL_T, WALL_TOP_Z - FACE);
            gas_bay_mounts();
            sen62_cradle();
            electronics_mounts();
        }
        // rebate for the lid's and the door's lips, cut through everything
        translate([0, 0, WALL_TOP_Z]) linear_extrude(LAP_DEPTH + 1) lip2d();
        usbc_opening();
        sen_ports();
        gas_bay_vents();
        panel_holes();
        batc_vents();
        // lead notch through the partition, at the top
        box_at(BATC_NOTCH_X0, BATC_Y1 - 0.1, WALL_TOP_Z - BATC_NOTCH_H,
               BATC_NOTCH_W, BATC_PART_T + 0.2, BATC_NOTCH_H + LAP_DEPTH + 1);
        // heat-set inserts, entered from the seam side
        for (p = concat(POST_XY, DOOR_POSTS))
            translate([p[0], p[1], WALL_TOP_Z - INSERT_DEPTH])
                cylinder(d = INSERT_D, h = INSERT_DEPTH + 0.1);
        // self-tapping cores
        for (h = SGP_HOLES) translate([h[0], h[1], SGP_Z - 4.5]) cylinder(d = SELFTAP_M25, h = 5);
        for (h = KH_SCREWS) translate([h[0], h[1], FACE + 0.8]) cylinder(d = SELFTAP_M3, h = KH_STANDOFF);
        for (h = CHG_HOLES_XY) translate([h[0], h[1], CHG_Z - 4.5]) cylinder(d = SELFTAP_M25, h = 5);
        // wire notch in the charger chamber's wall, at the top
        box_at(CHG_WALL_X - 0.1, CHG_NOTCH_Y0, WALL_TOP_Z - 6, CHG_WALL_T + 0.2, CHG_NOTCH_W, 7);
        for (h = FB_HOLES) translate([h[0], h[1], FB_Z - 4.5]) cylinder(d = SELFTAP_M2, h = 5);
        for (h = D1_HOLES) translate([D1_X + h[0], D1_Y + h[1], MOD_Z]) cylinder(d = SELFTAP_M2, h = 4);
        for (h = J2_HOLES) translate([J2_X + h[0], J2_Y + h[1], J2_Z - 5]) cylinder(d = SELFTAP_M2, h = 5.1);
        // branding on the compartment's front, engraved, mirrored (seen from -Z)
        translate([CASE_W / 2, KH_Y + KH_L / 2, -0.01]) mirror([1, 0, 0])
            linear_extrude(0.7)
                text("AIR CHECK", size = 6.0, halign = "center", valign = "center",
                     font = "Helvetica:style=Bold", spacing = 1.25);
    }
}

// ---------------------------------------------------------------------
// LID and DOOR.  Modelled with device X/Y and z = 0 at the outer back face;
// mirrored in Z they drop into the assembly.
// ---------------------------------------------------------------------

function lidz(z) = CASE_D - z;

// y0..y1: the plate's extent; cy0..cy1: the cavity its lip rings
module back_plate(y0, y1, cy0, cy1) {
    difference() {
        union() {
            intersection() {
                slab(CASE_W, CASE_H, BACK_SHELL_D, CORNER_R, CHAMFER_BED);
                translate([0, 0, -1]) linear_extrude(BACK_SHELL_D + 2) band2d(y0, y1);
            }
            translate([0, 0, BACK_SHELL_D - 1.0]) linear_extrude(LAP_DEPTH + 1.0)
                difference() {
                    intersection() { lip2d(); band2d(cy0 - LAP_WALL, cy1 + LAP_WALL); }
                    intersection() { inner2d(); band2d(cy0, cy1); }
                }
        }
    }
}

module back_shell() {
    difference() {
        union() {
            back_plate(PLATE_SPLIT_Y + PLATE_GAP / 2, CASE_H, UP_Y0, CASE_H);
            for (p = POST_XY)
                post(p[0], p[1], FACE - 0.01, lidz(WALL_TOP_Z) - 0.2, POST_D);
            // ribs that meet the gas bay walls (0.2 mm gap, no foam)
            box_at(GAS_X1, UP_Y0, FACE - 0.01, GAS_WALL_T, GAS_Y1 - UP_Y0 + GAS_WALL_T,
                   lidz(WALL_TOP_Z) - 0.2 - FACE);
            box_at(WALL, GAS_Y1, FACE - 0.01, GAS_X1 - WALL, GAS_WALL_T,
                   lidz(WALL_TOP_Z) - 0.2 - FACE);
        }
        for (p = POST_XY) {
            translate([p[0], p[1], -1]) cylinder(d = SCREW_D, h = CASE_D);
            translate([p[0], p[1], -0.01]) cylinder(d = SCREW_HEAD_D, h = SCREW_HEAD_DEPTH);
        }
        keyholes();
    }
}

module door() {
    difference() {
        union() {
            back_plate(0, PLATE_SPLIT_Y - PLATE_GAP / 2, WALL, BATC_Y1);
            for (p = DOOR_POSTS)
                post(p[0], p[1], FACE - 0.01, lidz(WALL_TOP_Z) - 0.2, DOOR_POST_D);
            // cell retainers: a rib over each cell, 1 mm above it, so no cell
            // can leave its holder in a fall (review E1, E2); over the NTC's
            // cell the rib becomes a finger that holds the bead (E3)
            for (x = CELL_XS)
                for (yy = (x == NTC_X) ? [[KH_Y + 10, NTC_Y - 6], [NTC_Y + 6, KH_Y + KH_L - 10]]
                                        : [[KH_Y + 10, KH_Y + KH_L - 10]])
                    box_at(x - 1.5, yy[0], FACE - 0.01, 3.0, yy[1] - yy[0],
                           lidz(CELL_TOP_Z + 1.0) - FACE);
            difference() {
                box_at(NTC_X - 3.0, NTC_Y - 4.5, FACE - 0.01, 6.0, 9.0,
                       lidz(CELL_TOP_Z + 2.4 - 0.3) - FACE);
                // a seat for the bead
                box_at(NTC_X - 2.2, NTC_Y - 2.0, lidz(CELL_TOP_Z + 2.4 - 0.3) - 1.2,
                       4.4, 4.0, 2.0);
            }
        }
        for (p = DOOR_POSTS) {
            translate([p[0], p[1], -1]) cylinder(d = SCREW_D, h = CASE_D);
            translate([p[0], p[1], -0.01]) cylinder(d = SCREW_HEAD_D, h = SCREW_HEAD_DEPTH);
        }
        // the label (Power-Standard review S1), engraved on the outside; the
        // door is printed face down, so the text is mirrored like the front's
        for (l = [["LiFePO4 3,2 V", 5.0, 16], ["nur 4 x AER18650m2A2", 4.0, 7],
                  ["gleiche Zellen, max. 20 mV", 3.6, -1], ["Polaritaet: siehe Halter", 3.6, -9],
                  ["Laden nur 0-45 C", 3.6, -17]])
            translate([CASE_W / 2, PLATE_SPLIT_Y / 2 + l[2], -0.01]) mirror([1, 0, 0])
                linear_extrude(0.6)
                    text(l[0], size = l[1], halign = "center", valign = "center",
                         font = "Helvetica:style=Bold");
    }
}

module keyholes() {
    for (x = KEYHOLE_X) {
        y = KEYHOLE_Y;
        translate([x, y, -1]) cylinder(d = KEYHOLE_D_BIG, h = FACE + 2);
        translate([x, y - KEYHOLE_LEN, -1]) cylinder(d = KEYHOLE_D_SMALL, h = FACE + 2);
        translate([x - KEYHOLE_D_SMALL / 2, y - KEYHOLE_LEN, -1])
            cube([KEYHOLE_D_SMALL, KEYHOLE_LEN, FACE + 2]);
        hull() for (dy = [0, -KEYHOLE_LEN])
            translate([x, y + dy, FACE - 1.4]) cylinder(d = KEYHOLE_CSK_D, h = 1.5);
    }
}

module desk_stand() {
    tilt = 15;
    w = 90;
    d = 56;
    difference() {
        linear_extrude(w)
            polygon([[0, 0], [d, 0], [d, 6], [10, 6 + (d - 10) * tan(tilt)],
                     [0, 6 + (d - 10) * tan(tilt)]]);
        rotate([0, -tilt, 0]) translate([14, 6, -1])
            cube([CASE_D + 2 * FIT_LOOSE, 40, w + 2]);
        translate([8, -1, 10]) cube([d - 18, 4, w - 20]);
    }
}

// ---------------------------------------------------------------------
// assembly + sections
// ---------------------------------------------------------------------

module lid_in_place()  { translate([0, 0, CASE_D]) mirror([0, 0, 1]) back_shell(); }
module door_in_place() { translate([0, 0, CASE_D]) mirror([0, 0, 1]) door(); }

module assembly() {
    color("Gainsboro", 0.55) front_shell();
    color("Gainsboro", 0.55) lid_in_place();
    color("Wheat", 0.8) door_in_place();
    mock_all();
}

module open_view() {
    color("Gainsboro") front_shell();
    mock_all();
}

module exploded(gap = 30) {
    color("Gainsboro") front_shell();
    translate([0, 0, gap * 0.6]) mock_all();
    translate([0, 0, gap * 1.6]) color("Wheat") door_in_place();
    translate([0, 0, gap * 2.0]) color("Gainsboro") lid_in_place();
}

// ---------------------------------------------------------------------
// numeric self-checks - these run on every render
// ---------------------------------------------------------------------

echo(str("CASE  ", CASE_W, " x ", CASE_H, " x ", CASE_D, " mm"));
echo(str("compartment Y ", WALL, " .. ", BATC_Y1, "   electronics from Y ", UP_Y0));
echo(str("gas bay X ", WALL, " .. ", GAS_X1, "  Y ", UP_Y0, " .. ", GAS_Y1));
echo(str("port open area: inlets ", INLET_AREA, " mm2 (>= 56), outlet ", OUTLET_AREA,
         " mm2 (>= 148)"));
GAS_OPEN = len(GAS_SIDE_SLOTS) * len(GAS_SIDE_ROWS) * VENT_SLOT_W * VENT_SLOT_H
         + len(GAS_FRONT_COLS) * len(GAS_FRONT_ROWS) * FRONT_VENT_W * FRONT_VENT_H;
echo(str("gas bay open area ", GAS_OPEN, " mm2"));

function overlap(a, b) =
    a[1] < b[1] + b[4] && b[1] < a[1] + a[4] &&
    a[2] < b[2] + b[5] && b[2] < a[2] + a[5] &&
    a[3] < b[3] + b[6] && b[3] < a[3] + a[6];
function inside_case(b) =
    b[1] >= WALL - 0.001 && b[1] + b[4] <= CASE_W - WALL + 0.001 &&
    b[2] >= WALL - 0.001 && b[2] + b[5] <= CASE_H - WALL + 0.001 &&
    b[3] >= FACE - 0.001 && b[3] + b[6] <= LID_IN_Z + 0.001;
function hits_post(p, b, r) =
    p[0] + r > b[1] && p[0] - r < b[1] + b[4] && p[1] + r > b[2] && p[1] - r < b[2] + b[5];
// a body is in the gas bay / the compartment / the electronics zone
function in_gas(b) = b[1] >= WALL && b[1] + b[4] <= GAS_X1 && b[2] >= UP_Y0 && b[2] + b[5] <= GAS_Y1;
function in_batc(b) = b[2] + b[5] <= BATC_Y1;
function in_elec(b) = b[2] >= UP_Y0 && !(b[1] < GAS_X1 + GAS_WALL_T && b[2] < GAS_Y1 + GAS_WALL_T);

// 1. no two bodies overlap
for (i = [0 : len(BODIES) - 1], j = [0 : len(BODIES) - 1])
    if (i < j && !(BODIES[i][0] == "SEN62" && BODIES[j][0] == "SEN62 plug keep-out")
              && !(BODIES[i][0] == "Sunrise" && BODIES[j][0] == "Sunrise filter clearance")
              && !(BODIES[i][0] == "FireBeetle" && BODIES[j][0] == "FireBeetle solder side")
              && !(BODIES[i][0] == "J2 USB-C power socket" && BODIES[j][0] == "J2 socket")
              && !(BODIES[i][0] == "J2 USB-C power socket" && BODIES[j][0] == "J2 solder side"))
        assert(!overlap(BODIES[i], BODIES[j]), str(BODIES[i][0], " collides with ", BODIES[j][0]));
// 2. everything inside the case (the USB plug excepted), and in its zone
for (b = BODIES) if (b[0] != "USB-C plug") assert(inside_case(b), str(b[0], " is outside the case"));
for (n = ["SHT40", "SGP40", "Sunrise", "Sunrise filter clearance"])
    for (b = BODIES) if (b[0] == n) assert(in_gas(b), str(n, " is not inside the gas bay"));
for (b = BODIES) if (b[0] == "holder 1 + cells" || b[0] == "holder 2 + cells" || b[0] == "#6091 charger"
                     || b[0] == "BMS") assert(in_batc(b), str(b[0], " is not in the compartment"));
for (n = ["FireBeetle", "SW1 Pololu 2810", "PS1 Pololu S9V11E2A", "D1 LM66200", "LED holder",
          "button", "SEN62", "SEN62 plug keep-out",
          "J2 USB-C power socket", "J2 socket"])
    for (b = BODIES) if (b[0] == n) assert(in_elec(b), str(n, " is not in the electronics zone"));
// 3. posts against bodies
for (p = POST_XY, b = BODIES) if (b[0] != "USB-C plug")
    assert(!hits_post(p, b, POST_D / 2) || b[3] >= WALL_TOP_Z, str("lid post ", p, " hits ", b[0]));
for (p = DOOR_POSTS, b = BODIES)
    assert(!hits_post(p, b, DOOR_POST_D / 2), str("door post ", p, " hits ", b[0]));
// 4. SEN62 and its ports
assert(SEN_PLUG_L0 < 17.2 && SEN_PLUG_L1 > 29.2 && SEN_CONN_W1 <= SEN_WID,
       "the plug keep-out does not cover the connector (STEP: 17.2..29.2 x 16.3..21.8)");
assert(SEN_Y - FIT_SLIDE - 2 > POST_XY[1][1] + POST_D / 2, "the SEN62 cradle hits the lower post");
assert(SEN_Y + SEN_LEN + FIT_SLIDE + 2 < POST_XY[3][1] - POST_D / 2, "the SEN62 cradle hits the upper post");
assert(INLET_AREA >= 56 && OUTLET_AREA >= 148, "port open area below Sensirion's minimum");
assert(PORT_ROWS_Z[len(PORT_ROWS_Z) - 1] + PORT_SLOT_H < WALL_TOP_Z, "a port slot runs into the lap joint");
for (z = PORT_ROWS_Z)
    assert(z >= SEN_Z && z + PORT_SLOT_H <= SEN_Z + SEN_WID, "a port row misses the port face");
assert(OUTLET_COL <= 12, "outlet slot roof is too long a bridge");
// 5. gas bay
assert(SR_Y + SR_W + 1.0 <= GAS_Y1 && SR_X >= WALL + 1.0, "Sunrise closer than 1 mm to a wall (ANO4947)");
assert(SR_Z + SR_H + 1.5 <= LID_IN_Z, "Sunrise filter closer than 1.5 mm to the lid (ANO4947)");
assert(SHT_Y + SHT_W < SR_Y && SHT_X + SHT_L < SGP_X, "SHT40 too close to its neighbours");
assert(GAS_OPEN >= 200, "gas bay under-ventilated");
for (y = GAS_SIDE_SLOTS)
    assert(y >= UP_Y0 && y + VENT_SLOT_W <= GAS_Y1, "a side vent is outside the gas bay");
for (x = GAS_FRONT_COLS, y = GAS_FRONT_ROWS)
    assert(x + FRONT_VENT_W <= GAS_X1 && y + FRONT_VENT_H <= GAS_Y1, "a front vent is outside the gas bay");
for (z = GAS_SIDE_ROWS) assert(z > FACE && z + VENT_SLOT_H < WALL_TOP_Z, "side vent row out of range");
// 6. battery compartment
assert(CELL_TOP_Z + 2.4 < LID_IN_Z - 2, "no room over the cells for the retainers and the NTC");
assert(KH1_X > DOOR_POSTS[0][0] + DOOR_POST_D / 2, "the left door post hits holder 1");
for (p = DOOR_POSTS) assert(p[1] - DOOR_POST_D / 2 > WALL && p[1] + DOOR_POST_D / 2 < BATC_Y1,
                            "a door post is outside the compartment");
for (y = CHG_VENT_YS) assert(y > DOOR_POSTS[1][1] + DOOR_POST_D / 2 || y + CHG_VENT_SW < DOOR_POSTS[1][1] - DOOR_POST_D / 2,
                             "a chamber vent cuts into the right door post");
assert(KH2_X + KH_W < CHG_WALL_X, "holder 2 reaches into the charger chamber");
// Power-Standard review E4: the charger >= 5 mm from every wall, vents low and high
assert(CHG_X - (CHG_WALL_X + CHG_WALL_T) >= 5 && CASE_W - WALL - (CHG_X + CHG_W) >= 5 - 0.001
       && CHG_Y - WALL >= 5 - 0.001 && BATC_Y1 - (CHG_Y + CHG_L) >= 5,
       "the #6091 is closer than 5 mm to a wall");
assert(CHG_VENT_LOW >= CHG_VENT_MIN_MM2 && CHG_VENT_HIGH >= CHG_VENT_MIN_MM2,
       str("charger chamber vents: ", CHG_VENT_LOW, " mm2 low, ", CHG_VENT_HIGH,
           " mm2 high; >= ", CHG_VENT_MIN_MM2, " each"));
echo(str("charger chamber vents: ", CHG_VENT_LOW, " mm2 in the bottom wall, ", CHG_VENT_HIGH,
         " mm2 high in the side wall"));
for (y = CHG_VENT_YS) assert(y > CHG_Y + CHG_L && y + CHG_VENT_SW < BATC_Y1 - 1, "a chamber vent misses the chamber top");
for (x = CHG_VENT_XS) assert(x > CHG_WALL_X + CHG_WALL_T && x + CHG_VENT_W < CASE_W - WALL - 3.6,
                             "a chamber vent misses the chamber or runs into the corner radius");
for (z = CHG_VENT_ZS) assert(z > FACE && z + CHG_VENT_H < WALL_TOP_Z, "a chamber vent row is out of range");
// E5: >= 30 mm from the charger to the gas bay's sensors
function gap2d(a, b) = let(dx = max(0, max(a[1] - (b[1] + b[4]), b[1] - (a[1] + a[4]))),
                           dy = max(0, max(a[2] - (b[2] + b[5]), b[2] - (a[2] + a[5]))))
                       sqrt(dx * dx + dy * dy);
for (a = BODIES, b = BODIES)
    if (a[0] == "#6091 charger" && (b[0] == "SHT40" || b[0] == "SGP40" || b[0] == "Sunrise"))
        assert(gap2d(a, b) >= 30, str("the charger is closer than 30 mm to the ", b[0]));
echo(str("charger to SHT40 / SGP40 / Sunrise: ",
         [for (a = BODIES, b = BODIES) if (a[0] == "#6091 charger" &&
             (b[0] == "SHT40" || b[0] == "SGP40" || b[0] == "Sunrise")) gap2d(a, b)], " mm"));
assert(BATC_NOTCH_X0 > GAS_X1 + GAS_WALL_T && BATC_NOTCH_X0 + BATC_NOTCH_W < SEN_X - SEN_PLUG_L,
       "the lead notch opens into the gas bay or the SEN62");
assert(BATC_PART_T - 2 * LAP_WALL >= 1.6, "the partition core is too thin");
assert(PLATE_SPLIT_Y > BATC_Y1 + LAP_WALL && PLATE_SPLIT_Y < UP_Y0 - LAP_WALL,
       "the lid / door split is not over the partition");
// 7. front panel
assert(LED_CX - LED_NUT_D / 2 > GAS_X1 + GAS_WALL_T && BTN_CX - BTN_BODY_D / 2 > GAS_X1 + GAS_WALL_T,
       "LED or button inside the gas bay");
assert(FACE + BTN_BEHIND < LID_IN_Z - 1, "the button does not fit the depth");
// 8. USB-C
assert(USB_CZ - USBC_CLEAR_H / 2 > FACE && USB_CZ + USBC_CLEAR_H / 2 < WALL_TOP_Z, "USB-C opening out of range");
assert(USB_CY - USBC_CLEAR_W / 2 > GAS_Y1 + GAS_WALL_T, "the USB-C opening cuts into the gas bay");
// 8b. USB-C power socket: opening inside the wall, clear of the top posts
assert(J2_CZ - J2_OPEN_H / 2 > FACE && J2_CZ + J2_OPEN_H / 2 < WALL_TOP_Z, "power socket opening out of range");
assert(J2_CX - J2_OPEN_W / 2 > POST_XY[2][0] + POST_D / 2 && J2_CX + J2_OPEN_W / 2 < POST_XY[3][0] - POST_D / 2,
       "power socket opening runs into a lid post");
assert(J2_Y + J2_W <= CASE_H - WALL, "the power socket board pokes into the top wall");
// 9. keyholes
for (x = KEYHOLE_X)
    assert(!(x > WALL && x < GAS_X1 + GAS_WALL_T && KEYHOLE_Y < GAS_Y1 + GAS_WALL_T),
           "a keyhole opens into the gas bay");
// 10. printability
assert(WALL >= 4 * NOZZLE, "wall thinner than four extrusions");
assert(VENT_SLOT_W <= 12 && USBC_CLEAR_W <= 13, "a horizontal hole roof is too long a bridge");
assert(WALL_TOP_Z - INSERT_DEPTH > FACE, "insert pocket breaks through the front face");

// ---------------------------------------------------------------------
// export dispatcher
// ---------------------------------------------------------------------

if (part == "front")         front_shell();
else if (part == "back")     translate([CASE_W, 0, 0]) mirror([1, 0, 0]) back_shell();
else if (part == "door")     translate([CASE_W, 0, 0]) mirror([1, 0, 0]) door();
else if (part == "stand")    desk_stand();
else if (part == "assembly") assembly();
else if (part == "exploded") exploded();
else if (part == "open")     open_view();
else if (part == "sec_z")
    projection(cut = true) translate([0, 0, -(FACE + 8)]) assembly();
else if (part == "sec_y")
    projection(cut = true) rotate([90, 0, 0]) translate([0, -(SR_Y + SR_W / 2), 0]) assembly();
else if (part == "sec_x")
    projection(cut = true) rotate([0, 90, 0]) translate([-(SR_X + SR_L / 2), 0, 0]) assembly();
