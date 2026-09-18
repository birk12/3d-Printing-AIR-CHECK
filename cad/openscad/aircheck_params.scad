// =====================================================================
// 3D Printing AIR CHECK - enclosure parameters
// ---------------------------------------------------------------------
// Every dimension that matters lives here.  Nothing downstream may use a
// bare number that is not derived from one of these.
//
// Coordinate frame used throughout the model:
//   X : 0 .. CASE_W   left -> right, seen from the BACK (from inside the case)
//   Y : 0 .. CASE_H   bottom -> top
//   Z : 0 .. CASE_D   outer front face -> outer back face
//
// Z grows away from someone standing in front of the device, so seen from the
// front X runs right -> left: the USB-C port at X = 0 is on the RIGHT-hand
// side of the finished device, the particle sensor on the left.  Text on the
// front face is mirrored in the model so that it reads correctly on the part.
//
// Component dimensions are from the manufacturer documents listed in
// docs/ENGINEERING_DECISIONS.md.  Fit allowances follow the printing rules
// in manufacturing/print-settings.md.
// =====================================================================

// ---------- printing ------------------------------------------------
NOZZLE            = 0.4;
LAYER             = 0.2;
FIT_SLIDE         = 0.25;   // sliding fit (part goes in, stays put)
FIT_LOOSE         = 0.40;   // loose fit (drop-in, e.g. the display module)
FIT_PRESS         = 0.05;   // press fit
CHAMFER_BED       = 0.6;    // chamfer on the face that touches the bed
CHAMFER_TOP       = 0.8;
$fn               = 64;

// ---------- overall case ---------------------------------------------
CASE_W            =  98.0;   // v1.1: no display, portrait battery
CASE_H            = 102.0;
CASE_D            =  32.0;
WALL              =   2.4;   // perimeter wall, 6 x 0.4 mm
FACE              =   2.4;   // front and back faces
CORNER_R          =   6.0;   // vertical (in print direction) corner radius

FRONT_SHELL_D     =  20.0;   // front shell depth including its front face
BACK_SHELL_D      = CASE_D - FRONT_SHELL_D;   // = 9.0
LAP_DEPTH         =   3.0;   // overlap of the two shells
LAP_WALL          =   1.1;   // thickness of the inner lip
SHADOW_GAP        =   0.6;   // visible groove at the seam

// ---------- fasteners -------------------------------------------------
INSERT_D          =   3.84;  // M2.5 heat-set insert hole in PETG
INSERT_DEPTH      =   6.0;
SCREW_D           =   2.8;   // M2.5 clearance
SCREW_HEAD_D      =   5.0;
SCREW_HEAD_DEPTH  =   2.0;
POST_D            =   9.0;   // screw post outer diameter (merges into the wall)
SELFTAP_CORE_D    =   2.1;   // M2.5 self-tapping core hole (PCB standoffs)

// ---------- Adafruit ESP32-C6 Feather (M1) ---------------------------
FEATHER_W         =  50.8;
FEATHER_H         =  22.9;
FEATHER_PCB_T     =   1.6;
FEATHER_TOP_PARTS =   3.2;   // tallest part on the Feather
HEADER_H          =   5.0;   // SHORT female Feather headers (Adafruit 2940)
USBC_W            =   9.2;   // USB-C receptacle body
USBC_H            =   3.3;
USBC_CLEAR_W      =  13.0;   // opening, allows for a chunky cable boot
USBC_CLEAR_H      =   7.5;
USBC_Y_FROM_PCB   =   1.2;   // underside of the shell above the PCB top

// ---------- carrier board ACC-1 --------------------------------------
PCB_W             =  70.0;
PCB_H             =  35.0;
PCB_T             =   1.6;
PCB_X             =   3.5;   // lower-left corner; USB-C end at the left wall
PCB_Y             =  54.0;

PCB_HOLE_INSET    =   3.5;   // mounting hole centre inset from the PCB edges

// ---------- status LED (LED1, Adafruit 159, 5 mm RGB) ------------------
// No window: the LED shines through a thin skin left in the front face.  Works
// with white or natural filament; with a dark filament cut the skin out.
LED_CX            =  49.0;
LED_CY            =  80.0;
LED_POCKET_D      =   5.4;   // 5 mm LED + clearance
LED_SKIN          =   0.6;   // three 0.2 mm layers of front face left over it
LED_BODY_H        =   8.6;   // dome tip to flange

// ---------- user button ------------------------------------------------
BTN_CX            =  49.0;
BTN_CY            =  62.0;
BTN_CAP_D         =   9.0;
BTN_HOLE_D        =   7.4;   // through the front face
BTN_TRAVEL        =   0.6;
BTN_SW_BODY       =   6.0;   // 6 x 6 mm through-hole tactile switch
BTN_SW_H          =   5.0;   // total height above the PCB, stem included
BTN_CAP_T         =   1.2;   // visible disc
BTN_CAP_POST      =   1.8;   // post that reaches down to the switch stem
BTN_CB_D          =   9.4;   // counterbore in the outer face
BTN_CB_DEPTH      =   1.4;
BTN_GAP           =   0.3;   // gap between the cap post and the switch stem

// ---------- SPS30 (U1) -------------------------------------------------
// 40.8 +-0.5 x 40.8 x 12.2 mm; 41.2 x 41.2 including the plastic fixation
// elements (SPS30 datasheet v2.0 fig. 7).  Two inlets and one outlet share
// one 41.2 x 12.2 edge face; the ZHR-5 connector is on the opposite face.
SPS30_W           =  41.2;
SPS30_H           =  41.2;
SPS30_T           =  12.2;
SPS30_X           =  43.5;   // lower-left corner of its footprint
SPS30_Y           =   4.4;
SPS30_Z           =   4.0;   // fully inside the front shell
// Position of the ports along the port face, measured from the outlet end.
// Read off the front view in "Mechanical Design and Assembly Guidelines for
// SPS30" v1.0: the outlet grille occupies roughly 5.5 .. 22 mm and the two
// inlets roughly 28 .. 40 mm of the 41.2 mm wide face.
SPS30_OUTLET_X0   =   3.5;
SPS30_OUTLET_X1   =  23.5;
SPS30_INLET_X0    =  26.5;
SPS30_INLET_X1    =  39.5;
SPS30_SEAL_T      =   1.5;   // EPDM / acoustic foam strip thickness
SPS30_DUCT_RIB    =   2.0;   // sealing rib between inlet and outlet

// ---------- gas sensor bay (SGP40 U2, SCD41 U3) ------------------------
// Breakouts are 25.5 x 17.7 mm (Adafruit STEMMA QT format).
GAS_BRK_W         =  25.5;
GAS_BRK_H         =  17.7;
GAS_BRK_T         =   6.0;
GAS_BAY_X         =   4.4;
GAS_BAY_Y         =   4.4;
GAS_BAY_W         =  34.0;
GAS_BAY_H         =  41.0;

// ---------- battery (BT1) ---------------------------------------------
// 606090 pouch: 6.0 x 60 x 90 mm plus the protection circuit and lead.
BAT_T             =   6.0;
BAT_W             =  60.0;   // v1.1: cell upright, 60 across, 90 up
BAT_H             =  90.0;
BAT_PAD           =   1.0;   // foam either side
BAT_CLEAR         =   1.5;   // extra room so a slightly fatter cell still fits

// ---------- ventilation for the gas sensors ----------------------------
VENT_SLOT_W       =  10.0;   // <= 12 mm so the slot roof is a printable bridge
VENT_SLOT_H       =   2.4;
VENT_SLOT_GAP     =   2.0;

// ---------- wall mounting ----------------------------------------------
KEYHOLE_D_BIG     =   8.0;
KEYHOLE_D_SMALL   =   4.2;
KEYHOLE_LEN       =   9.0;
KEYHOLE_SPACING   =  82.0;   // outside the battery bay, see asserts
KEYHOLE_Y         =  52.0;

// ---------- derived ----------------------------------------------------
INNER_W           = CASE_W - 2 * WALL;
INNER_H           = CASE_H - 2 * WALL;
INNER_D           = CASE_D - 2 * FACE;

// Z plane of the front face of the carrier board.  Set by the button: it puts
// the 5.0 mm tactile's stem tip 3.5 mm behind the outer face, 0.3 mm under the
// printed cap.
PCB_Z             = 8.5;
// Z plane of the top of the tallest thing on the carrier (Feather + parts)
STACK_Z           = PCB_Z + PCB_T + HEADER_H + FEATHER_PCB_T + FEATHER_TOP_PARTS;
// Z plane of the front face of the battery
BAT_Z             = CASE_D - FACE - BAT_T - 2 * BAT_PAD;

// ---------- zone boundaries -------------------------------------------
// The case is split by a sealed rib into a sensor bay along the bottom and
// an electronics compartment above it.  Air only ever moves through the
// sensor bay; the electronics and the battery are outside the air path.
SENSOR_BAY_TOP    =  47.0;   // Y of the sealed divider rib
DIVIDER_T         =   2.0;
GAS_PARTITION_X   =  38.7;   // sealed wall between the gas bay and the SPS30 bay
GAS_PARTITION_T   =   2.0;

BAT_X             = (CASE_W - BAT_W) / 2;
BAT_Y             =  6.0;   // bay walls end 0.5 mm short of the top and bottom walls


// The seam sits between the front shell and the back shell:
SEAM_Z            = FRONT_SHELL_D;

// ---------- screw post positions --------------------------------------
POST_XY = [[6.5, 6.5], [CASE_W - 6.5, 6.5],
           [6.5, CASE_H - 6.5], [CASE_W - 6.5, CASE_H - 6.5]];

// ---------- gas sensor breakout placement ------------------------------
SGP40_X = 12.0;  SGP40_Y = 5.5;
SCD41_X = 12.0;  SCD41_Y = 25.0;
GAS_STANDOFF = 4.0;
