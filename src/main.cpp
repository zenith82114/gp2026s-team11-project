#define GLM_ENABLE_EXPERIMENTAL
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "shader.h"
#include "opengl_utils.h"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include "camera.h"
#include "texture.h"
#include "texture_cube.h"
#include "model.h"
#include "mesh.h"
#include "FreeImage.h"
#include <time.h>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
void drawGuiOverlay();
void updateWindowTitle();
void setSamplingModeFromGui(int mode);
bool mouseOverGui();
void initAccumTargets(int width, int height);
void resizeAccumTargets(int width, int height);
void clearAccumTargets(int width, int height);
FIBITMAP* captureFramebufferBitmap();
void saveVerticalComposite(const std::vector<FIBITMAP*>& rows, const char* filename);
void releaseBitmaps(std::vector<FIBITMAP*>& bitmaps);
void saveBitmap(FIBITMAP* bitmap, const char* filename);
void startCompareCapture64();
void startReferenceAutomation();
void startPsnrAutomation();
void finishAutomation();
void ensureReferenceScriptExists();
void runReferenceBuildScript();
void ensurePsnrScriptExists();
void runPsnrGraphScript();
const char* currentSamplingModeName();

bool isWindowed = true;
int samplingMode = 1; // 0: random, 1: Sobol, 2: Sobol with per-pixel scrambling

// setting
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;
const bool ENABLE_ACCUMULATION = true; // Optional for Figure 1(h).
int framebufferWidth = SCR_WIDTH;
int framebufferHeight = SCR_HEIGHT;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f);
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 1.0f / 60.0f;
float lastFrame = 0.0f;
int frameCountWithoutMove = 0;

// Optional accumulation targets.
unsigned int accumFBO = 0;
unsigned int accumTex[2] = { 0, 0 };

// GUI state.
GLFWwindow* gWindow = nullptr;
double mouseX = 0.0;
double mouseY = 0.0;
bool rightMouseDown = false;

struct GuiColor {
    float r;
    float g;
    float b;
    float a;
};

struct GuiButton {
    int x;
    int y;
    int w;
    int h;
    const char* label;
    int action; // 0 random, 1 sobol, 2 scrambled, 10 compare, 19 ref, 20 psnr
};

const GuiButton guiButtons[] = {
    {12, 12, 92, 34, "RANDOM", 0},
    {112, 12, 82, 34, "SOBOL", 1},
    {202, 12, 128, 34, "SCRAMBLED", 2},
    {350, 12, 110, 34, "COMPARE", 10},
    {468, 12, 70, 34, "REF", 19},
    {548, 12, 90, 34, "PSNR", 20}
};
const int guiButtonCount = sizeof(guiButtons) / sizeof(guiButtons[0]);

enum AutomationType {
    AUTO_NONE = 0,
    AUTO_COMPARE64 = 1,
    AUTO_PSNR = 2,
    AUTO_REF = 3
};

struct CaptureTask {
    int mode;
    int targetFrames;
    int randomSeedOffset;
    std::string filename;
    bool saveIndividual;
    bool collectForComposite;
};

AutomationType automationType = AUTO_NONE;
std::vector<CaptureTask> automationTasks;
int automationTaskIndex = 0;
int automationRenderMode = -1;
int automationPreviousMode = 1;
std::vector<FIBITMAP*> automationRows;
const int psnrFrameTargets[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
const int referenceSpp = 2048;

void saveImage(const char* filename) {
    int width = framebufferWidth;
    int height = framebufferHeight;
    BYTE* pixels = new BYTE[3 * width * height];
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels);
    FIBITMAP* image = FreeImage_ConvertFromRawBits(pixels, width, height, 3 * width, 24,
        0xFF0000, 0x00FF00, 0x0000FF, false);
    FreeImage_Save(FIF_PNG, image, filename, 0);
    FreeImage_Unload(image);
    delete[] pixels;
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gWindow = window;
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    updateWindowTitle();

    Shader rayTracingShader("../shaders/shader_ray_tracing.vs", "../shaders/shader_ray_tracing.fs");

    std::vector<float> quad_data({
        1.0f, 1.0f, 0.0f,  1.0f, 1.0f,
        1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
        -1.0f,  -1.0f, 0.0f, 0.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f
    });
    std::vector<unsigned int> quad_indices_vec({ 0,1,3,1,2,3 });
    std::vector<unsigned int> attrib_sizes({ 3, 2 });
    VAO* quad = getVAOFromAttribData(quad_data, attrib_sizes, quad_indices_vec);

    if (ENABLE_ACCUMULATION) {
        initAccumTargets(framebufferWidth, framebufferHeight);
        clearAccumTargets(framebufferWidth, framebufferHeight);
    }

    unsigned int vs_ubo = glGetUniformBlockIndex(rayTracingShader.ID, "mesh_vertices_ubo");
    glUniformBlockBinding(rayTracingShader.ID, vs_ubo, 0);

    rayTracingShader.use();
    rayTracingShader.setFloat("H", framebufferHeight);
    rayTracingShader.setFloat("W", framebufferWidth);
    rayTracingShader.setFloat("fovY", glm::radians(camera.Zoom));
    rayTracingShader.setInt("accumPrev", 1);
    rayTracingShader.setInt("displayOnly", 0);
    rayTracingShader.setInt("sampleMode", samplingMode);
    rayTracingShader.setInt("randomSeedOffset", 0);
    rayTracingShader.setInt("frameCountWithoutMove", 0);

    rayTracingShader.setVec3("material_ground.albedo", glm::vec3(0.8, 0.8, 0.0));
    rayTracingShader.setInt("material_ground.material_type", 0);

    rayTracingShader.setVec3("material_sphere_middle.albedo", glm::vec3(0.3, 0.3, 0.8));
    rayTracingShader.setInt("material_sphere_middle.material_type", 0);

    rayTracingShader.setVec3("material_sphere_left.albedo", glm::vec3(0.8, 0.8, 0.8));
    rayTracingShader.setInt("material_sphere_left.material_type", 2);
    rayTracingShader.setFloat("material_sphere_left.ior", 1.5f);
    rayTracingShader.setFloat("material_sphere_left.fuzz", 0.3f);

    rayTracingShader.setVec3("material_inside_left.albedo", glm::vec3(0.9, 0.9, 1.0));
    rayTracingShader.setInt("material_inside_left.material_type", 2);
    rayTracingShader.setFloat("material_inside_left.ior", 1.0f / 1.5f);
    rayTracingShader.setFloat("material_inside_left.fuzz", 0.3f);

    rayTracingShader.setVec3("material_sphere_right.albedo", glm::vec3(0.8, 0.6, 0.2));
    rayTracingShader.setInt("material_sphere_right.material_type", 1);
    rayTracingShader.setFloat("material_sphere_right.fuzz", 1.0f);

    glm::mat4 viewMatBefore = camera.GetViewMatrix();
    float zoomBefore = camera.Zoom;

    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        rayTracingShader.use();
        rayTracingShader.setFloat("H", framebufferHeight);
        rayTracingShader.setFloat("W", framebufferWidth);
        rayTracingShader.setFloat("fovY", glm::radians(camera.Zoom));
        rayTracingShader.setVec3("cameraPosition", camera.Position);
        glm::mat4 viewMatNow = camera.GetViewMatrix();
        rayTracingShader.setMat3("cameraToWorldRotMatrix", glm::transpose(glm::mat3(viewMatNow)));

        int currentRandomSeedOffset = 0;
        if (automationType != AUTO_NONE && automationTaskIndex < (int)automationTasks.size()) {
            const CaptureTask& task = automationTasks[automationTaskIndex];
            currentRandomSeedOffset = task.randomSeedOffset;
            if (task.mode != automationRenderMode) {
                samplingMode = task.mode;
                clearAccumTargets(framebufferWidth, framebufferHeight);
                frameCountWithoutMove = 0;
                automationRenderMode = task.mode;
                updateWindowTitle();
            }
        }

        rayTracingShader.setInt("sampleMode", samplingMode);
        rayTracingShader.setInt("randomSeedOffset", currentRandomSeedOffset);

        if (ENABLE_ACCUMULATION) {
            int fbW, fbH;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW != framebufferWidth || fbH != framebufferHeight) {
                framebufferWidth = fbW;
                framebufferHeight = fbH;
                resizeAccumTargets(framebufferWidth, framebufferHeight);
                clearAccumTargets(framebufferWidth, framebufferHeight);
                frameCountWithoutMove = 0;
            }

            if (viewMatBefore != viewMatNow || zoomBefore != camera.Zoom) {
                frameCountWithoutMove = 0;
                viewMatBefore = viewMatNow;
                zoomBefore = camera.Zoom;
            }

            rayTracingShader.setInt("frameCountWithoutMove", frameCountWithoutMove);

            int accumCurr = frameCountWithoutMove % 2;
            int accumPrev = 1 - accumCurr;

            glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex[accumCurr], 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            rayTracingShader.setInt("displayOnly", 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, accumTex[accumPrev]);
            glBindVertexArray(quad->ID);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            rayTracingShader.setInt("displayOnly", 1);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, accumTex[accumCurr]);
            glBindVertexArray(quad->ID);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            if (automationType != AUTO_NONE && automationTaskIndex < (int)automationTasks.size()) {
                const CaptureTask& task = automationTasks[automationTaskIndex];
                if (frameCountWithoutMove + 1 == task.targetFrames) {
                    FIBITMAP* bmp = captureFramebufferBitmap();
                    if (task.saveIndividual) {
                        saveBitmap(bmp, task.filename.c_str());
                        std::cout << "Saved image: " << task.filename << std::endl;
                    }
                    if (task.collectForComposite) {
                        automationRows.push_back(bmp);
                    }
                    else {
                        FreeImage_Unload(bmp);
                    }

                    automationTaskIndex++;
                    automationRenderMode = -1;

                    if (automationTaskIndex >= (int)automationTasks.size()) {
                        finishAutomation();
                    }
                    else {
                        updateWindowTitle();
                    }
                }
            }

            frameCountWithoutMove++;
        }
        else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glBindVertexArray(quad->ID);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }

        processInput(window);
        drawGuiOverlay();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

const char** guiGlyph(char c) {
    static const char* SPACE[7] = {"00000","00000","00000","00000","00000","00000","00000"};
    static const char* A[7] = {"01110","10001","10001","11111","10001","10001","10001"};
    static const char* B[7] = {"11110","10001","10001","11110","10001","10001","11110"};
    static const char* C[7] = {"01111","10000","10000","10000","10000","10000","01111"};
    static const char* D[7] = {"11110","10001","10001","10001","10001","10001","11110"};
    static const char* E[7] = {"11111","10000","10000","11110","10000","10000","11111"};
    static const char* F[7] = {"11111","10000","10000","11110","10000","10000","10000"};
    static const char* G[7] = {"01111","10000","10000","10011","10001","10001","01111"};
    static const char* I[7] = {"11111","00100","00100","00100","00100","00100","11111"};
    static const char* L[7] = {"10000","10000","10000","10000","10000","10000","11111"};
    static const char* M[7] = {"10001","11011","10101","10101","10001","10001","10001"};
    static const char* N[7] = {"10001","11001","10101","10011","10001","10001","10001"};
    static const char* O[7] = {"01110","10001","10001","10001","10001","10001","01110"};
    static const char* P[7] = {"11110","10001","10001","11110","10000","10000","10000"};
    static const char* R[7] = {"11110","10001","10001","11110","10100","10010","10001"};
    static const char* S[7] = {"01111","10000","10000","01110","00001","00001","11110"};
    static const char* T[7] = {"11111","00100","00100","00100","00100","00100","00100"};
    static const char* V[7] = {"10001","10001","10001","10001","10001","01010","00100"};
    static const char* DIG0[7] = {"01110","10001","10011","10101","11001","10001","01110"};
    static const char* DIG1[7] = {"00100","01100","00100","00100","00100","00100","01110"};
    static const char* DIG2[7] = {"01110","10001","00001","00010","00100","01000","11111"};
    static const char* DIG3[7] = {"11110","00001","00001","01110","00001","00001","11110"};
    static const char* DIG4[7] = {"10010","10010","10010","11111","00010","00010","00010"};
    static const char* DIG5[7] = {"11111","10000","10000","11110","00001","00001","11110"};
    static const char* DIG6[7] = {"01111","10000","10000","11110","10001","10001","01110"};
    static const char* DIG7[7] = {"11111","00001","00010","00100","01000","01000","01000"};
    static const char* DIG8[7] = {"01110","10001","10001","01110","10001","10001","01110"};
    static const char* DIG9[7] = {"01110","10001","10001","01111","00001","00001","11110"};

    switch (c) {
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
    case 'E': return E; case 'F': return F; case 'G': return G; case 'I': return I; case 'L': return L;
    case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
    case 'R': return R; case 'S': return S; case 'T': return T; case 'V': return V;
    case '0': return DIG0; case '1': return DIG1; case '2': return DIG2; case '3': return DIG3;
    case '4': return DIG4; case '5': return DIG5; case '6': return DIG6; case '7': return DIG7;
    case '8': return DIG8; case '9': return DIG9;
    default: return SPACE;
    }
}

void drawGuiRect(int x, int y, int w, int h, GuiColor c) {
    if (w <= 0 || h <= 0) return;
    int sy = framebufferHeight - y - h;
    glScissor(x, sy, w, h);
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void drawGuiText(int x, int y, const char* text, int scale, GuiColor c) {
    int cursorX = x;
    for (const char* p = text; *p; p++) {
        const char** glyph = guiGlyph(*p);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row][col] == '1') {
                    drawGuiRect(cursorX + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
        cursorX += 6 * scale;
    }
}

void drawGuiButton(const GuiButton& b) {
    bool selected = (b.action >= 0 && b.action <= 2 && samplingMode == b.action);
    bool hovered = mouseX >= b.x && mouseX <= b.x + b.w && mouseY >= b.y && mouseY <= b.y + b.h;

    GuiColor border = selected ? GuiColor{1.0f, 1.0f, 1.0f, 1.0f} : GuiColor{0.12f, 0.12f, 0.12f, 1.0f};
    GuiColor fill;

    if (b.action == 0) fill = GuiColor{0.42f, 0.15f, 0.15f, 1.0f};
    else if (b.action == 1) fill = GuiColor{0.12f, 0.32f, 0.65f, 1.0f};
    else if (b.action == 2) fill = GuiColor{0.12f, 0.50f, 0.28f, 1.0f};
    else if (b.action == 10) fill = automationType == AUTO_COMPARE64 ? GuiColor{0.70f, 0.42f, 0.08f, 1.0f} : GuiColor{0.42f, 0.20f, 0.55f, 1.0f};
    else if (b.action == 19) fill = automationType == AUTO_REF ? GuiColor{0.70f, 0.42f, 0.08f, 1.0f} : GuiColor{0.36f, 0.36f, 0.18f, 1.0f};
    else fill = automationType == AUTO_PSNR ? GuiColor{0.70f, 0.42f, 0.08f, 1.0f} : GuiColor{0.18f, 0.42f, 0.60f, 1.0f};

    if (hovered) {
        fill.r = MIN(fill.r + 0.18f, 1.0f);
        fill.g = MIN(fill.g + 0.18f, 1.0f);
        fill.b = MIN(fill.b + 0.18f, 1.0f);
    }

    drawGuiRect(b.x, b.y, b.w, b.h, border);
    drawGuiRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, fill);

    int textScale = 2;
    int textW = int(strlen(b.label)) * 6 * textScale;
    int tx = b.x + (b.w - textW) / 2;
    int ty = b.y + (b.h - 7 * textScale) / 2;
    drawGuiText(tx, ty, b.label, textScale, GuiColor{1.0f, 1.0f, 1.0f, 1.0f});
}

void drawGuiOverlay() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_SCISSOR_TEST);

    for (int i = 0; i < guiButtonCount; i++) {
        drawGuiButton(guiButtons[i]);
    }

    if (automationType != AUTO_NONE) {
        int totalTasks = MAX((int)automationTasks.size(), 1);
        float partial = 0.0f;
        if (automationTaskIndex < (int)automationTasks.size()) {
            partial = float(frameCountWithoutMove) / float(MAX(automationTasks[automationTaskIndex].targetFrames, 1));
            partial = MIN(MAX(partial, 0.0f), 1.0f);
        }
        float progress = (float)automationTaskIndex + partial;
        int barW = int(626.0f * progress / float(totalTasks));
        drawGuiRect(12, 52, 626, 6, GuiColor{0.05f, 0.05f, 0.05f, 1.0f});
        drawGuiRect(12, 52, barW, 6, GuiColor{0.95f, 0.75f, 0.20f, 1.0f});
    }

    glDisable(GL_SCISSOR_TEST);
}

bool mouseOverGui() {
    for (int i = 0; i < guiButtonCount; i++) {
        const GuiButton& b = guiButtons[i];
        if (mouseX >= b.x && mouseX <= b.x + b.w && mouseY >= b.y && mouseY <= b.y + b.h) {
            return true;
        }
    }
    return automationType != AUTO_NONE && mouseX >= 12 && mouseX <= 638 && mouseY >= 52 && mouseY <= 58;
}

const char* currentSamplingModeName() {
    if (samplingMode == 0) return "Random";
    if (samplingMode == 1) return "Sobol";
    return "Scrambled Sobol";
}

void updateWindowTitle() {
    if (gWindow == nullptr) return;

    char title[256];
    if (automationType == AUTO_COMPARE64) {
        int displayTask = MIN(automationTaskIndex + 1, (int)automationTasks.size());
        sprintf(title, "Ray Tracing Sampler GUI | COMPARE 64 | Task %d/%d | Mode: %s",
            displayTask, (int)automationTasks.size(), currentSamplingModeName());
    }
    else if (automationType == AUTO_PSNR) {
        int displayTask = MIN(automationTaskIndex + 1, (int)automationTasks.size());
        sprintf(title, "Ray Tracing Sampler GUI | PSNR RUN | Task %d/%d | Mode: %s",
            displayTask, (int)automationTasks.size(), currentSamplingModeName());
    }
    else if (automationType == AUTO_REF) {
        int displayTask = MIN(automationTaskIndex + 1, (int)automationTasks.size());
        sprintf(title, "Ray Tracing Sampler GUI | REF RUN | Task %d/%d | Mode: %s",
            displayTask, (int)automationTasks.size(), currentSamplingModeName());
    }
    else {
        sprintf(title, "Ray Tracing Sampler GUI | Mode: %s | Frame/SPP: %d | Right-drag to rotate",
            currentSamplingModeName(), frameCountWithoutMove);
    }
    glfwSetWindowTitle(gWindow, title);
}

void setSamplingModeFromGui(int mode) {
    if (automationType != AUTO_NONE || mode == samplingMode) return;
    samplingMode = mode;
    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "Sampling mode: " << currentSamplingModeName() << std::endl;
    updateWindowTitle();
}

void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (automationType == AUTO_NONE) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera.ProcessKeyboard(FORWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera.ProcessKeyboard(BACKWARD, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera.ProcessKeyboard(LEFT, deltaTime);
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera.ProcessKeyboard(RIGHT, deltaTime);

        if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
            std::cout << "Position " << camera.Position.x << "," << camera.Position.y << "," << camera.Position.z << std::endl;
            std::cout << "Yaw " << camera.Yaw << std::endl;
        }
    }
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    framebufferWidth = width;
    framebufferHeight = height;
    if (ENABLE_ACCUMULATION) {
        resizeAccumTargets(width, height);
        frameCountWithoutMove = 0;
    }
    glViewport(0, 0, width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0) {
        mouseX = xpos;
        mouseY = ypos;
    }
    else {
        mouseX = xpos * double(framebufferWidth) / double(winW);
        mouseY = ypos * double(framebufferHeight) / double(winH);
    }

    if (!rightMouseDown || mouseOverGui()) {
        firstMouse = true;
        lastX = float(xpos);
        lastY = float(ypos);
        return;
    }

    if (firstMouse) {
        lastX = float(xpos);
        lastY = float(ypos);
        firstMouse = false;
    }

    float xoffset = float(xpos) - lastX;
    float yoffset = lastY - float(ypos);
    lastX = float(xpos);
    lastY = float(ypos);
    camera.ProcessMouseMovement(xoffset, yoffset);
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rightMouseDown = (action == GLFW_PRESS);
        firstMouse = true;
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;

    for (int i = 0; i < guiButtonCount; i++) {
        const GuiButton& b = guiButtons[i];
        bool inside = mouseX >= b.x && mouseX <= b.x + b.w && mouseY >= b.y && mouseY <= b.y + b.h;
        if (!inside) continue;

        if (b.action >= 0 && b.action <= 2) {
            setSamplingModeFromGui(b.action);
        }
        else if (b.action == 10) {
            if (automationType == AUTO_NONE) {
                startCompareCapture64();
            }
        }
        else if (b.action == 19) {
            if (automationType == AUTO_NONE) {
                startReferenceAutomation();
            }
        }
        else if (b.action == 20) {
            if (automationType == AUTO_NONE) {
                startPsnrAutomation();
            }
        }
        return;
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (!mouseOverGui()) {
        camera.ProcessMouseScroll(yoffset);
    }
}

void initAccumTargets(int width, int height) {
    glGenFramebuffers(1, &accumFBO);
    glGenTextures(2, accumTex);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, accumTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex[0], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Accumulation framebuffer is not complete." << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void resizeAccumTargets(int width, int height) {
    if (width <= 0 || height <= 0 || accumTex[0] == 0) return;
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, accumTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void clearAccumTargets(int width, int height) {
    if (width <= 0 || height <= 0 || accumFBO == 0 || accumTex[0] == 0) return;

    glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    for (int i = 0; i < 2; i++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex[i], 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void saveBitmap(FIBITMAP* bitmap, const char* filename) {
    if (bitmap == nullptr) return;
    FreeImage_Save(FIF_PNG, bitmap, filename, 0);
}

void startCompareCapture64() {
    automationType = AUTO_COMPARE64;
    automationTasks.clear();
    automationTasks.push_back({0, 64, 0, "", false, true});
    automationTasks.push_back({1, 64, 0, "", false, true});
    automationTasks.push_back({2, 64, 0, "", false, true});
    automationTaskIndex = 0;
    automationRenderMode = -1;
    automationPreviousMode = samplingMode;
    releaseBitmaps(automationRows);
    automationRows.clear();
    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "Compare capture started (64 frames only)." << std::endl;
    updateWindowTitle();
}

void startReferenceAutomation() {
    automationType = AUTO_REF;
    automationTasks.clear();
    automationTaskIndex = 0;
    automationRenderMode = -1;
    automationPreviousMode = samplingMode;
    releaseBitmaps(automationRows);
    automationRows.clear();

    automationTasks.push_back({0, referenceSpp, 1, "random_ref_2048.png", true, false});
    automationTasks.push_back({1, referenceSpp, 0, "sobol_ref_2048.png", true, false});
    automationTasks.push_back({2, referenceSpp, 0, "scrambled_ref_2048.png", true, false});

    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "REF automation started. It will render random_ref_2048.png, sobol_ref_2048.png, scrambled_ref_2048.png, then average them into ref_2048.png." << std::endl;
    updateWindowTitle();
}

void startPsnrAutomation() {
    std::ifstream refCheck("ref_2048.png", std::ios::binary);
    if (!refCheck.good()) {
        std::cout << "ref_2048.png not found. Press REF first to create the reference image." << std::endl;
        return;
    }

    automationType = AUTO_PSNR;
    automationTasks.clear();
    automationTaskIndex = 0;
    automationRenderMode = -1;
    automationPreviousMode = samplingMode;
    releaseBitmaps(automationRows);
    automationRows.clear();

    const char* modeNames[3] = { "random", "sobol", "scrambled" };
    for (int ti = 0; ti < 10; ti++) {
        int spp = psnrFrameTargets[ti];
        for (int mode = 0; mode < 3; mode++) {
            char filename[64];
            sprintf(filename, "%s_%d.png", modeNames[mode], spp);
            int seedOffset = (mode == 0) ? 1 : 0;
            automationTasks.push_back({mode, spp, seedOffset, filename, true, false});
        }
    }

    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "PSNR automation started. It will use existing ref_2048.png and render all test images up to 512 spp." << std::endl;
    updateWindowTitle();
}

void finishAutomation() {
    if (automationType == AUTO_COMPARE64) {
        if (!automationRows.empty()) {
            saveVerticalComposite(automationRows, "compare_64.png");
            std::cout << "Saved compare_64.png" << std::endl;
            releaseBitmaps(automationRows);
            automationRows.clear();
        }
        std::cout << "Compare capture done." << std::endl;
    }
    else if (automationType == AUTO_PSNR) {
        std::cout << "All PSNR input images are rendered. Generating graphs..." << std::endl;
        runPsnrGraphScript();
        std::cout << "PSNR run done. Output files: mse_vs_spp.png, psnr_vs_spp.png, psnr_results.csv" << std::endl;
    }
    else if (automationType == AUTO_REF) {
        std::cout << "All reference seed images are rendered. Building ref_2048.png..." << std::endl;
        runReferenceBuildScript();
        std::cout << "REF run done. Output file: ref_2048.png" << std::endl;
    }

    samplingMode = automationPreviousMode;
    automationTasks.clear();
    automationTaskIndex = 0;
    automationRenderMode = -1;
    automationType = AUTO_NONE;
    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    updateWindowTitle();
}

void ensureReferenceScriptExists() {
    std::ofstream out("generate_ref_image.py", std::ios::binary);
    out << R"PY(import os
from PIL import Image
import numpy as np

REFERENCE_FILES = [
    'random_ref_2048.png',
    'sobol_ref_2048.png',
    'scrambled_ref_2048.png',
]
REF_PATH = 'ref_2048.png'


def load_img(path):
    img = Image.open(path).convert('RGB')
    return np.asarray(img).astype(np.float32) / 255.0


def save_img(path, arr):
    arr = np.clip(arr, 0.0, 1.0)
    Image.fromarray((arr * 255.0 + 0.5).astype(np.uint8), mode='RGB').save(path)


def main():
    imgs = []
    for path in REFERENCE_FILES:
        if not os.path.exists(path):
            raise FileNotFoundError(f'Missing reference image: {path}')
        imgs.append(load_img(path))

    ref = np.mean(np.stack(imgs, axis=0), axis=0)
    save_img(REF_PATH, ref)

    for path in REFERENCE_FILES:
        if os.path.exists(path):
            os.remove(path)

    print('Generated ref_2048.png from random_ref_2048.png, sobol_ref_2048.png, scrambled_ref_2048.png.')
    print('Deleted intermediate reference images.')


if __name__ == '__main__':
    main()
)PY";
}

void runReferenceBuildScript() {
    ensureReferenceScriptExists();
    int result = std::system("python3 generate_ref_image.py");
    if (result != 0) {
        std::cout << "Warning: failed to run generate_ref_image.py (exit code " << result << ")" << std::endl;
        std::cout << "Make sure python3, Pillow, and numpy are available." << std::endl;
    }
}

void ensurePsnrScriptExists() {
    std::ofstream out("generate_psnr_graphs.py", std::ios::binary);
    out << R"PY(import csv
import math
import os
from PIL import Image
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

SPPS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
METHODS = {
    'Random': 'random_{}.png',
    'Sobol': 'sobol_{}.png',
    'Scrambled Sobol': 'scrambled_{}.png',
}
REF_PATH = 'ref_2048.png'


def load_img(path):
    img = Image.open(path).convert('RGB')
    return np.asarray(img).astype(np.float32) / 255.0


def mse(img, ref):
    return float(np.mean((img - ref) ** 2))


def psnr_from_mse(val):
    if val <= 0.0:
        return float('inf')
    return 10.0 * math.log10(1.0 / val)


def cleanup_intermediate_images():
    for method_name, pattern in METHODS.items():
        for spp in SPPS:
            path = pattern.format(spp)
            if os.path.exists(path):
                os.remove(path)


def main():
    if not os.path.exists(REF_PATH):
        raise FileNotFoundError(f'Missing reference image: {REF_PATH}. Press REF first.')

    ref = load_img(REF_PATH)
    rows = []

    for method_name, pattern in METHODS.items():
        for spp in SPPS:
            path = pattern.format(spp)
            if not os.path.exists(path):
                raise FileNotFoundError(f'Missing image: {path}')
            img = load_img(path)
            err = mse(img, ref)
            rows.append({
                'method': method_name,
                'spp': spp,
                'mse': err,
                'psnr': psnr_from_mse(err),
            })

    with open('psnr_results.csv', 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=['method', 'spp', 'mse', 'psnr'])
        writer.writeheader()
        writer.writerows(rows)

    plt.figure()
    for method_name in METHODS.keys():
        xs = [r['spp'] for r in rows if r['method'] == method_name]
        ys = [r['mse'] for r in rows if r['method'] == method_name]
        plt.plot(xs, ys, marker='o', label=method_name)
    plt.xscale('log', base=2)
    plt.yscale('log')
    plt.xlabel('Samples per pixel')
    plt.ylabel('MSE to reference')
    plt.title('MSE vs spp')
    plt.grid(True, which='both')
    plt.legend()
    plt.tight_layout()
    plt.savefig('mse_vs_spp.png', dpi=200)
    plt.close()

    plt.figure()
    for method_name in METHODS.keys():
        xs = [r['spp'] for r in rows if r['method'] == method_name]
        ys = [r['psnr'] for r in rows if r['method'] == method_name]
        plt.plot(xs, ys, marker='o', label=method_name)
    plt.xscale('log', base=2)
    plt.xlabel('Samples per pixel')
    plt.ylabel('PSNR (dB)')
    plt.title('PSNR vs spp')
    plt.grid(True, which='both')
    plt.legend()
    plt.tight_layout()
    plt.savefig('psnr_vs_spp.png', dpi=200)
    plt.close()

    cleanup_intermediate_images()
    print('Generated: mse_vs_spp.png, psnr_vs_spp.png, psnr_results.csv')
    print('Deleted intermediate sampling images. Kept ref_2048.png.')


if __name__ == '__main__':
    main()
)PY";
}


void runPsnrGraphScript() {
    ensurePsnrScriptExists();
    int result = std::system("python3 generate_psnr_graphs.py");
    if (result != 0) {
        std::cout << "Warning: failed to run generate_psnr_graphs.py (exit code " << result << ")" << std::endl;
        std::cout << "Make sure python3, Pillow, numpy, and matplotlib are available." << std::endl;
    }
}

FIBITMAP* captureFramebufferBitmap() {
    int width = framebufferWidth;
    int height = framebufferHeight;
    BYTE* pixels = new BYTE[3 * width * height];
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels);
    FIBITMAP* bitmap = FreeImage_ConvertFromRawBits(pixels, width, height, 3 * width, 24,
        0xFF0000, 0x00FF00, 0x0000FF, false);
    delete[] pixels;
    return bitmap;
}

void saveVerticalComposite(const std::vector<FIBITMAP*>& rows, const char* filename) {
    if (rows.empty()) return;

    int width = (int)FreeImage_GetWidth(rows[0]);
    int rowHeight = (int)FreeImage_GetHeight(rows[0]);
    FIBITMAP* composite = FreeImage_Allocate(width, rowHeight * (int)rows.size(), 24);
    if (composite == nullptr) return;

    for (int i = 0; i < (int)rows.size(); i++) {
        FreeImage_Paste(composite, rows[i], 0, i * rowHeight, 256);
    }

    FreeImage_Save(FIF_PNG, composite, filename, 0);
    FreeImage_Unload(composite);
}

void releaseBitmaps(std::vector<FIBITMAP*>& bitmaps) {
    for (FIBITMAP* bitmap : bitmaps) {
        if (bitmap != nullptr) {
            FreeImage_Unload(bitmap);
        }
    }
    bitmaps.clear();
}
