// =====================================================================
// 3D Printing AIR CHECK - enclosure parameters (v1.4)
// ---------------------------------------------------------------------
// Every dimension that matters lives here.  Nothing downstream may use a
// bare number that is not derived from one of these.
//
// Coordinate frame used throughout the model:
//   X : 0 .. CASE_W   left -> right, seen from the BACK (from inside the case)
//   Y : 0 .. CASE_H   bottom -> top
//   Z : 0 .. CASE_D   outer front face -> outer back face
//
// Seen from the front X runs right -> left: the USB-C port and the gas bay
// vents at X = 0 are on the RIGHT-hand side of the finished device, the
// SEN62 and its air ports on the LEFT.  Text on the front face is mirrored in
// the model so that it reads correctly on the part.
//
// Three zones, walled off from each other (EDR-21, EDR-17):
//   battery compartment   bottom, Y 0 .. BATC_Y1, own screwed door at the back:
//                         4 x LiFePO4 18650 in two Keystone 1049 holders, and a
//                         vented charger chamber with the #6091 and the BMS
//   gas bay               above it at X = 0: Sunrise, SGP40, SHT40, vented
//                         through the X = 0 wall and the front face
//   electronics           the rest: FireBeetle, power modules, SEN62 (sealed
//                         to its own side-wall ports)
//
// Component dimensions are from the manufacturer documents listed in
// docs/ENGINEERING_DECISIONS.md (EDR-19 has the module table).
// =====================================================================

// ---------- printing ------------------------------------------------
NOZZLE            = 0.4;
LAYER             = 0.2;
FIT_SLIDE         = 0.25;   // sliding fit (part goes in, stays put)
FIT_LOOSE         = 0.40;   // loose fit (drop-in)
CHAMFER_BED       = 0.6;
$fn               = 64;

// ---------- overall case ---------------------------------------------
CASE_W            = 136.0;
CASE_H            = 174.0;
CASE_D            =  34.0;
WALL              =   2.4;   // perimeter wall, 6 x 0.4 mm
FACE              =   2.4;   // front and back faces
CORNER_R          =   6.0;

// Deep front shell; the back is two flat plates: the lid over the
// electronics and the battery door over the compartment.
FRONT_SHELL_D     =  30.0;
BACK_SHELL_D      = CASE_D - FRONT_SHELL_D;   // = 4.0
LAP_DEPTH         =   3.0;
LAP_WALL          =   1.1;
PLATE_GAP         =   0.3;   // between lid and door, over the partition

// ---------- fasteners -------------------------------------------------
INSERT_D          =   3.84;  // M2.5 heat-set insert hole in PETG
INSERT_DEPTH      =   6.0;
SCREW_D           =   2.8;   // M2.5 clearance
SCREW_HEAD_D      =   5.0;
SCREW_HEAD_DEPTH  =   2.0;
POST_D            =   9.0;   // lid screw posts
DOOR_POST_D       =   7.0;   // door screw posts
SELFTAP_M25       =   2.1;   // M2.5 self-tapping core hole
SELFTAP_M2        =   1.6;   // M2 self-tapping core hole
SELFTAP_M3        =   2.5;   // M3 self-tapping core hole (battery holders)

// ---------- battery compartment: 1S4P LiFePO4 (EDR-21) ------------------------
// Keystone 1049 (catalogue p. 29): 77.1 x 39.8 mm, 18.0 mm to the top of an
// inserted cell, housing wall ~14.8 mm; cells 19.1 mm apart; 4 contact pins
// d1.2 at (+-9.55, +-35.8) holder-local, ~3.6 mm long; 2 x d3.2 screw holes
// through the floor at (0, +-27.6); two d3.7 locating pegs.  Cells lie along
// Y.  AER18650m2A2: d18.5 x 64.95 mm.
KH_L              =  77.1;           // along Y (cell axis)
KH_W              =  39.8;           // along X
KH_H              =  18.0;           // floor to the top of an inserted cell
KH_WALL_H         =  14.8;
KH_CELL_PITCH     =  19.1;
KH_SCREW_Y        =  27.6;           // +- from the holder centre, along Y
KH_PIN_Y          =  35.8;
KH_STANDOFF       =   5.0;           // pins and their solder joints underneath
KH_GAP            =   4.0;           // between the holders: the NTC's cell
CELL_D            =  18.5;
KH1_X             =  10.0;           // clear of the left door post
KH2_X             = KH1_X + KH_W + KH_GAP;
KH_Y              = WALL + 1.2;
KH_Z              = FACE + KH_STANDOFF;
CELL_TOP_Z        = KH_Z + KH_H;
BATC_Y1           = KH_Y + KH_L + 1.2;                   // compartment inner top
BATC_PART_T       =   4.0;   // partition: fire and air separation
UP_Y0             = BATC_Y1 + BATC_PART_T;               // electronics zone floor
PLATE_SPLIT_Y     = BATC_Y1 + BATC_PART_T / 2;           // lid / door split line
// charger chamber: its own wall, vents in the bottom and the right-hand wall
CHG_WALL_X        = KH2_X + KH_W + 1.0;
CHG_WALL_T        =   2.0;
// Adafruit #6091 (board file rev B1): 31.75 x 25.40 mm, 4 x d2.5 holes 2.54 mm
// in, JST PH BATT and LOAD on one long edge (5.5 mm tall), USB-C on the other
// (unused).  Turned so the 31.75 mm edge runs along Y and the JSTs face the
// holders; >= 5 mm from every wall (review checklist E4).
CHG_L             =  31.75;          // along Y
CHG_W             =  25.40;          // along X
CHG_PARTS         =   5.5;
CHG_HOLES         = [[2.54, 2.54], [29.21, 2.54], [2.54, 22.86], [29.21, 22.86]];  // (along L, along W)
CHG_X             = CASE_W - WALL - 5.0 - CHG_W;
CHG_Y             = WALL + 5.0;
CHG_Z             = FACE + 5.0;
// eremit HY2112 BMS strip, "3 x 30 mm": vertical along Y, next to the
// chamber wall above the charger
BMS_L             =  30.0;
BMS_W             =   4.0;
BMS_X             = CHG_WALL_X + CHG_WALL_T + 3.0;
BMS_Y             = CHG_Y + CHG_L + 6.0;
// door screws: posts in the side walls
DOOR_POST_X       = WALL + DOOR_POST_D / 2 - 0.3;
DOOR_POST_Y_L     = KH_Y + KH_L / 2;
DOOR_POST_Y_R     = CHG_Y + CHG_L + 5.0;     // between the charger and the vents
// lead notches: partition (cells -> electronics) and chamber wall (cells -> charger)
BATC_NOTCH_X0     =  CHG_WALL_X - 8.0;
BATC_NOTCH_W      =   6.0;
BATC_NOTCH_H      =   6.0;
CHG_NOTCH_Y0      = KH_Y + 12.0;
CHG_NOTCH_W       =   8.0;
// vents: compartment pressure relief, charger chamber through-flow
BATC_VENT_W       =   8.0;
BATC_VENT_H       =   1.6;
CHG_VENT_W        =   8.0;   // bottom-wall slots, along X (bridged roof)
CHG_VENT_SW       =   6.0;   // side-wall slots, along Y (bridged roof)
CHG_VENT_H        =   2.4;
CHG_VENT_MIN_MM2  = 300.0;   // per side: ~1.5-1.8 W of charger heat (battery review P1)

// ---------- lid screw posts (upper zone corners) -------------------------
POST_XY = [[6.5, UP_Y0 + 6.5], [CASE_W - 6.5, UP_Y0 + 6.5],
           [6.5, CASE_H - 6.5], [CASE_W - 6.5, CASE_H - 6.5]];

// ---------- SEN62 (U1) ----------------------------------------------------
// SEN6x datasheet v0.92: 55.2 x 25.6 x 21.3 mm; ports on the 55.2 x 25.6
// face, which faces the X = CASE_W wall; connector pocket in the opposite
// face (STEP model: 17.2..29.2 along the length, 16.3..21.8 across).
SEN_LEN           =  55.2;   // along Y
SEN_WID           =  25.6;   // along Z
SEN_HGT           =  21.3;   // along X
SEN_SEAL_T        =   1.5;   // EPDM gasket - outside the gas bay (EDR-17)
SEN_X             = CASE_W - WALL - SEN_SEAL_T - SEN_HGT;
SEN_Y             = UP_Y0 + 13.7;
SEN_Z             = (CASE_D - SEN_WID) / 2;
SEN_PLUG_L        =   8.0;
SEN_PLUG_L0       =  15.0;
SEN_PLUG_L1       =  31.5;
SEN_CONN_W0       =  16.3;
SEN_CONN_W1       =  21.8;
SEN_INLET_L0      =   2.2;
SEN_INLET_L1      =  11.2;
SEN_OUTLET_C      =  42.4;
SEN_OUTLET_D      =  20.5;
PORT_ROWS_Z       = [6.0, 10.2, 14.4, 18.6, 22.4];
PORT_SLOT_H       =   2.4;
PORT_BAR          =   1.6;
GASKET_RIB_H      =   0.6;
GASKET_RIB_W      =   1.2;

// ---------- gas bay ----------------------------------------------------------
GAS_X1            =  83.0;   // inner side wall
GAS_Y1            = UP_Y0 + 47.7;   // inner top
GAS_WALL_T        =   2.0;
GAS_STANDOFF      =   5.0;   // boards float 5 mm above the front face
// Seeed Grove SHT40 (101021032), 40 x 20 mm, held by a snug fence: Seeed
// publish no hole drawing.  Low and next to the X = 0 vents, furthest from
// the Sunrise lamp and the FireBeetle.
SHT_L             =  40.0;
SHT_W             =  20.0;
SHT_T             =   1.6;
SHT_PARTS         =   2.5;
SHT_X             = WALL + 10.6;       // clear of the lid post
SHT_Y             = UP_Y0 + 3.0;
// SparkFun SGP40 (SEN-18345), 25.4 x 25.4 mm, 4 x d3.3 holes 2.54 mm in
SGP_S             =  25.4;
SGP_HOLE_IN       =   2.54;
SGP_T             =   1.6;
SGP_PARTS         =   3.0;
SGP_X             = GAS_X1 - SGP_S - 2.0;
SGP_Y             = UP_Y0 + 3.0;
// Senseair Sunrise 006-0-0008, 33.5 x 19.7 x 11.5 mm, housing ~26 mm long,
// pin rows 30.48 mm apart at its ends.  Filter towards the lid; ANO4947:
// >= 1.5 mm from the filter and >= 1.0 mm from the housing to anything.
SR_L              =  33.5;
SR_W              =  19.7;
SR_H              =  11.5;
SR_HOUSING_L      =  26.05;
SR_PIN_PITCH_ROWS =  30.48;
SR_X              = WALL + 10.6;
SR_Y              = SHT_Y + SHT_W + 3.5;
SR_Z              = FACE + GAS_STANDOFF + 1.0;   // on a pedestal, pins hang free
// vents
VENT_SLOT_W       =  10.0;   // <= 12 mm so the roof is a printable bridge
VENT_SLOT_H       =   2.4;
FRONT_VENT_W      =   8.0;
FRONT_VENT_H      =   1.6;

// ---------- FireBeetle 2 ESP32-C6 (M1) --------------------------------------
// DFR1075 drawing: 60.0 x 25.4 mm, 4 x d2.0 holes 1.7 mm from each edge;
// USB-C centred on a short edge, overhanging it by ~1.2 mm.
FB_W              =  60.0;
FB_H              =  25.4;
FB_PCB_T          =   1.6;
FB_TOP_PARTS      =   4.8;   // JST PH socket, the tallest part
FB_HOLE_IN        =   1.7;
FB_STANDOFF       =   5.0;   // header pins and solder joints underneath
FB_X              = WALL + 1.8;          // USB end
FB_Y              = GAS_Y1 + GAS_WALL_T + 1.0;
FB_Z              = FACE + FB_STANDOFF;
USBC_H            =   3.3;
USBC_CLEAR_W      =  13.0;
USBC_CLEAR_H      =   7.5;

// ---------- power modules (no holes on the Pololus: snug pockets) -----------
POCKET_WALL       =   1.2;
POCKET_H          =   3.0;
SW1_S             =  15.24;          // Pololu #2810
SW1_X             = FB_X + FB_W + 3.0;
SW1_Y             = FB_Y;
PS1_W             =  10.9;           // Pololu S9V11E2A
PS1_L             =  16.5;
PS1_T             =   4.0;
PS1_X             = SW1_X + SW1_S + 4.0;
PS1_Y             = FB_Y;
D1_L              =  16.51;          // Adafruit LM66200, 2 x d2.5 holes
D1_W              =  10.16;
D1_HOLES          = [[2.54, 7.62], [13.97, 7.62]];
D1_X              = SW1_X;
D1_Y              = SW1_Y + SW1_S + 3.0;
MOD_Z             = FACE + 1.0;      // modules on 1 mm pads
// C3, the bulk capacitor at SW1's VIN (EDR-22): Panasonic EEU-FR0J471,
// 8 x 11.5 mm radial, standing, soldered straight onto the #2810's pins and
// held by its own leads plus a strip of tape - no mount, but it needs room.
C3_D              =   8.6;           // 8.0 mm body + sleeve and tolerance
C3_H              =  11.5;
C3_X              =  88.0;
C3_Y              = 156.0;

// ---------- USB-C power socket (Adafruit #6050, sunken) -----------------------
// Board file: 20.32 x 13.97 mm, 2 x d2.5 plated holes at (2.286, 10.414) and
// (18.034, 10.414), the socket at the 13.97 mm edge, which faces the top wall.
// It sits above the FireBeetle's top edge on two posts.
J2_L              =  20.32;
J2_W              =  13.97;
J2_T              =   1.6;
J2_HOLES          = [[2.286, 10.414], [18.034, 10.414]];
J2_SOCKET_X0      =   4.5;           // board-local extent of the socket body
J2_SOCKET_X1      =  15.8;
J2_SOCKET_Y0      =   7.0;
J2_SOCKET_H       =   3.4;           // straddles the board
J2_X              =  20.0;
J2_Y              = CASE_H - WALL - 0.3 - J2_W;
J2_Z              = FB_Z + FB_PCB_T + FB_TOP_PARTS + 1.2;   // clear of the FireBeetle
J2_OPEN_W         =  13.0;
J2_OPEN_H         =   7.5;

// ---------- front panel: LED holder and push button ---------------------------
// Signal Construct SMR1089: d8 hole, M8 x 0.75 nut, 17 mm behind the panel.
// Reichelt T 250A: 7 mm hole, ~29 mm long overall, ~24 mm behind the panel.
LED_CX            =  91.0;
LED_CY            = GAS_Y1 - 8.0;
LED_HOLE_D        =   8.2;
LED_BEHIND        =  17.0;
LED_NUT_D         =  11.0;
BTN_CX            =  91.0;
BTN_CY            = UP_Y0 + 16.0;
BTN_HOLE_D        =   7.2;
BTN_BEHIND        =  24.0;
BTN_BODY_D        =  10.0;
// both need a flat, thin panel for their nuts: a counterbore from the inside
PANEL_T           =   2.0;
PANEL_SPOT_D      =  12.0;

// ---------- wall mounting (in the lid) ---------------------------------------
KEYHOLE_D_BIG     =   7.0;
KEYHOLE_D_SMALL   =   3.6;
KEYHOLE_LEN       =   8.0;
KEYHOLE_Y         = FB_Y + 10.0;
KEYHOLE_X         = [30.0, 100.0];
KEYHOLE_CSK_D     =   8.5;

// ---------- derived ----------------------------------------------------
INNER_W           = CASE_W - 2 * WALL;
INNER_H           = CASE_H - 2 * WALL;
SEAM_Z            = FRONT_SHELL_D;
WALL_TOP_Z        = FRONT_SHELL_D - LAP_DEPTH;   // top of every front-shell wall
LID_IN_Z          = CASE_D - FACE;               // inner face of lid and door
