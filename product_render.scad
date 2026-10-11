// Airport Gate Guide Bot — Product Concept Render
// Full assembled view

// ---- Colours ----
shell_color = [0.15, 0.15, 0.18];     // dark charcoal
accent_color = [0.0, 0.75, 0.85];     // teal accent
wheel_color = [0.2, 0.2, 0.2];        // dark rubber
tire_color = [0.1, 0.1, 0.1];
phone_color = [0.05, 0.05, 0.08];     // black phone
sensor_color = [0.7, 0.7, 0.72];      // silver sensors
led_color = [0.0, 1.0, 0.5, 0.8];     // green glow
chassis_color = [0.3, 0.3, 0.32];

// ---- Dimensions (mm) ----
body_l = 255;
body_w = 155;
body_h = 75;
corner_r = 20;

$fn = 60;

// ---- Modules ----

module rounded_box(l, w, h, r) {
    hull() {
        for (x = [r, l-r])
            for (y = [r, w-r])
                translate([x, y, 0])
                    cylinder(h=h, r=r);
    }
}

module wheel(d=66, w=26) {
    rotate([90, 0, 0]) {
        // Tire
        color(tire_color)
            difference() {
                cylinder(d=d, h=w, center=true);
                cylinder(d=d-12, h=w+1, center=true);
            }
        // Hub
        color(wheel_color)
            cylinder(d=d-12, h=w-2, center=true);
        // Spokes
        color(wheel_color)
            for (a = [0:60:300])
                rotate([0, 0, a])
                    translate([0, 0, 0])
                        cube([d/2-8, 4, w-4], center=true);
    }
}

module caster() {
    color(sensor_color) {
        // Fork
        translate([0, 0, 5])
            cylinder(d=8, h=15);
        // Wheel
        translate([0, 0, 3])
            rotate([90, 0, 0])
                cylinder(d=20, h=8, center=true);
    }
}

module ultrasonic_sensor() {
    color(sensor_color) {
        // PCB
        cube([20, 45, 2], center=true);
        // Left transducer
        translate([0, -13, 2])
            cylinder(d=16, h=12);
        // Right transducer
        translate([0, 13, 2])
            cylinder(d=16, h=12);
    }
}

module phone() {
    color(phone_color) {
        // Body
        hull() {
            for (x = [-35, 35])
                for (z = [-75, 75])
                    translate([x, 0, z])
                        rotate([90, 0, 0])
                            cylinder(r=5, h=8, center=true);
        }
        // Screen
        color([0.1, 0.1, 0.15])
            translate([0, -4.5, 0])
                cube([62, 0.5, 135], center=true);
        // Camera bump
        color([0.08, 0.08, 0.1])
            translate([-15, 4.5, 55])
                cylinder(d=18, h=2);
    }
}

module led_strip() {
    for (i = [0:8]) {
        color(accent_color)
            translate([i * 12, 0, 0])
                cube([8, 2, 3], center=true);
    }
}

module shell_body() {
    difference() {
        union() {
            // Main shell
            color(shell_color)
            difference() {
                // Outer
                hull() {
                    rounded_box(body_l, body_w, body_h, corner_r);
                    // Dome top
                    translate([body_l/2, body_w/2, body_h])
                        scale([body_l/2 - 5, body_w/2 - 5, 15])
                            sphere(r=1);
                }
                // Hollow
                translate([3, 3, -1])
                    rounded_box(body_l - 6, body_w - 6, body_h + 20, corner_r - 3);
            }

            // Accent stripe (teal line around middle)
            color(accent_color)
                translate([0, 0, body_h/2 - 2])
                    difference() {
                        rounded_box(body_l, body_w, 4, corner_r);
                        translate([2, 2, -1])
                            rounded_box(body_l - 4, body_w - 4, 6, corner_r - 2);
                    }
        }

        // Ultrasonic holes (front)
        translate([body_l + 1, body_w/2 - 13, 40])
            rotate([0, -90, 0])
                cylinder(d=17, h=10);
        translate([body_l + 1, body_w/2 + 13, 40])
            rotate([0, -90, 0])
                cylinder(d=17, h=10);

        // Phone camera window (front top)
        translate([body_l - 4, body_w/2 - 40, body_h - 20])
            cube([8, 80, 25]);

        // Wheel cutouts
        translate([-1, -1, 5])
            cube([30, 5, 50]);
        translate([body_l - 29, -1, 5])
            cube([30, 5, 50]);
        translate([-1, body_w - 2, 5])
            cube([30, 5, 50]);
        translate([body_l - 29, body_w - 2, 5])
            cube([30, 5, 50]);

        // Front LED slot
        translate([body_l - 2, body_w/2 - 55, body_h/2 + 10])
            cube([5, 110, 5]);

        // Rear caster gap
        translate([-1, body_w/2 - 20, -1])
            cube([25, 40, 12]);
    }
}

module full_assembly() {
    // Shell
    shell_body();

    // Wheels (rear, both sides)
    translate([15, -5, 33]) wheel();
    translate([body_l - 15, -5, 33]) wheel();
    translate([15, body_w + 5, 33]) wheel();
    translate([body_l - 15, body_w + 5, 33]) wheel();

    // Caster (front)
    translate([5, body_w/2, 0]) caster();

    // Ultrasonic sensor (front face)
    translate([body_l - 2, body_w/2, 40])
        rotate([0, 90, 0])
            ultrasonic_sensor();

    // Phone (front top, camera facing forward)
    translate([body_l - 5, body_w/2, body_h + 5])
        rotate([0, 15, 0])  // slight forward tilt
            phone();

    // Front LED strip
    translate([body_l - 1, body_w/2 - 48, body_h/2 + 12])
        rotate([0, 0, 0])
            led_strip();

    // Label
    color(accent_color)
        translate([body_l/2, body_w + 3.5, body_h/2 + 5])
            rotate([90, 0, 0])
                linear_extrude(1)
                    text("GATE GUIDE", size=10, halign="center", font="Arial:style=Bold");

    // Small logo on other side
    color(accent_color)
        translate([body_l/2, -0.5, body_h/2 + 5])
            rotate([90, 0, 180])
                linear_extrude(1)
                    text("GATE GUIDE", size=10, halign="center", font="Arial:style=Bold");
}

full_assembly();
