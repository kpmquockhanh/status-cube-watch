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
// The fin stands on the lid fin_gap behind the carrier, up to board-local z fin_zb, and back to world y
// fin_end; it stops a press on the screen from flexing the carrier backwards. Lightened by a window that
// leaves fin_wall all round, with fin_win_r corners.
fin_t = 5;  fin_gap = 0.2;  fin_zb = 32;  fin_sink = 0.6;  fin_end = 65;  fin_wall = 6;  fin_win_r = 3;

// Lead slots in the lid, one beside each leg
lead_x = [6.5, 35.0];  slot_w = 4.5;  slot_l = 5.5;  slot_y0 = 4.0;

nudge = 0.02;               // separates faces that touch by design, in collision checks only
swing = 0;  sag = 0;        // check_swing: bezel opened this many degrees about P; board sagged on its pegs

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

// ---- tray ---------------------------------------------------------------------------
// Prints as modelled, open side up. Holds the cell flat; the front space takes the lead's bend.
front_pil = [for (x = leg_screw_x) [x, leg_screw_y]];
foot_xy = [for (x = [foot_in, W - foot_in], y = [foot_in, D - foot_in]) [x, y]];
module tray_body() hull() {
  translate([0, 0, base_cham]) linear_extrude(tray_h - base_cham) plan_rr(W, D, base_r);
  linear_extrude(0.01) offset(delta = -base_cham) plan_rr(W, D, base_r);
}
module tray_void() translate([wall, wall, wall]) linear_extrude(tray_h) plan_rr(W - 2*wall, D - 2*wall, base_r - wall);
module pilasters() {
  for (p = front_pil) {
    vhole(p, pil_d, 0, tray_h);
    translate([p[0] - 1, wall - 0.5, 0]) cube([2, p[1] - wall + 0.5, tray_h]);   // web to the front wall
  }
  vhole(rear_screw, pil_d, 0, tray_h);
  translate([rear_screw[0] - 1, rear_screw[1], 0]) cube([2, D - wall + 0.5 - rear_screw[1], tray_h]);
}
module tray() difference() {
  union() { difference() { tray_body(); tray_void(); } intersection() { pilasters(); tray_body(); } }
  for (p = concat(front_pil, [rear_screw])) vhole(p, pilot_d, pilot_floor, tray_h + 0.01);
  for (f = foot_xy) vhole(f, foot_d, -0.01, foot_depth);
}

// ---- frame: lid and fin ------------------------------------------------------------------
// Prints lid down. The lid closes the tray; the carrier stands on it and the fin stiffens it.
// world (y, z) of a board-local point given by its board-local y and its world z
function on_plane(yb, z) = let (zb = (z - O[2] + yb*sin(lean))/cos(lean)) [O[1] + yb*cos(lean) + zb*sin(lean), z];
fin_yb = back_y + fin_gap;
fin_pts = [on_plane(fin_yb, deck - fin_sink),
           [b2g([0, fin_yb, fin_zb])[1], b2g([0, fin_yb, fin_zb])[2]],
           [fin_end, deck - fin_sink]];
module fin() translate([(W - fin_t)/2, 0, 0]) difference() {
  across(fin_t) polygon(fin_pts);
  translate([-1, 0, 0]) across(fin_t + 2) offset(r = fin_win_r) offset(delta = -(fin_wall + fin_win_r)) polygon(fin_pts);
}
module lid() translate([0, 0, tray_h]) linear_extrude(lid_t) plan_rr(W, D, base_r);
module lead_slot(x, inset = 0) translate([x + inset, slot_y0 + inset]) square([slot_w - 2*inset, slot_l - 2*inset]);
module frame() difference() {
  union() { lid(); fin(); }
  for (p = front_pil) vhole(p, screw_d, tray_h - 0.01, deck + 0.01);
  vhole(rear_screw, screw_d, tray_h - 0.01, deck + 0.01);
  vhole(rear_screw, cb_d, deck - cb_depth, deck + 0.01);
  for (x = lead_x) translate([0, 0, tray_h - 0.01]) linear_extrude(lid_t + 0.02) lead_slot(x);
}
module tray_print() tray();
module frame_print() translate([0, 0, -tray_h]) frame();

// Everything the battery lead passes: from 3 mm below the tray top to 1 mm above the deck, through each
// slot (inset 0.05). Above that the chin wall leans over the slot, so the lead bends back into the chin.
module lead_path() for (x = lead_x) translate([0, 0, tray_h - 3]) linear_extrude(deck + 1 - (tray_h - 3)) lead_slot(x, 0.05);

// ---- carrier --------------------------------------------------------------------------
// Prints rear face down. Stands on the lid and holds the board from behind: four posts press the PCB
// rear (pegs locate it in its holes), two legs under the board take the vertical leg screws and carry
// the hinge ridges, and the back plate takes the two top screws into the bezel.
leg_x0 = [for (x = leg_screw_x) x - O[0] - leg_w/2];    // board-local x of each leg's left side
shoulder_y = pcb_rear - preload;
// a cylinder along board-local +y at (x, z), from y0 to y1
module ycyl(p, d, y0, y1) translate([p[0], y0, p[1]]) rotate([-90, 0, 0]) cylinder(d = d, h = y1 - y0);
// 2D (y, z) arc band about P, radii r0..r1, between y0 and y1, upper half only
module arc_band(r0, r1, y0, y1) intersection() {
  translate(P) difference() { circle(r = r1, $fn = 96); circle(r = r0, $fn = 96); }
  translate([y0, P[1]]) square([y1 - y0, r1 + 1]);
}
module posts() for (h = holes) ycyl(h, post_d, shoulder_y, plate_y0 + 0.01);
module pegs() if (peg_l > 0) for (h = holes) ycyl(h, peg_d, shoulder_y - peg_l, shoulder_y + 0.01);
module legs_raw() for (x = leg_x0) translate([x, leg_y0, -60]) cube([leg_w, plate_y0 + 0.01 - leg_y0, 60 + leg_top]);
module ridges() for (x = leg_x0) translate([x, 0, 0]) across(leg_w) arc_band(ridge_r[0], ridge_r[1], ridge_y0, leg_y0 + 0.01);
module legs_body() intersection() { board_frame() legs_raw(); above(deck); }
module carrier_raw() board_frame() {
  yslab(plate_y0, back_y) outline2d();
  posts();
  pegs();
  legs_raw();
  ridges();
}
module leg_screw_holes() for (p = front_pil) {
  vhole(p, screw_d, deck - 1, deck + leg_cb_up + 0.01);
  vhole(p, cb_d, deck + leg_cb_up, deck + 12);
}
module top_holes(d, y0, y1) board_frame() for (t = top_pts) ycyl(t, d, y0, y1);
module board_openings() bf() { usb_slot(12); usb_plug_relief(12); button_holes(12); }
module carrier() difference() {
  intersection() { carrier_raw(); above(deck); }
  leg_screw_holes();
  top_holes(screw_d, plate_y0 - 0.01, back_y + 0.01);
  top_holes(cb_d, back_y - cb_depth, back_y + 0.01);
  board_openings();
}
module carrier_print() translate([0, 0, back_y]) rotate([lean - 90, 0, 0]) translate(-O) carrier();
module heads() for (p = front_pil) vhole(p, head_d, deck + leg_cb_up, deck + leg_cb_up + head_h);

// ---- bezel --------------------------------------------------------------------------------
// Prints face down. Frames the glass from the front; its chin wall hooks over the ridges on the legs
// (grooves concentric with P, so it swings on and off about P), and two top screws from the carrier's
// rear hold it closed. Everything behind the chin wall and below the glass is open: legs, screw heads
// and the battery lead live there.
module bezel_raw() board_frame() hull() {
  yslab(-lip_t, -lip_t + 0.01) offset(delta = -face_cham) outline2d();
  yslab(-lip_t + face_cham, plate_y0) outline2d();
}
module chin_cuts() board_frame() {
  translate([-clr, chin_y1, -60]) cube([glass_b + 2*clr, plate_y0 + 1 - chin_y1, 60 - clr]);   // chin cavity
  // lower pocket corners squared: round ones sweep into the glass and PCB corners on the swing
  translate([-clr - 0.01, 0, -clr - 0.01]) cube([glass_b + 2*clr + 0.02, plate_y0 + 1, glass_r + 1]);
  translate([-clr, 0, chin_top]) cube([glass_b + 2*clr, chin_y1 + 0.01, -clr - chin_top + 0.01]); // over the chin wall
  for (x = leg_x0) translate([x - groove_clr, 0, 0])
    across(leg_w + 2*groove_clr) arc_band(ridge_r[0] - groove_clr, ridge_r[1] + groove_clr, groove_y0, chin_y1 + 0.01);
}
// blind: they stop 0.8 behind the face
module top_pilots(d = pilot_d, y1 = plate_y0 + 0.01, y0 = -lip_t + 0.8) board_frame() for (t = top_pts) ycyl(t, d, y0, y1);
module bezel() difference() {
  intersection() { bezel_raw(); above(deck); }
  bf() { pocket(extra = 0.01); window(); }
  chin_cuts();
  top_pilots();
  board_openings();
}
module bezel_print() translate([0, 0, lip_t]) rotate([90 + lean, 0, 0]) translate(-O) children();
// moves a world-placed part along the board's outward normal (forward and up), by d
module fwd(d) translate([0, -d*cos(lean), d*sin(lean)]) children();
// the bezel opened by `a` degrees about P
module swung(a) board_frame() translate([0, P[0], P[1]]) rotate([a, 0, 0]) translate([0, -P[0], -P[1]])
  rotate([lean, 0, 0]) translate(-O) children();

// ---- assembly and the all-parts check ----------------------------------------------
module assembly() {
  color("gainsboro") tray();
  color("dimgray") frame();
  color("lightgray") carrier();
  color("whitesmoke") bezel();
  color("royalblue", 0.6) cell_ghost();
  color("black") board_at();
}
// cut at the left leg screw, keeping the right side
module section_view() difference() { assembly(); translate([-1, -1, -1]) cube([leg_screw_x[0] + 1, D + 2, 100]); }
// Each part lifted by nudge per layer it stands on, and the bezel also moved off the carrier plate:
// any overlap left is a real collision.
module fit_parts(i) {
  if (i == 0) tray();
  if (i == 1) translate([0, 0, nudge]) cell_ghost();
  if (i == 2) translate([0, 0, nudge]) frame();
  if (i == 3) translate([0, 0, 2*nudge]) carrier();
  if (i == 4) translate([0, 0, 2*nudge]) fwd(nudge) bezel();
  if (i == 5) board_at();
}

// ---- reference points for tools/stability.py ------------------------------------------
module meta() echo(str("EASEL{\"lean\":", lean, ",\"press\":", b2g(press_pt), ",\"top\":", b2g(top_pt),
                       ",\"pivot_y\":", pivot_y, ",\"front_y\":", front_y, "}"));

// ---- parts (rendered by test.sh) ----------------------------------------------
if (part == "none") {}
else if (part == "meta") meta();
else if (part == "at_cell") cell_ghost();
else if (part == "at_board") board_at();
else if (part == "at_tray") tray();
else if (part == "at_frame") frame();
else if (part == "check_cell_tray") intersection() { translate([0, 0, nudge]) cell_ghost(); tray(); }
else if (part == "check_cell_frame") intersection() { cell_ghost(); frame(); }
else if (part == "check_lead_path") intersection() { lead_path(); union() { tray(); frame(); carrier(); bezel(); } }
// the rear counterbore grown by 0.9 must stay inside the lid's outline
else if (part == "check_rear_wall") difference() {
    vhole(rear_screw, cb_d + 2*0.9, deck - cb_depth, deck - 0.01);
    translate([0, 0, -1]) linear_extrude(deck + 2) plan_rr(W, D, base_r); }
else if (part == "at_carrier") carrier();
else if (part == "check_carrier_frame") intersection() { translate([0, 0, nudge]) carrier(); frame(); }
else if (part == "check_carrier_board") intersection() { carrier(); board_at(); }
else if (part == "check_shoulders") intersection() { board_frame() posts(); board_at(); }
else if (part == "check_pegs") intersection() { board_frame() pegs(); board_at(); }
else if (part == "check_leg_head") difference() { heads(); legs_body(); }
else if (part == "at_bezel") bezel();
else if (part == "check_window") intersection() { bezel(); bf() active_prism(); }
// a 1.2 wall all round each top pilot: the pilot grown by 1.2 must stay inside the bezel
else if (part == "check_pilot_wall") difference() { top_pilots(pilot_d + 2*1.2, plate_y0 - 0.05, -lip_t + 0.85); bezel(); top_pilots(pilot_d + 2*nudge, plate_y0 + 0.1, -lip_t + 0.7); }
else if (part == "check_ridge_groove") intersection() { board_frame() ridges(); bezel(); }
else if (part == "check_bezel_carrier") intersection() { carrier(); fwd(nudge) bezel(); }
else if (part == "check_bezel_frame") intersection() { translate([0, 0, nudge]) bezel(); frame(); }
else if (part == "check_head_bezel") intersection() { heads(); bezel(); }
else if (part == "check_swing") intersection() { swung(swing) bezel(); union() { carrier(); board_at(sag); } }
else if (part == "check_fit") for (i = [0:4], j = [i + 1:5]) intersection() { fit_parts(i); fit_parts(j); }
else if (part == "assembly") assembly();
else if (part == "section") section_view();
else if (part == "tray") tray_print();
else if (part == "frame") frame_print();
else if (part == "carrier") carrier_print();
else if (part == "bezel") bezel_print() bezel();
else assert(false, str("unknown part: ", part));
