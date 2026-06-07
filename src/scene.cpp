#include "scene.h"
#include "shader.h"
#include <string>

// --- Scenes 0/1: 6-slot named materials ------------------------------------
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
// Scenes 0/1 use this 6-slot machinery; scene 2 (the ladder) uses ladderScene below instead.
// SCENE 2 has no entry here, so guard the index to avoid reading out of bounds.
const DisneyMaterialParams* const initialDisneyMaterials =
    initialDisneyMaterialsByScene[SCENE < 2 ? SCENE : 0];
DisneyMaterialParams disneyMaterials[MATERIAL_COUNT];

// --- Scene 2: roughness ladder ---------------------------------------------
// Initial ladder setup. Layout per DisneyMaterialParams row:
//   baseColor, roughness, metallic, specular, specularTint,
//   transmission, ior, sheen, sheenTint, clearcoat, clearcoatGloss, emission.
const LadderSceneParams initialLadderScene = {
    // ground - neutral gray diffuse so reflections/color bleed stay legible
    { glm::vec3(0.5f, 0.5f, 0.5f), 0.8f, 0.0f, 0.3f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(0.0f) },
    // ladder - shared warm-gold metal; .roughness is replaced per slot by ladderRoughness[]
    { glm::vec3(0.9f, 0.85f, 0.75f), 0.1f, 1.0f, 0.5f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f, 0.0f, 0.6f, glm::vec3(0.0f) },
    // clearcoat - matte red base + sharp coat (two-lobe highlight a non-Disney model can't do)
    { glm::vec3(0.6f, 0.05f, 0.05f), 0.5f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f, 1.0f, 0.9f, glm::vec3(0.0f) },
    // small emitter - bright warm, small radius => NEE-favored
    { glm::vec3(0.0f), 0.5f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(8.0f, 7.0f, 5.0f) },
    // large emitter - dimmer cool, large radius => MIS-favored
    { glm::vec3(0.0f), 0.5f, 0.0f, 0.4f, 0.0f, 0.0f, 1.5f, 0.0f, 0.5f, 0.0f, 0.5f, glm::vec3(1.8f, 1.9f, 2.2f) },
    { 0.02f, 0.10f, 0.25f, 0.45f, 0.70f }
};
LadderSceneParams ladderScene;

// --- Uniform upload --------------------------------------------------------
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

void uploadLadderMaterials(Shader& shader) {
    uploadDisneyMaterial(shader, "material_ground", ladderScene.ground);
    uploadDisneyMaterial(shader, "material_ladder", ladderScene.ladder);
    uploadDisneyMaterial(shader, "material_clearcoat", ladderScene.clearcoat);
    uploadDisneyMaterial(shader, "material_emitter_small", ladderScene.emitterSmall);
    uploadDisneyMaterial(shader, "material_emitter_large", ladderScene.emitterLarge);
    for (int i = 0; i < LADDER_COUNT; i++) {
        shader.setFloat("ladderRoughness[" + std::to_string(i) + "]", ladderScene.ladderRoughness[i]);
    }
}
