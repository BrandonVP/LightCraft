/*
===========================================================================
Name        : wallplate_3gang.scad
Author      : Brandon Van Pelt
Description : 3-gang wall plate carrying the LightCraft panel (Guition
              ESP32-4848S040) on the room's existing 3-gang metal box.

              REV 3 -- after the first full-size test print.

              CONFIRMED CORRECT on hardware:
                - plate face 6.375" x 4.500" matches the stock 3-gang cover
                - ear hole spacing on BOTH axes: 1.8125" gang pitch and the
                  3.281" device spacing

              A NOTE ON THE 3.281", because it is genuinely confusing: a
              switch strap has TWO pairs of holes. The pair the stock wall
              plate screws into is ~55.5mm apart. The pair that bolts the
              strap to the BOX EARS is 3.281" (83.34mm) apart. We are pulling
              the switches out and bolting this plate straight to those box
              ears, so 83.34 is the number that matters. Measuring the stock
              plate gives the other pair and sends you off by 28mm -- it cost
              a revision here, so it is written down.

              CORRECTED in this revision:
                - ear holes widened 3.6 -> 4.2 (3.6 printed too tight for 6-32)
                - opening 50 -> 61 for the measured 60 x 60 panel enclosure

              TWO SILENT FAILURE MODES, both now asserted against. Each one
              renders a perfectly valid-looking plate that cannot mount
              anything, so neither is obvious from a preview:
                1. An opening larger than the panel screw circle deletes the
                   screw bosses.
                2. An ear screw lying inside the opening is deleted by it.
                   This is why screw_mode = 2 is unusable at a 61mm opening:
                   the centre gang's ears sit at x = 0, inside the hole.
                   Use screw_mode = 4.

              PRINT: front face DOWN, support-free. 4-5 perimeters, 0.2mm.
              MATERIAL: not plain PLA -- see the note at the end.
===========================================================================
*/

inch = 25.4;

// --- Plate face (confirmed against the stock cover) ------------------------
plate_w  = 6.375 * inch;    // 161.93
plate_h  = 4.500 * inch;    // 114.30
plate_t  = 3.0;
corner_r = 4.0;

// --- Box mounting ---------------------------------------------------------
// Device standard, i.e. the spacing of the switch strap's BOX-MOUNTING holes.
// See the note in the header before "correcting" this against a stock plate.
ear_dy     = 3.281 * inch;  // 83.34 -- confirmed correct
gang_pitch = 1.8125 * inch; // 46.04 -- confirmed correct

// 2 = centre gang, 4 = outer gangs, 6 = all three. At this ear_dy the centre
// gang clears the 61mm opening, so all six positions are usable; 4 is plenty.
screw_mode = 4;

screw_d         = 4.2;      // 6-32 clearance; 3.6 printed too tight
ear_slot_travel = 0.0;      // spacing is known, so round hole + conical sink
csink_d         = 8.4;
csink_h         = 1.6;

// --- Panel opening --------------------------------------------------------
// Measured enclosure is 60 x 60, plus 0.5 clearance per side.
opening_w = 61.0;
opening_h = 61.0;

// --- Panel retention -----------------------------------------------------
// OFF until the panel's own mounting hole spacing is known. At a 61mm opening
// the bosses must be outboard of it: panel_screw_dx > 61 + panel_screw_d +
// 2*wall, i.e. about 67mm minimum.
with_bosses    = false;
panel_screw_dx = 70.0;
panel_screw_dy = 70.0;
panel_screw_d  = 2.8;       // 2.8 self-taps M3; 3.2 for clearance
boss_d = 7.5;
boss_h = 4.0;
wall   = 1.5;               // minimum material around any hole

// ---------------------------------------------------------------------------

ear_xs = (screw_mode == 2) ? [0]
       : (screw_mode == 4) ? [-gang_pitch, gang_pitch]
       :                     [-gang_pitch, 0, gang_pitch];

// Failure mode 1: the opening swallowing the panel screw bosses.
assert(!with_bosses || opening_w <= panel_screw_dx - panel_screw_d - 2 * wall,
       "opening_w too large for panel_screw_dx -- the opening consumes the bosses");
assert(!with_bosses || opening_h <= panel_screw_dy - panel_screw_d - 2 * wall,
       "opening_h too large for panel_screw_dy -- the opening consumes the bosses");

// Failure mode 2: an ear screw lying inside the opening.
keep_out_w = opening_w / 2 + screw_d / 2 + ear_slot_travel / 2 + wall;
keep_out_h = opening_h / 2 + screw_d / 2 + ear_slot_travel / 2 + wall;
assert(min([for (x = ear_xs) abs(x)]) >= keep_out_w
       || ear_dy / 2 >= keep_out_h,
       "an ear screw falls inside the opening -- use screw_mode=4 or shrink the opening");

module rrect(w, h, r, t)
{
    linear_extrude(t)
        offset(r = r)
            square([w - 2 * r, h - 2 * r], center = true);
}

// Vertical slot: a round end of `d` swept `travel` along Y. travel = 0 gives
// a plain circle.
module vslot(d, travel, h)
{
    linear_extrude(h)
        offset(r = d / 2)
            square([0.001, max(travel, 0.001)], center = true);
}

module plate()
{
    difference()
    {
        union()
        {
            rrect(plate_w, plate_h, corner_r, plate_t);

            if (with_bosses)
                for (sx = [-1, 1])
                    for (sy = [-1, 1])
                        translate([sx * panel_screw_dx / 2,
                                   sy * panel_screw_dy / 2, -boss_h])
                            cylinder(h = boss_h + plate_t, d = boss_d, $fn = 48);
        }

        // Panel opening.
        translate([0, 0, -boss_h - 1])
            linear_extrude(boss_h + plate_t + 2)
                square([opening_w, opening_h], center = true);

        // Panel screws, entered from the front into the bosses.
        if (with_bosses)
            for (sx = [-1, 1])
                for (sy = [-1, 1])
                    translate([sx * panel_screw_dx / 2,
                               sy * panel_screw_dy / 2, -boss_h - 1])
                        cylinder(h = boss_h + plate_t + 2,
                                 d = panel_screw_d, $fn = 32);

        // Box ear screws.
        for (x = ear_xs)
            for (sy = [-1, 1])
                translate([x, sy * ear_dy / 2, 0])
                {
                    translate([0, 0, -boss_h - 1])
                        vslot(screw_d, ear_slot_travel, boss_h + plate_t + 2);

                    if (ear_slot_travel > 0)
                        // A slot cannot take a cone, so counterbore instead.
                        // Oval-head plate screws still sit at or below flush.
                        translate([0, 0, plate_t - csink_h])
                            vslot(csink_d, ear_slot_travel, csink_h + 1);
                    else
                        translate([0, 0, plate_t - csink_h])
                            cylinder(h = csink_h + 0.01,
                                     d1 = screw_d, d2 = csink_d, $fn = 32);
                }
    }
}

plate();

/*
---------------------------------------------------------------------------
STILL OPEN: how the panel is retained.

The enclosure is 60 x 60 and the bezel is 86.5 x 86.5, so with a 61mm opening
the bezel overlaps the plate by about 13mm all round -- plenty of landing
area. What is not yet known is whether the panel's own mounting holes can be
used. If those holes sit at roughly 60mm centres (the 86-box standard) they
fall INSIDE a 61mm opening and are unusable, which leaves two options:

  A. PROUD MOUNT -- shrink the opening to a wire pass-through (say 30 x 20),
     let the panel sit entirely on the plate face, and screw into bosses at
     the panel's real hole spacing. One piece, simple, panel stands ~17mm off
     the wall.

  B. CLAMPED MOUNT -- keep the 61mm opening so the body drops into the box,
     thicken the plate to ~5mm with a 2.5mm recess for the bezel, and add a
     rear retainer frame that screws to the plate and traps the bezel flange.
     Two pieces, nearly flush.

---------------------------------------------------------------------------
MATERIAL

This plate covers a box containing mains splices and a switching supply, in a
sealed metal enclosure with no airflow.

Do NOT use plain PLA: glass transition around 55-60C, and it is not flame
retardant. In order of preference:
  - ABS-FR or ASA rated UL94 V-0   (best; enclosed printer)
  - PETG-FR / "FRJet" V-0          (nearly as good, open printer is fine)
  - plain ASA or ABS               (temperature fine, flammability not)

"Tested to UL94 V-0" is not the same as a UL certification with a Yellow
Card. For a part sitting against mains, buy from a vendor that publishes the
actual rating.
---------------------------------------------------------------------------
*/
