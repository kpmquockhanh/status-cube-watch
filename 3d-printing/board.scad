// board.scad: the Waveshare ESP32-S3-Touch-LCD-1.69 as both stands see it. Units: mm.
// Board facts, fit tunables and board-local helpers. No geometry and no $fn here: `include` it right
// after `$fn = 48;`. The includer defines `O` (board origin in world coordinates), `lean` (screen
// lean from vertical) and `board_flip`; the frame helpers read them when called. Assign no variable here
// and there too.

// ---- fit tunables -------------------------------------------------------------
lip_t = 1.0;                // material in front of the glass
clr = 0.3;                  // glass pocket clearance, per side
win_cham = 0.6;      // 45 deg break around the window, where a finger meets the screen edge

// Board, from the Waveshare drawing, corrected from Waveshare's STEP model (ESP32-S3-Touch-LCD-1_69.stp).
// Drawing coords: a = from its left edge, b = from its top edge.
// In the firmware's portrait view: portrait x = b, portrait z = a.
glass_a = 41.13;  glass_b = 33.13;  glass_r = 6;               // STEP: corners r ~6.4 and slightly bowed ends; 6 keeps the pocket outside
act_a0 = 2.97;  act_b0 = 2.585;  act_a = 32.634;  act_b = 27.972; // offsets from the STEP
pcb_a = 37.14;  pcb_b = 29.85;  pcb_a0 = 1.91;  pcb_b0 = 1.64;  // STEP
lcd_a = 37.42;  lcd_b = 30.08;  lcd_a0 = 1.81;  lcd_b0 = 1.53;  lcd_y1 = 3.8;  // STEP: LCD module behind the glass
foam_a = 18;  foam_b = 28;  foam_a0 = 7.29;  foam_b0 = 2.53;   // STEP: 1.5 mm foam pad from the LCD to the PCB
pcb_y0 = 5.3;  pcb_t = 1.0;                                     // STEP: 3.8 glass + LCD, 1.5 foam pad, then the PCB
board_t = 10.7;                                                 // STEP: glass front to tallest rear part (a 2-pin 1.0 mm header)
hole_d = 2.36;  hole_ia = 3.2;  hole_ib = 0.59;                 // STEP: not holes but notches open to the long edges; centres from the PCB corner (ends 3.13 / 3.26, averaged)
btn_a = [11.23, 20.14, 29.05];                                  // STEP: RST, BOOT, PWR along portrait z (VERIFY which is which)
usb_a = 20.3;  usb_w = 9.0;  usb_h = 3.3;                       // STEP: centre 20.31, shell 8.34 wide
usb_plug_w = 13;  usb_plug_h = 7.5;  usb_plug_x0 = 3;           // VERIFY: Type-C plug overmold relief; x0 = start beyond the PCB edge
win_margin = 1.0;                                               // window = active area + this per side

// ---- derived ------------------------------------------------------------------
pcb_rear = pcb_y0 + pcb_t;
usb_y = pcb_rear + 1.27;        // Type-C centre depth: mid-mount receptacle (STEP)
btn_y = pcb_rear + 0.7;         // side-button centre depth (STEP: switch body 5.9..8.1)
pin_d = 2.4;                    // pinhole for RST / BOOT / PWR

// Board-local point to world (reads the includer's O and lean).
function b2g(p) = [O[0] + p[0],
                   O[1] + p[1]*cos(lean) + p[2]*sin(lean),
                   O[2] - p[1]*sin(lean) + p[2]*cos(lean)];
function flipp(p) = board_flip ? [glass_b - p[0], p[1], glass_a - p[2]] : p;

// ---- helpers ----------------------------------------------------------------
// 2D shape given in (x, z) extruded along +y.
module xz_extrude(h) { rotate([-90, 0, 0]) linear_extrude(height = h) mirror([0, 1, 0]) children(); }
module rrect(x0, z0, w, h, r) { translate([x0 + r, z0 + r]) offset(r = r) square([w - 2*r, h - 2*r]); }
// Rounds the convex corners of a 2D shape by r (shrink then grow back); r <= 0 leaves it alone.
module round2d(r) { if (r > 0.01) offset(r = r) offset(r = -r) children(); else children(); }
// 2D (y, z) profile extruded along +x
module across(w) { rotate([90, 0, 90]) linear_extrude(w) children(); }

// Board-local frame: x = portrait right, y = INTO the stand from the glass front, z = portrait up.
module board_frame() { translate(O) rotate([-lean, 0, 0]) children(); }
module flip() {
  if (board_flip) translate([glass_b/2, 0, glass_a/2]) rotate([0, 180, 0]) translate([-glass_b/2, 0, -glass_a/2]) children();
  else children();
}
module bf() { board_frame() flip() children(); }

// ---- board pocket and window ----------------------------------------------------
// `extra` deepens the pocket past board_t + c (a pocket that must break through a rear face)
module pocket(c = clr, extra = 0) xz_extrude(board_t + c + extra) rrect(-c, -c, glass_b + 2*c, glass_a + 2*c, glass_r + c);
// through the lip, framing the active area, with the outer rim broken by win_cham
module win_rect(g = 0) rrect(act_b0 - win_margin - g, act_a0 - win_margin - g,
                             act_b + 2*(win_margin + g), act_a + 2*(win_margin + g), 2 + g);
module window() {
  translate([0, -lip_t - 0.5, 0]) xz_extrude(lip_t + 1.0) win_rect();
  if (win_cham > 0.01) hull() {   // widens to win_cham oversize at the face, nominal win_cham deeper in
    translate([0, -lip_t - 0.5, 0]) xz_extrude(0.01) win_rect(win_cham + 0.5);
    translate([0, -lip_t + win_cham, 0]) xz_extrude(0.01) win_rect();
  }
}
module pcb_slab() translate([pcb_b0, pcb_y0, pcb_a0]) cube([pcb_b, pcb_t, pcb_a]);
module board_ghost() {   // stand-in for the real board, for assembly renders and tests
  xz_extrude(1.8) rrect(0, 0, glass_b, glass_a, glass_r);
  translate([lcd_b0, 1.8, lcd_a0]) cube([lcd_b, lcd_y1 - 1.8, lcd_a]);
  translate([foam_b0, lcd_y1, foam_a0]) cube([foam_b, pcb_y0 - lcd_y1, foam_a]);
  pcb_slab();
}
// exactly the touch/display area, in front of the glass
module active_prism() translate([act_b0, -lip_t - 0.5, act_a0]) cube([act_b, lip_t + 0.7, act_a]);

// ---- openings (board-local; `reach` = how far they run out past the board) ----------------------
module usb_slot(reach)
  translate([pcb_b0 + pcb_b - 3, usb_y - usb_h/2 - 0.4, usb_a - usb_w/2 - 0.4])
    cube([reach, usb_h + 0.8, usb_w + 0.8]);
// Relief for the plug overmold (wider/taller than the slot), cut through the wall so the plug can
// reach within a few mm of the PCB edge. Centred on the slot.
module usb_plug_relief(reach)
  translate([pcb_b0 + pcb_b + usb_plug_x0, usb_y - usb_plug_h/2, usb_a - usb_plug_w/2])
    cube([reach, usb_plug_h, usb_plug_w]);
module button_holes(reach) for (a = btn_a)
  translate([pcb_b0 + 1.0 - reach, btn_y, a]) rotate([0, 90, 0]) cylinder(d = pin_d, h = reach);
