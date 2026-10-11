// Airport Guide Bot Shell
// For DollaTek 2WD chassis (25cm x 15cm)
// Smooth, lowkey pebble shape

// ---- Dimensions (mm) ----
chassis_l = 250;    // length
chassis_w = 150;    // width
wall = 2.5;         // wall thickness
shell_h = 70;       // total height (covers ~5cm components + clearance)
corner_r = 15;      // corner rounding

// Wheel cutouts
wheel_d = 66;       // wheel diameter
wheel_w = 28;       // wheel width
wheel_y_offset = 30; // from rear edge

// Ultrasonic sensor (front, centered)
us_eye_d = 16;      // transducer diameter
us_eye_spacing = 26; // center to center
us_eye_h = 35;      // height from base

// Phone cutout (front top, for camera facing forward)
phone_w = 80;       // width of phone cutout
phone_h = 30;       // height
phone_from_top = 5; // from top edge

// Caster wheel cutout (front bottom)
caster_w = 40;
caster_d = 30;

module rounded_box(l, w, h, r) {
    hull() {
        for (x = [r, l-r]) {
            for (y = [r, w-r]) {
                translate([x, y, 0])
                    cylinder(h=h, r=r, $fn=40);
            }
        }
    }
}

module shell() {
    difference() {
        // Outer shell - smooth rounded box with domed top
        union() {
            // Main body
            rounded_box(chassis_l + wall*2, chassis_w + wall*2, shell_h, corner_r);

            // Domed top for smooth look
            translate([corner_r, corner_r, shell_h - 1])
                resize([chassis_l + wall*2 - corner_r*2, chassis_w + wall*2 - corner_r*2, 20])
                    scale([1, 1, 0.3])
                        sphere(r=50, $fn=60);
        }

        // Hollow inside
        translate([wall, wall, -1])
            rounded_box(chassis_l, chassis_w, shell_h + 25, corner_r - wall);

        // Bottom opening (open bottom to sit on chassis)
        translate([-1, -1, -1])
            cube([chassis_l + wall*2 + 2, chassis_w + wall*2 + 2, wall + 1]);

        // ---- Wheel cutouts (left and right, near rear) ----
        // Left wheel
        translate([-1, -1, 5])
            cube([wheel_w + 5, wall + 2, wheel_d]);
        translate([chassis_l + wall*2 - wheel_w - 4, -1, 5])
            cube([wheel_w + 5, wall + 2, wheel_d]);

        // Right side wheels
        translate([-1, chassis_w + wall, 5])
            cube([wheel_w + 5, wall + 2, wheel_d]);
        translate([chassis_l + wall*2 - wheel_w - 4, chassis_w + wall, 5])
            cube([wheel_w + 5, wall + 2, wheel_d]);

        // ---- Ultrasonic sensor holes (front face) ----
        // Left eye
        translate([chassis_l + wall*2 + 1, (chassis_w + wall*2)/2 - us_eye_spacing/2, us_eye_h])
            rotate([0, 90, 0])
                cylinder(d=us_eye_d, h=wall + 2, $fn=30);

        // Right eye
        translate([chassis_l + wall*2 + 1, (chassis_w + wall*2)/2 + us_eye_spacing/2, us_eye_h])
            rotate([0, 90, 0])
                cylinder(d=us_eye_d, h=wall + 2, $fn=30);

        // ---- Phone camera cutout (front face, near top) ----
        translate([chassis_l + wall*2 - 1, (chassis_w + wall*2 - phone_w)/2, shell_h - phone_from_top - phone_h])
            cube([wall + 2, phone_w, phone_h]);

        // ---- Caster wheel cutout (rear bottom) ----
        translate([-1, (chassis_w + wall*2 - caster_w)/2, -1])
            cube([caster_d, caster_w, 15]);

        // ---- Wire routing holes (sides, near bottom) ----
        translate([-1, (chassis_w + wall*2)/2, 10])
            rotate([0, 90, 0])
                cylinder(d=12, h=wall + 2, $fn=20);
        translate([chassis_l + wall*2 - 1, (chassis_w + wall*2)/2, 10])
            rotate([0, 90, 0])
                cylinder(d=12, h=wall + 2, $fn=20);
    }
}

// ---- Mounting tabs (snap onto chassis) ----
module mount_tab() {
    difference() {
        cube([10, 8, 15]);
        translate([5, 4, 10])
            rotate([0, 90, 0])
                cylinder(d=3.2, h=12, center=true, $fn=20);
    }
}

// Assemble
shell();

// Mounting tabs inside (4 corners)
translate([wall + 10, wall + 5, 0]) mount_tab();
translate([wall + 10, chassis_w + wall - 13, 0]) mount_tab();
translate([chassis_l - 10, wall + 5, 0]) mount_tab();
translate([chassis_l - 10, chassis_w + wall - 13, 0]) mount_tab();
