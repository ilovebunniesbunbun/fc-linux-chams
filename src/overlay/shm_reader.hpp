#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <time.h>
#include <semaphore.h>
#include <cstddef>
#include <thread>
#include <glm/glm.hpp>

namespace shm {
    constexpr std::size_t MAX_PLAYERS = 64;
    constexpr std::size_t MAX_BONES = 128;
    constexpr std::size_t MAX_PROJECTILES = 8;
    constexpr std::size_t MAX_MODEL_NAME = 64;
    constexpr std::size_t MAX_MAP_NAME = 64;
    constexpr std::size_t VIEW_MATRIX_SIZE = 16;
}

using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

static_assert(sizeof(Vec3) == 12, "SHM wire format: Vec3 must be 12 bytes");
static_assert(sizeof(Vec4) == 16, "SHM wire format: Vec4 must be 16 bytes");
static_assert(std::is_trivially_copyable_v<Vec3>, "SHM wire format: Vec3 must be trivially copyable");
static_assert(std::is_trivially_copyable_v<Vec4>, "SHM wire format: Vec4 must be trivially copyable");

// Full bone transform: position + quaternion rotation
struct BoneTransform {
    Vec3 position;
    Vec4 rotation;  // quaternion (x, y, z, w)
};

enum GrenadeType : uint8_t {
    GRENADE_NONE = 0,
    GRENADE_HE,
    GRENADE_FLASH,
    GRENADE_SMOKE,
    GRENADE_MOLOTOV,
    GRENADE_DECOY
};

enum WeaponId : uint16_t {
    weapon_none = 0,
    weapon_deagle = 1,
    weapon_elite = 2,
    weapon_fiveseven = 3,
    weapon_glock = 4,
    weapon_ak47 = 7,
    weapon_aug = 8,
    weapon_awp = 9,
    weapon_famas = 10,
    weapon_g3sg1 = 11,
    weapon_galilar = 13,
    weapon_m249 = 14,
    weapon_m4a1 = 16,
    weapon_mac10 = 17,
    weapon_p90 = 19,
    weapon_ump = 24,
    weapon_xm1014 = 25,
    weapon_bizon = 26,
    weapon_mag7 = 27,
    weapon_negev = 28,
    weapon_sawedoff = 29,
    weapon_tec9 = 30,
    weapon_taser = 31,
    weapon_hkp2000 = 32,
    weapon_mp7 = 33,
    weapon_mp9 = 34,
    weapon_nova = 35,
    weapon_p250 = 36,
    weapon_scar20 = 38,
    weapon_sg556 = 39,
    weapon_ssg08 = 40,
    weapon_knife = 42,
    weapon_flashbang = 43,
    weapon_hegrenade = 44,
    weapon_smokegrenade = 45,
    weapon_molotov = 46,
    weapon_decoy = 47,
    weapon_incgrenade = 48,
    weapon_c4 = 49,
    weapon_knife_t = 59,
    weapon_m4a1_silencer = 60,
    weapon_usp_silencer = 61,
    weapon_cz75a = 63,
    weapon_revolver = 64,
    weapon_bayonet = 500,
    weapon_knife_flip = 505,
    weapon_knife_gut = 506,
    weapon_knife_karambit = 507,
    weapon_knife_m9_bayonet = 508,
    weapon_knife_tactical = 509,
    weapon_knife_falchion = 512,
    weapon_knife_survival_bowie = 514,
    weapon_knife_butterfly = 515,
    weapon_knife_push = 516,
    weapon_knife_kukri = 526
};

#pragma pack(push, 1)

struct InFlightProjectile {
    uint32_t entity_handle;     // CS2 entity index (EntIndex)
    uint8_t type;               // GrenadeType enum
    Vec3 initial_position;      // Position when thrown
    Vec3 initial_velocity;      // Velocity when thrown
    Vec3 current_position;      // Real-time world origin / detonation origin (via SceneNode)
    float spawn_time;           // Game time (curtime) when thrown
    float timer_start_time;     // Effect start time (curtime)
    float duration;             // Total duration (e.g. 18s, 15s)
    uint8_t active;             // 1 if active, 0 if empty slot
};

struct InfernoData {
    uint32_t entity_handle;
    float start_time;
    float duration;
    int fire_count;
    Vec3 fire_positions[64];
    uint8_t active;
};

struct PlayerData {
    int team;
    int health;
    int active;
    int has_defuser;
    Vec3 origin;
    char model_name[shm::MAX_MODEL_NAME];        // e.g. "ctm_sas", "tm_leet" for mesh lookup
    int bone_count;             // actual bone count for this player
    BoneTransform bones[shm::MAX_BONES];   // full skeletal transforms
};

struct ShmPacket {
    uint32_t frame_index;
    float view_matrix[shm::VIEW_MATRIX_SIZE];
    Vec3 local_eye;
    char map_name[shm::MAX_MAP_NAME];
    
    // --- Local Player Throw State ---
    uint16_t held_weapon_id;
    uint8_t held_grenade_type;  // GrenadeType (0 if none held/pin not pulled)
    uint8_t pin_pulled;         // 1 = True, 0 = False
    float throw_strength;       // 0.0 to 1.0
    Vec3 local_velocity;        // Local player velocity vector
    Vec3 local_angles;          // Local player view angles (pitch/yaw/roll)
    
    // --- In-Flight Projectiles ---
    int projectile_count;
    InFlightProjectile projectiles[shm::MAX_PROJECTILES]; // Max 8 active projectiles tracked concurrently
    
    // --- Ground Inferno Zones ---
    int inferno_count;
    InfernoData infernos[4];

    int player_count;
    PlayerData players[shm::MAX_PLAYERS];
};
#pragma pack(pop)

static_assert(sizeof(InFlightProjectile) == 54, "InFlightProjectile packing mismatch");
static_assert(sizeof(InfernoData) == 785, "InfernoData packing mismatch");
static_assert(sizeof(PlayerData) == 3680, "PlayerData packing mismatch");
static_assert(sizeof(ShmPacket) == 239280, "ShmPacket packing mismatch");

#include <semaphore.h>

class ShmReader {
private:
    int shm_fd = -1;
    void* mapped_addr = nullptr;
    ShmPacket* mapped_data = nullptr;
    const char* shm_name = "/fc2_chams_shm_bridge";
    const char* sem_name = "/fc2_chams_shm_sem";
    sem_t* shm_sem = SEM_FAILED;
    size_t shm_size = 0;
    uint32_t last_frame_idx = 0xFFFFFFFF;

public:
    bool initialize() {
        const char* env_shm = std::getenv("FC2_SHM_NAME");
        const char* env_sem = std::getenv("FC2_SEM_NAME");
        if (env_shm) shm_name = env_shm;
        if (env_sem) sem_name = env_sem;

        shm_size = sizeof(ShmPacket);
        shm_fd = shm_open(shm_name, O_RDONLY, 0666);
        if (shm_fd < 0) {
            return false;
        }

        void* addr = mmap(nullptr, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
        if (addr == MAP_FAILED) {
            close(shm_fd);
            shm_fd = -1;
            return false;
        }

        mapped_addr = addr;
        mapped_data = static_cast<ShmPacket*>(addr);

        // Open existing named semaphore created by the collector bridge
        shm_sem = sem_open(sem_name, 0);
        if (shm_sem == SEM_FAILED) {
            // Fallback: create it if not initialized yet
            shm_sem = sem_open(sem_name, O_CREAT, 0666, 0);
        }

        return true;
    }

    bool wait_for_frame(int timeout_ms) {
        if (shm_sem == SEM_FAILED) return false;
        
        if (timeout_ms <= 0) {
            return sem_wait(shm_sem) == 0;
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000;
            }
            return sem_timedwait(shm_sem, &ts) == 0;
        }
    }

    // Non-blocking check: returns true if a new frame signal was posted
    bool try_frame() {
        if (shm_sem == SEM_FAILED) return false;
        return sem_trywait(shm_sem) == 0;
    }

    bool fetch_latest(ShmPacket& out_packet) {
        if (__builtin_expect(!mapped_data, 0)) return false;
        
        for (int retry = 0; retry < 5; ++retry) {
            uint32_t seq1 = mapped_data->frame_index;
            if (__builtin_expect(seq1 % 2 != 0, 0)) {
                // Writer is currently updating the buffer
                std::this_thread::yield();
                continue;
            }
            
            if (seq1 == last_frame_idx) {
                return false;
            }
            
            std::memcpy(&out_packet, mapped_data, offsetof(ShmPacket, players));
            int copy_count = out_packet.player_count;
            if (copy_count < 0) copy_count = 0;
            if (copy_count > static_cast<int>(shm::MAX_PLAYERS)) copy_count = static_cast<int>(shm::MAX_PLAYERS);
            if (copy_count > 0) {
                std::memcpy(out_packet.players, mapped_data->players, copy_count * sizeof(PlayerData));
            }
            
            uint32_t seq2 = mapped_data->frame_index;
            if (__builtin_expect(seq1 == seq2, 1)) {
                last_frame_idx = seq1;
                return true;
            }
        }
        
        return false;
    }

    // Read the latest view matrix directly from mapped shared memory (not cached packet).
    // This can return a fresher view matrix than the last packet if the bridge updates more frequently.
    bool read_view_matrix(float* out) const {
        if (__builtin_expect(!mapped_data, 0)) return false;
        
        for (int retry = 0; retry < 5; ++retry) {
            uint32_t seq1 = mapped_data->frame_index;
            if (__builtin_expect(seq1 % 2 != 0, 0)) {
                std::this_thread::yield();
                continue;
            }
            
            std::memcpy(out, mapped_data->view_matrix, sizeof(float) * shm::VIEW_MATRIX_SIZE);
            
            uint32_t seq2 = mapped_data->frame_index;
            if (__builtin_expect(seq1 == seq2, 1)) {
                return true;
            }
        }
        return false;
    }

    void shutdown() {
        if (mapped_addr) {
            munmap(mapped_addr, shm_size);
            mapped_addr = nullptr;
            mapped_data = nullptr;
        }
        if (shm_fd >= 0) {
            close(shm_fd);
            shm_fd = -1;
        }
        if (shm_sem != SEM_FAILED) {
            sem_close(shm_sem);
            shm_sem = SEM_FAILED;
        }
    }

    ~ShmReader() {
        shutdown();
    }
};
