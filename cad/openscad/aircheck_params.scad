// =====================================================================
// 3D Printing AIR CHECK - enclosure parameters (v1.2)
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
// side of the finished device, the SEN63C and its air ports on the LEFT.
// Text on the front face is mirrored in the model so that it reads correctly
// on the part.
//
// Component dimensions are from the manufacturer documents listed in
// docs/ENGINEERING_DECISIONS.md.  Fit allowances follow the printing rules
// in manufacturing/print-settings.md.
// =====================================================================

// ---------- printing ------------------------------------------------
NOZZLE            = 0.4;
LAYER             = 0.2;
FIT_SLIDE         = 0.25;   // sliding fit (part goes in, stays put)
FIT_LOOSE         = 0.40;   // loose fit (drop-in)
FIT_PRESS         = 0.05;   // press fit
CHAMFER_BED       = 0.6;    // chamfer on the face that touches the bed
CHAMFER_TOP       = 0.8;
$fn               = 64;

// ---------- overall case ---------------------------------------------
CASE_W            = 112.0;   // v1.2: the SEN63C stands on its side along X = max
CASE_H            = 102.0;
CASE_D            =  32.0;
WALL              =   2.4;   // perimeter wall, 6 x 0.4 mm
FACE              =   2.4;   // front and back faces
CORNER_R          =   6.0;   // vertical (in print direction) corner radius

// A deep front shell and a flat lid.  The SEN63C's ports face the side wall
// and span 20 mm of depth; with the seam at 28 mm every port opening is in one
// printed part, clear of the lap joint.
FRONT_SHELL_D     =  28.0;   // front shell depth including its front face
BACK_SHELL_D      = CASE_D - FRONT_SHELL_D;   // = 4.0, the lid
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

// ---------- DFRobot FireBeetle 2 ESP32-C6 (M1) -------------------------
// DFR1075 dimension drawing: 60.0 x 25.4 mm, holes on 56.6 x 22.0 mm, header
// rows 22.86 mm apart.  USB-C at one short end, centred across the width; the
// JST PH battery socket on the same end.
FB_W              =  60.0;
FB_H              =  25.4;
FB_PCB_T          =   1.6;
FB_TOP_PARTS      =   6.0;   // tallest part: the JST PH socket (not on the
                             // drawing - measure before printing, TESTING.md)
HEADER_H          =   2.5;   // soldered straight onto the carrier with the
                             // supplied male headers: just the plastic spacer
USBC_W            =   9.2;   // USB-C receptacle body
USBC_H            =   3.3;
USBC_CLEAR_W      =  13.0;   // opening, allows for a chunky cable boot
USBC_CLEAR_H      =   7.5;

// ---------- carrier board ACC-1 rev C ----------------------------------
PCB_W             =  66.0;
PCB_H             =  35.0;
PCB_T             =   1.6;
PCB_X             =   3.5;   // lower-left corner seen from the back; USB end
PCB_Y             =  58.0;
// Corner notch (PCB-local, top of the USB end) that clears the screw post
PCB_NOTCH_W       =  10.0;
PCB_NOTCH_H       =   9.0;
// Mounting holes, PCB-local: outside the FireBeetle and outside the notch
PCB_HOLES         = [[3.5, 3.5], [62.5, 3.5], [62.5, 31.5], [15.5, 31.5]];
FB_ON_PCB         = [0.0, 0.5];   // FireBeetle's USB end flush with the PCB edge

// ---------- status LED (LED1, Adafruit 159, 5 mm RGB) ------------------
// No window: the LED shines through a thin skin left in the front face.  Works
// with white or natural filament; with a dark filament set LED_SKIN = 0.
LED_CX            =  48.5;   // PCB-local (45, 30.5): above the FireBeetle
LED_CY            =  88.5;
LED_POCKET_D      =   5.4;   // 5 mm LED + clearance
LED_SKIN          =   0.6;   // three 0.2 mm layers of front face left over it
LED_BODY_H        =   8.6;   // dome tip to flange

// ---------- user button ------------------------------------------------
BTN_CX            =  33.5;   // PCB-local (30, 30.5)
BTN_CY            =  88.5;
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

// ---------- SEN63C (U1) --------------------------------------------------
// SEN6x datasheet v0.5 fig. 10: 55.2 x 25.6 x 21.3 mm.  The 55.2 x 25.6 face
// carries both inlets and the outlet; the connector is recessed in the
// opposite face, next to the outlet.  Sensirion's design-in guide: the ports
// face sideways, not up or down; inlets and outlet are sealed from each other
// and from the rest of the device; open area >= 56 mm2 for the inlets and
// >= 148 mm2 for the outlet.  So the module stands on its side with the port
// face against the X = CASE_W wall and its long axis vertical.
SEN_LEN           =  55.2;   // along Y
SEN_WID           =  25.6;   // along Z
SEN_HGT           =  21.3;   // along X, port face to connector face
SEN_SEAL_T        =   1.5;   // foam gasket between port face and wall
SEN_X             = CASE_W - WALL - SEN_SEAL_T - SEN_HGT;   // connector face
SEN_Y             =  14.0;   // clear of the bottom screw post
SEN_Z             = (CASE_D - SEN_WID) / 2;
SEN_PLUG_L        =   8.0;   // GH plug and cable bend behind the connector face
// Where the connector is, from Sensirion's STEP model (PS_CD_SEN6x_D1.STEP,
// developer.sensirion.com) and datasheet v0.92 fig. 12: the ACES 51468 header
// sits in a pocket 6.35 mm deep in the connector face, 17.2..29.2 mm along the
// length (from the inlet end) and 16.3..21.8 mm across the width (from the
// square-inlet edge).  With the square inlet towards the front, that puts
// the plug at device Z 19.5..25 - towards the lid, level with the battery -
// which is why the battery bay has to stop short of it.  The keep-out below
// adds 2 mm along the length and spans the full depth for the cable.
SEN_PLUG_L0       =  15.0;   // sensor-local, along the length
SEN_PLUG_L1       =  31.5;
SEN_CONN_W0       =  16.3;   // sensor-local, across the width
SEN_CONN_W1       =  21.8;
// Port positions on the port face, sensor-local (along the length, across the
// width), from the datasheet drawing: square inlet 3.2..10.2 x 2.8..9.8,
// round inlet d6 at (6.7, 18.2), outlet d20.5 at (42.4, 12.8).  Sensor-local
// length maps to device Y from SEN_Y upward, width to device Z from SEN_Z.
SEN_INLET_L0      =   2.2;   // one window over both inlets, 1 mm margin
SEN_INLET_L1      =  11.2;
SEN_OUTLET_C      =  42.4;
SEN_OUTLET_D      =  20.5;
// Slot rows through the side wall (device Z), each 2.4 mm tall, all below the
// lap joint at FRONT_SHELL_D - LAP_DEPTH.
PORT_ROWS_Z       = [6.0, 10.2, 14.4, 18.6, 22.4];
PORT_SLOT_H       =   2.4;
PORT_BAR          =   1.6;   // bar between the two outlet columns
GASKET_RIB_H      =   0.6;   // rib on the inner wall that bites into the gasket
GASKET_RIB_W      =   1.2;

// ---------- gas sensor bay (SGP40 breakout U2) ---------------------------
// Adafruit 4829, 25.5 x 17.7 mm (STEMMA QT format).
GAS_BRK_W         =  25.5;
GAS_BRK_H         =  17.7;
GAS_BRK_T         =   6.0;
SGP40_X           =  14.0;
SGP40_Y           =  20.0;
GAS_STANDOFF      =   4.0;

// ---------- battery (BT1) ---------------------------------------------
// 606090 pouch: 6.0 x 60 x 90 mm plus the protection circuit and lead.
BAT_T             =   6.0;
BAT_W             =  60.0;   // upright: 60 across, 90 up
BAT_H             =  90.0;
BAT_PAD           =   1.0;   // foam behind, the cover plate in front
BAT_CLEAR         =   1.5;   // extra room so a slightly fatter cell still fits
BAT_BAY_T         =   1.6;   // bay wall

// ---------- ventilation for the gas bay --------------------------------
VENT_SLOT_W       =  10.0;   // <= 12 mm so the slot roof is a printable bridge
VENT_SLOT_H       =   2.4;
VENT_SLOT_GAP     =   2.0;

// ---------- wall mounting ----------------------------------------------
// The battery fills the lid between the left screw posts and the SEN63C, so
// the two keyholes sit one above the other in the strip beside it: hang the
// device on two screws KEYHOLE_SPACING apart, vertically.
KEYHOLE_D_BIG     =   7.0;
KEYHOLE_D_SMALL   =   3.6;
KEYHOLE_LEN       =   8.0;
KEYHOLE_X         =   7.0;
KEYHOLE_Y0        =  36.0;
KEYHOLE_SPACING   =  40.0;
KEYHOLE_CSK_D     =   8.5;   // pocket on the inside for the screw head

// ---------- derived ----------------------------------------------------
INNER_W           = CASE_W - 2 * WALL;
INNER_H           = CASE_H - 2 * WALL;
INNER_D           = CASE_D - 2 * FACE;

// Z plane of the front face of the carrier board.  Set by the button: it puts
// the 5.0 mm tactile's stem tip 3.5 mm behind the outer face, 0.3 mm under the
// printed cap.
PCB_Z             = 8.5;
// Z plane of the top of the tallest thing on the carrier (FireBeetle + parts)
STACK_Z           = PCB_Z + PCB_T + HEADER_H + FB_PCB_T + FB_TOP_PARTS;
// Z plane of the front of the battery bay (the cover plate) and of the cell
BAT_Z             = CASE_D - FACE - BAT_T - 2 * BAT_PAD;
BATCOVER_T        =   0.8;   // lies in the front pad space, BAT_Z .. BAT_Z + 0.8

// ---------- zone boundaries -------------------------------------------
// The VOC breakout gets its own sealed bay in the lower corner at the USB end,
// vented through the bottom and side walls.  The SEN63C is sealed by its own
// gaskets.  Air never moves through the electronics or past the battery.
SENSOR_BAY_TOP    =  52.0;   // Y of the gas bay's top rib
DIVIDER_T         =   2.0;
GAS_PARTITION_X   =  46.0;   // gas bay's inner side wall
GAS_PARTITION_T   =   2.0;
RIB_TOP_Z         = BAT_Z - 0.6;   // ribs stop short of the battery cover;
                                   // the X2 foam strip closes the gap

// ---------- battery bay, on the lid ---------------------------------------
BAT_BAY_X0        =  11.5;         // clear of the X = 6.5 screw posts
BAT_X             = BAT_BAY_X0 + BAT_BAY_T + BAT_CLEAR;
BAT_Y             =   6.0;

// The seam sits between the front shell and the lid:
SEAM_Z            = FRONT_SHELL_D;

// ---------- screw post positions --------------------------------------
POST_XY = [[6.5, 6.5], [CASE_W - 6.5, 6.5],
           [6.5, CASE_H - 6.5], [CASE_W - 6.5, CASE_H - 6.5]];
