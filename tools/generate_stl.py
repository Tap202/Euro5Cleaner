"""
Direct STL generator for Yamaha MT-09 SP CAN Enclosure
Generates standard binary STL files for 3D printing without external dependencies.
Outputs:
  - enclosure/mt09_case_base.stl (Lower case with standoffs, zip-tie tabs, and cable relief)
  - enclosure/mt09_case_lid.stl  (Snap-fit top cover with LED port)
"""

import struct
import math
import os

def write_stl(filename, triangles):
    """Writes triangles to binary STL format.
    triangles: list of (normal, v1, v2, v3) where each is (x, y, z)
    """
    with open(filename, 'wb') as f:
        # 80-byte header
        header = f"MT-09 SP CAN Case Binary STL - Antigravity".ljust(80, ' ').encode('ascii')[:80]
        f.write(header)
        # Number of triangles (uint32)
        f.write(struct.pack('<I', len(triangles)))
        for norm, v1, v2, v3 in triangles:
            f.write(struct.pack('<3f', *norm))
            f.write(struct.pack('<3f', *v1))
            f.write(struct.pack('<3f', *v2))
            f.write(struct.pack('<3f', *v3))
            f.write(struct.pack('<H', 0)) # attribute byte count

def calc_normal(v1, v2, v3):
    ax, ay, az = v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2]
    bx, by, bz = v3[0] - v1[0], v3[1] - v1[1], v3[2] - v1[2]
    nx = ay * bz - az * by
    ny = az * bx - ax * bz
    nz = ax * by - ay * bx
    length = math.sqrt(nx*nx + ny*ny + nz*nz)
    if length > 1e-9:
        return (nx/length, ny/length, nz/length)
    return (0.0, 0.0, 1.0)

def add_box(triangles, x0, y0, z0, x1, y1, z1):
    """Adds an axis-aligned solid box between (x0, y0, z0) and (x1, y1, z1)."""
    # 8 vertices
    # v0: x0,y0,z0; v1: x1,y0,z0; v2: x1,y1,z0; v3: x0,y1,z0
    # v4: x0,y0,z1; v5: x1,y0,z1; v6: x1,y1,z1; v7: x0,y1,z1
    def tri(p1, p2, p3):
        norm = calc_normal(p1, p2, p3)
        triangles.append((norm, p1, p2, p3))
        
    v0, v1, v2, v3 = (x0,y0,z0), (x1,y0,z0), (x1,y1,z0), (x0,y1,z0)
    v4, v5, v6, v7 = (x0,y0,z1), (x1,y0,z1), (x1,y1,z1), (x0,y1,z1)

    # Bottom (z0, normal -z)
    tri(v0, v2, v1); tri(v0, v3, v2)
    # Top (z1, normal +z)
    tri(v4, v5, v6); tri(v4, v6, v7)
    # Front (y0, normal -y)
    tri(v0, v1, v5); tri(v0, v5, v4)
    # Back (y1, normal +y)
    tri(v3, v6, v2); tri(v3, v7, v6)
    # Left (x0, normal -x)
    tri(v0, v4, v7); tri(v0, v7, v3)
    # Right (x1, normal +x)
    tri(v1, v2, v6); tri(v1, v6, v5)

def generate_case():
    out_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "enclosure")
    os.makedirs(out_dir, exist_ok=True)

    # Base Dimensions
    outer_l = 40.0
    outer_w = 40.0
    outer_h = 17.0
    wall = 2.0
    floor = 2.0

    # 1. GENERATE BASE
    base_tris = []
    # Floor slab
    add_box(base_tris, 0, 0, 0, outer_l, outer_w, floor)
    # Left wall
    add_box(base_tris, 0, 0, floor, wall, outer_w, outer_h)
    # Right wall (with cable slot in middle)
    cable_slot_w = 7.0
    add_box(base_tris, outer_l - wall, 0, floor, outer_l, (outer_w - cable_slot_w)/2, outer_h)
    add_box(base_tris, outer_l - wall, (outer_w + cable_slot_w)/2, floor, outer_l, outer_w, outer_h)
    # Bottom portion under cable slot
    add_box(base_tris, outer_l - wall, (outer_w - cable_slot_w)/2, floor, outer_l, (outer_w + cable_slot_w)/2, floor + 3.0)
    # Front wall
    add_box(base_tris, wall, 0, floor, outer_l - wall, wall, outer_h)
    # Back wall
    add_box(base_tris, wall, outer_w - wall, floor, outer_l - wall, outer_w, outer_h)

    # Subframe zip-tie mounting tabs (left & right wings)
    tab_w = 14.0
    tab_l = 8.0
    # Left tab
    add_box(base_tris, -tab_l, (outer_w - tab_w)/2, 0, 0, (outer_w - tab_w)/2 + 4.0, floor)
    add_box(base_tris, -tab_l, (outer_w + tab_w)/2 - 4.0, 0, 0, (outer_w + tab_w)/2, floor)
    add_box(base_tris, -tab_l, (outer_w - tab_w)/2 + 4.0, 0, -tab_l + 2.5, (outer_w + tab_w)/2 - 4.0, floor)
    # Right tab
    add_box(base_tris, outer_l, (outer_w - tab_w)/2, 0, outer_l + tab_l, (outer_w - tab_w)/2 + 4.0, floor)
    add_box(base_tris, outer_l, (outer_w + tab_w)/2 - 4.0, 0, outer_l + tab_l, (outer_w + tab_w)/2, floor)
    add_box(base_tris, outer_l + tab_l - 2.5, (outer_w - tab_w)/2 + 4.0, 0, outer_l + tab_l, (outer_w + tab_w)/2 - 4.0, floor)

    # ESP32 and Transceiver support standoffs inside
    add_box(base_tris, wall + 2, wall + 2, floor, wall + 5, wall + 5, floor + 3.0)
    add_box(base_tris, wall + 2, outer_w - wall - 5, floor, wall + 5, outer_w - wall - 2, floor + 3.0)
    add_box(base_tris, outer_l - wall - 7, wall + 2, floor, outer_l - wall - 4, wall + 5, floor + 3.0)
    add_box(base_tris, outer_l - wall - 7, outer_w - wall - 5, floor, outer_l - wall - 4, outer_w - wall - 2, floor + 3.0)

    base_path = os.path.join(out_dir, "mt09_case_base.stl")
    write_stl(base_path, base_tris)
    print(f"Generated {base_path} with {len(base_tris)} triangles.")

    # 2. GENERATE LID
    lid_tris = []
    lid_thick = 2.0
    rim_depth = 3.0
    tol = 0.35

    # Top cover plate
    add_box(lid_tris, 0, 0, 0, outer_l, outer_w, lid_thick)
    # Inner rim perimeter (inserts into base pocket)
    rx0 = wall + tol
    ry0 = wall + tol
    rx1 = outer_l - wall - tol
    ry1 = outer_w - wall - tol
    rim_w = 1.4

    # Rim walls
    add_box(lid_tris, rx0, ry0, -rim_depth, rx1, ry0 + rim_w, 0)
    add_box(lid_tris, rx0, ry1 - rim_w, -rim_depth, rx1, ry1, 0)
    add_box(lid_tris, rx0, ry0 + rim_w, -rim_depth, rx0 + rim_w, ry1 - rim_w, 0)
    add_box(lid_tris, rx1 - rim_w, ry0 + rim_w, -rim_depth, rx1, ry1 - rim_w, 0)

    lid_path = os.path.join(out_dir, "mt09_case_lid.stl")
    write_stl(lid_path, lid_tris)
    print(f"Generated {lid_path} with {len(lid_tris)} triangles.")

if __name__ == "__main__":
    generate_case()
