#!/usr/bin/env bash
# Geometry tests for stand.scad. Usage: docs/enclosure/test.sh   (set OPENSCAD=... to override)
set -u
cd "$(dirname "$0")"
OPENSCAD="${OPENSCAD:-$PWD/tools/openscad}"
BACKEND=""
"$OPENSCAD" --help 2>&1 | grep -q -- "--backend" && BACKEND="--backend=manifold"
mkdir -p build
fail=0

render() {  # render <part> [extra openscad args...] -> build/<part>.stl and build/<part>.log
  local part="$1"; shift
  rm -f "build/$part.stl"
  "$OPENSCAD" $BACKEND -D "part=\"$part\"" "$@" -o "build/$part.stl" stand.scad >"build/$part.log" 2>&1
}
is_empty()  { grep -q "object is empty" "build/$1.log"; }
has_error() { grep -q -E "ERROR|Assertion|assertion" "build/$1.log"; }

expect_empty() {  # label part [-D ...]   passes when the part has NO geometry (no collision)
  local label="$1" part="$2"; shift 2
  render "$part" "$@"
  if has_error "$part"; then echo "FAIL $label: openscad error"; sed -n 1,4p "build/$part.log"; fail=1
  elif is_empty "$part"; then echo "PASS $label"
  else echo "FAIL $label: expected empty, got geometry (overlap)"; fail=1; fi
}
expect_solid() {  # label part [-D ...]   passes when the part HAS geometry
  local label="$1" part="$2"; shift 2
  render "$part" "$@"
  if has_error "$part"; then echo "FAIL $label: openscad error"; sed -n 1,4p "build/$part.log"; fail=1
  elif is_empty "$part" || [ ! -s "build/$part.stl" ]; then echo "FAIL $label: expected geometry, got empty"; fail=1
  else echo "PASS $label"; fi
}
expect_manifold() {  # label part [--max X Y Z]   watertight and within a bounding box
  local label="$1" part="$2"; shift 2
  expect_solid "$label renders" "$part" || true
  python3 tools/stlcheck.py "build/$part.stl" "$@" && echo "PASS $label watertight" || { echo "FAIL $label watertight"; fail=1; }
}
expect_error() {  # label part   passes when openscad rejects the part
  render "$2"
  if has_error "$2"; then echo "PASS $1"; else echo "FAIL $1: expected an error"; fail=1; fi
}

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

exit $fail
