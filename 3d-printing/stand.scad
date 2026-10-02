// stand.scad: desk stand for the Claude status cube
// (Waveshare ESP32-S3-Touch-LCD-1.69 + 12x40x65 mm LiPo). Units: mm.
// Rendered by test.sh via the `part` variable (see README.md for the print and fit-check procedure).

part = "assembly";

$fn = 48;

// ---- tunables ---------------------------------------------------------------
tilt = 65;                  // screen angle from the desk
board_flip = false;         // rotate the board 180 deg in its pocket (also needs lcd.setRotation(2))
insert_mode = false;        // true: heat-set insert holes instead of self-tapping pilots
pilot_d = 1.8;              // M2 self-tapping pilot
insert_d = 3.2;             // M2 heat-set insert hole

wall = 2.4;  plate_t = 2.4;
lip_t = 1.0;                // material in front of the glass
rim = 3.0;                  // face margin below the glass
rim_top = 5.0;              // face margin above the glass (keeps the apex skin thick at any tilt)
clr = 0.3;                  // glass pocket clearance, per side
bcl = 0.4;                  // battery clearance, per side
side_gap = 1.0;             // extra room beside the battery pocket
end_zone = 7.5;             // room at each end of the battery for bosses / pushers
boss_r = 2.6;  boss_h = 8;

// Battery: [length, width, thickness]
bat = [65, 40, 12];

// Board, from the Waveshare drawing. Drawing coords: a = from its left edge, b = from its top edge.
// In the firmware's portrait view: portrait x = b, portrait z = a.
glass_a = 41.13;  glass_b = 33.13;  glass_r = 4;               // VERIFY glass_r
act_a0 = 2.9;  act_b0 = 2.4;  act_a = 32.634;  act_b = 27.972;  // VERIFY the two offsets
pcb_a = 37.12;  pcb_b = 29.83;  pcb_a0 = 0.7;  pcb_b0 = 1.65;   // VERIFY the two offsets
pcb_y0 = 2.0;  pcb_t = 1.2;                                     // VERIFY: PCB front face depth, PCB thickness
board_t = 7;                                                    // VERIFY: glass front to tallest rear part
hole_d = 2.2;  hole_ia = 4.16;  hole_ib = 1.75;                 // hole_d is reference only (no screw passes through the board); offsets are from the PCB corner
btn_a = [10.7, 19.8, 29.1];                                     // VERIFY: RST, BOOT, PWR along portrait z
usb_a = 20.3;  usb_w = 9.0;  usb_h = 3.3;                       // VERIFY
usb_plug_w = 13;  usb_plug_h = 7.5;  usb_plug_x0 = 3;           // VERIFY: Type-C plug overmold relief; x0 = start beyond the PCB edge
win_margin = 1.0;                                               // window = active area + this per side

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
pcb_rear = pcb_y0 + pcb_t;
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
function b2g(p) = [O[0] + p[0],
                   O[1] + p[1]*cos(lean) + p[2]*sin(lean),
                   O[2] - p[1]*sin(lean) + p[2]*cos(lean)];
function flipp(p) = board_flip ? [glass_b - p[0], p[1], glass_a - p[2]] : p;
push_pts = [for (p = push_local) b2g(flipp(p))];
push_ymax = max([for (p = push_pts) p[1]]) + push_d/2;
end_front = max(end_zone, push_ymax + push_gap - wall);
inner_d = bay_l + end_front + end_zone;
D = inner_d + 2*wall;
by0 = wall + end_front;
assert(by0 >= push_ymax + push_gap - 1e-6, "pusher posts too close to the battery bay: raise tilt or end_zone");
boss_xy = [for (x = [wall + boss_r, W - wall - boss_r], y = [wall + boss_r, D - wall - boss_r]) [x, y]];

// ---- helpers ----------------------------------------------------------------
// 2D shape given in (x, z) extruded along +y.
module xz_extrude(h) { rotate([-90, 0, 0]) linear_extrude(height = h) mirror([0, 1, 0]) children(); }
module rrect(x0, z0, w, h, r) { translate([x0 + r, z0 + r]) offset(r = r) square([w - 2*r, h - 2*r]); }

// Board-local frame: x = portrait right, y = INTO the stand from the glass front, z = portrait up.
module board_frame() { translate(O) rotate([-lean, 0, 0]) children(); }
module flip() {
  if (board_flip) translate([glass_b/2, 0, glass_a/2]) rotate([0, 180, 0]) translate([-glass_b/2, 0, -glass_a/2]) children();
  else children();
}
module bf() { board_frame() flip() children(); }

// ---- shell body -------------------------------------------------------------
function profile_pts(zb) = [[0, zb], [0, z0], F1, [D, z_back], [D, zb]];
module profile_outer() polygon(profile_pts(0));
// 2D (y, z) profile extruded along +x
module across(w) { rotate([90, 0, 90]) linear_extrude(w) children(); }
module outer_solid() across(W) profile_outer();
module outer_inset(t) translate([t, 0, 0]) across(W - 2*t) offset(delta = -t) profile_outer();
// open at the bottom: the profile is extended below z=0 before offsetting inwards
module inner_void() translate([wall, 0, 0]) across(W - 2*wall) offset(delta = -wall) polygon(profile_pts(-5));

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

// ---- board pocket and window ----------------------------------------------------
module pocket(c = clr) xz_extrude(board_t + c) rrect(-c, -c, glass_b + 2*c, glass_a + 2*c, glass_r + c);
// through the lip, framing the active area
module window() translate([0, -lip_t - 0.5, 0]) xz_extrude(lip_t + 1.0)
  rrect(act_b0 - win_margin, act_a0 - win_margin, act_b + 2*win_margin, act_a + 2*win_margin, 2);
module pcb_slab() translate([pcb_b0, pcb_y0, pcb_a0]) cube([pcb_b, pcb_t, pcb_a]);
module board_ghost() {   // stand-in for the real board, for assembly renders and tests
  xz_extrude(1.6) rrect(0, 0, glass_b, glass_a, glass_r);
  pcb_slab();
}
// exactly the touch/display area, in front of the glass
module active_prism() translate([act_b0, -lip_t - 0.5, act_a0]) cube([act_b, lip_t + 0.7, act_a]);

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

module base_plate() difference() {
  union() {
    translate([0, 0, -plate_t]) cube([W, D, plate_t]);
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
usb_y = pcb_rear + usb_h/2;     // Type-C centre depth (VERIFY)
btn_y = pcb_rear + 0.8;         // side-button centre depth (VERIFY)
pin_d = 2.4;                    // pinhole for RST / BOOT / PWR

module usb_cut() bf()
  translate([pcb_b0 + pcb_b - 3, usb_y - usb_h/2 - 0.4, usb_a - usb_w/2 - 0.4])
    cube([W, usb_h + 0.8, usb_w + 0.8]);
// Relief for the plug overmold (wider/taller than the slot), cut through the wall so the plug can
// reach within a few mm of the PCB edge. Centred on the slot.
module usb_plug_cut() bf()
  translate([pcb_b0 + pcb_b + usb_plug_x0, usb_y - usb_plug_h/2, usb_a - usb_plug_w/2])
    cube([W, usb_plug_h, usb_plug_w]);
module button_cuts() bf() for (a = btn_a)
  translate([pcb_b0 + 1.0 - W, btn_y, a]) rotate([0, 90, 0]) cylinder(d = pin_d, h = W);

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
else if (part == "check_btn_left")  intersection() { button_cuts(); wall_left(); }
else if (part == "check_btn_right") intersection() { button_cuts(); wall_right(); }
else if (part == "front") front_shell();
else if (part == "base") base_print();
else if (part == "fit") fit_check();
else if (part == "assembly") assembly();
else if (part == "section") section_view();
else assert(false, str("unknown part: ", part));
