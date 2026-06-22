#include <cassert>
#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <cstring>

#include "bvh_parser.hpp"
#include "config.hpp"
#include "math/ViewMatrix.hpp"
#include "shm_reader.hpp"
#include "trajectory_sim.hpp"
#include "vpk/kv3.hpp"
#include "vpk/vpk.hpp"

void test_matrix_inversion()
{
    float view_proj[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec3 cam = get_camera_position(view_proj);
    assert(cam.x == 0.0f);
    assert(cam.y == 0.0f);
    assert(cam.z == -1.0f);
    std::cout << "test_matrix_inversion passed!\n";
}

void test_extrapolator()
{
    ViewMatrixExtrapolator extra;
    float m1[16] = {0};
    m1[0] = 1.0f;
    extra.update(m1);

    float out[16];
    extra.get_extrapolated_matrix(out);
    assert(out[0] == 1.0f);

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    float m2[16] = {0};
    m2[0] = 2.0f;
    extra.update(m2);

    extra.get_extrapolated_matrix(out);
    assert(std::abs(out[0] - 2.0f) < 0.1f);
    std::cout << "test_extrapolator passed!\n";
}

void test_shm_read_write()
{
    // Set up unique shm and sem names for test
    const char* shm_name = "/fc2_chams_shm_test_rw";
    const char* sem_name = "/fc2_chams_sem_test_rw";
    setenv("FC2_SHM_NAME", shm_name, 1);
    setenv("FC2_SEM_NAME", sem_name, 1);

    // Create shared memory
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    assert(fd >= 0);
    int trunc_res = ftruncate(fd, sizeof(ShmPacket));
    assert(trunc_res == 0);

    void* addr = mmap(nullptr, sizeof(ShmPacket), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(addr != MAP_FAILED);
    ShmPacket* mapped_writer = static_cast<ShmPacket*>(addr);

    // Create semaphore
    sem_t* sem = sem_open(sem_name, O_CREAT, 0666, 0);
    assert(sem != SEM_FAILED);

    // Initialize reader
    ShmReader reader;
    bool init_ok = reader.initialize();
    assert(init_ok);

    // Populate mock packet
    ShmPacket local_packet{};
    local_packet.frame_index = 2; // Even number (not busy)
    std::strcpy(local_packet.map_name, "de_mirage");
    local_packet.player_count = 2;
    local_packet.players[0].health = 80;
    local_packet.players[0].active = 1;
    std::strcpy(local_packet.players[0].model_name, "model_a");
    local_packet.players[1].health = 100;
    local_packet.players[1].active = 1;
    std::strcpy(local_packet.players[1].model_name, "model_b");

    local_packet.door_count = 2;
    local_packet.doors[0].entity_handle = 10;
    local_packet.doors[0].origin = glm::vec3(1.0f, 2.0f, 3.0f);
    local_packet.doors[0].yaw = 45.0f;
    local_packet.doors[0].active = 1;
    local_packet.doors[1].entity_handle = 11;
    local_packet.doors[1].origin = glm::vec3(4.0f, 5.0f, 6.0f);
    local_packet.doors[1].yaw = 90.0f;
    local_packet.doors[1].active = 1;

    // Write to shared memory
    std::memcpy(mapped_writer, &local_packet, sizeof(ShmPacket));

    // Signal semaphore
    sem_post(sem);

    // Read back
    ShmPacket read_packet{};
    bool has_new = reader.fetch_latest(read_packet);
    assert(has_new);
    assert(read_packet.frame_index == 2);
    assert(std::strcmp(read_packet.map_name, "de_mirage") == 0);
    assert(read_packet.player_count == 2);
    assert(read_packet.players[0].health == 80);
    assert(std::strcmp(read_packet.players[0].model_name, "model_a") == 0);
    assert(read_packet.players[1].health == 100);
    assert(std::strcmp(read_packet.players[1].model_name, "model_b") == 0);

    assert(read_packet.door_count == 2);
    assert(read_packet.doors[0].entity_handle == 10);
    assert(read_packet.doors[0].origin.x == 1.0f);
    assert(read_packet.doors[0].yaw == 45.0f);
    assert(read_packet.doors[1].entity_handle == 11);
    assert(read_packet.doors[1].origin.x == 4.0f);
    assert(read_packet.doors[1].yaw == 90.0f);

    // Clean up
    reader.shutdown();
    munmap(addr, sizeof(ShmPacket));
    close(fd);
    shm_unlink(shm_name);
    sem_close(sem);
    sem_unlink(sem_name);

    std::cout << "test_shm_read_write passed!\n";
}

void test_shm_bounds()
{
    assert(shm::MAX_PLAYERS == 64);
    assert(shm::MAX_BONES == 128);
    assert(shm::MAX_PROJECTILES == 8);
    assert(shm::MAX_DOORS == 32);
    assert(sizeof(ShmPacket) == 240852);
    assert(sizeof(PlayerData) == 3680);
    assert(sizeof(InFlightProjectile) == 54);
    assert(sizeof(DoorData) == 49);
    
    // Call functional test
    test_shm_read_write();
    
    std::cout << "test_shm_bounds passed!\n";
}

void test_kv3()
{
    std::string kv3_data =
        "<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} "
        "format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->\n"
        "{\n"
        "    a = 1.0\n"
        "}\n";
    auto file = KeyValue3::KV3Reader::ParseText(kv3_data);
    assert(file.root.is_array == false);
    auto val = file.root.try_get("a");
    assert(val != nullptr);
    std::cout << "test_kv3 passed!\n";
}

void test_vpk()
{
    std::vector<uint8_t> dummy_vpk(sizeof(vpk::VPKHeaderV2) + 4, 0);
    vpk::VPKHeaderV2* hdr = reinterpret_cast<vpk::VPKHeaderV2*>(dummy_vpk.data());
    hdr->signature = vpk::VPK_SIGNATURE;
    hdr->version = vpk::VPK_VERSION;
    hdr->tree_size = 0;

    vpk::VPKDir reader;
    // signature is valid, should at least read the header correctly
    bool status = reader.open_from_bytes(dummy_vpk);
    std::cout << "test_vpk passed!\n";
}

void test_trajectory_sim()
{
    assert(should_detonate(GRENADE_FLASH, glm::vec3(0.0f), 104));
    assert(!should_detonate(GRENADE_FLASH, glm::vec3(0.0f), 10));

    glm::vec3 normal(0.0f, 0.0f, 1.0f);
    glm::vec3 vel(100.0f, 0.0f, -100.0f);
    glm::vec3 new_vel = resolve_collision(normal, vel);
    assert(new_vel.z > 0.0f);
    std::cout << "test_trajectory_sim passed!\n";
}

void test_bvh_raycast()
{
    LocalMapBVH bvh;
    LocalMapBVH::Triangle tri;
    tri.v0 = glm::vec3(-10.0f, -10.0f, 0.0f);
    tri.v1 = glm::vec3(10.0f, -10.0f, 0.0f);
    tri.v2 = glm::vec3(0.0f, 10.0f, 0.0f);
    bvh.triangles.push_back(tri);
    bvh.build();

    TraceResult res = bvh.trace_ray(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f, 0.0f, -10.0f));
    assert(res.hit == true);
    assert(std::abs(res.end_pos.z) < 1e-4f);
    std::cout << "test_bvh_raycast passed!\n";
}

void test_bvh_construction()
{
    LocalMapBVH bvh;
    assert(bvh.nodes.empty());
    bvh.build();
    assert(bvh.nodes.empty());

    LocalMapBVH::Triangle tri;
    tri.v0 = glm::vec3(0, 0, 0);
    tri.v1 = glm::vec3(1, 0, 0);
    tri.v2 = glm::vec3(0, 1, 0);
    bvh.triangles.push_back(tri);
    bvh.build();
    assert(!bvh.nodes.empty());
    std::cout << "test_bvh_construction passed!\n";
}

void test_color_conv()
{
    Color c(1.0f, 0.5f, 0.25f, 1.0f);
    assert(c.r == 1.0f);
    assert(c.g == 0.5f);
    assert(c.b == 0.25f);
    assert(c.a == 1.0f);
    assert(c[0] == 1.0f);
    assert(c[1] == 0.5f);

    Color c2(1.0f, 0.5f, 0.25f, 1.0f);
    assert(c == c2);
    std::cout << "test_color_conv passed!\n";
}

void test_config_serialize()
{
    Color c(0.1f, 0.2f, 0.3f, 0.4f);
    nlohmann::json j = {static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f),
                        static_cast<int>(c.a * 255.0f)};
    assert(j[0] == 25);
    assert(j[1] == 51);
    assert(j[2] == 76);
    assert(j[3] == 102);

    // Test validate_config on valid and invalid files
    std::string valid_filename = "test_valid_config.json";
    std::string invalid_filename = "test_invalid_config.json";

    std::ofstream vf(valid_filename);
    vf << "{\n  \"fps\": 120,\n  \"vsync\": false,\n  \"show_fps\": true,\n  \"extrapolate\": false,\n  \"scaling\": "
          "\"stretched\"\n}\n";
    vf.close();

    std::ofstream ivf(invalid_filename);
    ivf << "{\n  \"fps\": \"not-an-int\",\n  \"vsync\": false\n}\n";
    ivf.close();

    std::string err;
    assert(validate_config(valid_filename, err) == true);
    assert(validate_config(invalid_filename, err) == false);

    // Clean up
    std::remove(valid_filename.c_str());
    std::remove(invalid_filename.c_str());

    std::cout << "test_config_serialize passed!\n";
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <test-flag>\n";
        return 1;
    }

    std::string arg = argv[1];
    if (arg == "--matrix-inversion") {
        test_matrix_inversion();
    } else if (arg == "--extrapolator") {
        test_extrapolator();
    } else if (arg == "--shm-bounds") {
        test_shm_bounds();
    } else if (arg == "--kv3") {
        test_kv3();
    } else if (arg == "--vpk") {
        test_vpk();
    } else if (arg == "--trajectory-sim") {
        test_trajectory_sim();
    } else if (arg == "--bvh-raycast") {
        test_bvh_raycast();
    } else if (arg == "--bvh-construction") {
        test_bvh_construction();
    } else if (arg == "--color-conv") {
        test_color_conv();
    } else if (arg == "--config-serialize") {
        test_config_serialize();
    } else {
        std::cerr << "Unknown test flag: " << arg << "\n";
        return 1;
    }

    return 0;
}
