// easel.scad: easel desk stand for the Claude status cube
// (Waveshare ESP32-S3-Touch-LCD-1.69 + 12x40x65 mm LiPo). Units: mm.
// A tray holds the cell flat; a frame lid with a fin closes it; a carrier stands on the lid at `tilt`
// and holds the board from behind; a bezel hooks onto the carrier's legs at the bottom and frames the
// glass. Every part is modelled where it sits in the assembly (world frame: X width, Y depth with the
// front at y = 0, Z up from the desk) and turned into print orientation only for output.
// Rendered by test.sh via the `part` variable (see README.md, "Easel stand").

part = "assembly";

$fn = 48;
include <board.scad>   // board facts, fit tunables (clr, lip_t, board_flip, ...) and board helpers

// ---- tunables ---------------------------------------------------------------
tilt = 65;                  // screen angle from the desk; the board turns about O, the base stays put
W = 46;  D = 81;            // footprint
wall = 2.4;                 // tray walls and floor
lid_t = 2.4;                // frame lid
base_r = 4;                 // plan corner radius of tray and lid
base_cham = 0.8;            // 45 deg chamfer under the tray, against the desk

// Cell: [length, width, thickness], same names as stand.scad
bat = [65, 40, 12];
bcl = 0.4;                  // cell clearance, per side
front_space = 7.5;          // tray room in front of the cell: lead bend and the two front pilasters

// Screws: M2 self-tapping into pilots, clearance holes and counterbores in the part on top.
pilot_d = 1.8;  screw_d = 2.4;  cb_d = 4.2;  cb_depth = 1.6;
pil_d = 4.2;                // pilaster diameter
pilot_floor = 5.2;          // pilots in the tray stop this high above the desk
leg_screw_x = [14.5, 31.5]; // world x of the two vertical leg screws (and of the tray's front pilasters)
leg_screw_y = 7.6;          // world y of the same
leg_cb_up = 2.4;            // leg counterbore floor above the deck
head_d = 4.0;  head_h = 1.6;    // M2 pan head, for the head checks
foot_d = 8;  foot_depth = 0.8;  foot_in = 7;   // rubber-foot recesses, centres foot_in from the edges

// Board placement: O is the glass's front-bottom-left corner (board-local origin) in world coordinates.
O_y = 6.2;                  // world y of O
glass_up = 9.1;             // O's height above the deck

// Carrier
carrier_t = 2.4;            // back plate
bez_side = 3.2;  bez_top = 4.0;  bez_r = 4;   // panel outline around the glass (carrier and bezel)
post_d = 3;  preload = 0.2; // shoulders press this far into the PCB rear
peg_d = 1.8;  peg_l = 1.0;  // pegs into the PCB holes; peg_l = 0 turns them off
leg_w = 6;  leg_y0 = 1.5;  leg_top = -1.3;    // legs under the board (board-local y front, z top)
ridge_r = [3.2, 4.4];  ridge_y0 = 0.5;        // hinge ridges on the leg fronts: arc band about P

// Bezel
face_cham = 0.6;            // 45 deg break on the face's top and side edges
chin_y1 = 1.2;              // rear face of the chin wall (board-local y)
chin_top = -0.6;            // top of the chin wall, below the glass (board-local z)
groove_clr = 0.2;           // hinge groove clearance around the ridge, radial and sideways
groove_y0 = 0.3;            // grooves start this far behind the face plane
pilot_in = 1.25;            // wall between a top-screw pilot and the glass pocket

// Frame fin
fin_t = 5;  fin_gap = 0.2;  fin_zb = 32;  fin_sink = 0.6;  fin_end = 65;  fin_web = 3;

// Lead slots in the lid, one beside each leg
lead_x = [6.5, 35.0];  slot_w = 4.5;  slot_l = 5.5;  slot_y0 = 4.0;

nudge = 0.02;               // separates faces that touch by design, in collision checks only

// ---- derived ----------------------------------------------------------------
lean = 90 - tilt;
bay_w = bat[1] + 2*bcl;  bay_l = bat[0] + 2*bcl;  bay_t = bat[2] + 2*bcl;
tray_h = wall + bay_t;
deck = tray_h + lid_t;                         // top of the lid: everything above stands on it
bay_x0 = (W - bay_w)/2;  bay_y0 = wall + front_space;  bay_y1 = bay_y0 + bay_l;
rear_screw = [W/2, bay_y1 + 2.2];
assert(D >= bay_y1 + wall + 0.5, "D too short for the cell");
O = [(W - glass_b)/2, O_y, deck + glass_up];
// board-local z of the deck plane at board-local y
function deck_zb(yb) = (deck - O[2])/cos(lean) + yb*tan(lean);
// Hinge line P: the bezel's front-bottom edge, on the deck (board-local y, z).
P = [-lip_t, deck_zb(-lip_t)];
back_y = board_t + clr + carrier_t;            // carrier rear face (board-local y)
plate_y0 = board_t + clr;                      // carrier front face = bezel rear face
out_x0 = -bez_side;  out_x1 = glass_b + bez_side;  out_z1 = glass_a + bez_top;
// PCB mounting holes (board-local x, z), following board_flip
holes = [for (bx = [hole_ib, pcb_b - hole_ib], az = [hole_ia, pcb_a - hole_ia])
         let (p = flipp([pcb_b0 + bx, 0, pcb_a0 + az])) [p[0], p[2]]];
// Top screws: on the 45 deg diagonal out of each upper glass corner arc, pilot_in clear of the pocket.
pilot_off = (glass_r + clr + pilot_in + pilot_d/2)/sqrt(2);
top_pts = [[glass_r - pilot_off, glass_a - glass_r + pilot_off],
           [glass_b - glass_r + pilot_off, glass_a - glass_r + pilot_off]];
// Stability reference points (board-local), and the tip pivots (world y)
press_pt = flipp([act_b0 + act_b/2, 0, act_a0 + act_a/2]);
top_pt = board_flip ? flipp([act_b0 + act_b/2, 0, act_a0]) : [act_b0 + act_b/2, 0, act_a0 + act_a];
pivot_y = D - foot_in + foot_d/2 - 1;          // 1 mm inside the rear edge of the rear feet
front_y = foot_in - foot_d/2 + 1;

// ---- shared helpers -------------------------------------------------------------
module yslab(y0, y1) translate([0, y0, 0]) xz_extrude(y1 - y0) children();
module above(z) translate([-200, -200, z]) cube([400, 400, 200]);
module plan_rr(w, d, r) translate([r, r]) offset(r = r) square([w - 2*r, d - 2*r]);
// a vertical hole of diameter d at world (x, y), from z0 to z1
module vhole(p, d, z0, z1) translate([p[0], p[1], z0]) cylinder(d = d, h = z1 - z0);
// the panel outline of carrier and bezel, in board-local (x, z)
module outline2d() rrect(out_x0, -30, out_x1 - out_x0, out_z1 + 30, bez_r);

// ---- ghosts ------------------------------------------------------------------------
module cell_ghost() translate([bay_x0 + bcl, bay_y0 + bcl, wall]) cube([bat[1], bat[0], bat[2]]);
// The board in place, with its four PCB holes (the pegs go in them); `sag` lowers it along the glass.
// The holes already follow board_flip, so the drilling sits outside flip().
module board_at(sag = 0) board_frame() translate([0, 0, -sag]) difference() {
  flip() board_ghost();
  for (h = holes) translate([h[0], pcb_y0 - 0.1, h[1]]) rotate([-90, 0, 0]) cylinder(d = hole_d, h = pcb_t + 0.2);
}

// ---- reference points for tools/stability.py ------------------------------------------
module meta() echo(str("EASEL{\"lean\":", lean, ",\"press\":", b2g(press_pt), ",\"top\":", b2g(top_pt),
                       ",\"pivot_y\":", pivot_y, ",\"front_y\":", front_y, "}"));

// ---- parts (rendered by test.sh) ----------------------------------------------
if (part == "none") {}
else if (part == "meta") meta();
else if (part == "at_cell") cell_ghost();
else if (part == "at_board") board_at();
else assert(false, str("unknown part: ", part));
