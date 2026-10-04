#!/usr/bin/env bash
# Geometry tests for stand.scad (wedge) and easel.scad. Usage: 3d-printing/test.sh [stand|easel]
# (set OPENSCAD=... to override). Renders land in build/<scad>-<part>.{stl,log}.
set -u
set -f   # -D values such as bat=[65,40,15] must not glob
cd "$(dirname "$0")"
only="${1:-}"
case "$only" in ""|stand|easel) ;; *) echo "usage: $0 [stand|easel]"; exit 2;; esac
OPENSCAD="${OPENSCAD:-$PWD/tools/openscad}"
BACKEND=""
"$OPENSCAD" --help 2>&1 | grep -q -- "--backend" && BACKEND="--backend=manifold"
mkdir -p build
fail=0
SCAD=stand.scad
VARIANT=""   # extra -D arguments applied to every render (word-split on purpose)

out() { echo "build/${SCAD%.scad}-$1"; }
render() {  # render <part> [extra openscad args...] -> $(out part).stl and .log
  local part="$1"; shift
  local o; o="$(out "$part")"
  rm -f "$o.stl"
  "$OPENSCAD" $BACKEND -D "part=\"$part\"" $VARIANT "$@" -o "$o.stl" "$SCAD" >"$o.log" 2>&1
}
is_empty()  { grep -q "object is empty" "$(out "$1").log"; }
has_error() { grep -q -E "ERROR|Assertion|assertion" "$(out "$1").log"; }

expect_empty() {  # label part [-D ...]   passes when the part has NO geometry (no collision)
  local label="$1" part="$2"; shift 2
  render "$part" "$@"
  if has_error "$part"; then echo "FAIL $label: openscad error"; sed -n 1,4p "$(out "$part").log"; fail=1
  elif is_empty "$part"; then echo "PASS $label"
  else echo "FAIL $label: expected empty, got geometry (overlap)"; fail=1; fi
}
expect_solid() {  # label part [-D ...]   passes when the part HAS geometry
  local label="$1" part="$2"; shift 2
  render "$part" "$@"
  if has_error "$part"; then echo "FAIL $label: openscad error"; sed -n 1,4p "$(out "$part").log"; fail=1
  elif is_empty "$part" || [ ! -s "$(out "$part").stl" ]; then echo "FAIL $label: expected geometry, got empty"; fail=1
  else echo "PASS $label"; fi
}
expect_manifold() {  # label part [--max X Y Z] [-D ...]   watertight and within a bounding box
  local label="$1" part="$2"; shift 2
  local r=() c=()
  while [ $# -gt 0 ]; do
    if [ "$1" = "--max" ]; then c+=("$1" "$2" "$3" "$4"); shift 4; else r+=("$1"); shift; fi
  done
  expect_solid "$label renders" "$part" "${r[@]}" || true
  python3 tools/stlcheck.py "$(out "$part").stl" "${c[@]}" && echo "PASS $label watertight" || { echo "FAIL $label watertight"; fail=1; }
}
expect_error() {  # label part   passes when openscad rejects the part
  render "$2"
  if has_error "$2"; then echo "PASS $1"; else echo "FAIL $1: expected an error"; fail=1; fi
}
expect_pass() {  # label cmd...   passes when the command exits 0
  local label="$1"; shift
  if "$@"; then echo "PASS $label"; else echo "FAIL $label"; fail=1; fi
}
expect_fail() {  # label cmd...   passes when the command exits non-zero (positive controls)
  local label="$1"; shift
  if "$@" >/dev/null 2>&1; then echo "FAIL $label: expected a failure"; fail=1; else echo "PASS $label"; fi
}
run() { [ -z "$only" ] || [ "$only" = "$1" ]; }

# ==== wedge: stand.scad =========================================================================
if run stand; then
SCAD=stand.scad
expect_empty "part=none renders nothing" none
expect_error "unknown part is rejected" bogus

expect_manifold "front shell body" front_shell --max 48 86 63

# window never covers the active area
expect_empty "active area is clear (nominal)"   check_window
expect_empty "active area is clear (tilt 55)"   check_window -D tilt=55
expect_empty "active area is clear (tilt 75)"   check_window -D tilt=75
# the pocket stays inside the outer body and keeps >= 0.8 mm of skin
expect_empty "pocket inside outer (nominal)"    check_pocket_inside
expect_empty "pocket skin >= 0.8 (nominal)"     check_skin
expect_empty "pocket skin >= 0.8 (tilt 55)"     check_skin -D tilt=55
expect_empty "pocket skin >= 0.8 (tilt 75)"     check_skin -D tilt=75
expect_empty "pocket skin >= 0.8 (board 9 mm)"  check_skin -D board_t=9
expect_empty "pocket skin >= 0.8 (clr 0.5)"     check_skin -D clr=0.5
expect_manifold "front shell with pocket" front_shell --max 48 86 63

# battery bay
expect_empty "battery pocket inside the void"        check_battery_void
expect_empty "battery pocket clear of bosses"        check_battery_bosses
expect_empty "battery pocket clear of the board"     check_battery_board
expect_empty "15 mm cell: inside the void"           check_battery_void  -D 'bat=[65,40,15]'
expect_empty "15 mm cell: clear of the board"        check_battery_board -D 'bat=[65,40,15]'
expect_empty "15 mm cell: clear of the roof"         check_battery_roof  -D 'bat=[65,40,15]'
expect_empty "battery pocket clear of the roof"      check_battery_roof
# positive controls: the same checks must be able to fail
expect_solid "control: side_gap=-1 pokes out of the void"  check_battery_void  -D side_gap=-1
expect_solid "control: end_zone=1 hits the bosses"         check_battery_bosses -D end_zone=1
expect_solid "control: 40 mm cell hits the board"          check_battery_board  -D 'bat=[65,40,40]' -D 'z0=16'
expect_solid "control: 40 mm cell hits the roof"           check_battery_roof   -D 'bat=[65,40,40]' -D 'z_back=10'

# base plate, ribs, pushers
expect_solid "pushers press into the PCB rear"    check_pusher_touch
expect_empty "pushers clear of the battery pocket" check_pusher_battery
expect_empty "pushers clear of the bosses"        check_pusher_bosses
expect_empty "pushers clear of the shell"         check_pusher_shell
expect_empty "plate ribs clear of the shell"      check_ribs_shell
expect_empty "plate ribs clear of the battery"    check_ribs_battery
expect_manifold "base plate" base_plate --max 48 86 30
# edge rounding: the rounded corners must not thin the wall, and the plate must stay inside the shell
expect_empty "wall >= 2.3 through the corners"            check_wall
expect_empty "wall >= 2.3 (edge_r 6, prof_r 5)"           check_wall -D edge_r=6 -D prof_r=5
expect_empty "plate stays inside the shell footprint"     check_plate_flush
expect_empty "hard edges still build (all radii 0)"       check_wall -D edge_r=0 -D prof_r=0 -D win_cham=0 -D plate_cham=0
expect_solid "control: unrounded void thins the corners"  check_wall_unrounded
expect_solid "control: plate_cham=-2 stands proud"        check_plate_flush -D plate_cham=-2
expect_solid "pushers still touch (tilt 55)"      check_pusher_touch -D tilt=55
expect_solid "pushers still touch (tilt 75)"      check_pusher_touch -D tilt=75
expect_solid "pushers still touch (board_flip)"   check_pusher_touch -D board_flip=true

# positive controls: the same checks must be able to fail
# pushers stay >= push_gap in front of the bay when flipped / tilted low (the front end zone grows)
expect_empty "flip: pushers clear of the battery (tilt 65)" check_pusher_battery -D board_flip=true
expect_empty "flip: pushers clear of the battery (tilt 60)" check_pusher_battery -D board_flip=true -D tilt=60
expect_empty "flip: pushers clear of the battery (tilt 55)" check_pusher_battery -D board_flip=true -D tilt=55
expect_empty "pushers clear of the battery (tilt 55)"       check_pusher_battery -D tilt=55
expect_solid "control: flip tilt 55 without the gap hits the cell" check_pusher_battery -D board_flip=true -D tilt=55 -D push_gap=-20
expect_solid "control: 15 mm posts hit the battery pocket"  check_pusher_battery -D push_d=15 -D push_gap=-20
expect_solid "control: 15 mm posts hit the bosses"          check_pusher_bosses  -D push_d=15 -D push_gap=-20
expect_solid "control: 25 mm posts hit the shell"           check_pusher_shell   -D push_d=25
expect_solid "control: 60 mm ribs hit the shell"            check_ribs_shell     -D rib_h=60
expect_solid "control: bcl=-1 cell hits the ribs"           check_ribs_battery   -D bcl=-1

# Type-C slot and button pinholes
expect_solid "Type-C slot reaches the right wall"          check_usb_right
expect_empty "Type-C slot misses the left wall"            check_usb_left
expect_solid "buttons reach the left wall"                 check_btn_left
expect_empty "buttons miss the right wall"                 check_btn_right
# board_flip: the board is turned 180 deg, so the openings swap walls
expect_solid "flip: Type-C slot reaches the left wall"     check_usb_left  -D board_flip=true
expect_empty "flip: Type-C slot misses the right wall"     check_usb_right -D board_flip=true
expect_solid "flip: buttons reach the right wall"          check_btn_right -D board_flip=true
expect_empty "flip: buttons miss the left wall"           check_btn_left  -D board_flip=true
# Type-C plug relief (overmold is wider/taller than the slot)
expect_solid "plug relief reaches the right wall"          check_plug_right
expect_empty "plug relief misses the left wall"            check_plug_left
expect_solid "flip: plug relief reaches the left wall"     check_plug_left  -D board_flip=true
expect_empty "flip: plug relief misses the right wall"     check_plug_right -D board_flip=true
expect_empty "plug relief keeps the roof/face skin"        check_plug_skin
expect_empty "plug relief clear of the glass pocket"       check_plug_pocket
expect_empty "plug relief clear of the window"             check_plug_window
expect_empty "plug relief clear of the bosses"             check_plug_bosses
expect_empty "plug relief clear of the battery bay"        check_plug_bay
expect_empty "flip: plug relief keeps the skin"            check_plug_skin   -D board_flip=true
expect_empty "flip: plug relief clear of the bosses"       check_plug_bosses -D board_flip=true
expect_empty "flip: plug relief clear of the bay"          check_plug_bay    -D board_flip=true
expect_solid "control: tall relief cuts the skin"          check_plug_skin   -D usb_plug_h=40
expect_solid "control: deep relief hits the glass pocket"  check_plug_pocket -D usb_plug_x0=-20
expect_solid "control: huge relief hits the bosses"        check_plug_bosses -D usb_plug_h=80 -D usb_plug_w=80 -D usb_plug_x0=-30
expect_solid "control: huge relief hits the bay"           check_plug_bay    -D usb_plug_h=80 -D usb_plug_w=80 -D usb_plug_x0=-30
expect_solid "control: huge relief hits the window"        check_plug_window -D usb_plug_h=80 -D usb_plug_w=80 -D usb_plug_x0=-30
expect_manifold "front shell with openings" front_shell --max 48 86 63

# Type-C blanking cap
expect_empty "cap passes through the opening"            check_cap_fit
expect_empty "flip: cap passes through the opening"      check_cap_fit -D board_flip=true
expect_solid "cap barbs reach behind the wall"           check_cap_snap
expect_empty "cap flange covers the opening"             check_cap_covers
expect_empty "cap flange seats on the wall"              check_cap_seat
expect_empty "cap clears the board"                      check_cap_board
expect_solid "control: cap_clr=-0.5 jams in the opening" check_cap_fit    -D cap_clr=-0.5
expect_empty "control: tongues too short to snap"        check_cap_snap   -D cap_land=-0.5
expect_solid "control: flange smaller than the opening"  check_cap_covers -D cap_flange=-2
expect_solid "control: 12 mm flange runs off the wall"   check_cap_seat   -D cap_flange=12
expect_solid "control: 20 mm tongues hit the board"      check_cap_board  -D cap_land=20
expect_manifold "Type-C cap" cap --max 20 11 5

# deliverables
expect_manifold "printable front shell" front --max 48 86 63
expect_manifold "printable base plate"  base  --max 48 86 30
expect_manifold "fit-check coupon"      fit   --max 48 60 70
expect_solid    "assembly renders"      assembly
expect_solid    "section renders"       section
expect_solid    "cap fitted renders"    cap_fitted

fi

# ==== easel: easel.scad =========================================================================
if run easel; then
SCAD=easel.scad
meta() {  # writes build/easel-meta.echo: the press points and pivots stability.py needs
  rm -f build/easel-meta.echo
  "$OPENSCAD" -D 'part="meta"' $VARIANT "$@" -o build/easel-meta.echo easel.scad >build/easel-meta.log 2>&1
}

expect_empty "easel: part=none renders nothing" none
expect_error "easel: unknown part is rejected" bogus
expect_solid "cell ghost in place"  at_cell
expect_solid "board ghost in place" at_board
meta; expect_pass "meta echoes the reference points" grep -q 'EASEL{' build/easel-meta.echo

# tray and frame
expect_empty "cell clear of the tray"              check_cell_tray
expect_empty "cell clear of the frame lid"         check_cell_frame
expect_empty "15 mm cell clear of the tray"        check_cell_tray  -D 'bat=[65,40,15]'
expect_empty "15 mm cell clear of the frame lid"   check_cell_frame -D 'bat=[65,40,15]'
expect_solid "control: front_space=6 puts the cell on the pilasters" check_cell_tray  -D front_space=6
expect_solid "control: bcl=-0.1 puts the cell into the lid"          check_cell_frame -D bcl=-0.1
expect_empty "lead slots open from the tray to the chin (tilt 65)"   check_lead_path
expect_solid "control: lead slots under the legs"                    check_lead_path -D 'lead_x=[12,30]'
expect_empty "rear counterbore keeps a 0.9 wall"                     check_rear_wall
expect_solid "control: D=80 thins the rear wall"                     check_rear_wall -D D=80
expect_manifold "tray"                 tray  --max 46.5 81.5 15.7
expect_manifold "tray (15 mm cell)"    tray  --max 46.5 81.5 18.7 -D 'bat=[65,40,15]'
expect_manifold "frame"                frame --max 46.5 81.5 36.8
expect_manifold "frame (tilt 55)"      frame --max 46.5 81.5 36.8 -D tilt=55
expect_manifold "frame (tilt 75)"      frame --max 46.5 81.5 40.4 -D tilt=75
expect_manifold "frame (15 mm cell)"   frame --max 46.5 81.5 36.8 -D 'bat=[65,40,15]'

# carrier
expect_empty "carrier clear of the frame and fin"     check_carrier_frame
expect_empty "carrier clear of the frame (tilt 55)"   check_carrier_frame -D tilt=55
expect_empty "carrier clear of the frame (tilt 75)"   check_carrier_frame -D tilt=75
expect_solid "control: fin_gap=-0.1 puts the fin into the carrier" check_carrier_frame -D fin_gap=-0.1
expect_empty "carrier clear of the board (pegs in the holes)"     check_carrier_board -D preload=-0.05
expect_empty "flip: carrier clear of the board"                   check_carrier_board -D preload=-0.05 -D board_flip=true
expect_solid "control: 2.6 mm pegs miss the holes"                check_carrier_board -D preload=-0.05 -D peg_d=2.6
expect_solid "shoulders press on the PCB"                         check_shoulders
expect_solid "flip: shoulders press on the PCB"                   check_shoulders -D board_flip=true
expect_empty "control: preload=-0.05 leaves the PCB loose"        check_shoulders -D preload=-0.05
expect_empty "pegs sit inside the PCB holes"                      check_pegs
expect_empty "flip: pegs sit inside the PCB holes"                check_pegs -D board_flip=true
expect_solid "control: 1.6 mm holes are too small for the pegs"   check_pegs -D hole_d=1.6
expect_empty "leg screw heads inside the legs"                    check_leg_head
expect_empty "leg screw heads inside the legs (tilt 55)"          check_leg_head -D tilt=55
expect_solid "control: at tilt 75 the heads break the leg front"  check_leg_head -D tilt=75
# battery lead: plug on the header, lead in the carrier groove, down the chin into the lid slot
expect_empty "battery lead clear, plug to tray"              check_cord
expect_empty "battery lead clear (board_flip)"               check_cord -D board_flip=true
expect_empty "battery lead clear (tilt 55)"                  check_cord -D tilt=55
expect_empty "battery lead clear (tilt 75)"                  check_cord -D tilt=75
expect_empty "battery lead clear (clr 0.5)"                  check_cord -D clr=0.5
expect_empty "battery lead clear (board 9 mm)"               check_cord -D board_t=9
expect_solid "control: cord_depth=0.4 drags the lead over the rear parts" check_cord -D cord_depth=0.4
expect_solid "control: bat_h=4.5 puts the plug into the plate"           check_cord -D bat_h=4.5
expect_solid "control: bat_exit=1 leaves no room to climb out"           check_cord -D bat_exit=1
expect_manifold "carrier"                carrier --max 40 55 10.5
expect_manifold "carrier (tilt 55)"      carrier --max 40 55.7 10.5 -D tilt=55
expect_manifold "carrier (tilt 75)"      carrier --max 40 55 10.5 -D tilt=75
expect_manifold "carrier (board_flip)"   carrier --max 40 55 10.5 -D board_flip=true
expect_manifold "carrier (clr 0.5)"      carrier --max 40 55 10.7 -D clr=0.5
expect_manifold "carrier (board 9 mm)"   carrier --max 40 55 12.5 -D board_t=9

# bezel and hinge
expect_empty "active area is clear"                  check_window
expect_empty "active area is clear (tilt 55)"        check_window -D tilt=55
expect_empty "active area is clear (tilt 75)"        check_window -D tilt=75
expect_empty "flip: active area is clear"            check_window -D board_flip=true
expect_solid "control: win_margin=-2 covers the active area" check_window -D win_margin=-2
expect_empty "top pilots keep 1.2 walls"             check_pilot_wall
expect_empty "top pilots keep 1.2 walls (clr 0.5)"   check_pilot_wall -D clr=0.5
expect_solid "control: pilot_in=0.5 thins the wall"  check_pilot_wall -D pilot_in=0.5
expect_empty "ridges run in the grooves"             check_ridge_groove
expect_solid "control: groove_clr=-0.1 jams the ridges" check_ridge_groove -D groove_clr=-0.1
expect_empty "bezel clear of the carrier"            check_bezel_carrier
expect_solid "control: nudge=-0.02 pushes the bezel into the carrier" check_bezel_carrier -D nudge=-0.02
expect_empty "bezel clear of the frame"              check_bezel_frame
expect_solid "control: nudge=-0.02 sinks the bezel into the lid"     check_bezel_frame -D nudge=-0.02
expect_empty "leg screw heads clear of the bezel"    check_head_bezel
for a in 5 10 20 30; do
  expect_empty "bezel swings on clear (${a} deg)"            check_swing -D swing=$a
  expect_empty "bezel swings on clear (${a} deg, sag 0.2)"   check_swing -D swing=$a -D sag=0.2
done
expect_solid "control: chin_top=-0.3 catches a sagged board" check_swing -D swing=5 -D sag=0.2 -D chin_top=-0.3
# every part against every other part and the ghosts (touching faces pulled apart by nudge)
expect_empty "parts and ghosts clear"                check_fit -D preload=-0.05
expect_empty "parts and ghosts clear (15 mm cell)"   check_fit -D preload=-0.05 -D 'bat=[65,40,15]'
expect_empty "parts and ghosts clear (clr 0.5)"      check_fit -D preload=-0.05 -D clr=0.5
expect_empty "parts and ghosts clear (board 9 mm)"   check_fit -D preload=-0.05 -D board_t=9
expect_empty "parts and ghosts clear (board_flip)"   check_fit -D preload=-0.05 -D board_flip=true
expect_empty "parts and ghosts clear (tilt 55)"      check_fit -D preload=-0.05 -D tilt=55
expect_empty "parts and ghosts clear (tilt 75)"      check_fit -D preload=-0.05 -D tilt=75
expect_solid "control: nudge=0 lets touching faces overlap" check_fit -D preload=-0.05 -D nudge=0
expect_manifold "bezel"                 bezel --max 40 55.7 8.3
expect_manifold "bezel (tilt 55)"       bezel --max 40 57.5 8.3 -D tilt=55
expect_manifold "bezel (tilt 75)"       bezel --max 40 55.7 8.3 -D tilt=75
expect_manifold "bezel (board_flip)"    bezel --max 40 55.7 8.3 -D board_flip=true
expect_manifold "bezel (clr 0.5)"       bezel --max 40 55.7 8.5 -D clr=0.5
expect_manifold "bezel (board 9 mm)"    bezel --max 40 55.7 10.3 -D board_t=9

# Type-C cap
expect_empty "cap body clears the relief"            check_cap_relief
expect_empty "cap body clears the relief (flip)"     check_cap_relief -D board_flip=true
expect_solid "control: cap_clr=-0.1 binds"           check_cap_relief -D cap_clr=-0.1
expect_empty "cap flange lands on the side face"     check_cap_seat
expect_empty "cap flange lands (flip)"               check_cap_seat -D board_flip=true
expect_empty "cap flange lands (tilt 55)"            check_cap_seat -D tilt=55
expect_solid "control: cap_flange=3 overhangs"       check_cap_seat -D cap_flange=3
expect_solid "cap ribs bite the bezel"               check_cap_ribs
expect_empty "control: cap_rib=0.1 does not bite"    check_cap_ribs -D cap_rib=0.1
expect_empty "cap ribs stay off the carrier"         check_cap_rib_carrier
expect_empty "cap clears the board"                  check_cap_board
expect_empty "cap clears the board (flip)"           check_cap_board -D board_flip=true
expect_manifold "cap"                   cap --max 17.7 10 3.1

# Stability: mass and tip force from the parts in place (estimates; see tools/stability.py)
for p in at_tray at_frame at_carrier at_bezel at_cell at_board; do expect_solid "$p renders" $p; done
meta
expect_pass "tip force: >= 3.0 N centre, >= 1.3 N top" python3 tools/stability.py build/easel --min-center 3.0 --min-top 1.3
expect_fail "control: no cell, it tips"                python3 tools/stability.py build/easel --cell-g 0 --min-center 3.0 --min-top 1.3
expect_solid "assembly renders" assembly
expect_solid "section renders"  section
fi

exit $fail
