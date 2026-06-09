#pragma once
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>

struct Vec3 {
    float x, y, z;
};

struct Vec4 {
    float x, y, z, w;
};

// Full bone transform: position + quaternion rotation
struct BoneTransform {
    Vec3 position;
    Vec4 rotation;  // quaternion (x, y, z, w)
};

#pragma pack(push, 1)
struct PlayerData {
    int team;
    int health;
    int active;
    Vec3 origin;
    char model_name[64];        // e.g. "ctm_sas", "tm_leet" for mesh lookup
    int bone_count;             // actual bone count for this player
    BoneTransform bones[128];   // full skeletal transforms
};

struct ShmPacket {
    uint32_t frame_index;
    float view_matrix[16];
    Vec3 local_eye;
    char map_name[64];
    int player_count;
    PlayerData players[64];
};
#pragma pack(pop)

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
        if (!mapped_data) return false;
        
        for (int retry = 0; retry < 5; ++retry) {
            uint32_t seq1 = mapped_data->frame_index;
            if (seq1 % 2 != 0) {
                // Writer is currently updating the buffer
                usleep(10);
                continue;
            }
            
            if (seq1 == last_frame_idx) {
                return false;
            }
            
            std::memcpy(&out_packet, mapped_data, sizeof(ShmPacket));
            
            uint32_t seq2 = mapped_data->frame_index;
            if (seq1 == seq2) {
                last_frame_idx = seq1;
                return true;
            }
        }
        
        return false;
    }

    // Read the latest view matrix directly from mapped shared memory (not cached packet).
    // This can return a fresher view matrix than the last packet if the bridge updates more frequently.
    bool read_view_matrix(float* out) const {
        if (!mapped_data) return false;
        
        for (int retry = 0; retry < 5; ++retry) {
            uint32_t seq1 = mapped_data->frame_index;
            if (seq1 % 2 != 0) {
                usleep(10);
                continue;
            }
            
            std::memcpy(out, mapped_data->view_matrix, sizeof(float) * 16);
            
            uint32_t seq2 = mapped_data->frame_index;
            if (seq1 == seq2) {
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
