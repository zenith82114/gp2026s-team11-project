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
#include "opengl_utils.h"
#include <iostream>
#include <string>
#include <vector>
#include "camera.h"
#include "texture.h"
#include "texture_cube.h"
#include "model.h"
#include "mesh.h"
#ifdef HAS_FREEIMAGE
#include "FreeImage.h"
#endif
#include <time.h>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
void initAccumTargets(int width, int height);
void resizeAccumTargets(int width, int height);
void updatePanelMode(GLFWwindow* window);
void uploadDisneyMaterials(Shader& shader);
bool drawMaterialPanel();

bool isWindowed = true;
bool isKeyboardDone[1024] = { 0 };
bool showMaterialPanel = false;
bool prevShowMaterialPanel = false;
int selectedEditableMaterial = 0;

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
const int MAX_FRAME_COUNT_WITHOUT_MOVE = 4; // cap accumulation for equal-sample experiments
int frameCountWithoutMove = 0;

// render mode, switched at runtime with keys 0/1/2
// 0: BSDF-only, 1: NEE-only, 2: NEE+MIS
int renderMode = 2;

// validation scene ID (compile-time; rebuild to switch)
//   0: small distant emitter — NEE wins
//   1: large close emitter    — MIS wins
const int SCENE = 0;
const int SCENE_SMALL_EMITTER = 0;
const int SCENE_LARGE_EMITTER = 1;

// Optional accumulation targets for Figure 1(h).
unsigned int accumFBO = 0;
unsigned int accumTex[2] = { 0, 0 };

struct DisneyMaterialParams {
    glm::vec3 baseColor;
    float roughness;
    float metallic;
    float specular;
    float specularTint;
    float transmission;
    float ior;
    float sheen;
    float sheenTint;
    float clearcoat;
    float clearcoatGloss;
    glm::vec3 emission; // black = non-emitter
};

// Six material slots; the 6th (material_sphere_diffuse) was added by the NEE scenes.
// Slot order matches the shader's setupScene() slot-to-uniform mapping.
const int MATERIAL_COUNT = 6;
const int EDITABLE_MATERIAL_OFFSET = 1; // ground (slot 0) is not user-editable
const int EDITABLE_MATERIAL_COUNT = MATERIAL_COUNT - EDITABLE_MATERIAL_OFFSET;
const char* materialLabels[MATERIAL_COUNT] = {
    "Ground",
    "Emitter Sphere",
    "Glass Outer",
    "Glass Inner",
    "Metal Sphere",
    "Diffuse Sphere"
};
const char* materialUniforms[MATERIAL_COUNT] = {
    "material_ground",
    "material_sphere_middle",
    "material_sphere_left",
    "material_inside_left",
    "material_sphere_right",
    "material_sphere_diffuse"
};

// Per-scene initial materials. Disney equivalents of the old type-based materials:
//   metal  -> metallic=1, low roughness;  glass -> transmission=1, ior 1.5 (bubble 1/1.5).
// Layout per row: baseColor, roughness, metallic, specular, specularTint,
//                 transmission, ior, sheen, sheenTint, clearcoat, clearcoatGloss, emission.
const DisneyMaterialParams initialDisneyMaterialsByScene[2][MATERIAL_COUNT] = {
    // SCENE 0: small distant emitter (NEE wins)
    {
        { glm::vec3(0.8f, 0.8f, 0.0f), 0.65f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },            // ground diffuse
        { glm::vec3(0.0f),             0.65f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(4.0f) },            // small emitter
        { glm::vec3(0.8f, 0.8f, 0.8f), 0.03f, 0.0f, 0.8f, 0.0f, 1.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },            // glass
        { glm::vec3(0.9f, 0.9f, 1.0f), 0.03f, 0.0f, 0.8f, 0.0f, 1.0f, 0.667f, 0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },            // glass bubble (ior 1/1.5)
        { glm::vec3(0.8f, 0.6f, 0.2f), 0.15f, 1.0f, 0.5f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.2f, 0.6f, glm::vec3(0.0f) },            // gold metal
        { glm::vec3(0.7f, 0.2f, 0.2f), 0.6f,  0.0f, 0.4f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) }             // red diffuse occluder
    },
    // SCENE 1: large close emitter (MIS wins)
    {
        { glm::vec3(0.8f, 0.8f, 0.0f), 0.65f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },            // ground diffuse
        { glm::vec3(0.0f),             0.65f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(2.0f, 1.9f, 1.6f) },// BIG emitter, close
        { glm::vec3(0.9f, 0.9f, 0.9f), 0.03f, 0.0f, 0.8f, 0.0f, 1.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },            // glass
        { glm::vec3(0.9f, 0.9f, 1.0f), 0.03f, 0.0f, 0.8f, 0.0f, 1.0f, 0.667f, 0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },            // glass bubble
        { glm::vec3(0.95f, 0.95f, 0.95f), 0.02f, 1.0f, 0.5f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f, 0.2f, 0.6f, glm::vec3(0.0f) },           // near-mirror metal
        { glm::vec3(0.7f, 0.2f, 0.2f), 0.6f,  0.0f, 0.4f, 0.0f, 0.0f, 1.5f,   0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) }             // red diffuse, lit by emitter
    }
};
// Active editable copy, initialized from the chosen SCENE at startup.
const DisneyMaterialParams* initialDisneyMaterials = initialDisneyMaterialsByScene[SCENE];
DisneyMaterialParams disneyMaterials[MATERIAL_COUNT];

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
    for (int i = 0; i < MATERIAL_COUNT; i++) {
        disneyMaterials[i] = initialDisneyMaterials[i];
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

    rayTracingShader.setInt("SCENE", SCENE);

    uploadDisneyMaterials(rayTracingShader);

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
            uploadDisneyMaterials(rayTracingShader);
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

        if (ENABLE_ACCUMULATION) {
            int fbW, fbH;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            if (fbW != framebufferWidth || fbH != framebufferHeight) {
                framebufferWidth = fbW;
                framebufferHeight = fbH;
                resizeAccumTargets(framebufferWidth, framebufferHeight);
                frameCountWithoutMove = 0;
            }

            if (viewMatBefore != viewMatNow || zoomBefore != camera.Zoom) {
                frameCountWithoutMove = 0;
                viewMatBefore = viewMatNow;
                zoomBefore = camera.Zoom;
            }

            // Once the sample cap is hit, stop tracing new samples and just keep
            // displaying the converged result (lets equal-sample experiments settle).
            bool accumulating = frameCountWithoutMove < MAX_FRAME_COUNT_WITHOUT_MOVE;
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

void uploadDisneyMaterial(Shader& shader, const char* uniformName, const DisneyMaterialParams& mat) {
    std::string name(uniformName);
    shader.setInt(name + ".material_type", 0);
    shader.setVec3(name + ".albedo", mat.baseColor);
    shader.setVec3(name + ".baseColor", mat.baseColor);
    shader.setFloat(name + ".roughness", mat.roughness);
    shader.setFloat(name + ".metallic", mat.metallic);
    shader.setFloat(name + ".specular", mat.specular);
    shader.setFloat(name + ".specularTint", mat.specularTint);
    shader.setFloat(name + ".transmission", mat.transmission);
    shader.setFloat(name + ".ior", mat.ior);
    shader.setFloat(name + ".sheen", mat.sheen);
    shader.setFloat(name + ".sheenTint", mat.sheenTint);
    shader.setFloat(name + ".clearcoat", mat.clearcoat);
    shader.setFloat(name + ".clearcoatGloss", mat.clearcoatGloss);
    shader.setFloat(name + ".fuzz", mat.roughness);
    shader.setVec3(name + ".emission", mat.emission);
}

void uploadDisneyMaterials(Shader& shader) {
    for (int i = 0; i < MATERIAL_COUNT; i++) {
        uploadDisneyMaterial(shader, materialUniforms[i], disneyMaterials[i]);
    }
}

bool drawMaterialPanel() {
    if (!showMaterialPanel) {
        return false;
    }

    bool changed = false;
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Disney BSDF", &showMaterialPanel)) {
        ImGui::Combo("Material", &selectedEditableMaterial, materialLabels + EDITABLE_MATERIAL_OFFSET, EDITABLE_MATERIAL_COUNT);

        int materialIndex = selectedEditableMaterial + EDITABLE_MATERIAL_OFFSET;
        DisneyMaterialParams& mat = disneyMaterials[materialIndex];
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

        if (ImGui::Button("Reset Material")) {
            mat = initialDisneyMaterials[materialIndex];
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset All")) {
            for (int i = EDITABLE_MATERIAL_OFFSET; i < MATERIAL_COUNT; i++) {
                disneyMaterials[i] = initialDisneyMaterials[i];
            }
            changed = true;
        }
        ImGui::Separator();
        const char* modeNames[3] = { "BSDF-only", "NEE-only", "NEE+MIS" };
        ImGui::Text("Render mode: %s   (keys 0/1/2)", modeNames[renderMode]);
        ImGui::Text("Scene: %d   (compile-time)", SCENE);
        ImGui::Text("M: toggle panel");
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
