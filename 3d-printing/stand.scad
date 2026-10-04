// stand.scad: desk stand for the Claude status cube
// (Waveshare ESP32-S3-Touch-LCD-1.69 + 12x40x65 mm LiPo). Units: mm.
// Rendered by test.sh via the `part` variable (see README.md for the print and fit-check procedure).

part = "assembly";

$fn = 48;
include <board.scad>   // board facts, fit tunables (clr, lip_t, board_flip, ...) and board helpers

// ---- tunables ---------------------------------------------------------------
tilt = 65;                  // screen angle from the desk
insert_mode = false;        // true: heat-set insert holes instead of self-tapping pilots
pilot_d = 1.8;              // M2 self-tapping pilot
insert_d = 3.2;             // M2 heat-set insert hole

wall = 2.4;  plate_t = 2.4;
rim = 3.0;                  // face margin below the glass
rim_top = 5.0;              // face margin above the glass (keeps the apex skin thick at any tilt)
bcl = 0.4;                  // battery clearance, per side
side_gap = 1.0;             // extra room beside the battery pocket
end_zone = 7.5;             // room at each end of the battery for bosses / pushers
boss_r = 2.6;  boss_h = 8;

// Edge rounding (cosmetic): set any of these to 0 for the original hard edges.
edge_r = 3.0;        // the four vertical corners, in plan; shell and base plate share it
prof_r = 2.5;        // apex ridge, back top edge, and the kink at the foot of the face
plate_cham = 0.8;    // 45 deg chamfer under the base plate, against the desk

// Battery: [length, width, thickness]
bat = [65, 40, 12];

// ---- derived ----------------------------------------------------------------
lean = 90 - tilt;                              // screen lean from vertical
bay_l = bat[0] + 2*bcl;  bay_w = bat[1] + 2*bcl;  bay_t = bat[2] + 2*bcl;
inner_w = bay_w + 2*side_gap;
W = inner_w + 2*wall;
z0 = max(16, bay_t + 3);                       // height of the face's bottom edge
face_len = glass_a + 2*clr + rim + rim_top;
F1 = [face_len*sin(lean), z0 + face_len*cos(lean)];   // top edge of the face, in (y, z)
z_back = bay_t + wall + 1.5;                   // outer roof height at the back
bx0 = (W - bay_w)/2;                           // battery pocket origin (z = 0 is the plate top)
// Board-local origin (glass front face, portrait bottom-left corner) in global coordinates.
O = [(W - glass_b)/2,
     (rim + clr)*sin(lean) + lip_t*cos(lean),
     z0 + (rim + clr)*cos(lean) - lip_t*sin(lean)];
// The two lowest PCB mounting-hole positions (world-lowest even with board_flip), at the PCB rear face.
push_local = [for (bb = [hole_ib, pcb_b - hole_ib])
              [pcb_b0 + bb, pcb_rear, pcb_a0 + (board_flip ? pcb_a - hole_ia : hole_ia)]];
push_d = 3.0;  preload = 0.2;                            // pusher post diameter; interference into the PCB rear
// Front-end room: the pusher posts must stay >= push_gap in front of the battery bay. With board_flip or
// a low tilt the posts sit further back, so the front end zone grows to keep that gap (deeper stand).
push_gap = 1.0;
push_pts = [for (p = push_local) b2g(flipp(p))];
push_ymax = max([for (p = push_pts) p[1]]) + push_d/2;
end_front = max(end_zone, push_ymax + push_gap - wall);
inner_d = bay_l + end_front + end_zone;
D = inner_d + 2*wall;
by0 = wall + end_front;
assert(by0 >= push_ymax + push_gap - 1e-6, "pusher posts too close to the battery bay: raise tilt or end_zone");
boss_xy = [for (x = [wall + boss_r, W - wall - boss_r], y = [wall + boss_r, D - wall - boss_r]) [x, y]];

// ---- shell body -------------------------------------------------------------
function profile_pts(zb) = [[0, zb], [0, z0], F1, [D, z_back], [D, zb]];
// Footprint in plan, inset by t, with the four vertical corners rounded.
module foot2d(t = 0) round2d(edge_r - t) offset(delta = -t) square([W, D]);
// The outer body, inset by t on every face and cut flat at z = zb (zb < 0 leaves the bottom open).
// Convex edges are rounded: edge_r in plan, prof_r in the side profile, each reduced by the inset, so
// the wall keeps its full thickness through a corner. The profile is extended well below the cut, so
// the bottom edge stays square: the shell prints on that rim and its seam with the plate stays tight.
module outer_body(t = 0, zb = 0) intersection() {
  translate([t, 0, 0]) across(W - 2*t) round2d(prof_r - t) offset(delta = -t) polygon(profile_pts(-5));
  translate([0, 0, zb]) linear_extrude(200) foot2d(t);
}
module outer_solid() outer_body();
module outer_inset(t) outer_body(t, t);
// open at the bottom
module inner_void() outer_body(wall, -5);

module boss(p) {
  sx = p[0] < W/2 ? -1 : 1;  sy = p[1] < D/2 ? -1 : 1;
  // starts 1 mm below z=0 (clipped in front_shell): a boss bottom coplanar with the wall bottom is non-manifold
  translate([p[0], p[1], -1]) hull() {
    cylinder(r = boss_r, h = boss_h + 1);
    translate([sx*(boss_r + wall/2) - 0.5, sy*(boss_r + wall/2) - 0.5, 0]) cube([1, 1, boss_h + 1]);
  }
}
module bosses() for (p = boss_xy) boss(p);
module boss_pilots() for (p = boss_xy) translate([p[0], p[1], -0.01]) cylinder(d = insert_mode ? insert_d : pilot_d, h = boss_h - 1);

module front_shell() difference() {
  intersection() {
    union() { difference() { outer_solid(); inner_void(); } bosses(); }
    outer_solid();   // clips the bosses' 1 mm overshoot below z=0
  }
  bf() { pocket(); window(); }
  usb_cut();
  usb_plug_cut();
  button_cuts();
  boss_pilots();
}

// ---- battery bay -------------------------------------------------------------------
// The bay is the open interior above the plate; the plate's ribs locate the cell.
module bay_ghost() translate([bx0, by0, 0]) cube([bay_w, bay_l, bay_t]);
// outer_inset(wall) has its own floor at z = wall; fill that floor so only the roof/front/back can be hit
module roof_inset() { outer_inset(wall); translate([0, 0, -1]) cube([W, D, wall + 1]); }
module battery_ghost() translate([bx0 + bcl, by0 + bcl, 0]) cube([bat[1], bat[0], bat[2]]);

// ---- base plate --------------------------------------------------------------------
screw_clear_d = 2.4;  cb_d = 4.2;  cb_depth = 1.6;       // M2 clearance and counterbore (underside)
foot_d = 8;  foot_depth = 0.8;                           // rubber-foot recesses (underside)
foot_xy = [for (x = [8, W - 8], y = [D/2 - 20, D/2 + 20]) [x, y]];
rib_t = 0.8;  rib_h = 6;  rib_seg = 4;  rib_side = 12;   // short ribs leave a finger gap beside the cell


// A vertical post from the plate, cut flat on the PCB rear plane (minus `preload`), so it presses the
// board up the slope and into the glass pocket.
module pusher(gp) difference() {
  translate([gp[0], gp[1], 0]) cylinder(d = push_d, h = gp[2] + 6);
  bf() translate([-100, -100, -100]) cube([300, 100 + pcb_rear - preload, 300]);
}
module pushers() for (gp = push_pts) pusher(gp);

module bay_ribs() {
  for (x = [bx0, bx0 + bay_w - rib_seg], y = [by0 - rib_t, by0 + bay_l])
    translate([x, y, 0]) cube([rib_seg, rib_t, rib_h]);                       // end ribs
  // side ribs run through the end ribs' outer faces: corner-only contact would be a non-manifold edge
  for (x = [bx0 - rib_t, bx0 + bay_w], y = [by0 - rib_t, by0 + bay_l - rib_side + rib_t])
    translate([x, y, 0]) cube([rib_t, rib_side, rib_h]);
}

// Same outline as the shell footprint, so the two parts stay flush around the seam, with the underside
// edge chamfered so nothing sharp meets the desk. Prints as a 45 deg outward overhang off the bed.
module plate_body() hull() {
  translate([0, 0, -plate_t + plate_cham]) linear_extrude(plate_t - plate_cham) foot2d();
  translate([0, 0, -plate_t]) linear_extrude(0.01) foot2d(plate_cham);
}

module base_plate() difference() {
  union() {
    plate_body();
    bay_ribs();
    pushers();
  }
  for (p = boss_xy) translate([p[0], p[1], -plate_t - 0.01]) {
    cylinder(d = screw_clear_d, h = plate_t + 0.02);
    cylinder(d = cb_d, h = cb_depth + 0.01);
  }
  for (f = foot_xy) translate([f[0], f[1], -plate_t - 0.01]) cylinder(d = foot_d, h = foot_depth + 0.01);
}

// ---- openings --------------------------------------------------------------------------
module usb_cut() bf() usb_slot(W);
module usb_plug_cut() bf() usb_plug_relief(W);
module button_cuts() bf() button_holes(W);

// ---- Type-C blanking cap ---------------------------------------------------------------
// A press-fit blank for the Type-C opening (the plug relief, usb_plug_w x usb_plug_h, is what shows on
// the outside). Two sprung tongues snap behind the inner wall face; the flange covers the opening from
// outside and carries a lift ear for a fingernail. Modelled in print orientation: flange face on z = 0,
// tongues up; cap_place() puts it in the wall, and follows board_flip like every other opening.
cap_clr = 0.15;      // per side, into the opening (raise it if the cap will not start)
cap_lip = 0.9;       // flange thickness
cap_flange = 1.6;    // how far the flange stands past the opening, per side
cap_ear = 2.2;       // radius of the lift ear beside the flange
cap_wall = 0.8;      // tongue thickness
cap_barb = 0.45;     // snap ridge, per side: cap_barb - cap_clr of it engages behind the wall
cap_tip = 0.4;       // lead-in chamfer on the tongue tips
cap_land = 1.0;      // how far the tongues reach past the inner wall face

cap_w = usb_plug_w - 2*cap_clr;   // cap x, along the board's long axis
cap_h = usb_plug_h - 2*cap_clr;   // cap y, into the stand
cap_l = wall + cap_land;

module cap_flange2d() hull() {
  rrect(-cap_w/2 - cap_flange, -cap_h/2 - cap_flange, cap_w + 2*cap_flange, cap_h + 2*cap_flange, 2);
  translate([-cap_w/2 - cap_flange - cap_ear + 1.0, 0]) circle(r = cap_ear);
}
// One tongue, on the +y side; cap() mirrors it. The barb is a wedge: a square shoulder just inside the
// wall, ramped away to nothing at the tip so the tongue can be pushed through.
module cap_tongue() hull() {
  translate([-cap_w/2, cap_h/2 - cap_wall, 0]) cube([cap_w, cap_wall, cap_l - cap_tip]);
  translate([-cap_w/2 + cap_tip, cap_h/2 - cap_wall, cap_l - 0.01]) cube([cap_w - 2*cap_tip, cap_wall - cap_tip, 0.01]);
}
module cap_barb1() hull() {
  translate([-cap_w/2 + 1, cap_h/2, wall - 0.15]) cube([cap_w - 2, cap_barb, 0.01]);
  translate([-cap_w/2 + 1, cap_h/2 - 0.01, cap_l]) cube([cap_w - 2, 0.01, 0.01]);
}
// The tongues alone: this is what has to pass through the opening. The flange is excluded because it
// seats flat on the wall, and a coplanar face reads as an overlap (check_cap_seat covers the flange).
module cap_plug() { cap_tongue(); mirror([0, 1, 0]) cap_tongue(); }
module cap_body() {   // everything except the barbs
  translate([0, 0, -cap_lip]) linear_extrude(cap_lip) cap_flange2d();
  cap_plug();
}
module cap_barbs() { cap_barb1(); mirror([0, 1, 0]) cap_barb1(); }
module cap() { cap_body(); cap_barbs(); }

// The cap's frame in the wall: z = 0 is the outer wall surface, +z points into the stand.
module cap_place() bf() translate([W - O[0], usb_y, usb_a]) rotate([0, -90, 0]) children();
module cap_print() translate([0, 0, cap_lip]) cap();
// test probes: the flange's seating face, and the flange swept through the wall
module cap_seat() cap_place() linear_extrude(0.2) cap_flange2d();
module cap_shadow() cap_place() translate([0, 0, -cap_lip]) linear_extrude(cap_lip + 6) cap_flange2d();

module wall_left()  cube([wall, D, 100]);
module wall_right() translate([W - wall, 0, 0]) cube([wall, D, 100]);

// ---- deliverables -------------------------------------------------------------------------
// Print the base plate right side up (flat underside on the bed); the shell prints as modelled.
module base_print() translate([0, 0, plate_t]) base_plate();

// A slice of the shell around the glass pocket, spanning both side walls: tests the glass fit,
// the window, the Type-C slot and the pinholes in about 20 minutes of printing.
module fit_check() intersection() {
  front_shell();
  bf() translate([-O[0], -lip_t - 1, -6]) cube([W, board_t + lip_t + 9, glass_a + 12]);
}

module assembly() {
  color("lightgray") front_shell();
  color("dimgray") base_plate();
  color("royalblue", 0.6) battery_ghost();
  color("black") bf() board_ghost();
}
module section_view() difference() {
  assembly();
  translate([-1, -1, -plate_t - 1]) cube([W/2 + 1, D + 2, 100]);   // remove the left half
}

// ---- parts (rendered by test.sh) ----------------------------------------------
if (part == "none") {}
else if (part == "front_shell") front_shell();
else if (part == "check_window") intersection() { front_shell(); bf() active_prism(); }
else if (part == "check_pocket_inside") difference() { bf() pocket(); outer_solid(); }
else if (part == "check_skin") difference() { bf() pocket(); outer_inset(0.8); }
else if (part == "check_battery_void") difference() { bay_ghost(); inner_void(); }
else if (part == "check_battery_bosses") intersection() { bay_ghost(); bosses(); }
else if (part == "check_battery_board") intersection() { bay_ghost(); bf() pocket(); }
else if (part == "check_battery_roof") difference() { bay_ghost(); roof_inset(); }
else if (part == "base_plate") base_plate();
else if (part == "check_pusher_touch") intersection() { pushers(); bf() pcb_slab(); }
else if (part == "check_pusher_battery") intersection() { pushers(); bay_ghost(); }
else if (part == "check_pusher_bosses") intersection() { pushers(); bosses(); }
else if (part == "check_pusher_shell") intersection() { pushers(); front_shell(); }
// The wall must keep at least wall - 0.1 everywhere above z = wall (the rounded corners are the risk).
// check_wall_unrounded is its positive control: the same void without the matching inner rounding.
else if (part == "check_wall") difference() {
    intersection() { inner_void(); translate([-1, -1, wall]) cube([W + 2, D + 2, 200]); }
    outer_inset(wall - 0.1); }
else if (part == "check_wall_unrounded") difference() {
    intersection() {
      translate([wall, 0, 0]) across(W - 2*wall) offset(delta = -wall) polygon(profile_pts(-5));
      translate([-1, -1, wall]) cube([W + 2, D + 2, 200]); }
    outer_inset(wall - 0.1); }
// the plate must not stand proud of the shell anywhere around the seam
else if (part == "check_plate_flush") difference() {
    base_plate();
    translate([0, 0, -plate_t - 1]) linear_extrude(200) foot2d(); }
else if (part == "check_ribs_shell") intersection() { bay_ribs(); front_shell(); }
else if (part == "check_ribs_battery") intersection() { bay_ribs(); battery_ghost(); }
else if (part == "check_usb_right") intersection() { usb_cut(); wall_right(); }
else if (part == "check_usb_left")  intersection() { usb_cut(); wall_left(); }
else if (part == "check_plug_right") intersection() { usb_plug_cut(); wall_right(); }
else if (part == "check_plug_left")  intersection() { usb_plug_cut(); wall_left(); }
else if (part == "check_plug_skin") intersection() { usb_plug_cut(); difference() { outer_solid(); outer_inset(0.8); }
    translate([wall + 0.01, -1, -1]) cube([W - 2*wall - 0.02, D + 2, 200]); }
else if (part == "check_plug_pocket") intersection() { usb_plug_cut(); bf() pocket(); }
else if (part == "check_plug_window") intersection() { usb_plug_cut(); bf() window(); }
else if (part == "check_plug_bosses") intersection() { usb_plug_cut(); bosses(); }
else if (part == "check_plug_bay") intersection() { usb_plug_cut(); bay_ghost(); }
// Type-C cap. The barbs are meant to interfere (that is the snap), so the fit check uses cap_body().
else if (part == "check_cap_fit") intersection() { cap_place() cap_plug(); front_shell(); }
else if (part == "check_cap_snap") intersection() { cap_place() cap_barbs(); inner_void(); }
else if (part == "check_cap_covers") difference() {
    intersection() { usb_plug_cut(); difference() { outer_solid(); outer_inset(0.4); } }
    cap_shadow(); }
else if (part == "check_cap_seat") difference() { cap_seat(); front_shell(); usb_plug_cut(); }
else if (part == "check_cap_board") intersection() { cap_place() cap(); bf() board_ghost(); }
else if (part == "check_btn_left")  intersection() { button_cuts(); wall_left(); }
else if (part == "check_btn_right") intersection() { button_cuts(); wall_right(); }
else if (part == "front") front_shell();
else if (part == "base") base_print();
else if (part == "fit") fit_check();
else if (part == "cap") cap_print();
else if (part == "assembly") assembly();
else if (part == "section") section_view();
else if (part == "cap_fitted") { assembly(); color("dimgray") cap_place() cap(); }
else assert(false, str("unknown part: ", part));
