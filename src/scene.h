#ifndef SCENE_H
#define SCENE_H

#include <glm/glm.hpp>

// Scene data for the path tracer's validation scenes.
// This header owns the material/scene *data* model and the uniform-upload interface;
// the live ImGui editing UI lives in main.cpp (it touches UI-only globals).
//
// The shader's setupScene() owns the geometry (sphere centers/radii) per scene;
// here we only describe the materials those spheres reference and push them as uniforms.

class Shader; // upload functions take a Shader&; avoid pulling shader.h into this header

// --- Scene selection -------------------------------------------------------
// Compile-time scene ID (rebuild to switch). Mirrors the shader's `uniform int SCENE`.
//   0: small distant emitter - NEE wins
//   1: large close emitter    - MIS wins
//   2: roughness ladder        - both NEE/MIS and the Disney roughness sweep in one frame
constexpr int SCENE_SMALL_EMITTER    = 0;
constexpr int SCENE_LARGE_EMITTER    = 1;
constexpr int SCENE_ROUGHNESS_LADDER = 2;
constexpr int SCENE = SCENE_ROUGHNESS_LADDER;

// --- Disney material parameters --------------------------------------------
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

// --- Scenes 0/1: the original 6-slot named-material scheme ------------------
// Six material slots; the 6th (material_sphere_diffuse) was added by the NEE scenes.
// Slot order matches the shader's setupScene() slot-to-uniform mapping.
constexpr int MATERIAL_COUNT = 6;
constexpr int EDITABLE_MATERIAL_OFFSET = 1; // ground (slot 0) is not user-editable
constexpr int EDITABLE_MATERIAL_COUNT = MATERIAL_COUNT - EDITABLE_MATERIAL_OFFSET;

extern const char* materialLabels[MATERIAL_COUNT];
extern const char* materialUniforms[MATERIAL_COUNT];

// Per-scene initial materials for scenes 0 and 1.
extern const DisneyMaterialParams initialDisneyMaterialsByScene[2][MATERIAL_COUNT];
// Initial material set for the active scene (scenes 0/1; falls back to scene 0 otherwise).
extern const DisneyMaterialParams* const initialDisneyMaterials;
// Mutable working copy edited by the ImGui panel and uploaded each change.
extern DisneyMaterialParams disneyMaterials[MATERIAL_COUNT];

// --- Scene 2: roughness ladder ---------------------------------------------
// Distinct from the 6-slot scheme above: 5 named materials drive 9 spheres, and the
// five ladder spheres share one metal whose roughness is swept by ladderRoughness[].
constexpr int LADDER_COUNT = 5;

struct LadderSceneParams {
    DisneyMaterialParams ground;
    DisneyMaterialParams ladder;        // shared metal; roughness overridden per slot at trace time
    DisneyMaterialParams clearcoat;
    DisneyMaterialParams emitterSmall;
    DisneyMaterialParams emitterLarge;
    float ladderRoughness[LADDER_COUNT];
};

extern const LadderSceneParams initialLadderScene;
extern LadderSceneParams ladderScene; // mutable working copy

// --- Uniform upload --------------------------------------------------------
// Push a single Disney material into the named shader uniform struct.
void uploadDisneyMaterial(Shader& shader, const char* uniformName, const DisneyMaterialParams& mat);
// Scenes 0/1: upload the 6 slot materials.
void uploadDisneyMaterials(Shader& shader);
// Scene 2: upload the 5 named ladder materials + the per-slot roughness array.
void uploadLadderMaterials(Shader& shader);

#endif // SCENE_H
