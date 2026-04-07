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

// Глобальные объекты шума
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

// Окно и камера
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;
const int BLOCK_UNKNOWN = -1;

// Параметры игрока (физика) - исправлено: камера по центру хитбокса
glm::vec3 playerPos;           // позиция ног (центр по X/Z)
glm::vec3 cameraPos;           // позиция камеры (глаза)
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

float yaw   = -90.0f;
float pitch =  0.0f;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Физика (без автоматического подъёма на ступеньки)
float playerWidth  = 0.6f;
float playerHeight = 1.8f;
float eyeHeight    = 1.62f;      // стандартная высота глаз в Minecraft
float velocityY    = 0.0f;
bool onGround      = true;
float gravity      = 20.0f;
float jumpSpeed    = 7.0f;
float walkSpeed    = 4.5f;
float sprintSpeed  = 7.0f;
float currentSpeed = walkSpeed;
bool jumpRequested = false;

// Типы блоков
struct BlockType {
    int id;
    std::string name;
    unsigned int textureID;
};
std::unordered_map<int, BlockType> blockTypes;
int currentBlockType = 1;

// Параметры чанков
const int CHUNK_SIZE_X = 17;
const int CHUNK_SIZE_Z = 17;
const int CHUNK_SIZE_Y = 128;

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

// Инициализация шумов
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
    if (cont > 0.25f) mountain = mountainNoise.GetNoise((float)wx, (float)wz) * 12.0f;
    float river = riverNoise.GetNoise((float)wx, (float)wz);
    float riverFactor = (std::abs(river) < 0.1f) ? -5.0f * (1.0f - std::abs(river) / 0.1f) : 0.0f;
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
    float beachZone = height - waterLevel;
    if (beachZone < 3.0f) return 6;
    float t = (temp + 1.0f) / 2.0f;
    float h = (humid + 1.0f) / 2.0f;
    if (height > waterLevel + 25.0f) return 2;
    if (t < 0.25f) return 7;
    if (t > 0.7f && h > 0.6f) return 1;
    if (h < 0.3f) return 8;
    return 0;
}

// Деревья
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

// Генерация чанка
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
                    if (y == surfaceY) blockId = (biome == 5) ? 3 : 4;
                    else if (y > surfaceY && y <= waterSurfaceY) blockId = 5;
                    else if (y < surfaceY) blockId = (y > surfaceY - 4) ? 4 : 3;
                } else if (biome == 6) {
                    if (y == surfaceY) blockId = 4;
                    else if (y < surfaceY) blockId = (y > surfaceY - 3) ? 4 : 3;
                } else {
                    if (y == surfaceY) blockId = (biome == 2 || biome == 7) ? 3 : ((biome == 8) ? 4 : 1);
                    else if (y > surfaceY - 4 && y < surfaceY) blockId = (biome == 2 || biome == 7) ? 3 : ((biome == 8) ? 4 : 2);
                    else if (y < surfaceY - 4) blockId = 3;
                }
                data->blocks[(x * CHUNK_SIZE_Y + y) * CHUNK_SIZE_Z + z] = blockId;
            }
            
            if ((biome == 0 || biome == 1) && surfaceY > waterSurfaceY + 2) {
                addTree(cx, cz, x, z, surfaceY, data->blocks);
            }
        }
    }
    
    // сглаживание берегов
    for (int x = 0; x < CHUNK_SIZE_X; ++x) {
        for (int z = 0; z < CHUNK_SIZE_Z; ++z) {
            int worldX = cx * CHUNK_SIZE_X + x;
            int worldZ = cz * CHUNK_SIZE_Z + z;
            float biomeTemp, biomeHumid, waterLevel;
            float landHeight = getHeightAt(worldX, worldZ, biomeTemp, biomeHumid, waterLevel);
            int surfaceY = (int)landHeight;
            bool hasWaterNeighbor = false;
            for (int dx = -1; dx <= 1 && !hasWaterNeighbor; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dz == 0) continue;
                    int nx = x + dx, nz = z + dz;
                    if (nx >= 0 && nx < CHUNK_SIZE_X && nz >= 0 && nz < CHUNK_SIZE_Z) {
                        int nWorldX = cx * CHUNK_SIZE_X + nx;
                        int nWorldZ = cz * CHUNK_SIZE_Z + nz;
                        float nTemp, nHumid, nWaterLevel;
                        float nHeight = getHeightAt(nWorldX, nWorldZ, nTemp, nHumid, nWaterLevel);
                        if (nHeight <= nWaterLevel) { hasWaterNeighbor = true; break; }
                    }
                }
            }
            if (hasWaterNeighbor && landHeight > waterLevel && landHeight - waterLevel < 4.0f) {
                int idx = (x * CHUNK_SIZE_Y + surfaceY) * CHUNK_SIZE_Z + z;
                int currentBlock = data->blocks[idx];
                if (currentBlock == 1 || currentBlock == 2) data->blocks[idx] = 4;
            }
        }
    }
    
    data->valid = true;
    return data;
}

// Очереди для фоновых потоков
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
        if (processed == 0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Прототипы
int getBlockAt(int wx, int wy, int wz);
void setBlockAt(int wx, int wy, int wz, int type);
int getBlockAtForMesh(int wx, int wy, int wz);

// Шейдерные переменные
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

// UI (меню)
unsigned int uiShaderProgram;
unsigned int uiVAO, uiVBO, uiEBO;
unsigned int menuBackgroundTexture = 0;
unsigned int menuButtonTexture = 0;

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
}

void updateButtonPositions(int screenW, int screenH) {
    for (int i=0; i<3; ++i) {
        buttons[i].absW = buttons[i].relW * screenW;
        buttons[i].absH = buttons[i].relH * screenH;
        buttons[i].absX = buttons[i].relX * screenW - buttons[i].absW/2;
        buttons[i].absY = buttons[i].relY * screenH - buttons[i].absH/2;
    }
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

// Музыка через SFML
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
    if (newIndex == currentTrackIndex && musicFiles.size() > 1) newIndex = (newIndex + 1) % musicFiles.size();
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
    if (!fs::exists(musicDir)) fs::create_directory(musicDir);
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
    if (musicFiles.empty()) std::cout << "No music files found in 'music' folder." << std::endl;
    else std::cout << "Total music files: " << musicFiles.size() << std::endl;
}

void updateMusic() {
    if (!musicPlaying || musicTransitioning) return;
    sf::SoundSource::Status status = currentMusic.getStatus();
    if (status == sf::SoundSource::Status::Stopped && (glfwGetTime() - trackStartTime) > 3.0f) {
        musicPlaying = false;
        playRandomMusic();
    }
}

// Структура чанка (рендер)
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
        if (fs::exists(filename)) pendingLoad.insert(pos);
        else pendingGen.insert(pos);
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
        std::thread([posCopy, blocksCopy]() { saveChunkToFile(posCopy, blocksCopy); }).detach();
    }
};

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

// Функция для коллизий: незагруженные чанки считаем воздухом (0)
int getBlockAtCollision(int wx, int wy, int wz) {
    if (wy < 0 || wy >= CHUNK_SIZE_Y) return 0;
    int cx = (wx >= 0) ? wx / CHUNK_SIZE_X : (wx - CHUNK_SIZE_X + 1) / CHUNK_SIZE_X;
    int cz = (wz >= 0) ? wz / CHUNK_SIZE_Z : (wz - CHUNK_SIZE_Z + 1) / CHUNK_SIZE_Z;
    auto it = loadedChunks.find({cx, cz});
    if (it == loadedChunks.end()) return 0; // не загружен -> воздух
    int lx = wx - cx * CHUNK_SIZE_X;
    int lz = wz - cz * CHUNK_SIZE_Z;
    if (lx < 0 || lx >= CHUNK_SIZE_X || lz < 0 || lz >= CHUNK_SIZE_Z) return 0;
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

// Управление чанками
void updateChunksAroundCamera(const glm::vec3& camPos) {
    int centerCX = (int)std::floor(camPos.x / CHUNK_SIZE_X);
    int centerCZ = (int)std::floor(camPos.z / CHUNK_SIZE_Z);
    const int RENDER_RADIUS = 20;
    const int LOAD_RADIUS = 24;

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
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// Загрузка текстур блоков
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

// RayCast
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

// ФИЗИКА И КОЛЛИЗИИ (исправлено: камера по центру, точная коллизия)
inline bool isSolidBlock(int id) {
    return id != 0 && id != 5;
}

void getPlayerAABB(const glm::vec3& pos, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(pos.x - playerWidth/2.0f, pos.y, pos.z - playerWidth/2.0f);
    outMax = glm::vec3(pos.x + playerWidth/2.0f, pos.y + playerHeight, pos.z + playerWidth/2.0f);
}

// Проверка коллизий с точной границей
bool checkCollision(const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    int minX = (int)std::floor(aabbMin.x);
    int maxX = (int)std::floor(aabbMax.x - 1e-6f);
    int minY = (int)std::floor(aabbMin.y);
    int maxY = (int)std::floor(aabbMax.y - 1e-6f);
    int minZ = (int)std::floor(aabbMin.z);
    int maxZ = (int)std::floor(aabbMax.z - 1e-6f);
    for (int x = minX; x <= maxX; ++x) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                int block = getBlockAtCollision(x, y, z);
                if (isSolidBlock(block)) return true;
            }
        }
    }
    return false;
}

// Корректировка позиции по одной оси (исправлено)
bool adjustAxis(glm::vec3& pos, float delta, int axis, const glm::vec3& aabbMin, const glm::vec3& aabbMax) {
    if (delta == 0.0f) return false;
    glm::vec3 newMin = aabbMin;
    glm::vec3 newMax = aabbMax;
    const float epsilon = 1e-5f;
    if (axis == 0) { // X
        newMin.x += delta;
        newMax.x += delta;
        if (!checkCollision(newMin, newMax)) {
            pos.x += delta;
            return false;
        } else {
            if (delta > 0) {
                pos.x = std::floor(aabbMax.x + delta - epsilon) - playerWidth/2.0f;
            } else {
                pos.x = std::ceil(aabbMin.x + delta + epsilon) + playerWidth/2.0f;
            }
            return true;
        }
    } else if (axis == 1) { // Y
        newMin.y += delta;
        newMax.y += delta;
        if (!checkCollision(newMin, newMax)) {
            pos.y += delta;
            return false;
        } else {
            if (delta > 0) {
                pos.y = std::floor(aabbMax.y + delta - epsilon) - playerHeight;
            } else {
                pos.y = std::ceil(aabbMin.y + delta + epsilon);
            }
            return true;
        }
    } else if (axis == 2) { // Z
        newMin.z += delta;
        newMax.z += delta;
        if (!checkCollision(newMin, newMax)) {
            pos.z += delta;
            return false;
        } else {
            if (delta > 0) {
                pos.z = std::floor(aabbMax.z + delta - epsilon) - playerWidth/2.0f;
            } else {
                pos.z = std::ceil(aabbMin.z + delta + epsilon) + playerWidth/2.0f;
            }
            return true;
        }
    }
    return false;
}

void updatePlayer(float dt) {
    if (dt > 0.05f) dt = 0.05f;

    // Спринт
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        currentSpeed = sprintSpeed;
    else
        currentSpeed = walkSpeed;

    // Горизонтальное направление
    glm::vec3 moveDir(0.0f);
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_W) == GLFW_PRESS) moveDir += cameraFront;
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_S) == GLFW_PRESS) moveDir -= cameraFront;
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_A) == GLFW_PRESS) moveDir -= glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_D) == GLFW_PRESS) moveDir += glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glm::length(moveDir) > 0.1f) moveDir = glm::normalize(moveDir);
    moveDir.y = 0.0f;

    glm::vec3 desiredMove = moveDir * currentSpeed * dt;

    // Вертикальное движение (гравитация и прыжок)
    velocityY -= gravity * dt;
    float dy = velocityY * dt;

    // Сначала горизонтальное перемещение (обычно так делают, чтобы избежать "скольжения" по стенам)
    glm::vec3 aabbMin, aabbMax;
    getPlayerAABB(playerPos, aabbMin, aabbMax);
    
    if (desiredMove.x != 0.0f) {
        adjustAxis(playerPos, desiredMove.x, 0, aabbMin, aabbMax);
        // После изменения X нужно обновить AABB для следующей оси
        getPlayerAABB(playerPos, aabbMin, aabbMax);
    }
    if (desiredMove.z != 0.0f) {
        adjustAxis(playerPos, desiredMove.z, 2, aabbMin, aabbMax);
        getPlayerAABB(playerPos, aabbMin, aabbMax);
    }
    
    // Вертикальное перемещение
    adjustAxis(playerPos, dy, 1, aabbMin, aabbMax);

    // Проверка, стоит ли на земле (с небольшим допуском вверх)
    getPlayerAABB(playerPos, aabbMin, aabbMax);
    glm::vec3 footCheckMin = aabbMin;
    glm::vec3 footCheckMax = aabbMax;
    footCheckMin.y -= 0.1f;     // небольшой запас, чтобы избежать "залипания"
    footCheckMax.y -= 0.1f;
    bool wasOnGround = onGround;
    onGround = checkCollision(footCheckMin, footCheckMax);
    if (onGround && velocityY < 0.0f) {
        velocityY = 0.0f;
        // Корректируем позицию, чтобы ноги ровно стояли на блоке
        getPlayerAABB(playerPos, aabbMin, aabbMax);
        // Найти высоту блока под ногами
        int blockY = (int)std::floor(aabbMin.y - 0.05f);
        if (blockY >= 0) {
            float newY = (float)blockY + 1.0f; // верхняя граница блока
            if (playerPos.y + playerHeight > newY + 0.01f) {
                playerPos.y = newY;
            }
        }
    }

    // Прыжок
    if (onGround && glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_SPACE) == GLFW_PRESS && !jumpRequested) {
        velocityY = jumpSpeed;
        onGround = false;
        jumpRequested = true;
    }
    if (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_SPACE) == GLFW_RELEASE) {
        jumpRequested = false;
    }

    // Небольшая эпсилон-коррекция для устранения застреваний
    getPlayerAABB(playerPos, aabbMin, aabbMax);
    if (checkCollision(aabbMin, aabbMax)) {
        for (float up = 0.01f; up <= 0.2f; up += 0.01f) {
            playerPos.y += up;
            getPlayerAABB(playerPos, aabbMin, aabbMax);
            if (!checkCollision(aabbMin, aabbMax)) break;
        }
    }

    // Установка камеры: строго по центру X/Z и на высоте eyeHeight от ног
    cameraPos = playerPos + glm::vec3(0.0f, eyeHeight, 0.0f);
}

// Сброс игрока в начальную точку
void resetPlayer() {
    float temp, humid, waterLevel;
    float groundY = getHeightAt(0, 0, temp, humid, waterLevel);
    if (groundY < waterLevel + 1.0f) groundY = waterLevel + 1.0f;
    playerPos = glm::vec3(0.0f, groundY + 1.0f, 0.0f);
    velocityY = 0.0f;
    onGround = true;
    cameraPos = playerPos + glm::vec3(0.0f, eyeHeight, 0.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
}

// Обработка мыши и клавиатуры
bool gameStarted = false;
bool loadingInProgress = false;
double mouseX = 0, mouseY = 0;

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if ((!gameStarted && !loadingInProgress) && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
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
    for (int i=1; i<=9; ++i) {
        if (glfwGetKey(window, GLFW_KEY_0 + i) == GLFW_PRESS) {
            if (blockTypes.find(i) != blockTypes.end()) currentBlockType = i;
        }
    }
}

// Шейдеры
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

// main
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

    // Шум
    initWorldNoise();

    // Музыка
    scanMusicFolder();
    gameStarted = false;
    loadingInProgress = false;

    while (!glfwWindowShouldClose(window)) {
        float now = glfwGetTime();
        deltaTime = now - lastFrame;
        lastFrame = now;

        if (!gameStarted) {
            glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
            if (menuBackgroundTexture) drawRectangle(0,0,screenW,screenH,menuBackgroundTexture,screenW,screenH);
            for (int i=0;i<3;++i) if(menuButtonTexture) drawRectangle(buttons[i].absX,buttons[i].absY,buttons[i].absW,buttons[i].absH,menuButtonTexture,screenW,screenH);
            
            if (loadingInProgress) {
                ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(screenW/2 - 100, screenH/2 + 80));
                ImGui::SetNextWindowBgAlpha(0.5f);
                ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
                ImGui::Text("Loading world... Please wait.");
                ImGui::End();
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }
            
            glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
            
            if (buttons[0].clicked) {
                buttons[0].clicked=false;
                loadingInProgress = true;
                resetPlayer();
                if (!workerThread.joinable()) {
                    workerRunning = true;
                    workerThread = std::thread(workerFunction);
                }
                updateChunksAroundCamera(cameraPos);
            }
            if (buttons[1].clicked) {
                buttons[1].clicked=false;
                loadingInProgress = true;
                resetPlayer();
                if (!workerThread.joinable()) {
                    workerRunning = true;
                    workerThread = std::thread(workerFunction);
                }
                updateChunksAroundCamera(cameraPos);
            }
            if (buttons[2].clicked) {
                buttons[2].clicked=false;
                break;
            }
            
            if (loadingInProgress) {
                int centerCX = (int)std::floor(playerPos.x / CHUNK_SIZE_X);
                int centerCZ = (int)std::floor(playerPos.z / CHUNK_SIZE_Z);
                bool allReady = true;
                for (int dx = -2; dx <= 2; ++dx) {
                    for (int dz = -2; dz <= 2; ++dz) {
                        glm::ivec2 cp(centerCX + dx, centerCZ + dz);
                        auto it = loadedChunks.find(cp);
                        if (it == loadedChunks.end() || !it->second.data) {
                            allReady = false;
                            break;
                        }
                    }
                    if (!allReady) break;
                }
                if (allReady) {
                    gameStarted = true;
                    loadingInProgress = false;
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    initReticle();
                    startMusic();
                } else {
                    updateChunksAroundCamera(cameraPos);
                }
            }
        } else {
            processInput(window);
            updatePlayer(deltaTime);
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
            ImGui::Text("Pos: (%.1f, %.1f, %.1f)", playerPos.x, playerPos.y, playerPos.z);
            ImGui::Text("Block: %s", blockTypes[currentBlockType].name.c_str());
            ImGui::Text("On ground: %s", onGround ? "Yes" : "No");
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
                {
                    std::lock_guard<std::mutex> lock(chunkMutex);
                    pendingData.clear();
                    pendingLoad.clear();
                    pendingGen.clear();
                }
                glfwGetWindowSize(window, &screenW, &screenH);
                updateButtonPositions(screenW, screenH);
                stopMusic();
                loadingInProgress = false;
                workerRunning = true;
            }
            ImGui::End(); ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    if (gameStarted) {
        saveAllChunks();
    }
    workerRunning = false;
    if (workerThread.joinable()) workerThread.join();
    for (auto& pair : loadedChunks) {
        for (auto& p : pair.second.vaoPerType) {
            glDeleteVertexArrays(1, &p.second);
            glDeleteBuffers(1, &pair.second.vboPerType[p.first]);
        }
    }
    stopMusic();
    if (menuBackgroundTexture) glDeleteTextures(1, &menuBackgroundTexture);
    if (menuButtonTexture) glDeleteTextures(1, &menuButtonTexture);
    glDeleteVertexArrays(1, &uiVAO); glDeleteBuffers(1, &uiVBO); glDeleteBuffers(1, &uiEBO); glDeleteProgram(uiShaderProgram);
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    for (auto& p : blockTypes) glDeleteTextures(1, &p.second.textureID);
    glDeleteProgram(shaderProgram);
    glDeleteVertexArrays(1, &reticleVAO); glDeleteProgram(reticleProgram);
    glfwTerminate();
    return 0;
}