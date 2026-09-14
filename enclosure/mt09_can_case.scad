// =============================================================================
// 🏍️ Yamaha MT-09 SP (Euro 5+) CAN Tool - 3D Printable Weatherproof Enclosure
// Compatible with: OpenSCAD, PrusaSlicer, Bambu Studio, Cura, OrcaSlicer
// Hardware: ESP32-C3 Super Mini Plus + SN65HVD230 CAN Transceiver Module
// =============================================================================

$fn = 60; // High smoothness for curves and fillets

// --- Module & Component Dimensions (mm) ---
esp_length     = 23.5;
esp_width      = 18.5;
esp_height     = 6.0;

can_length     = 28.5;
can_width      = 12.0;
can_height     = 12.5;

wall_thickness = 2.0;
floor_thickness= 2.0;
tolerance      = 0.35; // FDM 3D printing clearance

// --- Interior Enclosure Dimensions ---
inner_length   = max(esp_length, can_length) + 6.0; // 34.5 mm
inner_width    = esp_width + can_width + 4.0;       // 34.5 mm
inner_height   = max(esp_height, can_height) + 2.5; // 15.0 mm

outer_length   = inner_length + 2 * wall_thickness;
outer_width    = inner_width + 2 * wall_thickness;
outer_height   = inner_height + floor_thickness;

// --- Cable & Zip-Tie Dimensions ---
cable_radius   = 3.2; // Slot for 4-core/6-core automotive CAN cable
ziptie_width   = 4.5;
ziptie_thick   = 2.2;

// =============================================================================
// RENDER SELECTOR (Set what you want to render/export to STL)
// =============================================================================
part_to_render = "both"; // Options: "base", "lid", "both"

if (part_to_render == "base" || part_to_render == "both") {
    case_base();
}

if (part_to_render == "lid" || part_to_render == "both") {
    // Offset lid next to base for easy 1-click plate printing
    translate([outer_length + 12, 0, 0])
        case_lid();
}

// =============================================================================
// CASE BASE (Main compartment with PCB cradles & mounting tabs)
// =============================================================================
module case_base() {
    difference() {
        union() {
            // Main rounded outer block
            rounded_cube([outer_length, outer_width, outer_height], r=3.0);

            // Subframe Zip-Tie Mounting Tabs (Left & Right)
            translate([-8, (outer_width - 14)/2, 0])
                mounting_tab();
            translate([outer_length, (outer_width - 14)/2, 0])
                mounting_tab();
        }

        // Hollow interior pocket
        translate([wall_thickness, wall_thickness, floor_thickness])
            rounded_cube([inner_length, inner_width, inner_height + 2], r=1.5);

        // Automotive Cable Entry Slot with Strain Relief Collar
        translate([outer_length - wall_thickness - 1, outer_width/2, outer_height - cable_radius])
            rotate([0, 90, 0])
                cylinder(r=cable_radius, h=wall_thickness + 2, center=false);

        // Snap-fit retention detents for the lid
        translate([wall_thickness + 6, wall_thickness - 0.4, outer_height - 2.5])
            sphere(r=0.9);
        translate([outer_length - wall_thickness - 6, wall_thickness - 0.4, outer_height - 2.5])
            sphere(r=0.9);
        translate([wall_thickness + 6, outer_width - wall_thickness + 0.4, outer_height - 2.5])
            sphere(r=0.9);
        translate([outer_length - wall_thickness - 6, outer_width - wall_thickness + 0.4, outer_height - 2.5])
            sphere(r=0.9);
    }

    // Internal PCB Separation & Cradle Wall
    translate([wall_thickness + esp_length + 2, wall_thickness, floor_thickness])
        cube([1.8, inner_width, 6.0]);

    // ESP32-C3 PCB support standoffs
    translate([wall_thickness + 2, wall_thickness + 2, floor_thickness])
        cube([3, 3, 2.5]);
    translate([wall_thickness + esp_length - 4, wall_thickness + 2, floor_thickness])
        cube([3, 3, 2.5]);
    translate([wall_thickness + 2, wall_thickness + esp_width - 4, floor_thickness])
        cube([3, 3, 2.5]);
    translate([wall_thickness + esp_length - 4, wall_thickness + esp_width - 4, floor_thickness])
        cube([3, 3, 2.5]);
}

// =============================================================================
// CASE LID (Snap-fit with embossed MT-09 SP branding & LED light pipe)
// =============================================================================
module case_lid() {
    lid_thickness = 2.0;
    rim_depth     = 3.2;

    union() {
        difference() {
            // Main outer lid plate
            rounded_cube([outer_length, outer_width, lid_thickness], r=3.0);

            // Embossed Logo / Text ("MT-09 SP")
            translate([outer_length/2, outer_width/2 + 3, lid_thickness - 0.6])
                linear_extrude(height = 1.0)
                    text("MT-09 SP", size=5.2, font="Arial:style=Bold", halign="center", valign="center");

            // Subtitle ("CAN TOOL")
            translate([outer_length/2, outer_width/2 - 5, lid_thickness - 0.6])
                linear_extrude(height = 1.0)
                    text("CAN TOOL", size=3.2, font="Arial:style=Bold", halign="center", valign="center");

            // LED Inspection Window / Light-pipe hole for GPIO 8 User LED
            translate([wall_thickness + 12, wall_thickness + 9, -1])
                cylinder(r=1.2, h=lid_thickness + 2);
        }

        // Inner snap-fit rim that inserts securely into base
        translate([wall_thickness + tolerance, wall_thickness + tolerance, -rim_depth])
            difference() {
                rounded_cube([inner_length - 2*tolerance, inner_width - 2*tolerance, rim_depth], r=1.2);
                translate([1.6, 1.6, -0.5])
                    cube([inner_length - 2*tolerance - 3.2, inner_width - 2*tolerance - 3.2, rim_depth + 1]);
            }

        // Snap beads matching base detents
        translate([wall_thickness + 6, wall_thickness + tolerance - 0.1, -2.0])
            sphere(r=0.8);
        translate([outer_length - wall_thickness - 6, wall_thickness + tolerance - 0.1, -2.0])
            sphere(r=0.8);
        translate([wall_thickness + 6, outer_width - wall_thickness - tolerance + 0.1, -2.0])
            sphere(r=0.8);
        translate([outer_length - wall_thickness - 6, outer_width - wall_thickness - tolerance + 0.1, -2.0])
            sphere(r=0.8);
    }
}

// =============================================================================
// HELPER MODULES (Mounting Tabs & Rounded Cubes)
// =============================================================================
module mounting_tab() {
    difference() {
        rounded_cube([8, 14, floor_thickness], r=2.0);
        // Zip-tie slot
        translate([2.2, (14 - ziptie_width)/2, -1])
            cube([ziptie_thick, ziptie_width, floor_thickness + 2]);
    }
}

module rounded_cube(size, r=2.0) {
    x = size[0];
    y = size[1];
    z = size[2];
    hull() {
        translate([r, r, 0]) cylinder(r=r, h=z);
        translate([x-r, r, 0]) cylinder(r=r, h=z);
        translate([r, y-r, 0]) cylinder(r=r, h=z);
        translate([x-r, y-r, 0]) cylinder(r=r, h=z);
    }
}
