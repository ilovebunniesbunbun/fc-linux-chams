import os
import subprocess
import tempfile
import numpy as np
import pytest

def get_camera_position_src():
    header_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), "src", "math", "ViewMatrix.hpp")
    with open(header_path, "r") as f:
        content = f.read()
    
    # We want to keep the include headers and the get_camera_position function
    return content

def py_get_camera_position(view_proj):
    vp = np.array(view_proj, dtype=np.float32).reshape((4, 4), order='F')
    try:
        inv = np.linalg.inv(vp)
    except np.linalg.LinAlgError:
        return np.array([0.0, 0.0, 0.0], dtype=np.float32)
        
    col2 = inv[:, 2]
    col3 = inv[:, 3]
    cam_h = col2 * -1.0 + col3
    if abs(cam_h[3]) < 1e-5:
        return np.array([0.0, 0.0, 0.0], dtype=np.float32)
    return cam_h[:3] / cam_h[3]

def test_camera_position():
    header_src = get_camera_position_src()
    
    # We will compile a simple C++ program that reads 16 floats, computes camera pos, and prints 3 floats
    cpp_source = f"""
#include <iostream>
#include <iomanip>

{header_src}

int main() {{
    float m[16];
    for (int i = 0; i < 16; ++i) {{
        if (!(std::cin >> m[i])) return 1;
    }}
    glm::vec3 cam = get_camera_position(m);
    std::cout << std::scientific << std::setprecision(9);
    std::cout << cam.x << " " << cam.y << " " << cam.z << std::endl;
    return 0;
}}
"""
    
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = os.path.join(tmpdir, "test_cam.cpp")
        bin_path = os.path.join(tmpdir, "test_cam")
        with open(src_path, "w") as f:
            f.write(cpp_source)
            
        # Compile with GLM include path
        # The workspace root has glm headers if find_package found them.
        # Usually they are in system header search paths (e.g. /usr/include).
        # We can try to compile. If it needs -I /usr/include, that's default.
        subprocess.run(["g++", "-O2", src_path, "-o", bin_path], check=True)
        
        # Test with multiple random matrices
        rng = np.random.default_rng(42)
        for _ in range(50):
            # Generate a random view matrix (invertible)
            while True:
                mat = rng.standard_normal((4, 4)).astype(np.float32)
                if abs(np.linalg.det(mat)) > 0.1:
                    break
            
            mat_flat = mat.flatten(order='F') # Flatten column-major
            input_str = " ".join(str(x) for x in mat_flat)
            
            proc = subprocess.run(
                [bin_path],
                input=input_str,
                text=True,
                capture_output=True,
                check=True
            )
            
            output_floats = [float(x) for x in proc.stdout.strip().split()]
            cpp_cam = np.array(output_floats, dtype=np.float32)
            
            np_cam = py_get_camera_position(mat_flat)
            
            # Assert close
            np.testing.assert_allclose(cpp_cam, np_cam, rtol=1e-5, atol=1e-5)
