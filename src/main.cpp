#define GLM_ENABLE_EXPERIMENTAL
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X,Y) ((X) > (Y) ? (X) : (Y))
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "shader.h"
#include "scene.h"
#include "opengl_utils.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include "camera.h"
#include "texture.h"
#include "texture_cube.h"
#include "model.h"
#include "mesh.h"
#ifdef HAS_FREEIMAGE
#include "FreeImage.h"
#endif
#include <time.h>

// Command used to invoke Python for the REF/PSNR automation scripts.
// On Windows the standard launcher is `py` (C:\Windows\py.exe); `python3`
// typically resolves to the Microsoft Store stub (or nothing) and fails under
// system(). Elsewhere `python3` is the canonical name.
#ifdef _WIN32
#define PYTHON_CMD "py"
#else
#define PYTHON_CMD "python3"
#endif

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
void initAccumTargets(int width, int height);
void resizeAccumTargets(int width, int height);
void clearAccumTargets(int width, int height);
void updatePanelMode(GLFWwindow* window);
bool drawMaterialPanel();

// --- Sampling-strategy comparison harness (ported from sobol_perpixel) ---
void applySamplingMode(int mode);
const char* currentSamplingModeName();
void startReferenceAutomation();
void startPsnrAutomation();
void startCompareCapture64();
void finishAutomation();
void ensureReferenceScriptExists();
void runReferenceBuildScript();
void ensurePsnrScriptExists();
void runPsnrGraphScript();
#ifdef HAS_FREEIMAGE
FIBITMAP* captureFramebufferBitmap();
void saveBitmap(FIBITMAP* bitmap, const char* filename);
void saveVerticalComposite(const std::vector<FIBITMAP*>& rows, const char* filename);
void releaseBitmaps(std::vector<FIBITMAP*>& bitmaps);
#endif

bool isWindowed = true;
bool isKeyboardDone[1024] = { 0 };
bool showMaterialPanel = false;
bool prevShowMaterialPanel = false;
int selectedEditableMaterial = 0;

// Procedural floor stripes (toggled from the ImGui panel; uploaded as shader uniforms).
bool enableFloorStripes = false;
float floorStripeCount = 100.0f; // stripe density: dark/light pairs per world unit in x

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
float deltaTime = 1.0f / 60.0f;	// time between current frame and last frame
float lastFrame = 0.0f;
const int MAX_FRAME_COUNT_WITHOUT_MOVE = 16; // cap accumulation for equal-sample experiments
int frameCountWithoutMove = 0;

// render mode, switched at runtime with keys 0/1/2
// 0: BSDF-only, 1: NEE-only, 2: NEE+MIS
int renderMode = 2;

// Active sampler: 0 random, 1 Sobol, 2 per-pixel scrambled Sobol.
int samplingMode = 1;

// --- Sampling-comparison automation state (ported from sobol_perpixel) ---
// The harness renders the scene under each sampler at a sweep of spp counts and
// (REF) averages a high-spp ground truth, then shells out to Python to plot the
// MSE/PSNR convergence curves. RENDER_MODE and SCENE are held fixed during a sweep
// and only encoded into the output filenames so results don't clobber across them.
enum AutomationType {
    AUTO_NONE = 0,
    AUTO_COMPARE64 = 1,
    AUTO_PSNR = 2,
    AUTO_REF = 3
};

struct CaptureTask {
    int mode;              // samplingMode for this task
    int targetFrames;      // spp == accumulation frames to render before capturing
    int randomSeedOffset;  // decorrelates independent Random runs (vs. the reference)
    std::string filename;  // output png (already tagged with scene/mode)
    bool saveIndividual;
    bool collectForComposite;
};

AutomationType automationType = AUTO_NONE;
std::vector<CaptureTask> automationTasks;
int automationTaskIndex = 0;
int automationAppliedMode = -1;   // last samplingMode pushed to the shader (avoids redundant resets)
int automationPreviousMode = 1;   // samplingMode to restore when a sweep finishes
std::string outputTag;            // "scene<S>_mode<R>_" prefix for the current sweep's files
const int psnrFrameTargets[10] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
const int referenceSpp = 2048;
#ifdef HAS_FREEIMAGE
std::vector<FIBITMAP*> automationRows;
#endif

// Optional accumulation targets for Figure 1(h).
unsigned int accumFBO = 0;
unsigned int accumTex[2] = { 0, 0 };

// Save Image to png file. press V key.
// file name : date.png (created in bin folder)
// Install FreeImage to use this function. (dlls, includes, lib)
void saveImage(const char* filename) {
#ifdef HAS_FREEIMAGE
    // Make the BYTE array, factor of 3 because it's RBG.
    int width = framebufferWidth;
    int height = framebufferHeight;
    BYTE* pixels = new BYTE[3 * width * height];
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels);

    // Convert to FreeImage format & save to file
    FIBITMAP* image = FreeImage_ConvertFromRawBits(pixels, width, height, 3 * width, 24, 0xFF0000, 0x00FF00, 0x0000FF, false);
    FreeImage_Save(FIF_PNG, image, filename, 0);

    // Free resources
    FreeImage_Unload(image);
    delete[] pixels;
#else
    std::cout << "FreeImage 없음: " << filename << " 저장 생략" << std::endl;
#endif
}

int main()
{
    if (SCENE == SCENE_ROUGHNESS_LADDER) {
        ladderScene = initialLadderScene;
    } else {
        for (int i = 0; i < MATERIAL_COUNT; i++) {
            disneyMaterials[i] = initialDisneyMaterials[i];
        }
    }

    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);

    // tell GLFW to capture our mouse
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

    // configure global opengl state
    // -----------------------------
    // glEnable(GL_DEPTH_TEST);

    // build and compile our shader program
    // ------------------------------------
    Shader rayTracingShader("../shaders/shader_ray_tracing.vs", "../shaders/shader_ray_tracing.fs");

    std::vector<float> quad_data({
        // positions         // uvs
        1.0f, 1.0f, 0.0f,  1.0f, 1.0f,  // top right
        1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  // top left
        -1.0f,  -1.0f, 0.0f, 0.0f, 0.0f,   // bottom left
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f  // bottom right
        });
    std::vector<unsigned int> quad_indices_vec({ 0,1,3,1,2,3 });
    std::vector<unsigned int> attrib_sizes({ 3, 2 });

    VAO* quad = getVAOFromAttribData(quad_data, attrib_sizes, quad_indices_vec);

    // std::vector<std::string> faces
    // {
    //     "../resources/skybox/right.jpg",
    //     "../resources/skybox/left.jpg",
    //     "../resources/skybox/top.jpg",
    //     "../resources/skybox/bottom.jpg",
    //     "../resources/skybox/front.jpg",
    //     "../resources/skybox/back.jpg"
    // };
    // CubemapTexture skyboxTexture = CubemapTexture(faces);

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
    rayTracingShader.setInt("frameCountWithoutMove", 0);
    rayTracingShader.setInt("sampleMode", samplingMode);
    rayTracingShader.setInt("randomSeedOffset", 0);
    rayTracingShader.setInt("enableFloorStripes", enableFloorStripes ? 1 : 0);
    rayTracingShader.setFloat("floorStripeCount", floorStripeCount);

    rayTracingShader.setInt("SCENE", SCENE);

    if (SCENE == SCENE_ROUGHNESS_LADDER) {
        uploadLadderMaterials(rayTracingShader);
    } else {
        uploadDisneyMaterials(rayTracingShader);
    }

    glm::mat4 viewMatBefore = camera.GetViewMatrix();
    float zoomBefore = camera.Zoom;

    while (!glfwWindowShouldClose(window))// render loop
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        glfwPollEvents();
        processInput(window);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        bool materialChanged = drawMaterialPanel();
        updatePanelMode(window);

        if (materialChanged) {
            rayTracingShader.use();
            if (SCENE == SCENE_ROUGHNESS_LADDER) {
                uploadLadderMaterials(rayTracingShader);
            } else {
                uploadDisneyMaterials(rayTracingShader);
            }
            frameCountWithoutMove = 0;
        }

        rayTracingShader.use();
        rayTracingShader.setFloat("H", framebufferHeight);
        rayTracingShader.setFloat("W", framebufferWidth);
        rayTracingShader.setFloat("fovY", glm::radians(camera.Zoom));
        rayTracingShader.setVec3("cameraPosition", camera.Position);
        glm::mat4 viewMatNow = camera.GetViewMatrix();
        rayTracingShader.setMat3("cameraToWorldRotMatrix", glm::transpose(glm::mat3(viewMatNow)));
        rayTracingShader.setInt("RENDER_MODE", renderMode);
        rayTracingShader.setInt("enableFloorStripes", enableFloorStripes ? 1 : 0);
        rayTracingShader.setFloat("floorStripeCount", floorStripeCount);

        // When a sweep is running, the active task dictates the sampler + seed offset.
        // Switching task mode restarts accumulation so each spp target is rendered clean.
        bool automationActive = (automationType != AUTO_NONE);
        int currentRandomSeedOffset = 0;
        if (automationActive && automationTaskIndex < (int)automationTasks.size()) {
            const CaptureTask& task = automationTasks[automationTaskIndex];
            currentRandomSeedOffset = task.randomSeedOffset;
            if (task.mode != automationAppliedMode) {
                samplingMode = task.mode;
                clearAccumTargets(framebufferWidth, framebufferHeight);
                frameCountWithoutMove = 0;
                automationAppliedMode = task.mode;
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
                frameCountWithoutMove = 0;
            }

            // Don't let a camera nudge reset accumulation mid-sweep (the automation
            // owns frameCountWithoutMove while it runs).
            if (!automationActive && (viewMatBefore != viewMatNow || zoomBefore != camera.Zoom)) {
                frameCountWithoutMove = 0;
                viewMatBefore = viewMatNow;
                zoomBefore = camera.Zoom;
            }

            // Normally we cap accumulation for equal-sample experiments and then just
            // keep displaying the converged result. During a sweep we must reach the
            // task's spp target (up to 2048), so the cap is bypassed.
            bool accumulating = automationActive ? true
                                                 : (frameCountWithoutMove < MAX_FRAME_COUNT_WITHOUT_MOVE);
            int accumCurr = frameCountWithoutMove % 2;
            int accumPrev = 1 - accumCurr;

            if (accumulating) {
                rayTracingShader.setInt("frameCountWithoutMove", frameCountWithoutMove);

                // Optional Figure 1(h) path:
                // Pass 1 writes the current frame to an offscreen accumulation target.
                glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accumTex[accumCurr], 0);
                glViewport(0, 0, framebufferWidth, framebufferHeight);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                rayTracingShader.setInt("displayOnly", 0);
                // glActiveTexture(GL_TEXTURE0);
                // glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture.textureID);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, accumTex[accumPrev]);
                glBindVertexArray(quad->ID);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            }

            // Pass 2 displays the accumulation target on the default framebuffer.
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            rayTracingShader.setInt("displayOnly", 1);
            glActiveTexture(GL_TEXTURE1);
            int displayTex = accumulating ? accumCurr : (frameCountWithoutMove - 1) % 2;
            glBindTexture(GL_TEXTURE_2D, accumTex[displayTex]);
            glBindVertexArray(quad->ID);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            // Automation capture: the default framebuffer now holds the tonemapped,
            // accumulated result for `frameCountWithoutMove + 1` samples. Capture when
            // that count hits the task's target (the +1/then-increment keeps target==1
            // capturing at frame 0, matching how the reference PNGs were generated).
            if (automationActive && automationTaskIndex < (int)automationTasks.size()) {
                const CaptureTask& task = automationTasks[automationTaskIndex];
                if (frameCountWithoutMove + 1 == task.targetFrames) {
#ifdef HAS_FREEIMAGE
                    FIBITMAP* bmp = captureFramebufferBitmap();
                    if (task.saveIndividual) {
                        saveBitmap(bmp, task.filename.c_str());
                        std::cout << "Saved image: " << task.filename << std::endl;
                    }
                    if (task.collectForComposite) {
                        automationRows.push_back(bmp);
                    } else {
                        FreeImage_Unload(bmp);
                    }
#endif
                    automationTaskIndex++;
                    automationAppliedMode = -1; // force the next task's mode to re-apply
                    if (automationTaskIndex >= (int)automationTasks.size()) {
                        finishAutomation();
                    }
                }
            }

            if (accumulating) frameCountWithoutMove++;
        }
        else {
            // Default direct-render path for students who stop before Figure 1(h).
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, framebufferWidth, framebufferHeight);
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            // glActiveTexture(GL_TEXTURE0);
            // glBindTexture(GL_TEXTURE_CUBE_MAP, skyboxTexture.textureID);
            glBindVertexArray(quad->ID);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
    }

    // optional: de-allocate all resources once they've outlived their purpose:
    // ------------------------------------------------------------------------
    //glDeleteVertexArrays(1,&VAOcube);
    //glDeleteBuffers(1, VBOcube);
    //glDeleteVertexArrays(1, &VAOquad);
    //glDeleteBuffers(1, &VBOquad);

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}

void setToggle(GLFWwindow* window, unsigned int key, bool *value) {
    if (glfwGetKey(window, key) == GLFW_PRESS && !isKeyboardDone[key]) {
        *value = !*value;
        isKeyboardDone[key] = true;
    }
    if (glfwGetKey(window, key) == GLFW_RELEASE) {
        isKeyboardDone[key] = false;
    }
}

void updatePanelMode(GLFWwindow* window) {
    if (showMaterialPanel == prevShowMaterialPanel) {
        return;
    }

    glfwSetInputMode(window, GLFW_CURSOR, showMaterialPanel ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    firstMouse = true;
    prevShowMaterialPanel = showMaterialPanel;
}

// Full set of Disney sliders for one material. Returns true if any value changed.
bool drawMaterialSliders(DisneyMaterialParams& mat) {
    bool changed = false;
    changed |= ImGui::ColorEdit3("Base Color", &mat.baseColor[0]);
    changed |= ImGui::SliderFloat("Roughness", &mat.roughness, 0.001f, 1.0f);
    changed |= ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Specular", &mat.specular, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Specular Tint", &mat.specularTint, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Transmission", &mat.transmission, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("IOR", &mat.ior, 0.2f, 2.5f);
    changed |= ImGui::SliderFloat("Sheen", &mat.sheen, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Sheen Tint", &mat.sheenTint, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Clearcoat", &mat.clearcoat, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Clearcoat Gloss", &mat.clearcoatGloss, 0.0f, 1.0f);
    // Emission can exceed 1.0 (HDR); edit color and intensity separately for usability.
    changed |= ImGui::ColorEdit3("Emission", &mat.emission[0], ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    return changed;
}

// Sampler selection + the PSNR/REF automation controls. Disabled while a sweep runs.
void drawSamplerControls() {
    ImGui::Separator();
    ImGui::Text("Sampling strategy");

    bool busy = (automationType != AUTO_NONE);
    ImGui::BeginDisabled(busy);
    const char* samplerNames[3] = { "Random", "Sobol", "Scrambled Sobol" };
    int prevSampler = samplingMode;
    if (ImGui::Combo("Sampler", &samplingMode, samplerNames, 3) && samplingMode != prevSampler) {
        applySamplingMode(samplingMode);
    }

    if (ImGui::Button("Build Reference (REF)")) startReferenceAutomation();
    ImGui::SameLine();
    if (ImGui::Button("Run PSNR sweep")) startPsnrAutomation();
    if (ImGui::Button("Compare (side-by-side)")) startCompareCapture64();
    ImGui::EndDisabled();

    if (busy) {
        int total = (int)automationTasks.size();
        int done = MIN(automationTaskIndex, total);
        const CaptureTask* task = (automationTaskIndex < total) ? &automationTasks[automationTaskIndex] : nullptr;
        ImGui::Text("Sweep: task %d / %d (%s)", MIN(automationTaskIndex + 1, total), total, currentSamplingModeName());
        float frac = (task && task->targetFrames > 0)
                   ? float(frameCountWithoutMove) / float(task->targetFrames) : 0.0f;
        ImGui::ProgressBar(frac, ImVec2(-1.0f, 0.0f));
        (void)done;
    }
}

// Returns true if a control here changed something that should restart accumulation.
bool drawPanelFooter() {
    bool changed = false;

    ImGui::Separator();
    ImGui::Text("Floor");
    changed |= ImGui::Checkbox("Stripe pattern", &enableFloorStripes);
    if (enableFloorStripes) {
        changed |= ImGui::SliderFloat("Stripe density", &floorStripeCount, 4.0f, 600.0f, "%.0f");
    }

    drawSamplerControls();

    ImGui::Separator();
    const char* modeNames[3] = { "BSDF-only", "NEE-only", "NEE+MIS" };
    ImGui::Text("Render mode: %s   (keys 0/1/2)", modeNames[renderMode]);
    ImGui::Text("Scene: %d   (compile-time)", SCENE);
    ImGui::Text("M: toggle panel");
    return changed;
}

// Scenes 0/1: the original 6-slot named-material editor.
bool drawDefaultMaterialPanel() {
    bool changed = false;
    ImGui::Combo("Material", &selectedEditableMaterial, materialLabels + EDITABLE_MATERIAL_OFFSET, EDITABLE_MATERIAL_COUNT);

    int materialIndex = selectedEditableMaterial + EDITABLE_MATERIAL_OFFSET;
    changed |= drawMaterialSliders(disneyMaterials[materialIndex]);

    if (ImGui::Button("Reset Material")) {
        disneyMaterials[materialIndex] = initialDisneyMaterials[materialIndex];
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All")) {
        for (int i = EDITABLE_MATERIAL_OFFSET; i < MATERIAL_COUNT; i++) {
            disneyMaterials[i] = initialDisneyMaterials[i];
        }
        changed = true;
    }
    changed |= drawPanelFooter();
    return changed;
}

// SCENE 2: edit the 5 named ladder materials + a dedicated roughness-sweep group.
bool drawLadderMaterialPanel() {
    bool changed = false;

    // The five editable materials (ground is non-emissive but tunable here too).
    const char* ladderMatLabels[5] = { "Ground", "Ladder Metal", "Clearcoat", "Emitter (small)", "Emitter (large)" };
    DisneyMaterialParams* ladderMats[5] = {
        &ladderScene.ground, &ladderScene.ladder, &ladderScene.clearcoat,
        &ladderScene.emitterSmall, &ladderScene.emitterLarge
    };
    ImGui::Combo("Material", &selectedEditableMaterial, ladderMatLabels, 5);
    int idx = selectedEditableMaterial < 0 ? 0 : (selectedEditableMaterial > 4 ? 4 : selectedEditableMaterial);
    changed |= drawMaterialSliders(*ladderMats[idx]);
    if (idx == 1) {
        ImGui::TextDisabled("(Ladder roughness is overridden per slot below)");
    }

    ImGui::Separator();
    ImGui::Text("Ladder roughness (L0 sharp -> L4 matte)");
    for (int i = 0; i < LADDER_COUNT; i++) {
        std::string label = "L" + std::to_string(i);
        changed |= ImGui::SliderFloat(label.c_str(), &ladderScene.ladderRoughness[i], 0.001f, 1.0f);
    }

    ImGui::Separator();
    if (ImGui::Button("Reset Scene")) {
        ladderScene = initialLadderScene;
        changed = true;
    }
    changed |= drawPanelFooter();
    return changed;
}

bool drawMaterialPanel() {
    if (!showMaterialPanel) {
        return false;
    }

    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Disney BSDF", &showMaterialPanel)) {
        if (SCENE == SCENE_ROUGHNESS_LADDER) {
            changed = drawLadderMaterialPanel();
        } else {
            changed = drawDefaultMaterialPanel();
        }
    }
    ImGui::End();
    return changed;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    setToggle(window, GLFW_KEY_M, &showMaterialPanel);
    updatePanelMode(window);

    // While a sampling sweep is running, ignore camera / mode input so the user
    // can't perturb the accumulation it owns. ESC and M (panel) stay live.
    if (automationType != AUTO_NONE) {
        return;
    }

    // Render-mode switch (0: BSDF-only, 1: NEE-only, 2: NEE+MIS).
    // Available regardless of panel state; changing mode resets accumulation.
    const int modeKeys[3] = { GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2 };
    for (int m = 0; m < 3; m++) {
        int key = modeKeys[m];
        if (glfwGetKey(window, key) == GLFW_PRESS && !isKeyboardDone[key]) {
            if (renderMode != m) {
                renderMode = m;
                frameCountWithoutMove = 0;
                const char* names[3] = { "BSDF-only", "NEE-only", "NEE+MIS" };
                std::cout << "Render mode: " << m << " (" << names[m] << ")" << std::endl;
            }
            isKeyboardDone[key] = true;
        }
        if (glfwGetKey(window, key) == GLFW_RELEASE) {
            isKeyboardDone[key] = false;
        }
    }

    if (showMaterialPanel) {
        return;
    }

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.ProcessKeyboard(FORWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.ProcessKeyboard(LEFT, deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.ProcessKeyboard(RIGHT, deltaTime);

    if (glfwGetKey(window, GLFW_KEY_K) == GLFW_PRESS) {
        std::cout << "Position" << camera.Position.x << "," << camera.Position.y << "," << camera.Position.z << std::endl;
        std::cout << "Yaw" << camera.Yaw << std::endl;
    }

    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS && isKeyboardDone[GLFW_KEY_V] == false) {
        time_t t = time(NULL);
        struct tm tm = *localtime(&t);
        char date_char[128];
        snprintf(date_char, sizeof(date_char), "%d_%d_%d_%d_%d_%d.png", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        saveImage(date_char);
        isKeyboardDone[GLFW_KEY_V] = true;
    }
    else if (glfwGetKey(window, GLFW_KEY_V) == GLFW_RELEASE) {
        isKeyboardDone[GLFW_KEY_V] = false;
    }
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    framebufferWidth = width;
    framebufferHeight = height;
    if (ENABLE_ACCUMULATION) {
        resizeAccumTargets(width, height);
        frameCountWithoutMove = 0;
    }
    glViewport(0, 0, width, height);
}


// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    if (showMaterialPanel) {
        return;
    }

    if (firstMouse)
    {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

    lastX = xpos;
    lastY = ypos;

    camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (showMaterialPanel) {
        return;
    }

    camera.ProcessMouseScroll(yoffset);
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
    if (width <= 0 || height <= 0 || accumTex[0] == 0) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, accumTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Zero both accumulation textures so the next frame starts a fresh running mean.
// main resets accumulation purely via frameCountWithoutMove = 0, but the automation
// switches samplers mid-run and needs the buffer physically cleared between tasks.
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

// ===========================================================================
//  Sampling-strategy comparison harness (ported from the sobol_perpixel branch)
//  Lets the user A/B the three samplers on main's Disney scenes, and runs an
//  automated PSNR/MSE sweep that quantifies their convergence.
// ===========================================================================

const char* currentSamplingModeName() {
    if (samplingMode == 0) return "Random";
    if (samplingMode == 1) return "Sobol";
    return "Scrambled Sobol";
}

// Switch sampler from the UI: reset accumulation so the change takes effect cleanly.
void applySamplingMode(int mode) {
    if (automationType != AUTO_NONE || mode == samplingMode) return;
    samplingMode = mode;
    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "Sampling mode: " << currentSamplingModeName() << std::endl;
}

// Per-sweep filename prefix so results from different scenes / render modes
// (the two axes we hold fixed during a sweep) never overwrite each other.
static std::string makeOutputTag() {
    return "scene" + std::to_string(SCENE) + "_mode" + std::to_string(renderMode) + "_";
}

#ifdef HAS_FREEIMAGE
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

void saveBitmap(FIBITMAP* bitmap, const char* filename) {
    if (bitmap == nullptr) return;
    FreeImage_Save(FIF_PNG, bitmap, filename, 0);
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
#endif // HAS_FREEIMAGE

void startCompareCapture64() {
    outputTag = makeOutputTag();
    automationType = AUTO_COMPARE64;
    automationTasks.clear();
    automationTasks.push_back({0, 64, 0, "", false, true});
    automationTasks.push_back({1, 64, 0, "", false, true});
    automationTasks.push_back({2, 64, 0, "", false, true});
    automationTaskIndex = 0;
    automationAppliedMode = -1;
    automationPreviousMode = samplingMode;
#ifdef HAS_FREEIMAGE
    releaseBitmaps(automationRows);
#endif
    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "Compare capture started (64 frames per sampler)." << std::endl;
}

void startReferenceAutomation() {
    outputTag = makeOutputTag();
    automationType = AUTO_REF;
    automationTasks.clear();
    automationTaskIndex = 0;
    automationAppliedMode = -1;
    automationPreviousMode = samplingMode;
#ifdef HAS_FREEIMAGE
    releaseBitmaps(automationRows);
#endif

    // Average the three samplers' high-spp renders into one low-noise ground truth.
    automationTasks.push_back({0, referenceSpp, 1, outputTag + "random_ref_2048.png", true, false});
    automationTasks.push_back({1, referenceSpp, 0, outputTag + "sobol_ref_2048.png", true, false});
    automationTasks.push_back({2, referenceSpp, 0, outputTag + "scrambled_ref_2048.png", true, false});

    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "REF automation started (" << outputTag << "). Rendering 3 seed images at "
              << referenceSpp << " spp, then averaging into " << outputTag << "ref_2048.png." << std::endl;
}

void startPsnrAutomation() {
    std::string tag = makeOutputTag();
    std::string refName = tag + "ref_2048.png";
    std::ifstream refCheck(refName.c_str(), std::ios::binary);
    if (!refCheck.good()) {
        std::cout << refName << " not found. Click 'Build Reference (REF)' first "
                  << "(for this scene + render mode)." << std::endl;
        return;
    }

    outputTag = tag;
    automationType = AUTO_PSNR;
    automationTasks.clear();
    automationTaskIndex = 0;
    automationAppliedMode = -1;
    automationPreviousMode = samplingMode;
#ifdef HAS_FREEIMAGE
    releaseBitmaps(automationRows);
#endif

    const char* modeNames[3] = { "random", "sobol", "scrambled" };
    for (int ti = 0; ti < 10; ti++) {
        int spp = psnrFrameTargets[ti];
        for (int mode = 0; mode < 3; mode++) {
            char filename[96];
            snprintf(filename, sizeof(filename), "%s%s_%d.png", outputTag.c_str(), modeNames[mode], spp);
            // Offset Random so its sweep is decorrelated from the Random reference seed.
            int seedOffset = (mode == 0) ? 1 : 0;
            automationTasks.push_back({mode, spp, seedOffset, filename, true, false});
        }
    }

    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
    std::cout << "PSNR automation started (" << outputTag << "). Rendering all 3 samplers up to 512 spp." << std::endl;
}

void finishAutomation() {
    if (automationType == AUTO_COMPARE64) {
#ifdef HAS_FREEIMAGE
        if (!automationRows.empty()) {
            std::string composite = outputTag + "compare_64.png";
            saveVerticalComposite(automationRows, composite.c_str());
            std::cout << "Saved " << composite << std::endl;
            releaseBitmaps(automationRows);
        }
#endif
        std::cout << "Compare capture done." << std::endl;
    }
    else if (automationType == AUTO_PSNR) {
        std::cout << "All PSNR input images rendered. Generating graphs..." << std::endl;
        runPsnrGraphScript();
        std::cout << "PSNR run done. Output: " << outputTag << "mse_vs_spp.png, "
                  << outputTag << "psnr_vs_spp.png, " << outputTag << "psnr_results.csv" << std::endl;
    }
    else if (automationType == AUTO_REF) {
        std::cout << "All reference seed images rendered. Building " << outputTag << "ref_2048.png..." << std::endl;
        runReferenceBuildScript();
        std::cout << "REF run done. Output: " << outputTag << "ref_2048.png" << std::endl;
    }

    samplingMode = automationPreviousMode;
    automationTasks.clear();
    automationTaskIndex = 0;
    automationAppliedMode = -1;
    automationType = AUTO_NONE;
    clearAccumTargets(framebufferWidth, framebufferHeight);
    frameCountWithoutMove = 0;
}

// The Python scripts take the scene/mode tag as argv[1] and prefix it onto every
// file they read/write, so per-scene/per-mode outputs stay separated. They are
// written next to the captured PNGs in the program's working directory (build/).
void ensureReferenceScriptExists() {
    std::ofstream out("generate_ref_image.py", std::ios::binary);
    out << R"PY(import os
import sys
from PIL import Image
import numpy as np

PREFIX = sys.argv[1] if len(sys.argv) > 1 else ''
REFERENCE_FILES = [
    PREFIX + 'random_ref_2048.png',
    PREFIX + 'sobol_ref_2048.png',
    PREFIX + 'scrambled_ref_2048.png',
]
REF_PATH = PREFIX + 'ref_2048.png'


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

    print(f'Generated {REF_PATH} from the three seed images.')
    print('Deleted intermediate reference images.')


if __name__ == '__main__':
    main()
)PY";
}

void runReferenceBuildScript() {
    ensureReferenceScriptExists();
    std::string cmd = PYTHON_CMD " generate_ref_image.py " + outputTag;
    int result = std::system(cmd.c_str());
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
import sys
from PIL import Image
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

PREFIX = sys.argv[1] if len(sys.argv) > 1 else ''
SPPS = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]
METHODS = {
    'Random': PREFIX + 'random_{}.png',
    'Sobol': PREFIX + 'sobol_{}.png',
    'Scrambled Sobol': PREFIX + 'scrambled_{}.png',
}
REF_PATH = PREFIX + 'ref_2048.png'


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
        raise FileNotFoundError(f'Missing reference image: {REF_PATH}. Build the reference first.')

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

    with open(PREFIX + 'psnr_results.csv', 'w', newline='') as f:
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
    plt.savefig(PREFIX + 'mse_vs_spp.png', dpi=200)
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
    plt.savefig(PREFIX + 'psnr_vs_spp.png', dpi=200)
    plt.close()

    cleanup_intermediate_images()
    print(f'Generated: {PREFIX}mse_vs_spp.png, {PREFIX}psnr_vs_spp.png, {PREFIX}psnr_results.csv')
    print('Deleted intermediate sampling images. Kept the reference image.')


if __name__ == '__main__':
    main()
)PY";
}

void runPsnrGraphScript() {
    ensurePsnrScriptExists();
    std::string cmd = PYTHON_CMD " generate_psnr_graphs.py " + outputTag;
    int result = std::system(cmd.c_str());
    if (result != 0) {
        std::cout << "Warning: failed to run generate_psnr_graphs.py (exit code " << result << ")" << std::endl;
        std::cout << "Make sure python3, Pillow, numpy, and matplotlib are available." << std::endl;
    }
}
