// pod.scad: closed, rain-proof pod for the Claude status cube, for the desk and the bike
// (Waveshare ESP32-S3-Touch-LCD-1.69 + 6.2 x 34 x 41 mm LiPo). Units: mm.
// A tub holds the board, a carrier plate behind it and the cell behind that, shifted along the board's
// long axis clear of the chip antenna; a front plate closes it over one flat TPU gasket that seals both
// the glass border and the rim. A quarter-turn boss on the tub's back locks into a desk foot or a bar clamp.
// Every part is modelled where it sits in the pod frame, which is the board-local frame (x = portrait
// right, y = back from the glass front, z = portrait up), and turned into print orientation only for output.
// Rendered by test.sh via the `part` variable (see README.md, "Pod").

part = "assembly";

$fn = 48;
include <board.scad>   // board facts, fit tunables (clr, lip_t, ...) and board helpers

// ---- tunables ---------------------------------------------------------------
board_flip = false;         // only the unflipped board is designed: the bay position follows the antenna
O = [0, 0, 0];  lean = 0;   // the pod frame is the board-local frame
sag = 0;                    // board lowered along the glass, for the checks
glass_dy = 0;               // board moved along y (MJF tolerance), for the checks
nudge = 0.02;               // separates faces that touch by design, in collision checks only

// Cell: [length along z, width along x, thickness along y]
bat = [41, 34, 6.2];
bat_buf = 5;                // extra bay length at the lead end, for a protection board (3..5)
bcl = 0.4;                  // cell clearance, per side
pad_t = 1.0;                // adhesive foam behind the cell, squeezed
lead_end = "bottom";        // end of the cell the lead and the protection board are at: "bottom" | "top"

// Antenna (board.scad: ant_*). The bay starts ant_keep past it; every metal part keeps metal_keep off its
// footprint, at every depth.
ant_keep = 3;
metal_keep = 2;

wall = 3.0;                 // side and bottom walls: room for the gasket and the bottom pilots
top_wall = 4.2;             // top wall: takes the two top pilots
back_t = 2.0;               // back wall behind the cell
step = 1.0;                 // the front cavity runs this far past the bay's top: the carrier's top ledge
carrier_t = 3.2;            // carrier plate (keeps 1.6 behind the lead groove)
pilot_d = 1.8;  pilot_in = 1.4;   // M2 self-tapping pilots; pilot wall to the cavity at the bottom corners
pilot_depth = 8;            // blind, from the rim face
edge_r = 2;                 // outer vertical edges
anchor_d = 1.6;  anchor_l = 3;    // blind hole in the right side for the plug's tether pin
// Gasket and front plate: one flat TPU gasket over the glass border and the rim (flush at y = 0), squeezed
// from gk_t to gk_c by the plate, whose outer skirt lands on the rim and sets the squeeze.
gk_t = 1.2;  gk_sq = 0.3;   // free thickness, squeeze
face_t = 2.0;               // front plate
face_cham = 0.6;            // 45 deg break on the face's outer edge
skirt_w = 1.0;              // skirt round the plate's rear, inside the outline
gk_gap = 0.2;               // gasket edge inside the skirt
seal_land = 1.0;            // least squeezed gasket on any water path into the cavity
pod_win_cham = 1.0;         // the window widens by this at the face
screw_d = 2.4;  gk_hole_d = 2.0;   // clearance in the plate; the gasket grips the M2 shank
cs_d = 4.0;                 // countersink at the face: 90 deg, for a 3.8 M2 countersunk head
screw_l = 8;
// Type-C plug (TPU): a body that fills the plug relief with plug_int interference per side, a flange on the
// side face, and a strap down to a pin pressed into the anchor hole.
plug_int = 0.15;            // VERIFY on the first print: per side, into the relief
plug_flange = 1.0;          // flange past the relief, per side
plug_lip = 1.0;             // flange thickness
plug_gap = 0.2;             // body tip to the relief floor
strap_w = 2.0;  strap_t = 0.8;
pin_d = 1.9;                // into the Ø1.6 anchor hole
plug_open = true;           // check_open_plug_cable: the plug hanging open (false = closed, the control)
// Quarter-turn (Garmin style). VERIFY every value against a real Garmin mount: no published drawing was
// found (third-party designs give only the outer disc, 32.5 or 33.5, and the 20 mm hole spacing), so these
// are starting values. The pod's own receivers use the same values, so the pod fits them either way.
qt_neck_d = 20.0;           // boss core under the tabs
qt_tab_r = 13.5;  qt_tab_w = 9.0;  qt_tab_t = 2.4;   // tab outer radius, chord, thickness
qt_gap = 2.6;               // back face to the tab underside: where the hooks go
qt_hook_t = 2.2;            // hook thickness
qt_clr = 0.2;
qt_base_d = 31;             // receiver outer
qt_floor = 2.0;             // receiver floor behind the boss
qt_detent = 0.4;            // bump height into the notches on the boss's top face
qt_bump_r = 6;  qt_bump_d = 1.6;   // bump position (radius, receiver angle 0) and size
qt_lock = 90;               // insert with the pod landscape, turn 90 deg to portrait
qt_a = 90;  qt_lift = 0;    // check_qt: boss turned qt_a from insert, pulled qt_lift out
// Desk foot. World frame: desk at z = 0, x across, y away from the reader. The pod leans back foot_lean from
// vertical with its boss centre foot_h above the desk; pod_turn = 180 mounts it the other way up.
foot_lean = 20;
foot_h = 32;                // boss centre above the desk
foot_w = 40;  foot_d = 52;  // base footprint; 40 stays inside the pod's sides, so the Type-C side is free
foot_yc = 4;                // base centre, world y (the boss is at y = 0)
foot_base_t = 2;            // base plate under the hull
foot_pad_d = 8;  foot_pad_depth = 0.8;  foot_pad_in = 6;   // rubber-foot recesses
desk_gap = 2;               // least gap from the pod to the desk
pod_turn = 0;
// Handlebar clamp. Clamp frame: bar along x through the origin, halves split at z = 0 with a `pinch` gap,
// the receiver on top with the locked pod's long axis across the bar (along y).
bar_d = 31.8;               // the bar in the checks
bore_d = 31.8;              // the clamp's bore (a 31.8 bar, or a shim)
shim_d = [25.4, 22.2];  shim = 0;   // shim bores; shim = 1 | 2 fits that bar, 0 = none
clamp_w = 16;               // along the bar
clamp_wall = 3;             // round the bore
pinch = 1.0;                // gap between the halves, closed by the screws
ear_t = 6;                  // screw ears, each half
m3_d = 3.4;  m3_cb_d = 6.4;   // M3 clearance, head counterbore
nut_af = 5.7;  nut_h = 2.6;   // nut trap in the lower half
shim_l = 14;
ledge = 0;                  // check_ledges: 0 = bottom ledge, 1 = top ledge
carrier_clr = 0.2;          // carrier outline inside the cavity, per side
post_d = 3;  preload = 0.2; // posts press this far into the PCB rear
peg_d = 1.8;  peg_l = 1.0;  // pegs into the PCB notches; peg_l = 0 turns them off

// Battery lead, as in the easel: the MX1.25 BAT header beside the Type-C opens towards the board centre, so
// the lead leaves flat across the PCB rear, climbs into a groove in the carrier's front face at cord_x and
// runs to a slot through the carrier into the bay's buffer, at the cell's lead end.
bat_x = 25.3;  bat_z = 10.5;                  // STEP: header inner face (board x), header centre (board z); VERIFY it opens inwards
bat_h = 3.6;  bat_w = 5.0;  bat_plug = 3.5;   // mated height off the PCB (STEP), plug width along z; VERIFY: plug out of the header
cord_d = 1.6;                                 // the lead (two 28 AWG wires), as a round envelope
cord_w = 3.0;  cord_depth = 1.6;              // groove width, and depth into the carrier
cord_x = 20;                                  // board x of the groove
bat_exit = 3.5;                               // VERIFY: free run past the plug where the lead climbs over the rear parts
slot_w = 4;  slot_h = 3;                      // lead slot through the carrier (x, z)

assert(!board_flip, "pod: board_flip is not supported");
assert(bat_buf >= 3 && bat_buf <= 5, "pod: bat_buf must be 3..5");
assert(lead_end == "bottom" || lead_end == "top", "pod: lead_end must be \"bottom\" or \"top\"");

// ---- derived ----------------------------------------------------------------
bay_w = bat[1] + 2*bcl;  bay_l = bat[0] + bat_buf + 2*bcl;  bay_t = bat[2] + 2*bcl + pad_t;
bay_x0 = glass_b/2 - bay_w/2;  bay_x1 = bay_x0 + bay_w;
bay_z0 = ant_a1 + ant_keep;  bay_z1 = bay_z0 + bay_l;
plate_y0 = board_t + clr;                      // carrier front face
bay_y0 = plate_y0 + carrier_t;                 // carrier rear face = cell front
bay_y1 = bay_y0 + bay_t;
back_y = bay_y1 + back_t;                      // tub rear face
cav_z1 = bay_z1 + step;
out_x0 = bay_x0 - wall;  out_x1 = bay_x1 + wall;
out_z0 = -clr - wall;    out_z1 = cav_z1 + top_wall;
// z range of the pouch and of the buffer block (protection board) inside the bay
buf_z0 = lead_end == "bottom" ? bay_z0 + bcl : bay_z1 - bcl - bat_buf;
pouch_z0 = lead_end == "bottom" ? bay_z0 + bcl + bat_buf : bay_z0 + bcl;
// Screw points (x, z): two on the diagonal out of each lower corner arc of the cavity, two in the top wall.
pilot_r = glass_r + clr + pilot_in + pilot_d/2;
screw_pts = [[glass_r - pilot_r/sqrt(2), glass_r - pilot_r/sqrt(2)],
             [glass_b - glass_r + pilot_r/sqrt(2), glass_r - pilot_r/sqrt(2)],
             [bay_x0 + 5, cav_z1 + top_wall/2], [bay_x1 - 5, cav_z1 + top_wall/2]];
anchor_pt = [usb_y, usb_a - usb_plug_w/2 - 3];   // tether anchor on the right side face (board y, z)
// PCB notches (board-local x, z)
holes = [for (bx = [hole_ib, pcb_b - hole_ib], az = [hole_ia, pcb_a - hole_ia]) [pcb_b0 + bx, pcb_a0 + az]];
shoulder_y = pcb_rear - preload;
slot_z = lead_end == "bottom" ? bay_z0 + bat_buf/2 : bay_z1 - bat_buf/2;   // centred on the buffer
cord_y = plate_y0 + cord_depth - cord_d/2 - 0.1;

// ---- ghosts ------------------------------------------------------------------------
// The board with its four PCB notches, moved by glass_dy and lowered by sag.
module board_at() translate([0, glass_dy, -sag]) difference() {
  board_ghost();
  for (h = holes) translate([h[0], pcb_y0 - 0.1, h[1]]) rotate([-90, 0, 0]) cylinder(d = hole_d, h = pcb_t + 0.2);
}
// pouch and protection-board block, the full cell section, against the carrier
module cell_ghost() translate([bay_x0 + bcl, bay_y0 + bcl, 0]) {
  translate([0, 0, pouch_z0]) cube([bat[1], bat[2], bat[0]]);
  translate([0, 0, buf_z0]) cube([bat[1], bat[2], bat_buf]);
}
// the antenna footprint grown by g in x and z, through every depth
module antenna_zone(g) translate([ant_b0 - g, -50, ant_a0 - g]) cube([ant_b1 - ant_b0 + 2*g, 100, ant_a1 - ant_a0 + 2*g]);
module metal() { cell_ghost(); cord_ghost(); screws(); }

// ---- carrier --------------------------------------------------------------------------
// Prints rear face down. Sits in the front cavity on two ledges of the tub (below the bay and past its top),
// presses the PCB rear with four posts, locates it with pegs in its notches, and parts the board from the cell.
// a cylinder along +y at (x, z), from y0 to y1
module ycyl(p, d, y0, y1) translate([p[0], y0, p[1]]) rotate([-90, 0, 0]) cylinder(d = d, h = y1 - y0);
// The front cavity (x, z): the glass pocket, widened to the bay above the lower corner arcs, up to cav_z1.
module cav2d() union() {
  rrect(-clr, -clr, glass_b + 2*clr, glass_a + 2*clr, glass_r + clr);
  translate([bay_x0, min(glass_r, bay_z0)]) square([bay_w, cav_z1 - min(glass_r, bay_z0)]);
}
module posts() for (h = holes) ycyl(h, post_d, shoulder_y, plate_y0 + 0.01);
module pegs() if (peg_l > 0) for (h = holes) ycyl(h, peg_d, shoulder_y - peg_l, shoulder_y + 0.01);
module cord_groove() hull() for (z = [bat_z, slot_z]) ycyl([cord_x, z], cord_w, plate_y0 - 0.01, plate_y0 + cord_depth);
module lead_slot() translate([cord_x - slot_w/2, plate_y0 - 0.01, slot_z - slot_h/2]) cube([slot_w, carrier_t + 0.02, slot_h]);
module carrier() difference() {
  union() {
    translate([0, plate_y0, 0]) xz_extrude(carrier_t) offset(delta = -carrier_clr) cav2d();
    posts();
    pegs();
  }
  cord_groove();
  lead_slot();
}
module carrier_print() translate([0, 0, bay_y0]) rotate([-90, 0, 0]) carrier();
// Parts on the PCB rear, up to board_t, except around the header and the run where the lead climbs out.
module rear_parts() difference() {
  translate([pcb_b0, pcb_rear + nudge, pcb_a0]) cube([pcb_b, board_t - pcb_rear - nudge, pcb_a]);
  translate([bat_x - bat_plug - bat_exit, 0, bat_z - bat_w/2 - nudge])
    cube([pcb_b0 + pcb_b + 1 - (bat_x - bat_plug - bat_exit), board_t + 1, bat_w + 2*nudge]);
}
// the mated plug on the header, and the lead from it along the groove, through the slot, 1 mm into the bay
cord_pts = [[bat_x - bat_plug - 0.5, pcb_rear + bat_h/2, bat_z], [cord_x, cord_y, bat_z], [cord_x, cord_y, slot_z],
            [cord_x, bay_y0 + 1, slot_z]];
module cord_ghost() {
  translate([bat_x - bat_plug, pcb_rear + nudge, bat_z - bat_w/2]) cube([bat_plug + 4.3, bat_h, bat_w]);
  for (i = [0 : len(cord_pts) - 2]) hull() for (q = [cord_pts[i], cord_pts[i + 1]]) translate(q) sphere(d = cord_d, $fn = 16);
}

// ---- tub --------------------------------------------------------------------------------
// Prints in any orientation (MJF). Rim face at y = 0, flush with the glass front; the gasket and the plate
// close it. Holds the board in the front cavity, the carrier on its two ledges and the cell in the bay.
module tub_body() xz_extrude(back_y) rrect(out_x0, out_z0, out_x1 - out_x0, out_z1 - out_z0, edge_r);
module pilots(d = pilot_d, y1 = pilot_depth) for (p = screw_pts) ycyl(p, d, -1, y1);
module anchor_hole() translate([out_x1 + 0.01, anchor_pt[0], anchor_pt[1]]) rotate([0, -90, 0]) cylinder(d = anchor_d, h = anchor_l + 0.01);
module tub() union() { tub_shell(); qt_on_pod() qt_boss(); }
module tub_shell() difference() {
  tub_body();
  translate([0, -1, 0]) xz_extrude(bay_y0 + 1) cav2d();
  translate([bay_x0, bay_y0 - 0.01, bay_z0]) cube([bay_w, bay_t + 0.01, bay_l]);
  usb_slot(20);
  usb_plug_relief(20);
  pilots();
  anchor_hole();
}
module tub_print() translate([0, 0, back_y]) rotate([-90, 0, 0]) tub();

// ---- front plate and gasket -------------------------------------------------------------
gk_c = gk_t - gk_sq;                           // squeezed thickness
face_y = -gk_c - face_t;                       // front face
module outline2d() rrect(out_x0, out_z0, out_x1 - out_x0, out_z1 - out_z0, edge_r);
module gasket2d() offset(delta = -skirt_w - gk_gap) outline2d();
// the window outline: board.scad's win_rect with r = 3 corners, which keep seal_land of gasket on the glass
// inside the glass's r = 6 corners (r = 2 leaves 0.84)
win_r = 3;
module pod_win_rect(g = 0) rrect(act_b0 - win_margin - g, act_a0 - win_margin - g,
                                 act_b + 2*(win_margin + g), act_a + 2*(win_margin + g), win_r + g);
// through plate and gasket, framing the active area, widening by pod_win_cham at the face
module pod_window() {
  translate([0, face_y - 0.5, 0]) xz_extrude(-face_y + 0.6) pod_win_rect();
  hull() {
    translate([0, face_y - 0.5, 0]) xz_extrude(0.01) pod_win_rect(pod_win_cham + 0.5);
    translate([0, face_y + pod_win_cham, 0]) xz_extrude(0.01) pod_win_rect();
  }
}
module countersinks() for (p = screw_pts) {
  ycyl(p, screw_d, face_y - 0.1, 0.1);
  translate([p[0], face_y - 0.01, p[1]]) rotate([-90, 0, 0]) cylinder(d1 = cs_d + 0.02, d2 = screw_d, h = (cs_d - screw_d)/2 + 0.01);
}
module plate() difference() {
  union() {
    hull() {
      translate([0, face_y, 0]) xz_extrude(0.01) offset(delta = -face_cham) outline2d();
      translate([0, face_y + face_cham, 0]) xz_extrude(face_t - face_cham) outline2d();
    }
    translate([0, -gk_c - 0.01, 0]) xz_extrude(gk_c + 0.01) difference() { outline2d(); offset(delta = -skirt_w) outline2d(); }
  }
  pod_window();
  countersinks();
}
module plate_print() translate([0, 0, -face_y]) rotate([90, 0, 0]) plate();
// the gasket t thick, its front face on the plate: t = gk_c squeezed in place, t = gk_t as if the plate were not there
module gasket(t = gk_c) difference() {
  translate([0, -gk_c, 0]) xz_extrude(t) gasket2d();
  pod_window();
  for (p = screw_pts) ycyl(p, gk_hole_d, -gk_c - 1, t + 1);
}
module gasket_print() translate([0, 0, gk_t - gk_c]) rotate([90, 0, 0]) gasket(gk_t);
// countersunk M2 x screw_l, head flush with the face
module screws() for (p = screw_pts) {
  ycyl(p, 2, face_y, face_y + screw_l);
  translate([p[0], face_y, p[1]]) rotate([-90, 0, 0]) cylinder(d1 = 3.8, d2 = 2, h = 0.9);
}

// ---- Type-C plug ------------------------------------------------------------------------
// Plug-local frame (print orientation): x = board z, y = board y, z = into the wall from the flange's outer
// face; origin on the relief centre. plug_place() maps it onto the tub's right side.
plug_depth = out_x1 - (pcb_b0 + pcb_b + usb_plug_x0) - plug_gap;
plug_root = -(usb_plug_w/2 + plug_flange);    // flange lower edge, where the strap starts and the plug hinges
pin_x = anchor_pt[1] - usb_a;
module plug_place() translate([out_x1 + plug_lip, usb_y, usb_a]) rotate([0, -90, 0]) children();
module plug_flange2d() square([usb_plug_w + 2*plug_flange, usb_plug_h + 2*plug_flange], center = true);
module plug_body() hull() {   // 0.3 lead-in at the tip
  translate([0, 0, plug_lip - 0.01]) linear_extrude(plug_depth - 0.3 + 0.01) square([usb_plug_w + 2*plug_int, usb_plug_h + 2*plug_int], center = true);
  translate([0, 0, plug_lip]) linear_extrude(plug_depth) square([usb_plug_w + 2*plug_int - 0.6, usb_plug_h + 2*plug_int - 0.6], center = true);
}
module plug_cap() { linear_extrude(plug_lip) plug_flange2d(); plug_body(); }
module plug_strap() {
  translate([pin_x - pin_d/2 - 0.6, -strap_w/2, plug_lip - strap_t]) cube([plug_root + 0.5 - (pin_x - pin_d/2 - 0.6), strap_w, strap_t]);
  translate([pin_x, 0, plug_lip - strap_t]) cylinder(d = pin_d, h = strap_t + anchor_l - 0.2);
}
module tplug() { plug_cap(); plug_strap(); }
// open: the cap turned 180 deg about the flange's lower outer edge, hanging below the opening
module plug_cap_open() translate([plug_root, 0, 0]) rotate([0, 180, 0]) translate([-plug_root, 0, 0]) plug_cap();
module plug_print() tplug();
// a Type-C overmold straight out from the relief floor, and the cable on from it
module cable_ghost() {
  translate([pcb_b0 + pcb_b + usb_plug_x0 + 0.2, usb_y - 3.25, usb_a - 6.25]) cube([25, 6.5, 12.5]);
  translate([pcb_b0 + pcb_b + usb_plug_x0 + 25, usb_y, usb_a]) rotate([0, 90, 0]) cylinder(d = 4, h = 60);
}
// the strip of side face the flange must land on (the relief opening is the plug minus plug_int)
module plug_seat() plug_place() translate([0, 0, plug_lip + nudge]) linear_extrude(0.2) difference() {
  plug_flange2d();
  square([usb_plug_w + 2*nudge, usb_plug_h + 2*nudge], center = true);
}

// ---- quarter-turn ----------------------------------------------------------------------
// Boss-local frame: the pod's back face at z = 0, the boss up along +z, locked tabs along ±x (across the pod).
// The receiver uses the same frame with the boss in place, locked; it starts qt_clr above the back face.
qt_top = qt_gap + qt_tab_t;                    // boss top face
qt_floor_z = qt_top + qt_clr;                  // receiver floor face
qt_c = [glass_b/2, (out_z0 + out_z1)/2];       // boss centre on the back (pod x, z)
module qt_tabs(r = qt_tab_r, w = qt_tab_w, z0 = qt_gap, h = qt_tab_t) translate([0, 0, z0]) intersection() {
  cylinder(r = r, h = h, $fn = 96);
  translate([-r - 1, -w/2, -1]) cube([2*r + 2, w, h + 2]);
}
module qt_boss() difference() {
  union() { translate([0, 0, -0.01]) cylinder(d = qt_neck_d, h = qt_top + 0.01, $fn = 96); qt_tabs(); }
  // the detent notches, both ends, so the pod locks either way up
  for (sx = [-1, 1]) translate([sx*qt_bump_r - (qt_bump_d + 0.2)/2, -(qt_bump_d + 0.2)/2, qt_top - qt_detent - 0.1])
    cube([qt_bump_d + 0.2, qt_bump_d + 0.2, 1]);
}
// boss-local -> pod, and back
module qt_on_pod() translate([qt_c[0], back_y, qt_c[1]]) rotate([-90, 0, 0]) children();
module qt_from_pod() rotate([90, 0, 0]) translate([-qt_c[0], -back_y, -qt_c[1]]) children();
module qt_bump() translate([qt_bump_r, 0, qt_floor_z - qt_detent]) cylinder(d = qt_bump_d, h = qt_detent + 0.01);
// the cup and hooks; the bump sits on a finger cut free on three sides and thinned to 0.8, which flexes on the turn
module qt_receiver() {
  difference() {
    translate([0, 0, qt_clr]) cylinder(d = qt_base_d, h = qt_floor_z + qt_floor - qt_clr, $fn = 96);
    translate([0, 0, -1]) cylinder(r = qt_neck_d/2 + qt_clr, h = qt_floor_z + 1, $fn = 96);                 // through the hooks
    translate([0, 0, qt_gap - qt_clr]) cylinder(r = qt_tab_r + qt_clr, h = qt_floor_z - qt_gap + qt_clr, $fn = 96);   // above them
    // entry gaps for the tabs at insert (along ±y)
    translate([0, 0, -1]) intersection() {
      cylinder(r = qt_tab_r + qt_clr, h = qt_floor_z + 1, $fn = 96);
      translate([-(qt_tab_w/2 + qt_clr), -qt_base_d, 0]) cube([qt_tab_w + 2*qt_clr, 2*qt_base_d, qt_floor_z + 1]);
    }
    // the finger: 6 long from the bump's far side, 3 wide, slots 0.6, thinned from behind
    translate([qt_bump_r - 3.6, -2.1, qt_floor_z - 0.01]) difference() {
      cube([6.6, 4.2, qt_floor + 0.02]);
      translate([0.6, 0.6, -0.01]) cube([6.1, 3, qt_floor + 0.04]);
    }
    translate([qt_bump_r - 3, -1.5, qt_floor_z + 0.8]) cube([6, 3, qt_floor]);
  }
  if (qt_detent > 0) qt_bump();
}
module qt_receiver_body() difference() { qt_receiver(); qt_bump(); }
// the boss turned a deg from insert and pulled `lift` back out of the receiver
module qt_turn(a, lift) translate([0, 0, -lift]) rotate([0, 0, a - qt_lock]) children();

// ---- desk foot ---------------------------------------------------------------------------
// Mount frame M: the boss centre on the pod's back plane, pod axes. M -> world on the foot.
module foot_frame() translate([0, 0, foot_h]) rotate([-foot_lean, 0, 0]) children();
module on_foot() foot_frame() rotate([0, pod_turn, 0]) translate([-qt_c[0], -back_y, -qt_c[1]]) children();
module receiver_on_foot() foot_frame() rotate([-90, 0, 0]) qt_receiver();
qt_back = qt_floor_z + qt_floor;               // receiver's rear face, M y
// the pod's envelope (tub, boss excepted, and plate), grown by g
module pod_box(g = 0) translate([out_x0 - g, face_y - g, out_z0 - g]) cube([out_x1 - out_x0 + 2*g, back_y - face_y + 2*g, out_z1 - out_z0 + 2*g]);
module foot_base2d() translate([-foot_w/2, foot_yc - foot_d/2]) offset(r = 3) offset(delta = -3) square([foot_w, foot_d]);
module foot() difference() {
  union() {
    difference() {
      hull() {
        foot_frame() translate([0, qt_back - 0.01, 0]) rotate([-90, 0, 0]) cylinder(d = qt_base_d, h = 2, $fn = 96);
        linear_extrude(foot_base_t) foot_base2d();
      }
      on_foot() pod_box(0.5);
      foot_frame() translate([0, -50, 0]) rotate([-90, 0, 0]) cylinder(d = qt_base_d + 0.4, h = 50 + qt_back - 0.5, $fn = 96);   // the receiver overlaps the hull by 0.5
    }
    receiver_on_foot();
  }
  for (sx = [-1, 1], sy = [-1, 1]) translate([sx*(foot_w/2 - foot_pad_in), foot_yc + sy*(foot_d/2 - foot_pad_in), -0.01])
    cylinder(d = foot_pad_d, h = foot_pad_depth + 0.01);
}
module foot_pod() on_foot() { tub(); plate(); }
// the pod's centre (its envelope's, where the cell and the board sit) in world coordinates
function rotx(p, a) = [p[0], p[1]*cos(a) - p[2]*sin(a), p[1]*sin(a) + p[2]*cos(a)];
pod_ctr_m = [(out_x0 + out_x1)/2 - qt_c[0], (face_y + back_y)/2 - back_y, (out_z0 + out_z1)/2 - qt_c[1]];
pod_ctr = [0, 0, foot_h] + rotx(pod_turn == 180 ? [-pod_ctr_m[0], pod_ctr_m[1], -pod_ctr_m[2]] : pod_ctr_m, -foot_lean);

// ---- handlebar clamp -------------------------------------------------------------------
clamp_top_z = bore_d/2 + clamp_wall;           // top of the tube; the receiver stands on it
screw_y = bore_d/2 + m3_d/2 + 1.5;             // the two screws, in front of and behind the bar
ear_y = screw_y + nut_af/2 + 1.5;
module xcyl(d, l) translate([-l/2, 0, 0]) rotate([0, 90, 0]) cylinder(d = d, h = l, $fn = 96);
module upper(z0 = 0) translate([-100, -100, z0]) cube([200, 200, 100]);
module lower(z1 = 0) translate([-100, -100, z1 - 100]) cube([200, 200, 100]);
module ears() translate([-clamp_w/2, -ear_y, -pinch/2 - ear_t]) cube([clamp_w, 2*ear_y, 2*ear_t + pinch]);
// the tube, a pedestal from its upper half to the receiver (no wider than the clamp along the bar, so the
// receiver overhangs and the bar runs free), and the ears
module clamp_raw() union() {
  xcyl(bore_d + 2*clamp_wall, clamp_w);
  intersection() {
    hull() {
      intersection() { xcyl(bore_d + 2*clamp_wall, clamp_w); upper(0); }
      translate([0, 0, clamp_top_z - 0.01]) cylinder(d = qt_base_d, h = 0.01, $fn = 96);
    }
    translate([-clamp_w/2, -50, 0]) cube([clamp_w, 100, 100]);
  }
  ears();
}
module clamp_holes() for (sy = [-1, 1]) {
  translate([0, sy*screw_y, -50]) cylinder(d = m3_d, h = 100);
  translate([0, sy*screw_y, pinch/2 + ear_t]) cylinder(d = m3_cb_d, h = 50);
  translate([0, sy*screw_y, -pinch/2 - ear_t - 0.01]) cylinder(d = nut_af/cos(30), h = nut_h + 0.01, $fn = 6);
}
// the receiver stands on the tube, floor down, overlapping it 0.5
module clamp_receiver() translate([0, 0, clamp_top_z - 0.5 + qt_back]) rotate([180, 0, 0]) qt_receiver();
module clamp_top() union() {
  difference() { intersection() { clamp_raw(); upper(pinch/2); } xcyl(bore_d, 200); clamp_holes(); }
  clamp_receiver();
}
module clamp_bottom() difference() { intersection() { clamp_raw(); lower(-pinch/2); } xcyl(bore_d, 200); clamp_holes(); }
module shim_half(i, top = true) intersection() {
  difference() { xcyl(bore_d, shim_l); xcyl(shim_d[i - 1], shim_l + 2); }
  if (top) upper(pinch/2); else lower(-pinch/2);
}
module shims() if (shim > 0) { shim_half(shim, true); shim_half(shim, false); }
// M3 x 16, head in the counterbore, nut in the trap
module clamp_screws() for (sy = [-1, 1]) translate([0, sy*screw_y, 0]) {
  translate([0, 0, -pinch/2 - ear_t + 0.1]) cylinder(d = 3, h = 16);
  translate([0, 0, -pinch/2 - ear_t + 16 + 0.1]) cylinder(d = 5.5, h = 3);
  translate([0, 0, -pinch/2 - ear_t + 0.1]) cylinder(d = 5.5/cos(30), h = 2.4, $fn = 6);
}

// ---- assembly and the all-parts check ----------------------------------------------
module pod_assembly() {
  color("gainsboro") tub();
  color("lightgray") carrier();
  color("royalblue", 0.6) cell_ghost();
  color("black") board_at();
  color("red") cord_ghost();
  color("dimgray") gasket();
  color("whitesmoke", 0.9) plate();
  color("dimgray") plug_place() tplug();
}
// cut at the board's mid x, keeping the right side
module section_view() difference() { pod_assembly(); translate([-50, -50, -50]) cube([50 + glass_b/2, 100, 150]); }
module on_foot_view() { color("gainsboro") foot(); on_foot() pod_assembly(); }
module on_bar_view() {
  color("gainsboro") { clamp_top(); clamp_bottom(); }
  color("dimgray") xcyl(bar_d, 80);
  translate([0, 0, clamp_top_z - 0.5 + qt_back]) rotate([180, 0, 0]) qt_from_pod() pod_assembly();
}
// Each part moved by nudge off what it rests on (the carrier forward off its ledges, gasket and plate forward
// off the glass and the rim): any overlap left is a real collision. The plug, the lead and the screws bite
// on purpose (interference, the lead into the protection board, threads) and have their own checks.
module fit_parts(i) {
  if (i == 0) tub();
  if (i == 1) translate([0, -nudge, 0]) carrier();
  if (i == 2) cell_ghost();
  if (i == 3) board_at();
  if (i == 4) translate([0, -nudge, 0]) gasket();
  if (i == 5) translate([0, -2*nudge, 0]) plate();
}

// ---- reference values ------------------------------------------------------------
module meta() echo(str("POD{\"w\":", out_x1 - out_x0, ",\"l\":", out_z1 - out_z0, ",\"d\":", back_y, "}"));

// ---- parts (rendered by test.sh) ----------------------------------------------
if (part == "none") {}
else if (part == "meta") meta();
else if (part == "board_frame") echo_board_frame(sag);
else if (part == "at_board") board_at();
else if (part == "at_cell") cell_ghost();
else if (part == "at_carrier") carrier();
else if (part == "carrier") carrier_print();
else if (part == "check_carrier_board") intersection() { carrier(); board_at(); }
else if (part == "check_shoulders") intersection() { posts(); board_at(); }
else if (part == "check_pegs") intersection() { pegs(); board_at(); }
else if (part == "check_cell_carrier") intersection() { cell_ghost(); carrier(); }
else if (part == "check_cord") intersection() { cord_ghost(); union() { carrier(); board_at(); rear_parts(); } }
else if (part == "at_tub") tub();
else if (part == "tub") tub_print();
else if (part == "check_board_tub") intersection() { board_at(); tub(); }
else if (part == "check_carrier_tub") intersection() { translate([0, -nudge, 0]) carrier(); tub(); }
// the carrier pushed 0.1 back meets the tub only on its ledges: below the bay, and past its top
else if (part == "check_ledges") intersection() {
    translate([0, 0.1, 0]) carrier(); tub();
    translate([-50, -50, ledge == 0 ? -50 : bay_z1]) cube([100, 100, ledge == 0 ? 50 + bay_z0 : 50]); }
else if (part == "check_cell_tub") intersection() { cell_ghost(); tub(); }
// a 1.0 wall round each pilot: the pilot grown by 1.0 (and 1.0 past its floor) must stay inside the tub
else if (part == "check_pilot_wall") difference() { pilots(pilot_d + 2, pilot_depth + 1); tub(); pilots(pilot_d + 2*nudge, pilot_depth + nudge); translate([-50, -50, -50]) cube([100, 50, 200]); }
else if (part == "at_plate") plate();
else if (part == "at_gasket") gasket();
else if (part == "plate") plate_print();
else if (part == "gasket") gasket_print();
else if (part == "check_window") intersection() { union() { plate(); gasket(); } translate([act_b0, -4, act_a0]) cube([act_b, 4 - nudge, act_a]); }
else if (part == "check_gasket_glass") intersection() { translate([0, -nudge, 0]) gasket(gk_t); board_at(); }
else if (part == "check_gasket_rim") intersection() { translate([0, -nudge, 0]) gasket(gk_t); tub(); }
else if (part == "check_plate_tub") intersection() { translate([0, -nudge, 0]) plate(); union() { tub(); board_at(); } }
// the water paths into the cavity: screw hole -> under the gasket -> cavity, seam -> gasket edge -> cavity,
// window -> under the gasket on the glass -> glass edge; each must cross seal_land of gasket
else if (part == "check_seal_land") linear_extrude(1) intersection() {
    for (p = screw_pts) translate(p) circle(d = gk_hole_d + 2*seal_land); cav2d(); }
else if (part == "check_gasket_band") linear_extrude(1) difference() { offset(r = seal_land) cav2d(); gasket2d(); }
else if (part == "check_glass_land") linear_extrude(1) difference() { offset(r = seal_land) pod_win_rect(); rrect(0, 0, glass_b, glass_a, glass_r); }
else if (part == "at_plug") plug_place() tplug();
else if (part == "plug") plug_print();
// the body alone: the flange touches the side face by design, the pin bites the anchor hole by design
else if (part == "check_plug_seal") intersection() { plug_place() plug_body(); tub(); }
else if (part == "check_plug_seat") difference() { plug_seat(); tub(); }
else if (part == "check_plug_board") intersection() { plug_place() tplug(); board_at(); }
else if (part == "check_open_plug_cable") intersection() { plug_place() if (plug_open) plug_cap_open(); else plug_cap(); cable_ghost(); }
// the receiver without its bump (a flexing finger), and the bump alone against the boss
else if (part == "check_qt") intersection() { qt_turn(qt_a, qt_lift) qt_boss(); qt_receiver_body(); }
else if (part == "check_qt_detent") intersection() { qt_turn(qt_a, 0) qt_boss(); qt_bump(); }
else if (part == "qt_test_boss") { translate([-18, -18, -4]) cube([36, 36, 4]); qt_boss(); }
else if (part == "qt_test_receiver") translate([0, 0, qt_floor_z + qt_floor]) rotate([180, 0, 0]) qt_receiver();
else if (part == "foot") foot();
else if (part == "check_foot_pod") intersection() { foot(); foot_pod(); }
else if (part == "check_foot_desk") intersection() { foot_pod(); translate([-100, -100, -100]) cube([200, 200, 100 + desk_gap]); }
else if (part == "check_foot_cable") intersection() { foot(); on_foot() { cable_ghost(); plug_place() plug_cap_open(); } }
else if (part == "check_foot_cog") assert(abs(pod_ctr[0]) <= foot_w/2 - 5 && abs(pod_ctr[1] - foot_yc) <= foot_d/2 - 5,
                                         str("pod centre ", pod_ctr, " is not 5 inside the foot"));
else if (part == "clamp_top") translate([0, 0, -pinch/2]) clamp_top();
else if (part == "clamp_bottom") translate([0, 0, pinch/2 + ear_t]) rotate([180, 0, 0]) clamp_bottom();
else if (part == "shim_25") translate([0, 0, -pinch/2]) shim_half(1);
else if (part == "shim_22") translate([0, 0, -pinch/2]) shim_half(2);
else if (part == "check_bar") intersection() { xcyl((shim > 0 ? shim_d[shim - 1] : bar_d) - 2*nudge, 60); union() { clamp_top(); clamp_bottom(); shims(); } }
else if (part == "check_clamp_halves") intersection() { clamp_top(); clamp_bottom(); }
else if (part == "check_clamp_screws") intersection() { clamp_screws(); union() { clamp_top(); clamp_bottom(); } }
else if (part == "check_fit") for (i = [0:4], j = [i + 1:5]) intersection() { fit_parts(i); fit_parts(j); }
else if (part == "assembly") pod_assembly();
else if (part == "section") section_view();
else if (part == "on_foot_view") on_foot_view();
else if (part == "on_bar_view") on_bar_view();
else if (part == "check_antenna_metal") intersection() { metal(); antenna_zone(metal_keep); }
else assert(false, str("unknown part: ", part));
