import os
import subprocess
import tempfile

def test_config_dirty_and_comparison():
    # Retrieve path to config header and source files to compile them
    base_dir = os.path.dirname(os.path.dirname(__file__))
    config_hpp = os.path.join(base_dir, "src", "config.hpp")
    config_cpp = os.path.join(base_dir, "src", "config.cpp")
    
    # We will compile a simple C++ program that links src/config.cpp
    cpp_source = """
#include "config.hpp"
#include <iostream>
#include <cassert>

int main() {
    OverlayConfig cfg;
    
    // 1. Check initial dirty state
    assert(!cfg.is_dirty());
    
    // 2. Check setter dirty marking
    cfg.set_menu_w(900);
    assert(cfg.is_dirty());
    cfg.clear_dirty();
    assert(!cfg.is_dirty());
    
    // 3. Check equality operator (C++20 default comparison)
    OverlayConfig copy_cfg = cfg;
    assert(cfg == copy_cfg);
    
    // 4. Check that changing a value directly and comparing works
    // (This simulates ImGui modifying a value directly via pointer)
    cfg.fps = 120; // Direct write
    
    // Compare config with the saved copy (ignoring dirty flag)
    OverlayConfig temp_current = cfg;
    OverlayConfig temp_old = copy_cfg;
    temp_current.clear_dirty();
    temp_old.clear_dirty();
    
    assert(!(temp_current == temp_old)); // Should be unequal due to fps difference
    
    std::cout << "Config dirty and comparison tests passed!" << std::endl;
    return 0;
}
"""
    
    with tempfile.TemporaryDirectory() as tmpdir:
        src_path = os.path.join(tmpdir, "test_dirty.cpp")
        bin_path = os.path.join(tmpdir, "test_dirty")
        with open(src_path, "w") as f:
            f.write(cpp_source)
            
        # Copy src/config.hpp and src/config.cpp to the temp directory so they can be compiled together easily
        # or just add include paths to the compiler.
        include_dir = os.path.join(base_dir, "src")
        
        # We also need nlohmann/json headers which are linked. They are in system paths usually.
        # Let's compile test_dirty.cpp along with src/config.cpp
        subprocess.run([
            "g++", "-std=c++20", "-O2",
            src_path, config_cpp,
            "-I", include_dir,
            "-lspdlog", "-lfmt",
            "-o", bin_path
        ], check=True)
        
        # Run test
        proc = subprocess.run([bin_path], capture_output=True, text=True, check=True)
        assert "Config dirty and comparison tests passed!" in proc.stdout
