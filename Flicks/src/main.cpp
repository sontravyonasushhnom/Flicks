#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <tchar.h>
#include <cmath>
#include <random>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <vector>
#include <map>
#include <fstream>
#include <cstring>
#include <filesystem>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "imgui.h"
#include "implot.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <xaudio2.h>

#include "renderer.h"
#include "audio_xa.h"
#include "ImguiTheme.h"

using Microsoft::WRL::ComPtr;

// Global variables
HWND g_hWnd = NULL;
int g_WindowWidth = 800;
int g_WindowHeight = 600;
Renderer g_renderer;
Renderer::FieldCache g_fieldCache;
// Mouse global variables
static bool g_active = true;
static double g_cursorPosX = 0;
static double g_cursorPosY = 0;
static int g_accumulatedMouseDeltaX = 0;
static int g_accumulatedMouseDeltaY = 0;
static bool g_leftButtonPressedRaw = false;
static bool g_leftButtonPressed = false;
static UINT g_mouseSpeed = 10;
float g_mouseSpeedMultiplier = 1.0f;
static bool g_mouseCaptured = false;

struct GameSettings {
    ImVec4 bgColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    ImVec4 fieldColor = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
    ImVec4 circleColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    ImVec4 cursorColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    float cursorThickness = 0.0f;
    ImVec4 cursorOutlineColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    bool useCustomCursor = true;

    float scale = 0.9f;
    float circleRadiusNorm = 0.112f;
    float cursorRadiusNorm = 0.015f;

    int circleLifetimeMs = 250;
    int gameTimeSec = 60;
    float distanceRatio = 0.2f;

    int minSpawnDelayMs = 0;
    int maxSpawnDelayMs = 0;

    bool endBySpawnCount = false;
    int maxSpawnCount = 0;

    UINT frameLatency = 1;
} settings;

struct GameResult {
    GameSettings settings;
    int hits = 0;
    int attempts = 0;
    float accuracy = 0.0f;
    float avgReactionTime = 0.0f;
    float score = 0.0f;
    std::vector<int> scoreHistory;
    std::vector<int> reactionTimes;
};

struct GameSummary {
    float circleRadiusNorm;
    float cursorRadiusNorm;
    int circleLifetimeMs;
    int gameTimeSec;
    bool endBySpawnCount;
    int maxSpawnCount;
    int minSpawnDelayMs;
    int maxSpawnDelayMs;
    int hits;
    float avgReactionTime;
    float score;
    std::time_t timestamp;
};

// Global vectors for summaries
std::vector<GameSummary> g_allGameSummaries;
std::vector<GameSummary> g_currentSettingSummaries;
static std::vector<int> g_reactionTimes;
static int g_lastReactionTime = 0;
static std::vector<double> reaction_xs, reaction_ys;
// Global variables to store initial settings and last game results
GameSettings gameStartSettings;
GameResult lastGameResult;

// Spawn delay management
bool isCircleActive = false;
bool isLastCircle = false;
long long nextSpawnTimeMs = 0;

int randomInt(int min, int max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    if (min > max) std::swap(min, max);
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}

// Cfg
void SaveColorSettings() {
    FILE* f;
    if (fopen_s(&f, "res/cfg.ini", "w") == 0) {
        fprintf(f, "bgColor=%.3f,%.3f,%.3f,%.3f\n",
            settings.bgColor.x, settings.bgColor.y, settings.bgColor.z, settings.bgColor.w);
        fprintf(f, "fieldColor=%.3f,%.3f,%.3f,%.3f\n",
            settings.fieldColor.x, settings.fieldColor.y, settings.fieldColor.z, settings.fieldColor.w);
        fprintf(f, "circleColor=%.3f,%.3f,%.3f,%.3f\n",
            settings.circleColor.x, settings.circleColor.y, settings.circleColor.z, settings.circleColor.w);
        fprintf(f, "cursorColor=%.3f,%.3f,%.3f,%.3f\n",
            settings.cursorColor.x, settings.cursorColor.y, settings.cursorColor.z, settings.cursorColor.w);
        fprintf(f, "cursorThickness=%.3f\n", settings.cursorThickness);
        fprintf(f, "cursorOutlineColor=%.3f,%.3f,%.3f,%.3f\n",
            settings.cursorOutlineColor.x, settings.cursorOutlineColor.y,
            settings.cursorOutlineColor.z, settings.cursorOutlineColor.w);
        fprintf(f, "useCustomCursor=%d\n", settings.useCustomCursor ? 1 : 0);
        fprintf(f, "scale=%.3f\n", settings.scale);
        fprintf(f, "circleRadiusNorm=%.3f\n", settings.circleRadiusNorm);
        fprintf(f, "cursorRadiusNorm=%.3f\n", settings.cursorRadiusNorm);
        fprintf(f, "circleLifetimeMs=%d\n", settings.circleLifetimeMs);
        fprintf(f, "gameTimeSec=%d\n", settings.gameTimeSec);
        fprintf(f, "minSpawnDelayMs=%d\n", settings.minSpawnDelayMs);
        fprintf(f, "maxSpawnDelayMs=%d\n", settings.maxSpawnDelayMs);
        fprintf(f, "endBySpawnCount=%d\n", settings.endBySpawnCount ? 1 : 0);
        fprintf(f, "maxSpawnCount=%d\n", settings.maxSpawnCount);

        fprintf(f, "frameLatency=%d\n", settings.frameLatency);

        fclose(f);
    }
}

void LoadColorSettings() {
    FILE* f;
    if (fopen_s(&f, "res/cfg.ini", "r") == 0) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            float r, g, b, a;
            float thickness;
            float scaleValue;
            int useCustom;
            int intVal;
            if (sscanf_s(line, "bgColor=%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                settings.bgColor = ImVec4(r, g, b, a);
            }
            else if (sscanf_s(line, "fieldColor=%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                settings.fieldColor = ImVec4(r, g, b, a);
            }
            else if (sscanf_s(line, "circleColor=%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                settings.circleColor = ImVec4(r, g, b, a);
            }
            else if (sscanf_s(line, "cursorColor=%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                settings.cursorColor = ImVec4(r, g, b, a);
            }
            if (sscanf_s(line, "cursorThickness=%f", &thickness) == 1) {
                settings.cursorThickness = thickness;
            }
            else if (sscanf_s(line, "cursorOutlineColor=%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                settings.cursorOutlineColor = ImVec4(r, g, b, a);
            }
            else if (sscanf_s(line, "useCustomCursor=%d", &useCustom) == 1) {
                settings.useCustomCursor = (useCustom != 0);
            }
            else if (sscanf_s(line, "scale=%f", &scaleValue) == 1) {
                settings.scale = scaleValue;
            }
            else if (sscanf_s(line, "circleRadiusNorm=%f", &scaleValue) == 1) {
                settings.circleRadiusNorm = scaleValue;
            }
            else if (sscanf_s(line, "cursorRadiusNorm=%f", &scaleValue) == 1) {
                settings.cursorRadiusNorm = scaleValue;
            }
            else if (sscanf_s(line, "circleLifetimeMs=%d", &intVal) == 1) {
                settings.circleLifetimeMs = intVal;
            }
            else if (sscanf_s(line, "gameTimeSec=%d", &intVal) == 1) {
                settings.gameTimeSec = intVal;
            }
            else if (sscanf_s(line, "minSpawnDelayMs=%d", &intVal) == 1) {
                settings.minSpawnDelayMs = intVal;
            }
            else if (sscanf_s(line, "maxSpawnDelayMs=%d", &intVal) == 1) {
                settings.maxSpawnDelayMs = intVal;
            }
            else if (sscanf_s(line, "endBySpawnCount=%d", &intVal) == 1) {
                settings.endBySpawnCount = (intVal != 0);
            }
            else if (sscanf_s(line, "maxSpawnCount=%d", &intVal) == 1) {
                settings.maxSpawnCount = intVal;
            }

            else if (sscanf_s(line, "frameLatency=%d", &intVal) == 1) {
                settings.frameLatency = intVal;
            }
        }
        fclose(f);
    }
}

// game_summaries
void SaveGameSummaries() {
    FILE* f;
    if (fopen_s(&f, "res/game_summaries.csv", "w") == 0) {
        fprintf(f,
            "circleRadiusNorm,"
            "cursorRadiusNorm,"
            "circleLifetimeMs,"
            "gameTimeSec,"
            "minSpawnDelayMs,"
            "maxSpawnDelayMs,"
            "endBySpawnCount,"
            "maxSpawnCount,"
            "hits,"
            "avgReactionTime,"
            "score,"
            "timestamp\n"
        );

        for (const auto& s : g_allGameSummaries) {
            fprintf(f,
                "%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%.1f,%.1f,%lld\n",
                s.circleRadiusNorm,
                s.cursorRadiusNorm,
                s.circleLifetimeMs,
                s.gameTimeSec,
                s.minSpawnDelayMs,
                s.maxSpawnDelayMs,
                s.endBySpawnCount ? 1 : 0,
                s.maxSpawnCount,
                s.hits,
                s.avgReactionTime,
                s.score,
                static_cast<long long>(s.timestamp)
            );
        }
        fclose(f);
    }
}

void LoadGameSummaries() {
    FILE* f;
    if (fopen_s(&f, "res/game_summaries.csv", "r") == 0) {
        char line[512];
        fgets(line, sizeof(line), f);
        while (fgets(line, sizeof(line), f)) {
            GameSummary s;
            long long ts = 0;
            int tempEnd = 0;
            int tempMax = 0;
            float avgRT = 0.0f;
            int count = sscanf_s(
                line,
                "%f,%f,%d,%d,%d,%d,%d,%d,%d,%f,%f,%lld",
                &s.circleRadiusNorm,
                &s.cursorRadiusNorm,
                &s.circleLifetimeMs,
                &s.gameTimeSec,
                &s.minSpawnDelayMs,
                &s.maxSpawnDelayMs,
                &tempEnd,
                &tempMax,
                &s.hits,
                &avgRT,
                &s.score,
                &ts
            );
            if (count == 12) {
                s.endBySpawnCount = (tempEnd != 0);
                s.maxSpawnCount = tempMax;
                s.avgReactionTime = avgRT;
                s.timestamp = static_cast<std::time_t>(ts);
                g_allGameSummaries.push_back(s);
            }
        }
        fclose(f);
    }
}

enum GameState {
    GAME_NOT_STARTED,
    GAME_RUNNING,
    GAME_FINISHED
};
GameState gameState = GAME_NOT_STARTED;

// Game variables
long long gameStartTimeMs = 0;
long long circleSpawnTimeMs = 0;
ImVec2 circlePos = ImVec2(0, 0);
int hits = 0;
int attempts = 0;
int spawnCount = 0;
bool showSettings = false;
bool showResults = false;

static ImVec2 lastCirclePos(0, 0);
static bool firstCircle = true;
static std::random_device rd;
static std::mt19937 gen(rd());

static std::vector<int> g_scoreHistory;
static int g_lastSampleSecond = 0;

void ResetGame();
void StartGame();
void SpawnCircle(int screenW, int screenH);
void ShowResultsWindow();
void ShowSettingsWindow();

// Cursor update
void UpdateCursor() {
    if (showSettings || showResults) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
    else if (!settings.useCustomCursor) {
        SetCursor(LoadCursor(NULL, IDC_ARROW));
    }
    else {
        SetCursor(NULL);
    }
}

void ForceCursorUpdate() {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hWnd, &pt);
    SendMessage(g_hWnd, WM_SETCURSOR, (WPARAM)g_hWnd, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    UpdateCursor();
}

static void SyncSystemCursorToInternal(HWND hWnd) {
    if (!hWnd) return;
    POINT ptClient;
    ptClient.x = static_cast<int>(std::round(g_cursorPosX));
    ptClient.y = static_cast<int>(std::round(g_cursorPosY));
    // Clip to client rect just in case
    RECT rc;
    GetClientRect(hWnd, &rc);
    ptClient.x = std::clamp(ptClient.x, rc.left, rc.right - 1);
    ptClient.y = std::clamp(ptClient.y, rc.top, rc.bottom - 1);

    POINT ptScreen = ptClient;
    ClientToScreen(hWnd, &ptScreen);
    SetCursorPos(ptScreen.x, ptScreen.y);
}

static void SyncInternalToSystemCursor(HWND hWnd) {
    if (!hWnd) return;
    POINT ptScreen;
    GetCursorPos(&ptScreen);
    POINT ptClient = ptScreen;
    ScreenToClient(hWnd, &ptClient);
    g_cursorPosX = static_cast<double>(ptClient.x);
    g_cursorPosY = static_cast<double>(ptClient.y);

    // Clamp to client area
    RECT rc;
    GetClientRect(hWnd, &rc);
    g_cursorPosX = std::clamp(g_cursorPosX, static_cast<double>(rc.left), static_cast<double>(rc.right - 1));
    g_cursorPosY = std::clamp(g_cursorPosY, static_cast<double>(rc.top), static_cast<double>(rc.bottom - 1));
}

void CaptureMouseInWindow(HWND hWnd, bool wantCenter = false) {
    if (!hWnd) return;
    if (g_mouseCaptured) return;

    RECT rc;
    GetClientRect(hWnd, &rc);
    POINT tl = { rc.left, rc.top }, br = { rc.right, rc.bottom };
    ClientToScreen(hWnd, &tl);
    ClientToScreen(hWnd, &br);
    RECT clip = { tl.x, tl.y, br.x, br.y };
    ClipCursor(&clip);
    g_mouseCaptured = true;

    if (wantCenter) {
        POINT centerClient = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        POINT centerScreen = centerClient;
        ClientToScreen(hWnd, &centerScreen);
        SetCursorPos(centerScreen.x, centerScreen.y);
        g_cursorPosX = static_cast<double>(centerClient.x);
        g_cursorPosY = static_cast<double>(centerClient.y);
    }
    else {
        SyncInternalToSystemCursor(hWnd);
    }

    UpdateCursor();
}

void ReleaseMouseFromWindow(HWND hWnd) {
    if (!g_mouseCaptured) return;
    SyncSystemCursorToInternal(hWnd);

    ClipCursor(NULL);
    g_mouseCaptured = false;

    UpdateCursor();
}

float randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

float distance(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

DEVMODE getCurrentDisplayMode() {
    DEVMODE dm = {};
    dm.dmSize = sizeof(dm);
    EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm);
    return dm;
}

void UpdateFieldCache() {
    g_renderer.UpdateFieldCache(
        g_fieldCache,
        settings.scale,
        settings.circleRadiusNorm,
        settings.cursorRadiusNorm
    );
}

void ResetGame() {
    hits = 0;
    attempts = 0;
    spawnCount = 0;
    gameState = GAME_NOT_STARTED;
    showResults = false;
    firstCircle = true;
    lastCirclePos = ImVec2(0, 0);
    isCircleActive = false;
    isLastCircle = false;
    nextSpawnTimeMs = 0;

    g_scoreHistory.clear();
    g_scoreHistory.reserve(settings.gameTimeSec + 1);

    g_reactionTimes.clear();
    g_reactionTimes.reserve(settings.endBySpawnCount ?
        settings.maxSpawnCount :
        settings.gameTimeSec * 10);
    g_lastReactionTime = 0;
}

void StartGame() {
    gameState = GAME_RUNNING;
    gameStartTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    g_scoreHistory.clear();
    g_scoreHistory.push_back(0);
    g_lastSampleSecond = 0;
    spawnCount = 0;
    isLastCircle = false;

    // Capture settings at game start
    gameStartSettings = settings;

    // Initialize spawn delay variables
    isCircleActive = false;

    // Установка задержки перед первым спауном
    int delay = randomInt(settings.minSpawnDelayMs, settings.maxSpawnDelayMs);
    nextSpawnTimeMs = gameStartTimeMs + delay;

    showResults = false;
}

void SpawnCircle(int screenW, int screenH) {
    float halfField = g_fieldCache.halfField;
    ImVec2 center = g_fieldCache.center;
    float R = g_fieldCache.circleRadiusPx;
    float a = g_fieldCache.spawnMaxRadius;
    float minDistance = std::max(0.0f, (g_fieldCache.fieldSize - 2.0f * R) * settings.distanceRatio);

    if (a <= 0.0f) {
        circlePos = center;
        circleSpawnTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        return;
    }

    float x, y;
    int attemptsLocal = 0;
    const int maxAttempts = 50;

    do {
        float t = randomFloat(0.0f, 2.0f * 3.1415926535f);
        float r = std::sqrt(randomFloat(0.0f, 1.0f));
        x = center.x + a * r * std::cos(t);
        y = center.y + a * r * std::sin(t);
        attemptsLocal++;
    } while (!firstCircle &&
        attemptsLocal < maxAttempts &&
        distance(x, y, lastCirclePos.x, lastCirclePos.y) < minDistance);

    if (attemptsLocal >= maxAttempts) {
        // Fallback to center if placement fails
        x = center.x;
        y = center.y;
    }

    circlePos = ImVec2(x, y);
    circleSpawnTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    lastCirclePos = circlePos;
    firstCircle = false;
}

void ShowResultsWindow() {
    ImGui::SetNextWindowSize(ImVec2(800, 800), ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(g_WindowWidth * 0.5f, g_WindowHeight * 0.5f),
        ImGuiCond_Always, ImVec2(0.5f, 0.5f)
    );
    ImGui::Begin("Results", &showResults,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);

    // Static Buffers for Hits Over Time
    static bool plotBuffersInitialized = false;
    static std::vector<float> timeValues;
    static std::vector<float> hitsValues;
    if (!plotBuffersInitialized) {
        timeValues.reserve(settings.gameTimeSec + 1);
        hitsValues.reserve(settings.gameTimeSec + 1);
        plotBuffersInitialized = true;
    }

    // Prepare data for current game
    timeValues.clear();
    hitsValues.clear();
    for (int i = 0, n = static_cast<int>(lastGameResult.scoreHistory.size()); i < n; ++i) {
        timeValues.push_back(static_cast<float>(i));
        hitsValues.push_back(static_cast<float>(lastGameResult.scoreHistory[i]));
    }

    int min_val = 0;
    int max_val = 0;
    float avgReaction = 0.0f;

    // Filter previous games to fit current settings
    g_currentSettingSummaries.clear();
    const float EPS = 0.001f;

    for (const auto& s : g_allGameSummaries) {
        if (std::abs(s.circleRadiusNorm - lastGameResult.settings.circleRadiusNorm) < EPS
            && std::abs(s.cursorRadiusNorm - lastGameResult.settings.cursorRadiusNorm) < EPS
            && s.circleLifetimeMs == lastGameResult.settings.circleLifetimeMs
            && s.gameTimeSec == lastGameResult.settings.gameTimeSec
            && s.minSpawnDelayMs == lastGameResult.settings.minSpawnDelayMs
            && s.maxSpawnDelayMs == lastGameResult.settings.maxSpawnDelayMs
            && s.endBySpawnCount == lastGameResult.settings.endBySpawnCount
            && (!s.endBySpawnCount || s.maxSpawnCount == lastGameResult.settings.maxSpawnCount)
            ) {
            g_currentSettingSummaries.push_back(s);
        }
    }

    if (ImGui::BeginTabBar("##ResultsTabs")) {
        if (ImGui::BeginTabItem("Current Game")) {
            const int scoreCount = static_cast<int>(timeValues.size());
            if (scoreCount > 0) {
                ImGui::Text("Hits Over Time:");
                int maxHits = *std::max_element(
                    lastGameResult.scoreHistory.begin(),
                    lastGameResult.scoreHistory.end()
                );

                if (ImPlot::BeginPlot("##HitsOverTime", ImVec2(-1, 200))) {
                    ImPlot::SetupAxes("Elapsed Time (sec)", "Hits");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 0, scoreCount - 1, ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, 0, maxHits + 1, ImPlotCond_Always);
                    ImPlot::SetNextLineStyle(ImVec4(0.55f, 1.0f, 0.55f, 1.0f), 2.0f);
                    ImPlot::PlotLine("line", timeValues.data(), hitsValues.data(), scoreCount);
                    ImPlot::EndPlot();
                }
            }

            if (!lastGameResult.reactionTimes.empty()) {
                reaction_xs.clear();
                reaction_ys.clear();

                reaction_xs.reserve(lastGameResult.reactionTimes.size());
                reaction_ys.reserve(lastGameResult.reactionTimes.size());

                for (size_t i = 0; i < lastGameResult.reactionTimes.size(); ++i) {
                    reaction_xs.push_back(static_cast<double>(i + 1));
                    reaction_ys.push_back(static_cast<double>(lastGameResult.reactionTimes[i]));
                }

                const double x_max = static_cast<double>(reaction_xs.size());

                // Calculate dynamic limits for Y
                min_val = lastGameResult.reactionTimes[0];
                max_val = lastGameResult.reactionTimes[0];
                long long sum = lastGameResult.reactionTimes[0];

                for (size_t i = 1; i < lastGameResult.reactionTimes.size(); ++i) {
                    const int rt = lastGameResult.reactionTimes[i];
                    if (rt < min_val) min_val = rt;
                    if (rt > max_val) max_val = rt;
                    sum += rt;
                }

                // rt avg 
                avgReaction = lastGameResult.avgReactionTime;

                // Calculating limits for a graph
                double padding = std::max(5.0, (max_val - min_val) * 0.1);
                double y_min = std::max(0.0, min_val - padding);
                double y_max = max_val + padding;
                if (min_val == max_val) {
                    y_min = std::max(0.0, min_val - 10.0);
                    y_max = max_val + 10.0;
                }
                ImGui::Text("Reaction Times (ms):");
                if (ImPlot::BeginPlot("##ReactionTimes", ImVec2(-1, 200))) {
                    ImPlot::SetupAxes("Circle #", "Reaction Time (ms)");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 1.0, x_max, ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);

                    ImPlot::SetNextLineStyle(ImVec4(0.55f, 0.0f, 0.95f, 1.0f), 2.0f);
                    ImPlot::PlotLine("##ReactionLine",
                        reaction_xs.data(),
                        reaction_ys.data(),
                        static_cast<int>(reaction_xs.size()));
                    ImPlot::EndPlot();
                }
            }

            // Stats
            ImGui::Separator();
            ImGui::Text("Score: %.1f", lastGameResult.score);
            ImGui::Text("Hits: %d", lastGameResult.hits);
            ImGui::Text("Accuracy: %.2f%%", lastGameResult.accuracy);
            ImGui::Separator();
            if (!lastGameResult.reactionTimes.empty()) {
                ImGui::Text("Min reaction time: %d ms", min_val);
                ImGui::Text("Max reaction time: %d ms", max_val);
                ImGui::Text("Avg reaction time: %.1f ms", avgReaction);
            }
            else {
                ImGui::Text("No reaction data available");
            }

            // Settigs
            ImGui::Separator();
            ImGui::Text("Circle size: %.0f", lastGameResult.settings.circleRadiusNorm * 1000.0f);
            ImGui::Text("Circle lifetime: %d ms", lastGameResult.settings.circleLifetimeMs);
            ImGui::Text("Min spawn delay: %d ms", lastGameResult.settings.minSpawnDelayMs);
            ImGui::Text("Max spawn delay: %d ms", lastGameResult.settings.maxSpawnDelayMs);
            if (lastGameResult.settings.endBySpawnCount) {
                ImGui::Text("Max spawns: %d", lastGameResult.settings.maxSpawnCount);
            }
            else {
                ImGui::Text("Game time: %d sec", lastGameResult.settings.gameTimeSec);
            }
            ImGui::Text("Cursor hitbox size: %.1f%%",
                lastGameResult.settings.cursorRadiusNorm * 100.0f);

            ImGui::Spacing(); ImGui::Separator();
            ImGui::Text("'R' to restart");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Statistics")) {
            const int gameCount = static_cast<int>(g_currentSettingSummaries.size());
            if (gameCount > 0) {
                float maxScore = 0.0f;
                float minScore = FLT_MAX;
                float totalScore = 0.0f;

                for (const auto& sum : g_currentSettingSummaries) {
                    maxScore = std::max(maxScore, sum.score);
                    minScore = std::min(minScore, sum.score);
                    totalScore += sum.score;
                }

                float avgScore = totalScore / gameCount;

                // Prepare data
                static std::vector<float> xs, ys;
                static bool statsBuffersInitialized = false;
                if (!statsBuffersInitialized) {
                    xs.reserve(1000);
                    ys.reserve(1000);
                    statsBuffersInitialized = true;
                }
                xs.clear(); ys.clear();
                for (int i = 0; i < gameCount; ++i) {
                    xs.push_back(static_cast<float>(i + 1));
                    ys.push_back(static_cast<float>(g_currentSettingSummaries[i].score));
                }

                float yRange = static_cast<float>(maxScore - minScore);
                float paddingHistory = yRange * 0.1f; 
                if (paddingHistory < 1.0f) paddingHistory = 1.0f; 

                float yMin = static_cast<float>(minScore) - paddingHistory;
                float yMax = static_cast<float>(maxScore) + paddingHistory;
                if (yMin < 0) yMin = 0;

                ImGui::Text("History:");
                if (ImPlot::BeginPlot("##AllScoresLine", ImVec2(-1, 200))) {
                    ImPlot::SetupAxes("Game #", "Score");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 1, gameCount, ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, yMin, yMax, ImPlotCond_Always);
                    ImPlot::SetNextLineStyle(ImVec4(0.55f, 1.0f, 0.55f, 1.0f), 2.0f);
                    ImPlot::PlotLine("line", xs.data(), ys.data(), gameCount);
                    ImPlot::SetNextMarkerStyle(
                        ImPlotMarker_Circle, 3.0f,
                        ImVec4(0.55f, 1.0f, 0.55f, 1.0f),
                        0.0f,
                        ImVec4(1.0f, 0.2f, 0.2f, 1.0f)
                    );
                    ImPlot::PlotScatter("##Points", xs.data(), ys.data(), gameCount);
                    ImPlot::EndPlot();
                }

                static bool rtBuffersInitialized = false;
                static std::vector<float> xs_rt, ys_rt;
                if (!rtBuffersInitialized) {
                    xs_rt.reserve(1000);
                    ys_rt.reserve(1000);
                    rtBuffersInitialized = true;
                }
                xs_rt.clear();
                ys_rt.clear();

                float maxRT = 0.0f;
                float minRT = FLT_MAX;
                float totalRT = 0.0f;
                int rtCount = 0;

                float globalMinRT = FLT_MAX;
                float globalMaxRT = 0.0f;

                for (int i = 0; i < gameCount; ++i) {
                    float rt = g_currentSettingSummaries[i].avgReactionTime;
                    xs_rt.push_back(static_cast<float>(i + 1));
                    ys_rt.push_back(rt);
                    maxRT = std::max(maxRT, rt);
                    minRT = std::min(minRT, rt);

                    if (rt > 0) {
                        totalRT += rt;
                        rtCount++;
                        globalMinRT = std::min(globalMinRT, rt);
                        globalMaxRT = std::max(globalMaxRT, rt);
                    }
                }
                float globalAvgRT = (rtCount > 0) ? (totalRT / rtCount) : 0.0f;
                float paddingRT = (maxRT - minRT) * 0.1f;
                if (paddingRT < 5.0f) paddingRT = 5.0f;

                ImGui::Text("Avg Reaction Time History:");
                if (ImPlot::BeginPlot("##AvgRTLine", ImVec2(-1, 200))) {
                    ImPlot::SetupAxes("Game #", "Avg Reaction (ms)");
                    ImPlot::SetupAxisLimits(ImAxis_X1, 1, gameCount, ImPlotCond_Always);
                    ImPlot::SetupAxisLimits(ImAxis_Y1, minRT - paddingRT, maxRT + paddingRT, ImPlotCond_Always);
                    ImPlot::SetNextLineStyle(ImVec4(0.55f, 0.0f, 0.95f, 1.0f), 2.0f);
                    ImPlot::PlotLine("line", xs_rt.data(), ys_rt.data(), gameCount);
                    ImPlot::SetNextMarkerStyle(
                        ImPlotMarker_Circle, 3.0f,
                        ImVec4(0.55f, 0.0f, 0.95f, 0.8f),
                        0.0f,
                        ImVec4(1.0f, 1.0f, 1.0f, 0.8f));
                    ImPlot::PlotScatter("##scatter", xs_rt.data(), ys_rt.data(), gameCount);

                    ImPlot::EndPlot();
                }

                ImGui::Separator();
                if (gameStartSettings.endBySpawnCount) {
                    ImGui::Text("Min score: %.1f", minScore);
                }
                else {
                    ImGui::Text("Max score: %.1f", maxScore);
                }
                ImGui::Text("Avg score: %.1f", avgScore);
                ImGui::Text("Played: %d", gameCount);
                ImGui::Separator();
                if (rtCount > 0) {
                    ImGui::Text("Min reaction time: %.1f ms", globalMinRT);
                    ImGui::Text("Max reaction time: %.1f ms", globalMaxRT);
                    ImGui::Text("Avg reaction time: %.1f ms", globalAvgRT);
                }
                else {
                    ImGui::Text("  No reaction data available");
                }

                ImGui::Separator();
                ImGui::Text("Circle size: %.0f", lastGameResult.settings.circleRadiusNorm * 1000.0f);
                ImGui::Text("Circle lifetime: %d ms", lastGameResult.settings.circleLifetimeMs);
                ImGui::Text("Min spawn delay: %d ms", lastGameResult.settings.minSpawnDelayMs);
                ImGui::Text("Max spawn delay: %d ms", lastGameResult.settings.maxSpawnDelayMs);
                if (lastGameResult.settings.endBySpawnCount) {
                    ImGui::Text("Max spawns: %d", lastGameResult.settings.maxSpawnCount);
                }
                else {
                    ImGui::Text("Game time: %d sec", lastGameResult.settings.gameTimeSec);
                }
                ImGui::Text("Cursor hitbox size: %.1f%%",
                    lastGameResult.settings.cursorRadiusNorm * 100.0f);

                ImGui::Spacing(); ImGui::Separator();
                ImGui::Text("'R' to restart");
            }
            else {
                ImGui::Text("No previous games with these settings");
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void ShowSettingsWindow() {
    static bool firstTime = true;
    if (firstTime) {
        ImGui::SetNextWindowPos(ImVec2(20, g_WindowHeight * 0.1f), ImGuiCond_FirstUseEver);
        firstTime = false;
    }
    ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Game Settings", &showSettings, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::CollapsingHeader("Colors", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Background", (float*)&settings.bgColor);
        ImGui::ColorEdit3("Game Field", (float*)&settings.fieldColor);
        ImGui::ColorEdit3("Circle", (float*)&settings.circleColor);
    }

    if (ImGui::CollapsingHeader("Sizes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Scale:");
        if (ImGui::DragFloat("##scale", &settings.scale, 0.01f, 0.1f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
            UpdateFieldCache();

        ImGui::Text("Circle size:");
        if (gameState == GAME_RUNNING) ImGui::BeginDisabled();
        float circlePercent = settings.circleRadiusNorm * 1000.0f;
        if (ImGui::DragFloat("##radius", &circlePercent, 0.1f, 1.0f, 1000.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            settings.circleRadiusNorm = circlePercent / 1000.0f;
            UpdateFieldCache();
        }
        if (gameState == GAME_RUNNING) ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool prevUseCustom = settings.useCustomCursor;
        ImGui::Checkbox("Use custom cursor", &settings.useCustomCursor);
        if (prevUseCustom != settings.useCustomCursor) ForceCursorUpdate();

        if (gameState == GAME_RUNNING) ImGui::BeginDisabled();
        float cursorPercent = settings.cursorRadiusNorm * 100.0f;
        if (ImGui::DragFloat("Cursor hitbox size (%)", &cursorPercent, 0.5f, 0.0f, 100.0f, "%.1f%%", ImGuiSliderFlags_AlwaysClamp)) {
            settings.cursorRadiusNorm = cursorPercent / 100.0f;
            UpdateFieldCache();
        }
        if (gameState == GAME_RUNNING) ImGui::EndDisabled();

        ImGui::ColorEdit3("Cursor color", (float*)&settings.cursorColor);
        ImGui::ColorEdit3("Cursor outline", (float*)&settings.cursorOutlineColor);
        ImGui::DragFloat("Outline thickness", &settings.cursorThickness, 0.1f, 0.0f, 10.0f, "%.1f px");

        static bool ForeGroundBoost = false;
        static bool DisableFiltering = false;
        ImGui::Checkbox("ForegroundBoost", &ForeGroundBoost);
        ImGui::SameLine();
        ImGui::Checkbox("DisableFiltering", &DisableFiltering);
    }

    if (ImGui::CollapsingHeader("Time", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Circle lifetime (ms):");
        if (gameState == GAME_RUNNING) ImGui::BeginDisabled();
        ImGui::DragInt("##lifetime", &settings.circleLifetimeMs, 10.0f, 50, 2000, "%d ms", ImGuiSliderFlags_AlwaysClamp);
        if (gameState == GAME_RUNNING) ImGui::EndDisabled();

        if (ImGui::CollapsingHeader("End Condition", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (gameState == GAME_RUNNING) ImGui::BeginDisabled();
            if (ImGui::RadioButton("By Time", !settings.endBySpawnCount)) {
                settings.endBySpawnCount = false;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("By Spawn Count", settings.endBySpawnCount)) {
                settings.endBySpawnCount = true;
            }
            if (!settings.endBySpawnCount) {
                ImGui::DragInt("Game time (sec)", &settings.gameTimeSec, 1, 5, 300, "%d sec");
            }
            else {
                ImGui::DragInt("Max spawns", &settings.maxSpawnCount, 1, 1, 1000, "%d spawns");
            }
            if (gameState == GAME_RUNNING) ImGui::EndDisabled();
        }

        // Added spawn delay settings
        ImGui::Text("Spawn Delay (ms):");
        if (gameState == GAME_RUNNING) ImGui::BeginDisabled();
        ImGui::DragInt("Min##spawn", &settings.minSpawnDelayMs, 10.0f, 0, 5000, "Min: %d ms", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragInt("Max##spawn", &settings.maxSpawnDelayMs, 10.0f, 0, 5000, "Max: %d ms", ImGuiSliderFlags_AlwaysClamp);
        if (gameState == GAME_RUNNING) ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (gameState == GAME_RUNNING) ImGui::BeginDisabled();

        if (ImGui::Button("BB250ms", buttonSize)) {
            settings.circleRadiusNorm = 0.112f;
            settings.circleLifetimeMs = 250;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = false;
            settings.maxSpawnCount = 10;
            settings.minSpawnDelayMs = 0;
            settings.maxSpawnDelayMs = 0;
            UpdateFieldCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("randBB250ms", buttonSize)) {
            settings.circleRadiusNorm = 0.112f;
            settings.circleLifetimeMs = 250;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = true;
            settings.maxSpawnCount = 60;
            settings.minSpawnDelayMs = 0;
            settings.maxSpawnDelayMs = 1200;
            UpdateFieldCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("RTDA225ms", buttonSize)) {
            settings.circleRadiusNorm = 0.112f;
            settings.circleLifetimeMs = 225;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = true;
            settings.maxSpawnCount = 80;
            settings.minSpawnDelayMs = 200;
            settings.maxSpawnDelayMs = 500;
            UpdateFieldCache();
        }
        if (ImGui::Button("BB230ms", buttonSize)) {
            settings.circleRadiusNorm = 0.120f;
            settings.circleLifetimeMs = 230;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 30;
            settings.endBySpawnCount = false;
            settings.maxSpawnCount = 10;
            settings.minSpawnDelayMs = 0;
            settings.maxSpawnDelayMs = 0;
            UpdateFieldCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("TDA200ms", buttonSize)) {
            settings.circleRadiusNorm = 0.120f;
            settings.circleLifetimeMs = 200;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = false;
            settings.maxSpawnCount = 10;
            settings.minSpawnDelayMs = 500;
            settings.maxSpawnDelayMs = 500;
            UpdateFieldCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("PRflick", buttonSize)) {
            settings.circleRadiusNorm = 0.125f;
            settings.circleLifetimeMs = 2000;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = true;
            settings.maxSpawnCount = 10;
            settings.minSpawnDelayMs = 500;
            settings.maxSpawnDelayMs = 3000;
            UpdateFieldCache();
        }
        if (ImGui::Button("SB270ms", buttonSize)) {
            settings.circleRadiusNorm = 0.065f;
            settings.circleLifetimeMs = 270;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = false;
            settings.maxSpawnCount = 10;
            settings.minSpawnDelayMs = 0;
            settings.maxSpawnDelayMs = 0;
            UpdateFieldCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("1w1t500ms", buttonSize)) {
            settings.circleRadiusNorm = 0.035f;
            settings.circleLifetimeMs = 500;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = false;
            settings.maxSpawnCount = 10;
            settings.minSpawnDelayMs = 0;
            settings.maxSpawnDelayMs = 0;
            UpdateFieldCache();
        }
        ImGui::SameLine();
        if (ImGui::Button("randSB270ms", buttonSize)) {
            settings.circleRadiusNorm = 0.065f;
            settings.circleLifetimeMs = 270;
            settings.cursorRadiusNorm = 0.015f;
            settings.gameTimeSec = 60;
            settings.endBySpawnCount = true;
            settings.maxSpawnCount = 60;
            settings.minSpawnDelayMs = 0;
            settings.maxSpawnDelayMs = 1200;
            UpdateFieldCache();
        }
        if (gameState == GAME_RUNNING) ImGui::EndDisabled();
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::SameLine();
    ImGui::Text("frame time: %.3f ms", 1000.0f / io.Framerate);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    if (gameState == GAME_RUNNING) ImGui::BeginDisabled();
    if (ImGui::Button("Reset config")) {
        settings = GameSettings();
        UpdateFieldCache();
    }
    if (gameState == GAME_RUNNING) ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::Text("'M' - settings");
    ImGui::SameLine();
    ImGui::Text("'R' - restart");
    ImGui::End();
}

// Registy MouseSensitivity to mupliplier
float GetMouseSpeedMultiplier(int mouseSpeed) {
    // Registry value -> multiplier
    static const std::map<int, float> sensitivityMap = {
        {1, 0.0625f},
        {2, 0.125f},
        {4, 0.25f},
        {6, 0.5f},
        {8, 0.75f},
        {10, 1.0f},
        {12, 1.5f},
        {14, 2.0f},
        {16, 2.5f},
        {18, 3.0f},
        {20, 3.5f}
    };

    // If the value is in the table, return
    auto it = sensitivityMap.find(mouseSpeed);
    if (it != sensitivityMap.end()) {
        return it->second;
    }

    // If the value is out of range, return the boundary
    if (mouseSpeed <= sensitivityMap.begin()->first) {
        return sensitivityMap.begin()->second;
    }
    if (mouseSpeed >= sensitivityMap.rbegin()->first) {
        return sensitivityMap.rbegin()->second;
    }

    // Linear interpolation for intermediate values
    auto it_low = sensitivityMap.lower_bound(mouseSpeed);
    auto it_prev = std::prev(it_low);
    int x0 = it_prev->first;
    int x1 = it_low->first;
    float y0 = it_prev->second;
    float y1 = it_low->second;

    float t = static_cast<float>(mouseSpeed - x0) / (x1 - x0);
    return y0 + t * (y1 - y0);
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_ACTIVATEAPP:
        g_active = (wParam == TRUE);
        if (g_active) {
            // Sync on window activation
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            g_cursorPosX = static_cast<float>(pt.x);
            g_cursorPosY = static_cast<float>(pt.y);

            CaptureMouseInWindow(hWnd, false);
        }
        else {
            // before going to background - we release, the system will get the cursor where the logical
            ReleaseMouseFromWindow(hWnd);
        }
        return 0;

    case WM_INPUT: {
        UINT dwSize = 0;
        // Get the required buffer size
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
            return 0;
        }
        if (dwSize == 0) return 0;

        // Using static buffer
        static constexpr UINT BUFFER_SIZE = 128;
        if (dwSize > BUFFER_SIZE) {
            return 0;
        }

        static BYTE buffer[BUFFER_SIZE];
        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buffer, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
            return 0; 
        }

        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
        if (raw->header.dwType == RIM_TYPEMOUSE) {
            g_accumulatedMouseDeltaX += raw->data.mouse.lLastX;
            g_accumulatedMouseDeltaY += raw->data.mouse.lLastY;

            // Button handling
            if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) {
                g_leftButtonPressedRaw = true;
            }
        }
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            UpdateCursor();
            return TRUE;
        }
        break;

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_WindowWidth = LOWORD(lParam);
            g_WindowHeight = HIWORD(lParam);
            g_renderer.Resize(g_WindowWidth, g_WindowHeight);
            UpdateFieldCache();

            g_cursorPosX = std::min(g_cursorPosX, static_cast<double>(g_WindowWidth));
            g_cursorPosY = std::min(g_cursorPosY, static_cast<double>(g_WindowHeight));
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}


int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd
) {
    srand((unsigned)time(NULL));
    ResetGame();
    LoadColorSettings();
    DEVMODE dm = getCurrentDisplayMode();
    if (dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0) {
        g_WindowWidth = dm.dmPelsWidth;
        g_WindowHeight = dm.dmPelsHeight;
    }
    int refreshRate = (dm.dmDisplayFrequency > 0) ? dm.dmDisplayFrequency : 60;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0, hInstance, NULL, NULL, NULL, NULL, _T("CircleGameClass"), NULL };
    RegisterClassEx(&wc);
    g_hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        _T("Flicks"),
        WS_POPUP,
        0, 0, g_WindowWidth, g_WindowHeight,
        NULL, NULL, hInstance, NULL
    );

    if (g_hWnd == NULL) return 1;

    POINT initialPos;
    GetCursorPos(&initialPos);
    ScreenToClient(g_hWnd, &initialPos);
    g_cursorPosX = static_cast<double>(initialPos.x);
    g_cursorPosY = static_cast<double>(initialPos.y);

    // Raw Input
    RAWINPUTDEVICE rid[1];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = g_hWnd;
    if (!RegisterRawInputDevices(rid, 1, sizeof(rid[0]))) {
        MessageBox(NULL, L"Failed to register raw input devices", L"Error", MB_OK);
    }

    // Get OS sens
    SystemParametersInfo(SPI_GETMOUSESPEED, 0, &g_mouseSpeed, 0);
    g_mouseSpeedMultiplier = GetMouseSpeedMultiplier(g_mouseSpeed);

    ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hWnd);

    if (!g_renderer.Initialize(g_hWnd, g_WindowWidth, g_WindowHeight, refreshRate)) {
        UnregisterClass(wc.lpszClassName, hInstance);
        return 1;
    }

    g_renderer.SetMaxFrameLatency(settings.frameLatency);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    ImguiTheme();
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(
        g_renderer.GetDevice(),
        g_renderer.GetDeviceContext()
    );

    InitXAudio2();
    CreateDirectory(L"res", NULL);
    LoadGameSummaries();
    UpdateFieldCache();

    bool done = false;
    bool prevShowAny = false;
    bool isLastCircle = false;
    bool prevWantCaptureMouse = true;
    bool forceFinish = false;

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        const auto currentTime = std::chrono::steady_clock::now();
        const long long currentTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime.time_since_epoch()
        ).count();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();

        if (io.WantCaptureMouse) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(g_hWnd, &pt);
            g_cursorPosX = static_cast<double>(pt.x);
            g_cursorPosY = static_cast<double>(pt.y);

            g_accumulatedMouseDeltaX = 0;
            g_accumulatedMouseDeltaY = 0;
        }
        else if (g_active) {
            const double speedFactor = static_cast<double>(g_mouseSpeedMultiplier);
            g_cursorPosX += static_cast<double>(g_accumulatedMouseDeltaX) * speedFactor;
            g_cursorPosY += static_cast<double>(g_accumulatedMouseDeltaY) * speedFactor;

            g_cursorPosX = std::clamp(g_cursorPosX, 0.0, static_cast<double>(g_WindowWidth));
            g_cursorPosY = std::clamp(g_cursorPosY, 0.0, static_cast<double>(g_WindowHeight));

            g_accumulatedMouseDeltaX = 0;
            g_accumulatedMouseDeltaY = 0;
        }

        if (g_leftButtonPressedRaw) {
            g_leftButtonPressed = (g_active && !io.WantCaptureMouse);
            g_leftButtonPressedRaw = false;
        }

        bool prevShowSettings = showSettings;
        bool prevShowResults = showResults;

        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            ResetGame();
            isLastCircle = false;
            forceFinish = false;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_M)) showSettings = !showSettings;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) PostQuitMessage(0);
        if (ImGui::IsKeyPressed(ImGuiKey_E) && gameState == GAME_RUNNING) {
            forceFinish = true;
        }

        // Handle circle spawning
        if (gameState == GAME_RUNNING) {
            const long long elapsedGameMs = currentTimeMs - gameStartTimeMs;
            const int elapsedSec = static_cast<int>(elapsedGameMs / 1000);

            if (elapsedSec > g_lastSampleSecond) {
                for (int s = g_lastSampleSecond + 1; s <= elapsedSec; ++s) {
                    g_scoreHistory.push_back(hits);
                }
                g_lastSampleSecond = elapsedSec;
            }

            if (!isLastCircle) {
                if (forceFinish) {
                    isLastCircle = true;
                }
                else if (gameStartSettings.endBySpawnCount && spawnCount >= gameStartSettings.maxSpawnCount) {
                    isLastCircle = true;
                }
                else if (!gameStartSettings.endBySpawnCount &&
                    elapsedGameMs >= static_cast<long long>(gameStartSettings.gameTimeSec) * 1000) {
                    isLastCircle = true;
                }
            }

            if (isCircleActive) {
                const long long elapsedCircleMs = currentTimeMs - circleSpawnTimeMs;
                if (elapsedCircleMs >= gameStartSettings.circleLifetimeMs) {
                    isCircleActive = false;
                    if (!isLastCircle) {
                        int delay = randomInt(gameStartSettings.minSpawnDelayMs, gameStartSettings.maxSpawnDelayMs);
                        nextSpawnTimeMs = currentTimeMs + delay;
                    }
                }
            }

            if (!isCircleActive && !isLastCircle && currentTimeMs >= nextSpawnTimeMs) {
                SpawnCircle(g_WindowWidth, g_WindowHeight);
                isCircleActive = true;
                spawnCount++;
            }

            if (isLastCircle && !isCircleActive) {
                gameState = GAME_FINISHED;

                if (elapsedSec > g_lastSampleSecond) {
                    for (int s = g_lastSampleSecond + 1; s <= elapsedSec; ++s) {
                        g_scoreHistory.push_back(hits);
                    }
                    g_lastSampleSecond = elapsedSec;
                }

                lastGameResult.avgReactionTime = 0.0f;
                if (!g_reactionTimes.empty()) {
                    long long sum = 0;
                    for (int rt : g_reactionTimes) sum += rt;
                    lastGameResult.avgReactionTime = static_cast<float>(sum) / g_reactionTimes.size();
                }

                float finalScore;
                if (gameStartSettings.endBySpawnCount) {
                    float sqrtValue = static_cast<float>(
                        std::sqrt(std::max(0, gameStartSettings.maxSpawnCount - hits))
                        );
                    finalScore = sqrtValue * 100.0f + lastGameResult.avgReactionTime;
                }
                else {
                    finalScore = static_cast<float>(hits);
                }

                lastGameResult.settings = gameStartSettings;
                lastGameResult.hits = hits;
                lastGameResult.attempts = attempts;
                lastGameResult.accuracy = (attempts > 0) ? (100.0f * hits / attempts) : 0.0f;
                lastGameResult.score = finalScore;
                lastGameResult.scoreHistory = g_scoreHistory;
                lastGameResult.reactionTimes = g_reactionTimes;

                GameSummary summary;
                summary.circleRadiusNorm = lastGameResult.settings.circleRadiusNorm;
                summary.cursorRadiusNorm = lastGameResult.settings.cursorRadiusNorm;
                summary.circleLifetimeMs = lastGameResult.settings.circleLifetimeMs;
                summary.gameTimeSec = lastGameResult.settings.gameTimeSec;
                summary.minSpawnDelayMs = lastGameResult.settings.minSpawnDelayMs;
                summary.maxSpawnDelayMs = lastGameResult.settings.maxSpawnDelayMs;
                summary.endBySpawnCount = lastGameResult.settings.endBySpawnCount;
                summary.maxSpawnCount = lastGameResult.settings.maxSpawnCount;
                summary.hits = lastGameResult.hits;
                summary.avgReactionTime = lastGameResult.avgReactionTime;
                summary.score = finalScore;
                summary.timestamp = std::time(nullptr);

                if (!forceFinish) {
                    g_allGameSummaries.push_back(summary);
                    SaveGameSummaries();
                }

                showResults = true;
                forceFinish = false;
            }
        }

        // Mouse
        if (g_leftButtonPressed && !io.WantCaptureMouse) {
            g_leftButtonPressed = false;
            ImVec2 mousePos(
                static_cast<float>(g_cursorPosX),
                static_cast<float>(g_cursorPosY)
            );

            if (gameState == GAME_NOT_STARTED) {
                ImVec2 center = g_fieldCache.center;
                float R = g_fieldCache.circleRadiusPx;
                float Cr = g_fieldCache.cursorRadiusPx;
                float dx = mousePos.x - center.x;
                float dy = mousePos.y - center.y;
                if (dx * dx + dy * dy <= (R + Cr) * (R + Cr)) {
                    StartGame();
                    isLastCircle = false;
                }
            }
            else if (gameState == GAME_RUNNING) {
                if (isCircleActive) {
                    float R = g_fieldCache.circleRadiusPx;
                    float Cr = g_fieldCache.cursorRadiusPx;
                    float dx = mousePos.x - circlePos.x;
                    float dy = mousePos.y - circlePos.y;
                    float distSq = dx * dx + dy * dy;
                    float hitRadiusSq = (R + Cr) * (R + Cr);

                    if (distSq <= hitRadiusSq) {
                        hits++;
                        attempts++;
                        g_lastReactionTime = static_cast<int>(currentTimeMs - circleSpawnTimeMs);
                        g_reactionTimes.push_back(g_lastReactionTime);
                        PlayHitSound();
                        isCircleActive = false;

                        if (!isLastCircle) {
                            int delay = randomInt(gameStartSettings.minSpawnDelayMs, gameStartSettings.maxSpawnDelayMs);
                            nextSpawnTimeMs = currentTimeMs + delay;
                        }
                    }
                    else {
                        attempts++;
                    }
                }
                else {
                    attempts++;
                }
            }
        }

        // RENDERING 
        float clear_color[4] = {
            settings.bgColor.x,
            settings.bgColor.y,
            settings.bgColor.z,
            settings.bgColor.w
        };

        g_renderer.BeginFrame(clear_color);

        g_renderer.DrawField(g_fieldCache, settings.fieldColor);

        g_renderer.BeginCircleRendering();

        if (gameState == GAME_NOT_STARTED) {
            g_renderer.DrawCircle(
                g_fieldCache.center,
                g_fieldCache.circleRadiusPx,
                settings.circleColor
            );
        }
        else if (gameState == GAME_RUNNING && isCircleActive) {
            g_renderer.DrawCircle(
                circlePos,
                g_fieldCache.circleRadiusPx,
                settings.circleColor
            );
        }

        if (settings.useCustomCursor && !showSettings && !showResults && !io.WantCaptureMouse) {
            ImVec2 mousePos(
                static_cast<float>(g_cursorPosX),
                static_cast<float>(g_cursorPosY)
            );
            float Cr = g_fieldCache.cursorRadiusPx;

            g_renderer.DrawCircle(
                mousePos,
                Cr,
                settings.cursorOutlineColor
            );

            float innerRadius = Cr - settings.cursorThickness;
            if (innerRadius > 0.0f) {
                g_renderer.DrawCircle(
                    mousePos,
                    innerRadius,
                    settings.cursorColor,
                    0.1f
                );
            }
        }

        g_renderer.EndCircleRendering();

        if (showSettings) ShowSettingsWindow();
        if (showResults) ShowResultsWindow();

        bool currentShowAny = showSettings || showResults;
        if (prevShowAny != currentShowAny) {
            if (currentShowAny) {
                ReleaseMouseFromWindow(g_hWnd);
            }
            else {
                CaptureMouseInWindow(g_hWnd, false);
            }
            ForceCursorUpdate();
        }
        prevShowAny = currentShowAny;
        if (prevShowSettings != showSettings) ForceCursorUpdate();
        if (prevShowResults != showResults) ForceCursorUpdate();

        if (showSettings || showResults) {
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        else {
            ImGui::EndFrame();
        }

        g_renderer.EndFrame();
    }

    SaveColorSettings();
    SaveGameSummaries();
    CleanupXAudio2();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    g_renderer.Cleanup();
    UnregisterClass(wc.lpszClassName, hInstance);
    return 0;
}