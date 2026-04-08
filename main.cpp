#include "include/glad/glad.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <limits>
#include <fstream>
#include <string>
#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <random>

#include <SFML/Audio.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "include/nlohmann/json.hpp"
#include "FastNoiseLite.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

// ----------------------------------------------------------------------
// Глобальные объекты шума
// ----------------------------------------------------------------------
FastNoiseLite continentNoise;
FastNoiseLite erosionNoise;
FastNoiseLite mountainNoise;
FastNoiseLite riverNoise;
FastNoiseLite biomeTempNoise;
FastNoiseLite biomeHumidNoise;
FastNoiseLite treeNoise;
FastNoiseLite detailNoise;
FastNoiseLite seaLevelNoise;
FastNoiseLite transitionNoise;

// ----------------------------------------------------------------------
// Окно и камера
// ----------------------------------------------------------------------
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
const int BLOCK_UNKNOWN = -1;

glm::vec3 cameraPos   = glm::vec3(0.0f, 50.0f,  0.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f,  0.0f);

float yaw   = -90.0f;
float pitch =  0.0f;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ----------------------------------------------------------------------
// Параметры чанков
// ----------------------------------------------------------------------
const int CHUNK_SIZE_X = 17;
const int CHUNK_SIZE_Z = 17;
const int CHUNK_SIZE_Y = 128;

// ----------------------------------------------------------------------
// Физика и коллизии
// ----------------------------------------------------------------------
glm::vec3 playerVelocity = glm::vec3(0.0f);
bool isOnGround = false;
const float GRAVITY = -25.0f;
const float JUMP_POWER = 8.0f;
const float PLAYER_HEIGHT = 1.8f;
const float PLAYER_WIDTH = 0.6f;
const float EYE_HEIGHT = 1.62f;

// Прототипы функций
int getBlockAt(int wx, int wy, int wz);
void setBlockAt(int wx, int wy, int wz, int type);
int getBlockAtForMesh(int wx, int wy, int wz);

// ----------------------------------------------------------------------
// Реализация коллизий (исправленная)
// ----------------------------------------------------------------------
bool isSolidBlock(int blockId) {
    return blockId != 0 && blockId != 5; // вода проходима
}

bool checkPlayerCollision(const glm::vec3& feetPos) {
    float halfWidth = PLAYER_WIDTH * 0.5f;
    glm::vec3 minCorner = feetPos + glm::vec3(-halfWidth, 0.0f, -halfWidth);
    glm::vec3 maxCorner = feetPos + glm::vec3( halfWidth, PLAYER_HEIGHT,  halfWidth);
    
    // Блоки центрированы в целых координатах и занимают [c-0.5, c+0.5].
    // Поэтому диапазон индексов должен учитывать сдвиг 0.5, иначе теряются
    // "положительные" грани при касании (например maxCorner == 1.5 не включает блок 2).
    int minX = static_cast<int>(std::ceil (minCorner.x - 0.5f));
    int maxX = static_cast<int>(std::floor(maxCorner.x + 0.5f));
    int minY = static_cast<int>(std::ceil (minCorner.y - 0.5f));
    int maxY = static_cast<int>(std::floor(maxCorner.y + 0.5f));
    int minZ = static_cast<int>(std::ceil (minCorner.z - 0.5f));
    int maxZ = static_cast<int>(std::floor(maxCorner.z + 0.5f));
    
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                int blockId = getBlockAt(x, y, z);
                if (isSolidBlock(blockId)) {
                    glm::vec3 blockMin(x - 0.5f, y - 0.5f, z - 0.5f);
                    glm::vec3 blockMax(x + 0.5f, y + 0.5f, z + 0.5f);
                    if (minCorner.x <= blockMax.x && maxCorner.x >= blockMin.x &&
                        minCorner.y <= blockMax.y && maxCorner.y >= blockMin.y &&
                        minCorner.z <= blockMax.z && maxCorner.z >= blockMin.z) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool isOnGroundCheck(const glm::vec3& feetPos) {
    glm::vec3 below = feetPos;
    below.y -= 0.05f;
    return checkPlayerCollision(below);
}

glm::vec3 applyCollision(const glm::vec3& oldFeetPos, const glm::vec3& delta) {
    glm::vec3 newFeetPos = oldFeetPos;
    glm::vec3 actualDelta(0.0f);
    constexpr float COLLISION_STEP = 0.01f;
    
    // Ось X
    newFeetPos.x += delta.x;
    if (checkPlayerCollision(newFeetPos)) {
        newFeetPos.x = oldFeetPos.x;
        const float direction = (delta.x > 0.0f) ? 1.0f : -1.0f;
        for (float t = COLLISION_STEP; t <= std::abs(delta.x); t += COLLISION_STEP) {
            newFeetPos.x = oldFeetPos.x + direction * t;
            if (checkPlayerCollision(newFeetPos)) {
                newFeetPos.x -= direction * COLLISION_STEP;
                break;
            }
        }
    }
    actualDelta.x = newFeetPos.x - oldFeetPos.x;
    
    // Ось Z
    newFeetPos.z += delta.z;
    if (checkPlayerCollision(newFeetPos)) {
        newFeetPos.z = oldFeetPos.z;
        const float direction = (delta.z > 0.0f) ? 1.0f : -1.0f;
        for (float t = COLLISION_STEP; t <= std::abs(delta.z); t += COLLISION_STEP) {
            newFeetPos.z = oldFeetPos.z + direction * t;
            if (checkPlayerCollision(newFeetPos)) {
                newFeetPos.z -= direction * COLLISION_STEP;
                break;
            }
        }
    }
    actualDelta.z = newFeetPos.z - oldFeetPos.z;
    
    // Ось Y
    newFeetPos.y += delta.y;
    if (checkPlayerCollision(newFeetPos)) {
        if (delta.y > 0.0f) {
            playerVelocity.y = 0.0f; // потолок
        } else if (delta.y < 0.0f) {
            playerVelocity.y = 0.0f; // земля
        }
        newFeetPos.y = oldFeetPos.y;
        const float direction = (delta.y > 0.0f) ? 1.0f : -1.0f;
        for (float t = COLLISION_STEP; t <= std::abs(delta.y); t += COLLISION_STEP) {
            newFeetPos.y = oldFeetPos.y + direction * t;
            if (checkPlayerCollision(newFeetPos)) {
                newFeetPos.y -= direction * COLLISION_STEP;
                break;
            }
        }
    }
    actualDelta.y = newFeetPos.y - oldFeetPos.y;
    
    isOnGround = isOnGroundCheck(newFeetPos);
    return actualDelta;
}

void placePlayerOnGround() {
    glm::vec3 feetPos = cameraPos - glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
    while (feetPos.y > -10.0f && !checkPlayerCollision(feetPos)) {
        feetPos.y -= 0.1f;
    }
    while (feetPos.y < CHUNK_SIZE_Y && checkPlayerCollision(feetPos)) {
        feetPos.y += 0.05f;
    }
    feetPos.y -= 0.05f;
    cameraPos = feetPos + glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
    playerVelocity = glm::vec3(0.0f);
    isOnGround = true;
}

// ----------------------------------------------------------------------
// Типы блоков
// ----------------------------------------------------------------------
struct BlockType {
    int id;
    std::string name;
    unsigned int textureID;
};
std::unordered_map<int, BlockType> blockTypes;
int currentBlockType = 1;

// ----------------------------------------------------------------------
// Сохранение/загрузка чанков
// ----------------------------------------------------------------------
const std::string SAVE_DIR = "saves/world";
const std::string CHUNKS_DIR = SAVE_DIR + "/chunks";

struct hash_ivec2 {
    size_t operator()(const glm::ivec2& v) const {
        return ((v.x * 73856093) ^ (v.y * 19349663));
    }
};

struct ChunkData {
    glm::ivec2 pos;
    std::vector<int> blocks;
    bool valid = false;
    ChunkData(int cx, int cz) : pos(cx, cz) {
        blocks.resize(CHUNK_SIZE_X * CHUNK_SIZE_Y * CHUNK_SIZE_Z, 0);
    }
};

void saveChunkToFile(const glm::ivec2& pos, const std::vector<int>& blocks) {
    if (!fs::exists(CHUNKS_DIR)) fs::create_directories(CHUNKS_DIR);
    std::string filename = CHUNKS_DIR + "/chunk_" + std::to_string(pos.x) + "_" + std::to_string(pos.y) + ".json";
    json j;
    j["cx"] = pos.x;
    j["cz"] = pos.y;
    j["blocks"] = json::array();
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
            for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                int id = blocks[(x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z];
                if (id != 0) {
                    json block;
                    block["x"] = x;
                    block["y"] = y;
                    block["z"] = z;
                    block["id"] = id;
                    j["blocks"].push_back(block);
                }
            }
        }
    }
    std::ofstream f(filename);
    if (f.is_open()) f << j.dump(4);
}

std::shared_ptr<ChunkData> loadChunkFromFile(int cx, int cz) {
    auto data = std::make_shared<ChunkData>(cx, cz);
    std::string filename = CHUNKS_DIR + "/chunk_" + std::to_string(cx) + "_" + std::to_string(cz) + ".json";
    std::ifstream f(filename);
    if (!f.is_open()) return nullptr;
    json j = json::parse(f);
    std::fill(data->blocks.begin(), data->blocks.end(), 0);
    for (auto& block : j["blocks"]) {
        int x = block["x"];
        int y = block["y"];
        int z = block["z"];
        int id = block["id"];
        if (x >= 0 && x < CHUNK_SIZE_X && y >= 0 && y < CHUNK_SIZE_Y && z >= 0 && z < CHUNK_SIZE_Z) {
            data->blocks[(x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z] = id;
        }
    }
    data->valid = true;
    return data;
}

// ----------------------------------------------------------------------
// Инициализация шумов
// ----------------------------------------------------------------------
void initWorldNoise() {
    continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    continentNoise.SetFrequency(0.0008f);
    continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    continentNoise.SetFractalOctaves(6);
    continentNoise.SetFractalLacunarity(2.0f);
    continentNoise.SetFractalGain(0.5f);

    erosionNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    erosionNoise.SetFrequency(0.008f);
    erosionNoise.SetFractalOctaves(4);
    erosionNoise.SetFractalGain(0.5f);

    mountainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    mountainNoise.SetFrequency(0.004f);
    mountainNoise.SetFractalOctaves(7);
    mountainNoise.SetFractalGain(0.6f);
    mountainNoise.SetFractalLacunarity(2.2f);

    riverNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    riverNoise.SetFrequency(0.02f);
    riverNoise.SetFractalOctaves(2);

    biomeTempNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    biomeTempNoise.SetFrequency(0.0006f);
    biomeTempNoise.SetFractalOctaves(3);
    
    biomeHumidNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    biomeHumidNoise.SetFrequency(0.0006f);
    biomeHumidNoise.SetFractalOctaves(3);

    detailNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    detailNoise.SetFrequency(0.04f);
    detailNoise.SetFractalOctaves(3);

    treeNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    treeNoise.SetFrequency(0.08f);

    seaLevelNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    seaLevelNoise.SetFrequency(0.0003f);
    seaLevelNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
    seaLevelNoise.SetFractalOctaves(4);
    seaLevelNoise.SetFractalLacunarity(2.0f);
    seaLevelNoise.SetFractalGain(0.5f);

    transitionNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    transitionNoise.SetFrequency(0.002f);
    transitionNoise.SetFractalOctaves(2);
}

// ----------------------------------------------------------------------
// Высота и биомы
// ----------------------------------------------------------------------
float getHeightAt(int wx, int wz, float& outBiomeTemp, float& outBiomeHumid, float& outWaterLevel) {
    float seaNoise = seaLevelNoise.GetNoise((float)wx, (float)wz);
    outWaterLevel = 35.0f + seaNoise * 20.0f;
    outWaterLevel = std::clamp(outWaterLevel, 15.0f, 58.0f);
    
    float cont = continentNoise.GetNoise((float)wx, (float)wz);
    float baseHeight;
    if (cont < -0.4f) {
        baseHeight = outWaterLevel - 12.0f + cont * 8.0f;
    } else if (cont < -0.15f) {
        float t = (cont + 0.4f) / 0.25f;
        baseHeight = outWaterLevel - 8.0f + t * 10.0f;
    } else if (cont < 0.2f) {
        float t = (cont + 0.15f) / 0.35f;
        baseHeight = outWaterLevel + 2.0f + t * 12.0f;
    } else {
        float t = (cont - 0.2f) / 0.8f;
        baseHeight = outWaterLevel + 14.0f + t * 35.0f;
    }
    
    float erosion = erosionNoise.GetNoise((float)wx, (float)wz) * 6.0f;
    float mountain = 0.0f;
    if (cont > 0.25f) {
        mountain = mountainNoise.GetNoise((float)wx, (float)wz) * 12.0f;
    }
    float river = riverNoise.GetNoise((float)wx, (float)wz);
    float riverFactor = 0.0f;
    if (std::abs(river) < 0.1f) {
        riverFactor = -5.0f * (1.0f - std::abs(river) / 0.1f);
    }
    float detail = detailNoise.GetNoise((float)wx, (float)wz) * 2.0f;
    
    float height = baseHeight + erosion + mountain + riverFactor + detail;
    height = std::clamp(height, 1.0f, CHUNK_SIZE_Y - 8.0f);
    
    outBiomeTemp = biomeTempNoise.GetNoise((float)wx, (float)wz);
    outBiomeHumid = biomeHumidNoise.GetNoise((float)wx, (float)wz);
    
    return height;
}

int getBiome(float temp, float humid, float height, float waterLevel) {
    if (height <= waterLevel) {
        float depth = waterLevel - height;
        if (depth < 3.0f) return 3;
        if (depth < 10.0f) return 4;
        return 5;
    }
    if (height - waterLevel < 3.0f) return 6;
    float t = (temp + 1.0f) / 2.0f;
    float h = (humid + 1.0f) / 2.0f;
    if (height > waterLevel + 25.0f) return 2;
    if (t < 0.25f) return 7;
    if (t > 0.7f && h > 0.6f) return 1;
    if (h < 0.3f) return 8;
    return 0;
}

// ----------------------------------------------------------------------
// Деревья
// ----------------------------------------------------------------------
bool isTreeNearby(int lx, int lz, int surfaceY, const std::vector<int>& blocks) {
    for (int dx = -3; dx <= 3; ++dx) {
        for (int dz = -3; dz <= 3; ++dz) {
            int x = lx + dx;
            int z = lz + dz;
            if (x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) continue;
            int y = surfaceY + 1;
            if (y < CHUNK_SIZE_Y) {
                int idx = (x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z;
                if (blocks[idx] == 6) return true;
            }
            y = surfaceY;
            if (y < CHUNK_SIZE_Y) {
                int idx = (x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z;
                if (blocks[idx] == 6) return true;
            }
        }
    }
    return false;
}

void addTree(int cx, int cz, int lx, int lz, int surfaceY, std::vector<int>& blocks) {
    int worldX = cx * CHUNK_SIZE_X + lx;
    int worldZ = cz * CHUNK_SIZE_Z + lz;
    float treeRand = treeNoise.GetNoise((float)worldX, (float)worldZ);
    if (treeRand < 0.65f) return;
    if (isTreeNearby(lx, lz, surfaceY, blocks)) return;

    for (int h = 1; h <= 6; ++h) {
        int y = surfaceY + h;
        if (y >= CHUNK_SIZE_Y) break;
        int idx = (lx * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + lz;
        if (blocks[idx] == 0) blocks[idx] = 6;
    }
    int trunkTop = surfaceY + 6;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                int x = lx + dx;
                int z = lz + dz;
                int y = trunkTop + dy;
                if (x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) continue;
                if (y < 0 || y >= CHUNK_SIZE_Y) continue;
                float dist = sqrt(dx*dx + dy*dy + dz*dz);
                if (dist <= 2.2f && !(dx == 0 && dy == 0 && dz == 0)) {
                    int idx = (x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z;
                    if (blocks[idx] == 0) blocks[idx] = 7;
                }
            }
        }
    }
    for (int dy = 1; dy <= 2; ++dy) {
        int y = trunkTop + dy;
        if (y >= CHUNK_SIZE_Y) continue;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                int x = lx + dx;
                int z = lz + dz;
                if (x < 0 || x >= CHUNK_SIZE_X || z < 0 || z >= CHUNK_SIZE_Z) continue;
                int idx = (x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z;
                if (blocks[idx] == 0) blocks[idx] = 7;
            }
        }
    }
}

// ----------------------------------------------------------------------
// Генерация чанка
// ----------------------------------------------------------------------
std::shared_ptr<ChunkData> generateChunk(int cx, int cz) {
    auto data = std::make_shared<ChunkData>(cx, cz);
    
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int worldX = cx * CHUNK_SIZE_X + x;
            int worldZ = cz * CHUNK_SIZE_Z + z;
            
            float biomeTemp, biomeHumid, waterLevel;
            float landHeight = getHeightAt(worldX, worldZ, biomeTemp, biomeHumid, waterLevel);
            int surfaceY = (int)landHeight;
            int waterSurfaceY = (int)waterLevel;
            
            surfaceY = std::clamp(surfaceY, 0, CHUNK_SIZE_Y - 1);
            waterSurfaceY = std::clamp(waterSurfaceY, 0, CHUNK_SIZE_Y - 1);
            
            int biome = getBiome(biomeTemp, biomeHumid, surfaceY, waterSurfaceY);
            
            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                int blockId = 0;
                
                if (biome == 3 || biome == 4 || biome == 5) {
                    if (y == surfaceY) {
                        blockId = (biome == 5) ? 3 : 4;
                    } else if (y > surfaceY && y <= waterSurfaceY) {
                        blockId = 5;
                    } else if (y < surfaceY) {
                        if (y > surfaceY - 4) blockId = 4;
                        else blockId = 3;
                    }
                } else if (biome == 6) {
                    if (y == surfaceY) {
                        blockId = 4;
                    } else if (y < surfaceY) {
                        if (y > surfaceY - 3) blockId = 4;
                        else blockId = 3;
                    }
                } else {
                    if (y == surfaceY) {
                        if (biome == 2 || biome == 7) blockId = 3;
                        else if (biome == 8) blockId = 4;
                        else blockId = 1;
                    } else if (y > surfaceY - 4 && y < surfaceY) {
                        if (biome == 2 || biome == 7) blockId = 3;
                        else if (biome == 8) blockId = 4;
                        else blockId = 2;
                    } else if (y < surfaceY - 4) {
                        blockId = 3;
                    }
                }
                
                data->blocks[(x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z] = blockId;
            }
            
            if ((biome == 0 || biome == 1) && surfaceY > waterSurfaceY + 2) {
                addTree(cx, cz, x, z, surfaceY, data->blocks);
            }
        }
    }
    
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int worldX = cx * CHUNK_SIZE_X + x;
            int worldZ = cz * CHUNK_SIZE_Z + z;
            float biomeTemp, biomeHumid, waterLevel;
            float landHeight = getHeightAt(worldX, worldZ, biomeTemp, biomeHumid, waterLevel);
            int surfaceY = (int)landHeight;
            
            bool hasWaterNeighbor = false;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue;
                    int nx = x + dx;
                    int nz = z + dz;
                    if (nx >= 0 && nx < CHUNK_SIZE_X && nz >= 0 && nz < CHUNK_SIZE_Z) {
                        int nWorldX = cx * CHUNK_SIZE_X + nx;
                        int nWorldZ = cz * CHUNK_SIZE_Z + nz;
                        float nTemp, nHumid, nWaterLevel;
                        float nHeight = getHeightAt(nWorldX, nWorldZ, nTemp, nHumid, nWaterLevel);
                        if (nHeight <= nWaterLevel) {
                            hasWaterNeighbor = true;
                            break;
                        }
                    }
                }
                if (hasWaterNeighbor) break;
            }
            
            if (hasWaterNeighbor && landHeight > waterLevel && landHeight - waterLevel < 4.0f) {
                int idx = (x * CHUNK_SIZE_Y + surfaceY) * CHUNK_SIZE_Z + z;
                if (data->blocks[idx] == 1 || data->blocks[idx] == 2) {
                    data->blocks[idx] = 4;
                }
            }
        }
    }
    
    data->valid = true;
    return data;
}

// ----------------------------------------------------------------------
// Фоновые задачи
// ----------------------------------------------------------------------
std::mutex chunkMutex;
std::unordered_map<glm::ivec2, std::shared_ptr<ChunkData>, hash_ivec2> pendingData;
std::unordered_set<glm::ivec2, hash_ivec2> pendingSave;
std::unordered_set<glm::ivec2, hash_ivec2> pendingLoad;
std::unordered_set<glm::ivec2, hash_ivec2> pendingGen;
std::atomic<bool> workerRunning(true);
std::thread workerThread;

void workerFunction() {
    while (workerRunning) {
        int processed = 0;
        {
            std::unique_lock<std::mutex> lock(chunkMutex);
            if (!pendingLoad.empty()) {
                glm::ivec2 pos = *pendingLoad.begin();
                pendingLoad.erase(pos);
                lock.unlock();
                auto data = loadChunkFromFile(pos.x, pos.y);
                lock.lock();
                if (data) pendingData[pos] = data;
                else pendingGen.insert(pos);
                processed++;
            }
        }
        {
            std::unique_lock<std::mutex> lock(chunkMutex);
            while (!pendingGen.empty() && processed < 20) {
                glm::ivec2 pos = *pendingGen.begin();
                pendingGen.erase(pos);
                lock.unlock();
                auto data = generateChunk(pos.x, pos.y);
                lock.lock();
                pendingData[pos] = data;
                processed++;
            }
        }
        if (processed == 0) std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

// ----------------------------------------------------------------------
// Шейдерные переменные
// ----------------------------------------------------------------------
unsigned int shaderProgram, reticleProgram, reticleVAO;
int u_time_location;
int u_isWater_location;

struct Chunk;
std::unordered_map<glm::ivec2, Chunk, hash_ivec2> loadedChunks;

static glm::vec3 lastCameraPosForWaterSort = glm::vec3(0.0f);
static std::vector<Chunk*> waterChunksCache;
static bool waterChunksCacheValid = false;

static Chunk* lastChunkForMesh = nullptr;
static glm::ivec2 lastChunkCoordsForMesh(0,0);

static GLint u_modelLoc = -1;
static GLint u_viewLoc = -1;
static GLint u_projLoc = -1;

// ----------------------------------------------------------------------
// UI (меню)
// ----------------------------------------------------------------------
unsigned int uiShaderProgram;
unsigned int uiVAO, uiVBO, uiEBO;
unsigned int menuBackgroundTexture = 0;
unsigned int menuButtonTexture = 0;
unsigned int menuPhotoTexture = 0;

struct Button {
    float relX, relY;
    float relW, relH;
    float absX, absY, absW, absH;
    bool clicked;
    const char* label;
};

Button buttons[3] = {
    {0.5f, 0.5f, 0.25f, 0.09f, 0,0,0,0, false, "Start Game"},
    {0.5f, 0.6f, 0.25f, 0.09f, 0,0,0,0, false, "Load World"},
    {0.5f, 0.7f, 0.25f, 0.09f, 0,0,0,0, false, "Exit"}
};

float photoRelX = 0.5f, photoRelY = 0.25f;
float photoRelW = 0.5f, photoRelH = 0.2f;
float photoAbsX = 0, photoAbsY = 0, photoAbsW = 0, photoAbsH = 0;

const char* uiVertexShaderSrc = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 projection;
uniform mat4 model;
void main() {
    gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* uiFragmentShaderSrc = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    FragColor = texture(uTexture, TexCoord);
}
)";

void initUI() {
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &uiVertexShaderSrc, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &uiFragmentShaderSrc, NULL);
    glCompileShader(fs);
    uiShaderProgram = glCreateProgram();
    glAttachShader(uiShaderProgram, vs);
    glAttachShader(uiShaderProgram, fs);
    glLinkProgram(uiShaderProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);

    float vertices[] = { 0,0,0,0, 1,0,1,0, 1,1,1,1, 0,1,0,1 };
    unsigned int indices[] = {0,1,2,0,2,3};
    glGenVertexArrays(1, &uiVAO);
    glGenBuffers(1, &uiVBO);
    glGenBuffers(1, &uiEBO);
    glBindVertexArray(uiVAO);
    glBindBuffer(GL_ARRAY_BUFFER, uiVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, uiEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glEnableVertexAttribArray(1);
}

void loadMenuTextures() {
    int w,h,ch;
    unsigned char* data = stbi_load("textures/menu_background.jpg", &w, &h, &ch, 4);
    if(data) {
        glGenTextures(1,&menuBackgroundTexture);
        glBindTexture(GL_TEXTURE_2D, menuBackgroundTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        stbi_image_free(data);
    }
    data = stbi_load("textures/menu_button.png", &w, &h, &ch, 4);
    if(data) {
        glGenTextures(1,&menuButtonTexture);
        glBindTexture(GL_TEXTURE_2D, menuButtonTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
        stbi_image_free(data);
    }
    data = stbi_load("textures/menu_photo.png", &w, &h, &ch, 4);
    if(data) {
        glGenTextures(1, &menuPhotoTexture);
        glBindTexture(GL_TEXTURE_2D, menuPhotoTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        std::cout << "Loaded menu photo: textures/menu_photo.png" << std::endl;
    } else {
        std::cerr << "Failed to load menu photo: textures/menu_photo.png" << std::endl;
    }
}

void updateButtonPositions(int screenW, int screenH) {
    for (int i=0; i<3; ++i) {
        buttons[i].absW = buttons[i].relW * screenW;
        buttons[i].absH = buttons[i].relH * screenH;
        buttons[i].absX = buttons[i].relX * screenW - buttons[i].absW/2;
        buttons[i].absY = buttons[i].relY * screenH - buttons[i].absH/2;
    }
}

void updatePhotoPosition(int screenW, int screenH) {
    photoAbsW = photoRelW * screenW;
    photoAbsH = photoRelH * screenH;
    photoAbsX = photoRelX * screenW - photoAbsW / 2;
    photoAbsY = photoRelY * screenH - photoAbsH / 2;
}

void drawRectangle(float x, float y, float w, float h, unsigned int texture, int screenW, int screenH) {
    glm::mat4 proj = glm::ortho(0.0f, (float)screenW, (float)screenH, 0.0f);
    glUseProgram(uiShaderProgram);
    glUniformMatrix4fv(glGetUniformLocation(uiShaderProgram,"projection"),1,GL_FALSE,glm::value_ptr(proj));
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x,y,0));
    model = glm::scale(model, glm::vec3(w,h,1));
    glUniformMatrix4fv(glGetUniformLocation(uiShaderProgram,"model"),1,GL_FALSE,glm::value_ptr(model));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(uiShaderProgram,"uTexture"),0);
    glBindVertexArray(uiVAO);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
}

// ----------------------------------------------------------------------
// Музыка через SFML
// ----------------------------------------------------------------------
std::vector<std::string> musicFiles;
sf::Music currentMusic;
int currentTrackIndex = -1;
bool musicPlaying = false;
bool musicTransitioning = false;
float trackStartTime = 0.0f;

void playRandomMusic() {
    if (musicFiles.empty()) return;
    if (musicTransitioning) return;
    
    musicTransitioning = true;
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, static_cast<int>(musicFiles.size()) - 1);
    int newIndex = dis(gen);
    if (newIndex == currentTrackIndex && musicFiles.size() > 1) {
        newIndex = (newIndex + 1) % musicFiles.size();
    }
    currentTrackIndex = newIndex;
    std::string path = musicFiles[currentTrackIndex];
    
    currentMusic.stop();
    
    if (currentMusic.openFromFile(path)) {
        currentMusic.play();
        musicPlaying = true;
        trackStartTime = glfwGetTime();
        std::cout << "Playing: " << fs::path(path).filename().string() << std::endl;
    } else {
        std::cerr << "Failed to load music: " << path << std::endl;
        musicPlaying = false;
    }
    
    musicTransitioning = false;
}

void stopMusic() {
    musicPlaying = false;
    musicTransitioning = false;
    currentMusic.stop();
}

void startMusic() {
    if (musicFiles.empty()) return;
    musicPlaying = true;
    playRandomMusic();
}

void scanMusicFolder() {
    std::string musicDir = "music";
    if (!fs::exists(musicDir)) {
        fs::create_directory(musicDir);
        std::cout << "Created music folder. Please add .wav/.ogg files there." << std::endl;
    }
    for (const auto& entry : fs::directory_iterator(musicDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".wav" || ext == ".ogg" || ext == ".flac" || ext == ".mp3") {
                musicFiles.push_back(entry.path().string());
                std::cout << "Found music: " << entry.path().filename().string() << std::endl;
            }
        }
    }
    if (musicFiles.empty()) {
        std::cout << "No music files found in 'music' folder." << std::endl;
    } else {
        std::cout << "Total music files: " << musicFiles.size() << std::endl;
    }
}

void updateMusic() {
    if (!musicPlaying) return;
    if (musicTransitioning) return;
    
    sf::SoundSource::Status status = currentMusic.getStatus();
    float elapsed = glfwGetTime() - trackStartTime;
    
    static float lastLogTime = 0;
    if (glfwGetTime() - lastLogTime > 10.0f) {
        lastLogTime = glfwGetTime();
        const char* statusStr = "Unknown";
        if (status == sf::SoundSource::Status::Playing) statusStr = "Playing";
        else if (status == sf::SoundSource::Status::Paused) statusStr = "Paused";
        else if (status == sf::SoundSource::Status::Stopped) statusStr = "Stopped";
        std::cout << "Music: " << statusStr << ", elapsed " << elapsed << "s" << std::endl;
    }
    
    if (status == sf::SoundSource::Status::Stopped && elapsed > 3.0f) {
        std::cout << "Track finished naturally, switching..." << std::endl;
        musicPlaying = false;
        playRandomMusic();
    }
}

// ----------------------------------------------------------------------
// Структура чанка (рендер)
// ----------------------------------------------------------------------
struct Chunk {
    glm::ivec2 pos;
    std::shared_ptr<ChunkData> data;
    std::unordered_map<int, unsigned int> vaoPerType;
    std::unordered_map<int, unsigned int> vboPerType;
    std::unordered_map<int, size_t> vertexCountPerType;
    bool meshReady = false;
    bool dirty = false;
    bool meshBuilding = false;
    std::mutex meshMutex;

    Chunk(int cx, int cz) : pos(cx, cz) {
        std::lock_guard<std::mutex> lock(chunkMutex);
        std::string filename = CHUNKS_DIR + "/chunk_" + std::to_string(cx) + "_" + std::to_string(cz) + ".json";
        if (fs::exists(filename))
            pendingLoad.insert(pos);
        else
            pendingGen.insert(pos);
    }

    bool updateData() {
        if (data) return true;
        std::lock_guard<std::mutex> lock(chunkMutex);
        auto it = pendingData.find(pos);
        if (it != pendingData.end()) {
            data = it->second;
            pendingData.erase(it);
            meshReady = false;
            dirty = false;
            invalidateNeighbors();
            return true;
        }
        return false;
    }

    void invalidateNeighbors() {
        glm::ivec2 neighbors[4] = {{pos.x-1,pos.y}, {pos.x+1,pos.y}, {pos.x,pos.y-1}, {pos.x,pos.y+1}};
        for (auto& npos : neighbors) {
            auto it = loadedChunks.find(npos);
            if (it != loadedChunks.end()) it->second.meshReady = false;
        }
    }

    int getLocalBlock(int x, int y, int z) const {
        if (!data) return 0;
        return data->blocks[(x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z];
    }

    void setLocalBlock(int x, int y, int z, int id) {
        if (!data) return;
        data->blocks[(x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z] = id;
        meshReady = false;
        dirty = true;
    }

    void buildMesh() {
        if (!data) return;
        std::lock_guard<std::mutex> lock(meshMutex);
        if (meshBuilding) return;
        meshBuilding = true;

        for (auto& p : vaoPerType) {
            glDeleteVertexArrays(1, &p.second);
            glDeleteBuffers(1, &vboPerType[p.first]);
        }
        vaoPerType.clear();
        vboPerType.clear();
        vertexCountPerType.clear();

        const float leftFace[] = { -0.5f,-0.5f,-0.5f, -0.5f,-0.5f,0.5f, -0.5f,0.5f,0.5f, -0.5f,0.5f,0.5f, -0.5f,0.5f,-0.5f, -0.5f,-0.5f,-0.5f };
        const float rightFace[] = { 0.5f,-0.5f,0.5f, 0.5f,-0.5f,-0.5f, 0.5f,0.5f,-0.5f, 0.5f,0.5f,-0.5f, 0.5f,0.5f,0.5f, 0.5f,-0.5f,0.5f };
        const float bottomFace[] = { -0.5f,-0.5f,-0.5f, 0.5f,-0.5f,-0.5f, 0.5f,-0.5f,0.5f, 0.5f,-0.5f,0.5f, -0.5f,-0.5f,0.5f, -0.5f,-0.5f,-0.5f };
        const float topFace[] = { -0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f, 0.5f,0.5f,-0.5f, 0.5f,0.5f,-0.5f, -0.5f,0.5f,-0.5f, -0.5f,0.5f,0.5f };
        const float frontFace[] = { -0.5f,-0.5f,0.5f, 0.5f,-0.5f,0.5f, 0.5f,0.5f,0.5f, 0.5f,0.5f,0.5f, -0.5f,0.5f,0.5f, -0.5f,-0.5f,0.5f };
        const float backFace[] = { 0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f,0.5f,-0.5f, -0.5f,0.5f,-0.5f, 0.5f,0.5f,-0.5f, 0.5f,-0.5f,-0.5f };
        const float baseUV[] = { 0,0, 1,0, 1,1, 1,1, 0,1, 0,0 };

        std::unordered_map<int, std::vector<float>> verticesPerType;

        for (int x = 0; x < CHUNK_SIZE_X; ++x) {
            for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
                    int type = getLocalBlock(x, y, z);
                    if (type == 0) continue;
                    float ox = pos.x * CHUNK_SIZE_X + x;
                    float oy = y;
                    float oz = pos.y * CHUNK_SIZE_Z + z;

                    auto addFace = [&](const float* face, float uOffset, std::vector<float>& outVerts) {
                        for (int i = 0; i < 18; i += 3) {
                            outVerts.push_back(face[i] + ox);
                            outVerts.push_back(face[i+1] + oy);
                            outVerts.push_back(face[i+2] + oz);
                            int uvIdx = (i/3)*2;
                            float u = baseUV[uvIdx];
                            float v = baseUV[uvIdx+1];
                            if (uOffset == 1.0f/3.0f) v = 1.0f - v;
                            outVerts.push_back(uOffset + u * (1.0f/3.0f));
                            outVerts.push_back(v);
                        }
                    };

                    std::vector<float>& verts = verticesPerType[type];
                    int neighbor;
                    if (type == 5) {
                        neighbor = getBlockAtForMesh(ox-1, oy, oz);
                        if (neighbor != 5 && neighbor != BLOCK_UNKNOWN) addFace(leftFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox+1, oy, oz);
                        if (neighbor != 5 && neighbor != BLOCK_UNKNOWN) addFace(rightFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy, oz+1);
                        if (neighbor != 5 && neighbor != BLOCK_UNKNOWN) addFace(frontFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy, oz-1);
                        if (neighbor != 5 && neighbor != BLOCK_UNKNOWN) addFace(backFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy+1, oz);
                        if (neighbor != 5 && neighbor != BLOCK_UNKNOWN) addFace(topFace, 0.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy-1, oz);
                        if (neighbor != 5 && neighbor != BLOCK_UNKNOWN) addFace(bottomFace, 2.0f/3.0f, verts);
                    } else {
                        neighbor = getBlockAtForMesh(ox-1, oy, oz);
                        if (neighbor == 0 || neighbor == 5) addFace(leftFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox+1, oy, oz);
                        if (neighbor == 0 || neighbor == 5) addFace(rightFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy, oz+1);
                        if (neighbor == 0 || neighbor == 5) addFace(frontFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy, oz-1);
                        if (neighbor == 0 || neighbor == 5) addFace(backFace, 1.0f/3.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy+1, oz);
                        if (neighbor == 0 || neighbor == 5) addFace(topFace, 0.0f, verts);
                        neighbor = getBlockAtForMesh(ox, oy-1, oz);
                        if (neighbor == 0 || neighbor == 5) addFace(bottomFace, 2.0f/3.0f, verts);
                    }
                }
            }
        }

        for (auto& pair : verticesPerType) {
            int type = pair.first;
            std::vector<float>& verts = pair.second;
            if (verts.empty()) continue;
            unsigned int vao, vbo;
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            vaoPerType[type] = vao;
            vboPerType[type] = vbo;
            vertexCountPerType[type] = verts.size() / 5;
        }
        meshReady = true;
        meshBuilding = false;
    }

    void render() {
        if (!data) return;
        if (!meshReady) buildMesh();
        for (auto& pair : vaoPerType) {
            int type = pair.first;
            if (type == 5) continue;
            auto it = blockTypes.find(type);
            if (it == blockTypes.end()) continue;
            glUniform1i(u_isWater_location, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, it->second.textureID);
            glBindVertexArray(pair.second);
            glDrawArrays(GL_TRIANGLES, 0, vertexCountPerType[type]);
        }
    }

    void renderWater() {
        auto waterIt = vaoPerType.find(5);
        if (waterIt == vaoPerType.end()) return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1i(u_isWater_location, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blockTypes[5].textureID);
        glBindVertexArray(waterIt->second);
        glDrawArrays(GL_TRIANGLES, 0, vertexCountPerType[5]);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    void saveAsync() {
        if (!dirty || !data) return;
        dirty = false;
        std::vector<int> blocksCopy = data->blocks;
        glm::ivec2 posCopy = pos;
        std::thread([posCopy, blocksCopy]() {
            saveChunkToFile(posCopy, blocksCopy);
        }).detach();
    }
};

// ----------------------------------------------------------------------
// getBlockAtForMesh с кешированием
// ----------------------------------------------------------------------
int getBlockAtForMesh(int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return 0;
    int cx = (wx >= 0) ? wx / CHUNK_SIZE_X : (wx - CHUNK_SIZE_X + 1) / CHUNK_SIZE_X;
    int cz = (wz >= 0) ? wz / CHUNK_SIZE_Z : (wz - CHUNK_SIZE_Z + 1) / CHUNK_SIZE_Z;
    if (lastChunkForMesh && lastChunkCoordsForMesh.x == cx && lastChunkCoordsForMesh.y == cz) {
        int lx = wx - cx * CHUNK_SIZE_X;
        int lz = wz - cz * CHUNK_SIZE_Z;
        if (lx >=0 && lx<CHUNK_SIZE_X && lz>=0 && lz<CHUNK_SIZE_Z)
            return lastChunkForMesh->getLocalBlock(lx, wy, lz);
        return BLOCK_UNKNOWN;
    }
    auto it = loadedChunks.find({cx, cz});
    if (it != loadedChunks.end()) {
        lastChunkForMesh = &it->second;
        lastChunkCoordsForMesh = {cx, cz};
        int lx = wx - cx * CHUNK_SIZE_X;
        int lz = wz - cz * CHUNK_SIZE_Z;
        if (lx>=0 && lx<CHUNK_SIZE_X && lz>=0 && lz<CHUNK_SIZE_Z)
            return it->second.getLocalBlock(lx, wy, lz);
    }
    return BLOCK_UNKNOWN;
}

int getBlockAt(int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return 0;
    int cx = (wx >= 0) ? wx / CHUNK_SIZE_X : (wx - CHUNK_SIZE_X + 1) / CHUNK_SIZE_X;
    int cz = (wz >= 0) ? wz / CHUNK_SIZE_Z : (wz - CHUNK_SIZE_Z + 1) / CHUNK_SIZE_Z;
    auto it = loadedChunks.find({cx, cz});
    if (it == loadedChunks.end()) return 0;
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;
    return it->second.getLocalBlock(lx, wy, lz);
}

void setBlockAt(int wx, int wy, int wz, int type) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return;
    int cx = (wx >= 0) ? wx / CHUNK_SIZE_X : (wx - CHUNK_SIZE_X + 1) / CHUNK_SIZE_X;
    int cz = (wz >= 0) ? wz / CHUNK_SIZE_Z : (wz - CHUNK_SIZE_Z + 1) / CHUNK_SIZE_Z;
    auto it = loadedChunks.find({cx, cz});
    if (it == loadedChunks.end()) return;
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;
    it->second.setLocalBlock(lx, wy, lz, type);
    if (lx == 0) {
        auto neigh = loadedChunks.find({cx-1, cz});
        if (neigh != loadedChunks.end()) neigh->second.meshReady = false;
    }
    if (lx == CHUNK_SIZE_X-1) {
        auto neigh = loadedChunks.find({cx+1, cz});
        if (neigh != loadedChunks.end()) neigh->second.meshReady = false;
    }
    if (lz == 0) {
        auto neigh = loadedChunks.find({cx, cz-1});
        if (neigh != loadedChunks.end()) neigh->second.meshReady = false;
    }
    if (lz == CHUNK_SIZE_Z-1) {
        auto neigh = loadedChunks.find({cx, cz+1});
        if (neigh != loadedChunks.end()) neigh->second.meshReady = false;
    }
    waterChunksCacheValid = false;
}

// ----------------------------------------------------------------------
// Управление чанками
// ----------------------------------------------------------------------
void updateChunksAroundCamera(const glm::vec3& cameraPos) {
    int centerCX = (int)std::floor(cameraPos.x / CHUNK_SIZE_X);
    int centerCZ = (int)std::floor(cameraPos.z / CHUNK_SIZE_Z);
    const int RENDER_RADIUS = 12;
    const int LOAD_RADIUS = RENDER_RADIUS + 1;

    std::unordered_set<glm::ivec2, hash_ivec2> neededForLoad;
    for (int dx = -LOAD_RADIUS; dx <= LOAD_RADIUS; ++dx)
        for (int dz = -LOAD_RADIUS; dz <= LOAD_RADIUS; ++dz)
            neededForLoad.insert({centerCX + dx, centerCZ + dz});

    bool changed = false;
    for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ) {
        if (neededForLoad.find(it->first) == neededForLoad.end()) {
            it->second.saveAsync();
            for (auto& p : it->second.vaoPerType) {
                glDeleteVertexArrays(1, &p.second);
                glDeleteBuffers(1, &it->second.vboPerType[p.first]);
            }
            it = loadedChunks.erase(it);
            changed = true;
        } else ++it;
    }
    for (const auto& key : neededForLoad) {
        if (loadedChunks.find(key) == loadedChunks.end()) {
            loadedChunks.emplace(std::piecewise_construct, std::forward_as_tuple(key.x, key.y), std::forward_as_tuple(key.x, key.y));
            changed = true;
        }
    }
    for (auto& pair : loadedChunks) pair.second.updateData();
    if (changed) waterChunksCacheValid = false;
}

void updateWaterChunksCache() {
    if (waterChunksCacheValid) return;
    waterChunksCache.clear();
    for (auto& pair : loadedChunks) {
        if (pair.second.vaoPerType.count(5))
            waterChunksCache.push_back(&pair.second);
    }
    waterChunksCacheValid = true;
}

void saveAllChunks() {
    for (auto& pair : loadedChunks) {
        if (pair.second.dirty) pair.second.saveAsync();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

// ----------------------------------------------------------------------
// Загрузка текстур блоков
// ----------------------------------------------------------------------
unsigned int loadTextureStrip(const char* path, bool forceAlpha = false) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    int width, height, nrChannels;
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, forceAlpha ? 4 : 0);
    if (data) {
        GLenum format = (forceAlpha || nrChannels == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout << "Loaded texture: " << path << std::endl;
    } else {
        std::cerr << "Failed to load texture: " << path << std::endl;
        unsigned char dummy[4] = {255,255,255,255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, dummy);
    }
    stbi_image_free(data);
    return tex;
}

bool loadBlockConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    json data = json::parse(f);
    for (auto& item : data["blocks"]) {
        BlockType bt;
        bt.id = item["id"];
        bt.name = item["name"];
        std::string texPath = item["texture_strip"];
        bool isWater = (bt.id == 5);
        bt.textureID = loadTextureStrip(texPath.c_str(), isWater);
        blockTypes[bt.id] = bt;
    }
    return !blockTypes.empty();
}

// ----------------------------------------------------------------------
// RayCast
// ----------------------------------------------------------------------
bool rayCast(glm::vec3 origin, glm::vec3 direction, int& hitX, int& hitY, int& hitZ, int& faceHit, float maxDistance) {
    direction = glm::normalize(direction);
    glm::ivec3 pos((int)std::floor(origin.x+0.5f), (int)std::floor(origin.y+0.5f), (int)std::floor(origin.z+0.5f));
    glm::ivec3 step;
    step.x = (direction.x>0)?1:(direction.x<0)?-1:0;
    step.y = (direction.y>0)?1:(direction.y<0)?-1:0;
    step.z = (direction.z>0)?1:(direction.z<0)?-1:0;
    float tDeltaX = (direction.x!=0)?fabs(1.0f/direction.x):INFINITY;
    float tDeltaY = (direction.y!=0)?fabs(1.0f/direction.y):INFINITY;
    float tDeltaZ = (direction.z!=0)?fabs(1.0f/direction.z):INFINITY;
    float tMaxX, tMaxY, tMaxZ;
    if (direction.x>0) tMaxX = ((pos.x+0.5f)-origin.x)/direction.x;
    else if (direction.x<0) tMaxX = ((pos.x-0.5f)-origin.x)/direction.x;
    else tMaxX = INFINITY;
    if (direction.y>0) tMaxY = ((pos.y+0.5f)-origin.y)/direction.y;
    else if (direction.y<0) tMaxY = ((pos.y-0.5f)-origin.y)/direction.y;
    else tMaxY = INFINITY;
    if (direction.z>0) tMaxZ = ((pos.z+0.5f)-origin.z)/direction.z;
    else if (direction.z<0) tMaxZ = ((pos.z-0.5f)-origin.z)/direction.z;
    else tMaxZ = INFINITY;
    const int MAX_STEPS = 200;
    for (int i=0; i<MAX_STEPS; ++i) {
        int block = getBlockAt(pos.x, pos.y, pos.z);
        if (block != 0 && block != 5) {
            hitX = pos.x; hitY = pos.y; hitZ = pos.z;
            return true;
        }
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ) {
            if (tMaxX > maxDistance) break;
            pos.x += step.x;
            faceHit = (direction.x>0)?1:0;
            tMaxX += tDeltaX;
        } else if (tMaxY <= tMaxX && tMaxY <= tMaxZ) {
            if (tMaxY > maxDistance) break;
            pos.y += step.y;
            faceHit = (direction.y>0)?3:2;
            tMaxY += tDeltaY;
        } else {
            if (tMaxZ > maxDistance) break;
            pos.z += step.z;
            faceHit = (direction.z>0)?5:4;
            tMaxZ += tDeltaZ;
        }
    }
    return false;
}

// ----------------------------------------------------------------------
// Обработка мыши и клавиатуры
// ----------------------------------------------------------------------
bool gameStarted = false;
double mouseX = 0, mouseY = 0;

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (!gameStarted && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        int w,h; glfwGetWindowSize(window,&w,&h);
        for (int i=0; i<3; ++i) {
            if (mouseX >= buttons[i].absX && mouseX <= buttons[i].absX+buttons[i].absW &&
                mouseY >= buttons[i].absY && mouseY <= buttons[i].absY+buttons[i].absH) {
                buttons[i].clicked = true;
            }
        }
        return;
    }
    if (!gameStarted) return;
    if (action != GLFW_PRESS) return;
    glm::vec3 rayDir = cameraFront;
    int hitX, hitY, hitZ, face;
    if (rayCast(cameraPos, rayDir, hitX, hitY, hitZ, face, 10.0f)) {
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            int blockType = getBlockAt(hitX, hitY, hitZ);
            if (blockType == 5) { std::cout << "Cannot remove water!\n"; return; }
            setBlockAt(hitX, hitY, hitZ, 0);
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            int newX=hitX, newY=hitY, newZ=hitZ;
            switch(face) {
                case 0: newX=hitX+1; break;
                case 1: newX=hitX-1; break;
                case 2: newY=hitY+1; break;
                case 3: newY=hitY-1; break;
                case 4: newZ=hitZ+1; break;
                case 5: newZ=hitZ-1; break;
            }
            if (getBlockAt(newX,newY,newZ) != 0 && getBlockAt(newX,newY,newZ) != 5) return;
            setBlockAt(newX,newY,newZ, currentBlockType);
        }
    }
}

void processInput(GLFWwindow *window) {
    if (!gameStarted) return;
    
    float moveSpeed = 12.0f;
    glm::vec3 moveDir = glm::vec3(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) moveDir += cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) moveDir -= cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) moveDir -= glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) moveDir += glm::normalize(glm::cross(cameraFront, cameraUp));
    
    if (glm::length(moveDir) > 0.1f) moveDir = glm::normalize(moveDir);
    glm::vec3 desiredMove = moveDir * moveSpeed * deltaTime;
    
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isOnGround) {
        playerVelocity.y = JUMP_POWER;
        isOnGround = false;
    }
    
    playerVelocity.y += GRAVITY * deltaTime;
    glm::vec3 delta = desiredMove;
    delta.y = playerVelocity.y * deltaTime;
    
    glm::vec3 feetPos = cameraPos - glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
    glm::vec3 actualDelta = applyCollision(feetPos, delta);
    feetPos += actualDelta;
    cameraPos = feetPos + glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
    
    if (feetPos.y < 0.0f) {
        feetPos.y = 0.0f;
        cameraPos = feetPos + glm::vec3(0.0f, EYE_HEIGHT, 0.0f);
        playerVelocity.y = 0.0f;
        isOnGround = true;
    }
    
    for (int i=1; i<=9; ++i) {
        if (glfwGetKey(window, GLFW_KEY_0 + i) == GLFW_PRESS) {
            if (blockTypes.find(i) != blockTypes.end()) currentBlockType = i;
        }
    }
}

// ----------------------------------------------------------------------
// Шейдеры
// ----------------------------------------------------------------------
const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";

const char *fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D ourTexture;
uniform float u_time;
uniform int u_isWater;
void main() {
    vec2 uv = TexCoord;
    if (u_isWater == 1) {
        float frames = 32.0;
        float speed = 0.7;
        float frame = fract(u_time * speed) * frames;
        int frameIdx = int(floor(frame));
        float frameOffset = float(frameIdx) / frames;
        uv.y = uv.y / frames + frameOffset;
    }
    vec4 color = texture(ourTexture, uv);
    if (u_isWater == 1) color.a = 0.7;
    FragColor = color;
}
)";

const char *reticleVertexSource = R"(
#version 330 core
void main() {
    gl_PointSize = 10.0;
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)";
const char *reticleFragmentSource = R"(
#version 330 core
out vec4 FragColor;
void main() {
    vec2 coord = gl_PointCoord;
    if (length(coord - vec2(0.5)) > 0.4) discard;
    FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)";

void checkShaderErrors(unsigned int shader, const std::string& type) {
    int success; char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "Shader error (" << type << "):\n" << infoLog << std::endl;
    }
}

void checkProgramErrors(unsigned int program) {
    int success; char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "Program linking error:\n" << infoLog << std::endl;
    }
}

void initReticle() {
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &reticleVertexSource, NULL);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &reticleFragmentSource, NULL);
    glCompileShader(fs);
    reticleProgram = glCreateProgram();
    glAttachShader(reticleProgram, vs);
    glAttachShader(reticleProgram, fs);
    glLinkProgram(reticleProgram);
    checkProgramErrors(reticleProgram);
    glDeleteShader(vs); glDeleteShader(fs);
    glGenVertexArrays(1, &reticleVAO);
    glBindVertexArray(reticleVAO);
    glBindVertexArray(0);
}

// ----------------------------------------------------------------------
// main
// ----------------------------------------------------------------------
int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Voxel Builder", NULL, NULL);
    if (!window) { std::cerr << "Failed to create window\n"; glfwTerminate(); return -1; }
    glfwSetWindowPos(window, 0, 0);
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int w, int h) { glViewport(0, 0, w, h); });
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
        mouseX = x; mouseY = y;
        if (!gameStarted) return;
        static bool first = true;
        if (first) { lastX = x; lastY = y; first = false; }
        float xoffset = x - lastX;
        float yoffset = lastY - y;
        lastX = x; lastY = y;
        const float sensitivity = 0.1f;
        xoffset *= sensitivity; yoffset *= sensitivity;
        yaw += xoffset; pitch += yoffset;
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
        glm::vec3 front;
        front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        front.y = sin(glm::radians(pitch));
        front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        cameraFront = glm::normalize(front);
    });
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowCloseCallback(window, [](GLFWwindow*) { if (gameStarted) saveAllChunks(); });
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::cerr << "Failed to init GLAD\n"; return -1; }
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW); glEnable(GL_DEPTH_TEST);

    // Шейдеры
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs); checkShaderErrors(vs, "vertex");
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs); checkShaderErrors(fs, "fragment");
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vs); glAttachShader(shaderProgram, fs);
    glLinkProgram(shaderProgram); checkProgramErrors(shaderProgram);
    glDeleteShader(vs); glDeleteShader(fs);
    glUseProgram(shaderProgram);
    glUniform1i(glGetUniformLocation(shaderProgram, "ourTexture"), 0);
    u_time_location = glGetUniformLocation(shaderProgram, "u_time");
    u_isWater_location = glGetUniformLocation(shaderProgram, "u_isWater");
    u_modelLoc = glGetUniformLocation(shaderProgram, "model");
    u_viewLoc = glGetUniformLocation(shaderProgram, "view");
    u_projLoc = glGetUniformLocation(shaderProgram, "projection");

    if (!loadBlockConfig("blocks.json")) { std::cerr << "Failed to load block config\n"; return -1; }

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    ImGui::StyleColorsDark();

    // UI
    initUI();
    loadMenuTextures();
    int screenW = mode->width, screenH = mode->height;
    updateButtonPositions(screenW, screenH);
    updatePhotoPosition(screenW, screenH);

    // Шум
    initWorldNoise();

    // Музыка
    scanMusicFolder();
    gameStarted = false;

    while (!glfwWindowShouldClose(window)) {
        float now = glfwGetTime();
        deltaTime = now - lastFrame;
        lastFrame = now;

        if (!gameStarted) {
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST); 
            glDisable(GL_CULL_FACE);
            
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            
            if (menuBackgroundTexture) drawRectangle(0,0,screenW,screenH,menuBackgroundTexture,screenW,screenH);
            if (menuPhotoTexture) drawRectangle(photoAbsX, photoAbsY, photoAbsW, photoAbsH, menuPhotoTexture, screenW, screenH);
            for (int i=0;i<3;++i) if(menuButtonTexture) drawRectangle(buttons[i].absX,buttons[i].absY,buttons[i].absW,buttons[i].absH,menuButtonTexture,screenW,screenH);
            
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST); 
            glEnable(GL_CULL_FACE);
            
            if (buttons[0].clicked) {
                buttons[0].clicked=false; gameStarted=true; glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
                workerThread = std::thread(workerFunction);
                updateChunksAroundCamera(cameraPos);
                placePlayerOnGround();
                initReticle();
                startMusic();
            }
            if (buttons[1].clicked) {
                buttons[1].clicked=false; gameStarted=true; glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_DISABLED);
                workerThread = std::thread(workerFunction);
                updateChunksAroundCamera(cameraPos);
                placePlayerOnGround();
                initReticle();
                startMusic();
            }
            if (buttons[2].clicked) { buttons[2].clicked=false; break; }
        } else {
            processInput(window);
            updateMusic();
            updateChunksAroundCamera(cameraPos);
            glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glm::mat4 model = glm::mat4(1.0f);
            glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
            int w,h; glfwGetWindowSize(window,&w,&h);
            glm::mat4 projection = glm::perspective(glm::radians(65.0f), (float)w/(float)h, 0.1f, 1000.0f);
            glUseProgram(shaderProgram);
            glUniformMatrix4fv(u_modelLoc,1,GL_FALSE,glm::value_ptr(model));
            glUniformMatrix4fv(u_viewLoc,1,GL_FALSE,glm::value_ptr(view));
            glUniformMatrix4fv(u_projLoc,1,GL_FALSE,glm::value_ptr(projection));
            glUniform1f(u_time_location, now);
            for (auto& pair : loadedChunks) pair.second.render();
            updateWaterChunksCache();
            if (glm::distance(cameraPos, lastCameraPosForWaterSort) > 0.5f) {
                std::sort(waterChunksCache.begin(), waterChunksCache.end(),
                    [&](const Chunk* a, const Chunk* b) {
                        glm::vec3 centerA(a->pos.x*CHUNK_SIZE_X+CHUNK_SIZE_X/2, 30.0f, a->pos.y*CHUNK_SIZE_Z+CHUNK_SIZE_Z/2);
                        glm::vec3 centerB(b->pos.x*CHUNK_SIZE_X+CHUNK_SIZE_X/2, 30.0f, b->pos.y*CHUNK_SIZE_Z+CHUNK_SIZE_Z/2);
                        return glm::distance(cameraPos, centerA) > glm::distance(cameraPos, centerB);
                    });
                lastCameraPosForWaterSort = cameraPos;
            }
            for (Chunk* chunk : waterChunksCache) chunk->renderWater();

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glUseProgram(reticleProgram);
            glBindVertexArray(reticleVAO);
            glDrawArrays(GL_POINTS, 0, 1);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);

            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
            ImGui::Begin("Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Chunks: %zu", loadedChunks.size());
            ImGui::Text("Pos: (%.1f, %.1f, %.1f)", cameraPos.x, cameraPos.y, cameraPos.z);
            ImGui::Text("Block: %s", blockTypes[currentBlockType].name.c_str());
            ImGui::Text("OnGround: %s", isOnGround ? "Yes" : "No");
            if (ImGui::Button("Exit to Menu")) {
                gameStarted = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                for (auto& pair : loadedChunks) {
                    for (auto& p : pair.second.vaoPerType) {
                        glDeleteVertexArrays(1, &p.second);
                        glDeleteBuffers(1, &pair.second.vboPerType[p.first]);
                    }
                }
                loadedChunks.clear(); waterChunksCacheValid = false;
                workerRunning = false;
                if (workerThread.joinable()) workerThread.join();
                workerRunning = true;
                glfwGetWindowSize(window, &screenW, &screenH);
                updateButtonPositions(screenW, screenH);
                updatePhotoPosition(screenW, screenH);
                stopMusic();
                playerVelocity = glm::vec3(0.0f);
                isOnGround = false;
            }
            ImGui::End(); ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (gameStarted) {
        saveAllChunks();
        workerRunning = false;
        if (workerThread.joinable()) workerThread.join();
        for (auto& pair : loadedChunks) {
            for (auto& p : pair.second.vaoPerType) {
                glDeleteVertexArrays(1, &p.second);
                glDeleteBuffers(1, &pair.second.vboPerType[p.first]);
            }
        }
    }
    stopMusic();
    if (menuBackgroundTexture) glDeleteTextures(1, &menuBackgroundTexture);
    if (menuButtonTexture) glDeleteTextures(1, &menuButtonTexture);
    if (menuPhotoTexture) glDeleteTextures(1, &menuPhotoTexture);
    glDeleteVertexArrays(1, &uiVAO); glDeleteBuffers(1, &uiVBO); glDeleteBuffers(1, &uiEBO); glDeleteProgram(uiShaderProgram);
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    for (auto& p : blockTypes) glDeleteTextures(1, &p.second.textureID);
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &reticleVAO); glDeleteProgram(reticleProgram);
    glfwTerminate();
    return 0;
}
