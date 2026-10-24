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
module metal() { cell_ghost(); cord_ghost(); }

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
else if (part == "check_antenna_metal") intersection() { metal(); antenna_zone(metal_keep); }
else assert(false, str("unknown part: ", part));
