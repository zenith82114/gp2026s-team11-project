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
#include "camera.h"
#include "texture.h"
#include "texture_cube.h"
#include "model.h"
#include "mesh.h"
#include "FreeImage.h"
#include <time.h>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow* window);
void initAccumTargets(int width, int height);
void resizeAccumTargets(int width, int height);

bool isWindowed = true;
bool isKeyboardDone[1024] = { 0 };

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
const int MAX_FRAME_COUNT_WITHOUT_MOVE = 256;
int frameCountWithoutMove = 0;

// Optional accumulation targets for Figure 1(h).
unsigned int accumFBO = 0;
unsigned int accumTex[2] = { 0, 0 };

// Save Image to png file. press V key.
// file name : date.png (created in bin folder)
// Install FreeImage to use this function. (dlls, includes, lib)
void saveImage(const char* filename) {
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
}

int main()
{
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


    // Set materials. You can change this.
    rayTracingShader.setVec3("material_ground.albedo", glm::vec3(0.8, 0.8, 0.0));
    rayTracingShader.setInt("material_ground.material_type", 0); // diffuse
    rayTracingShader.setVec3("material_ground.emission", glm::vec3(0.0f));

    rayTracingShader.setVec3("material_sphere_middle.albedo", glm::vec3(0.3, 0.3, 0.8));
    rayTracingShader.setInt("material_sphere_middle.material_type", 0); // diffuse
    rayTracingShader.setVec3("material_sphere_middle.emission", glm::vec3(4.0f, 4.0f, 4.0f)); // emitter

    rayTracingShader.setVec3("material_sphere_left.albedo", glm::vec3(0.8, 0.8, 0.8));
    rayTracingShader.setInt("material_sphere_left.material_type", 2); // refractive
    rayTracingShader.setFloat("material_sphere_left.ior", 1.5f);
    rayTracingShader.setFloat("material_sphere_left.fuzz", 0.3f);
    rayTracingShader.setVec3("material_sphere_left.emission", glm::vec3(0.0f));

    rayTracingShader.setVec3("material_inside_left.albedo", glm::vec3(0.9, 0.9, 1.0));
    rayTracingShader.setInt("material_inside_left.material_type", 2); // refractive
    rayTracingShader.setFloat("material_inside_left.ior", 1.0f / 1.5f);
    rayTracingShader.setFloat("material_inside_left.fuzz", 0.3f);
    rayTracingShader.setVec3("material_inside_left.emission", glm::vec3(0.0f));

    rayTracingShader.setVec3("material_sphere_right.albedo", glm::vec3(0.8, 0.6, 0.2));
    rayTracingShader.setInt("material_sphere_right.material_type", 1); // reflective
    rayTracingShader.setFloat("material_sphere_right.fuzz", 1.0f);
    rayTracingShader.setVec3("material_sphere_right.emission", glm::vec3(0.0f));

    rayTracingShader.setVec3("material_sphere_diffuse.albedo", glm::vec3(0.7, 0.2, 0.2));
    rayTracingShader.setInt("material_sphere_diffuse.material_type", 0); // diffuse
    rayTracingShader.setVec3("material_sphere_diffuse.emission", glm::vec3(0.0f));

    glm::mat4 viewMatBefore = camera.GetViewMatrix();
    float zoomBefore = camera.Zoom;

    while (!glfwWindowShouldClose(window))// render loop
    {
        rayTracingShader.use();
        rayTracingShader.setFloat("H", framebufferHeight);
        rayTracingShader.setFloat("W", framebufferWidth);
        rayTracingShader.setFloat("fovY", glm::radians(camera.Zoom));
        rayTracingShader.setVec3("cameraPosition", camera.Position);
        glm::mat4 viewMatNow = camera.GetViewMatrix();
        rayTracingShader.setMat3("cameraToWorldRotMatrix", glm::transpose(glm::mat3(viewMatNow)));

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

            bool accumulating = frameCountWithoutMove < MAX_FRAME_COUNT_WITHOUT_MOVE;
            int accumCurr = frameCountWithoutMove % 2;

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
                glBindTexture(GL_TEXTURE_2D, accumTex[1 - accumCurr]);
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

        // input
        processInput(window);

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // optional: de-allocate all resources once they've outlived their purpose:
    // ------------------------------------------------------------------------
    //glDeleteVertexArrays(1,&VAOcube);
    //glDeleteBuffers(1, VBOcube);
    //glDeleteVertexArrays(1, &VAOquad);
    //glDeleteBuffers(1, &VBOquad);

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
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

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

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
        sprintf(date_char, "%d_%d_%d_%d_%d_%d.png", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
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
