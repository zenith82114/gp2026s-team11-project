#version 330
out vec4 FragColor;

in vec2 TexCoords;

// You can change the code whatever you want

const int MAX_DEPTH = 10; // maximum bounce
const float PI = 3.14159265359;

const int RAYS_PER_FRAME_PIXEL = 4;

// Render mode selector for NEE/MIS validation (set at runtime from main.cpp; keys 0/1/2)
//   0: BSDF-only      — pure path tracer, lit only by BSDF rays hitting emitters / sky
//   1: NEE-only       — direct estimate at the first non-delta hit, then terminate
//   2: full (NEE+MIS) — direct estimate at every non-delta hit AND keep bouncing
uniform int RENDER_MODE;
const int mode_bsdf_only = 0;
const int mode_nee_only  = 1;
const int mode_full      = 2;

// Optional for Figure 1(h): accumulation support.
uniform sampler2D accumPrev;
uniform int frameCountWithoutMove;
uniform bool displayOnly;

struct Ray {
    vec3 origin;
    vec3 direction;
};

struct Material {
    int material_type;

    vec3 albedo;
    vec3 baseColor;
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
    float fuzz;

    vec3 emission; // radiance emitted by the surface (black = non-emitter)
};

const int bsdf_diffuse = 1;
const int bsdf_glossy = 2;
const int bsdf_specular = 4;
const int bsdf_transmission = 8;

struct BSDFSample {
    vec3 wi;
    vec3 f;
    float pdf;
    int flags;
    bool valid;
};

// Just consider Light as a fixed environment light

// hit information
struct HitRecord {
    float t;        // distance to hit point
    vec3 p;         // hit point
    vec3 normal;    // hit point normal
    bool frontFace; // whether the ray hits the front face
    Material mat;   // hit point material
};

// Geometry
struct Sphere {
    vec3 center;
    float radius;
    Material mat;
};

uniform Material material_ground;
uniform Material material_sphere_middle;
uniform Material material_sphere_left;
uniform Material material_sphere_right;
uniform Material material_inside_left;
uniform Material material_sphere_diffuse;

// Roughness-ladder scene (SCENE 2) materials.
// The five ladder spheres share `material_ladder`; only roughness varies, per `ladderRoughness[i]`.
const int NUM_LADDER = 5;
uniform Material material_ladder;        // shared metal for the whole ladder
uniform float ladderRoughness[NUM_LADDER]; // per-slot roughness override (the swept parameter)
uniform Material material_clearcoat;     // colored base + sharp coat showcase
uniform Material material_emitter_small; // small/bright warm light (NEE-favored)
uniform Material material_emitter_large; // large/close cool light (MIS-favored)

// Active validation scene, driven by `const int SCENE` in main.cpp.
//   0: small distant emitter  — NEE wins
//   1: large close emitter    — MIS wins
//   2: roughness ladder        — both features in one frame
uniform int SCENE;
const int scene_small_emitter    = 0;
const int scene_large_emitter    = 1;
const int scene_roughness_ladder = 2;

// SCENE 2 uses the most spheres: ground + 5 ladder + clearcoat + 2 emitters = 9.
const int NUM_SPHERES = 9;
Sphere spheres[NUM_SPHERES];

// Populate the global `spheres[]` for the active SCENE. Call once before tracing.
// Slot-to-material mapping is kept identical across scenes so main.cpp can reuse the same uniform names;
// only centers/radii (and the chosen materials) differ.
// A degenerate, far-away non-emissive sphere used to pad unused slots so that
// scenes using fewer than NUM_SPHERES don't trace undefined geometry.
// radius 0 => no real intersection; trace()/directLight() skip it harmlessly.
Sphere nullSphere() {
    return Sphere(vec3(0.0, -1e6, 0.0), 0.0, material_ground);
}

void setupScene() {
    // Default every slot to a harmless null sphere; each scene overwrites the slots it uses.
    for (int i = 0; i < NUM_SPHERES; i++) {
        spheres[i] = nullSphere();
    }

    if (SCENE == scene_roughness_ladder) {
        // Veach-style roughness ladder: a row of metal spheres whose roughness climbs from
        // near-mirror to matte, facing two emitters of different size. Across the row, no single
        // sampling strategy (BSDF vs. NEE) stays clean — that's the MIS demonstration — and the
        // row itself is a direct Disney GGX roughness sweep.
        spheres[0] = Sphere(vec3(0,-100.5,-1), 100, material_ground);          // neutral diffuse ground

        // Ladder L0..L4: shared metal, only roughness varies per slot.
        for (int i = 0; i < NUM_LADDER; i++) {
            Material m = material_ladder;
            m.roughness = ladderRoughness[i];
            float x = -2.0 + float(i) * 1.0; // -2, -1, 0, 1, 2
            spheres[1 + i] = Sphere(vec3(x, -0.1, -1.2), 0.4, m);
        }

        spheres[6] = Sphere(vec3(0.0, -0.1, -0.3), 0.4, material_clearcoat);   // clearcoat showcase, up front
        spheres[7] = Sphere(vec3(-1.5, 1.6, 0.3), 0.15, material_emitter_small); // small bright warm light
        spheres[8] = Sphere(vec3(1.6, 1.4, 0.5), 0.6, material_emitter_large);   // large dim cool light
    } else if (SCENE == scene_large_emitter) {
        // Large emitter close to the diffuse + metal spheres, subtending a wide cone.
        spheres[0] = Sphere(vec3(0,-100.5,-1), 100,  material_ground);         // ground, diffuse
        spheres[1] = Sphere(vec3(-0.9,1.1,-1.2), 0.9, material_sphere_middle); // BIG emitter, close
        spheres[2] = Sphere(vec3(-1.4,0,-0.2), 0.5, material_sphere_left);     // glass
        spheres[3] = Sphere(vec3(-1.4,0,-0.2), 0.45, material_inside_left);    // glass bubble
        spheres[4] = Sphere(vec3(1,0,-1), 0.5, material_sphere_right);         // near-mirror metal
        spheres[5] = Sphere(vec3(0.1,0,-0.2), 0.5, material_sphere_diffuse);   // diffuse, lit by emitter
    } else {
        // Original small/distant emitter scene.
        spheres[0] = Sphere(vec3(0,-100.5,-1), 100, material_ground);          // diffuse(Lambertian)
        spheres[1] = Sphere(vec3(0,2,-1), 0.2, material_sphere_middle);        // small emitter
        spheres[2] = Sphere(vec3(-1,0,-1), 0.5, material_sphere_left);         // refractive
        spheres[3] = Sphere(vec3(-1,0,-1), 0.45, material_inside_left);        // refractive
        spheres[4] = Sphere(vec3(1,0,-1), 0.5, material_sphere_right);         // reflective
        spheres[5] = Sphere(vec3(-0.2,0,0), 0.5, material_sphere_diffuse);     // diffuse occluder
    }
}


// Math functions
/* returns a varying number between 0 and 1 */
float rand(vec2 co) {
  return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

vec3 rand_unit_vec3(vec2 seed) {
    vec3 r = vec3(rand(seed), rand(seed + 1.7), rand(seed + 3.3)) * 2.0 - 1.0;
    return normalize(r);
}

float max3 (vec3 v) {
  return max (max (v.x, v.y), v.z);
}

float min3 (vec3 v) {
  return min (min (v.x, v.y), v.z);
}

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

float schlickWeight(float cosine) {
    float m = saturate(1.0 - cosine);
    return m * m * m * m * m;
}

float luminance(vec3 c) {
    return dot(c, vec3(0.3, 0.6, 0.1));
}

vec3 tintColor(vec3 color) {
    float lum = luminance(color);
    return lum > 0.0 ? color / lum : vec3(1.0);
}

vec3 tangentFromNormal(vec3 n) {
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    return normalize(cross(up, n));
}

vec3 toWorld(vec3 localDir, vec3 n) {
    vec3 t = tangentFromNormal(n);
    vec3 b = cross(n, t);
    return normalize(localDir.x * t + localDir.y * b + localDir.z * n);
}

vec3 cosineSampleHemisphere(vec2 u, vec3 n) {
    float r = sqrt(u.x);
    float phi = 2.0 * PI * u.y;
    vec3 localDir = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u.x)));
    return toWorld(localDir, n);
}

float D_GGX(float NoH, float alpha) {
    float a2 = alpha * alpha;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float smithG1_GGX(float NoV, float alpha) {
    float a = alpha;
    float a2 = a * a;
    float b = NoV * NoV;
    return 2.0 * NoV / max(NoV + sqrt(a2 + b - a2 * b), 1e-6);
}

vec3 sampleGGX(vec2 u, float alpha, vec3 n) {
    float a2 = alpha * alpha;
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt(max(0.0, (1.0 - u.y) / max(1.0 + (a2 - 1.0) * u.y, 1e-6)));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    return toWorld(vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta), n);
}


uniform vec3 cameraPosition;
uniform mat3 cameraToWorldRotMatrix;
uniform float fovY; //set to 45
uniform float H;
uniform float W;

Ray getRay(vec2 uv, int sampleId) {
    float halfH = tan(fovY * 0.5);
    float halfW = (W / H) * halfH;
    vec2 ndc = 2.0 * uv - 1.0;
    vec3 dirCam = vec3(ndc.x * halfW, ndc.y * halfH, -1.0);
    vec3 dirWorld = normalize(cameraToWorldRotMatrix * dirCam);
    vec3 randNoise = rand_unit_vec3(ndc + float(sampleId) + 17.0 * float(frameCountWithoutMove));
    dirWorld = normalize(dirWorld + 1e-4 * randNoise);
    return Ray(cameraPosition, dirWorld);
}

const float bias = 0.0001; // to prevent point too close to surface.

bool sphereIntersect(Sphere sp, Ray r, inout HitRecord hit){
    vec3 oc = r.origin - sp.center;
    float a = dot(r.direction, r.direction);
    float halfB = dot(oc, r.direction);
    float c = dot(oc, oc) - sp.radius * sp.radius;
    float disc = halfB * halfB - a * c;
    if (disc < 0.0) return false;

    float sqrtDisc = sqrt(disc);
    float t = (-halfB - sqrtDisc) / a;
    if (t < bias || t >= hit.t) {
        t = (-halfB + sqrtDisc) / a;
        if (t < bias || t >= hit.t) return false;
    }

    hit.t = t;
    hit.p = r.origin + t * r.direction;
    vec3 outwardNormal = (hit.p - sp.center) / sp.radius;
    hit.frontFace = dot(r.direction, outwardNormal) < 0.0;
    hit.normal = hit.frontFace ? outwardNormal : -outwardNormal;
    hit.mat = sp.mat;
    return true;
}

float fresnelDielectricSchlick(float cosine, float ior) {
    float f0 = (1.0 - ior) / (1.0 + ior);
    f0 = f0 * f0;
    return f0 + (1.0 - f0) * schlickWeight(cosine);
}

bool trace(Ray r, out HitRecord hit){
    hit.t = 1.0 / 0.0; // +inf
    bool anyHit = false;
    for (int i = 0; i < spheres.length(); i++) {
        if (sphereIntersect(spheres[i], r, hit)) {
            anyHit = true;
        }
    }
    return anyHit;
}

vec3 skyColor(Ray ray) {
    vec3 dir = normalize(ray.direction);
    float a = 0.5 * (dir.y + 1.0);
    // Dimmed so the emissive spheres dominate the lighting (NEE/MIS validation needs a dark env).
    return 0.01 * ((1.0 - a) * vec3(1.0) + a * vec3(0.5, 0.7, 1.0));
}

// Build an orthonormal basis whose +z axis is w (Duff et al. 2017, branchless).
void buildONB(vec3 w, out vec3 u, out vec3 v) {
    float sign = w.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sign + w.z);
    float b = w.x * w.y * a;
    u = vec3(1.0 + sign * w.x * w.x * a, sign * b, -sign * w.x);
    v = vec3(b, sign + w.y * w.y * a, -w.y);
}

// Solid-angle pdf of sampling direction `wi` from point `p` towards sphere `sp`,
// under the uniform-cone scheme used by sampleSphereLight().
// Returns 0 if `wi` misses the cone subtended by the sphere, or if `p` is inside the sphere.
// Handles arbitrary wi (not just sampled ones) so the same formula serves MIS.
float spherePdfLight(Sphere sp, vec3 p, vec3 wi) {
    vec3 wc = sp.center - p;
    float dc2 = dot(wc, wc);
    float r2 = sp.radius * sp.radius;
    if (dc2 <= r2) return 0.0; // shading point inside the light: cone undefined
    float cosThetaMax = sqrt(1.0 - r2 / dc2);
    // wi must lie within the cone around the direction to the center
    if (dot(normalize(wi), normalize(wc)) < cosThetaMax) return 0.0;
    float solidAngle = 2.0 * PI * (1.0 - cosThetaMax);
    return 1.0 / solidAngle;
}

// Uniformly sample a direction within the cone subtended by sphere `sp` as seen from `p`.
// Returns the (normalized) direction in `wi`, distance to the sampled point in `dist`,
// and the solid-angle pdf in `pdf`.
// pdf == 0 signals an invalid sample (i.e., p inside sphere).
void sampleSphereLight(Sphere sp, vec3 p, vec2 seed, out vec3 wi, out float dist, out float pdf) {
    vec3 wc = sp.center - p;
    float dc2 = dot(wc, wc);
    float dc = sqrt(dc2);
    float r2 = sp.radius * sp.radius;
    if (dc2 <= r2) { pdf = 0.0; wi = vec3(0,0,1); dist = 0.0; return; }

    float cosThetaMax = sqrt(1.0 - r2 / dc2);

    float r1 = rand(seed);
    float r2u = rand(seed + 2.9);
    float cosTheta = 1.0 - r1 * (1.0 - cosThetaMax);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * PI * r2u;

    vec3 w = wc / dc;
    vec3 u, v;
    buildONB(w, u, v);
    wi = normalize(cos(phi) * sinTheta * u + sin(phi) * sinTheta * v + cosTheta * w);

    // Solve |p + t*wi - center|^2 = r^2 for the smaller positive root
    float b = dot(wi, -wc);
    float disc = b * b - (dc2 - r2);
    dist = disc > 0.0 ? (-b - sqrt(disc)) : dc; // fall back to center distance if grazing
    if (dist <= 0.0) dist = dc;

    pdf = 1.0 / (2.0 * PI * (1.0 - cosThetaMax));
}

// Occlusion-only shadow test:
// is there any geometry strictly between `origin` and the light point at distance `maxDist` along `dir`?
// Mirrors trace() but early-outs on any hit.
// `lightIndex` (the light sphere being sampled) is excluded so it never spuriously shadows its own ray.
bool occluded(vec3 origin, vec3 dir, float maxDist, int lightIndex) {
    Ray r = Ray(origin, dir);
    HitRecord hit;
    hit.t = maxDist - bias; // anything at/after the light surface doesn't shadow it
    for (int i = 0; i < spheres.length(); i++) {
        if (i == lightIndex) continue; // skip the light itself to prevent spurious self-occlusion
        if (sphereIntersect(spheres[i], r, hit)) {
            return true;
        }
    }
    return false;
}

// Power heuristic (beta = 2) for two single-sample strategies (PBRT eq. 14.10 / Veach).
// Returns the MIS weight for strategy `a` given the two pdfs of the *same* direction.
float powerHeuristic(float pa, float pb) {
    float a2 = pa * pa;
    float b2 = pb * pb;
    float denom = a2 + b2;
    return denom > 0.0 ? a2 / denom : 0.0;
}

void getLobeWeights(Material mat, out float diffuseW, out float specularW, out float transmissionW, out float clearcoatW) {
    float metallic = saturate(mat.metallic);
    float transmission = saturate(mat.transmission) * (1.0 - metallic);
    diffuseW = max((1.0 - metallic) * (1.0 - transmission), 0.0);
    specularW = max((0.05 + mat.specular + metallic) * (1.0 - 0.5 * transmission), 0.001);
    transmissionW = max(transmission, 0.0);
    clearcoatW = max(0.25 * mat.clearcoat, 0.0);

    float sumW = max(diffuseW + specularW + transmissionW + clearcoatW, 1e-6);
    diffuseW /= sumW;
    specularW /= sumW;
    transmissionW /= sumW;
    clearcoatW /= sumW;
}

vec3 disneyF0(Material mat) {
    vec3 baseColor = max(mat.baseColor, vec3(0.0));
    vec3 tint = tintColor(baseColor);
    vec3 dielectricF0 = 0.08 * mat.specular * mix(vec3(1.0), tint, saturate(mat.specularTint));
    return mix(dielectricF0, baseColor, saturate(mat.metallic));
}

vec3 evalBSDF(Material mat, vec3 n, vec3 wo, vec3 wi) {
    float NoV = dot(n, wo);
    float NoL = dot(n, wi);
    if (NoV <= 0.0 || NoL <= 0.0) {
        return vec3(0.0);
    }

    vec3 h = normalize(wo + wi);
    float NoH = saturate(dot(n, h));
    float LoH = saturate(dot(wi, h));
    float VoH = saturate(dot(wo, h));
    float roughness = clamp(mat.roughness, 0.001, 1.0);
    float alpha = max(roughness * roughness, 0.001);
    vec3 baseColor = max(mat.baseColor, vec3(0.0));
    vec3 tint = tintColor(baseColor);

    float Fd90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float FL = schlickWeight(NoL);
    float FV = schlickWeight(NoV);
    vec3 diffuse = baseColor * (1.0 / PI) * mix(1.0, Fd90, FL) * mix(1.0, Fd90, FV);
    diffuse *= (1.0 - saturate(mat.metallic)) * (1.0 - saturate(mat.transmission));

    float D = D_GGX(NoH, alpha);
    float G = smithG1_GGX(NoL, alpha) * smithG1_GGX(NoV, alpha);
    vec3 F = disneyF0(mat) + (vec3(1.0) - disneyF0(mat)) * schlickWeight(VoH);
    vec3 specular = D * G * F / max(4.0 * NoL * NoV, 1e-6);

    vec3 sheenColor = mix(vec3(1.0), tint, saturate(mat.sheenTint));
    vec3 sheen = mat.sheen * sheenColor * schlickWeight(LoH) * (1.0 - saturate(mat.metallic));

    float coatAlpha = mix(0.25, 0.03, saturate(mat.clearcoatGloss));
    float coatD = D_GGX(NoH, coatAlpha);
    float coatG = smithG1_GGX(NoL, 0.25) * smithG1_GGX(NoV, 0.25);
    float coatF = mix(0.04, 1.0, schlickWeight(VoH));
    vec3 clearcoat = vec3(0.25 * mat.clearcoat * coatD * coatG * coatF);

    return diffuse + specular + sheen + clearcoat;
}

float pdfGGX(vec3 n, vec3 wo, vec3 wi, float alpha) {
    if (dot(n, wo) <= 0.0 || dot(n, wi) <= 0.0) {
        return 0.0;
    }

    vec3 h = normalize(wo + wi);
    float NoH = saturate(dot(n, h));
    float VoH = saturate(dot(wo, h));
    return D_GGX(NoH, alpha) * NoH / max(4.0 * VoH, 1e-6);
}

float pdfBSDF(Material mat, vec3 n, vec3 wo, vec3 wi) {
    if (dot(n, wo) <= 0.0 || dot(n, wi) <= 0.0) {
        return 0.0;
    }

    float diffuseW;
    float specularW;
    float transmissionW;
    float clearcoatW;
    getLobeWeights(mat, diffuseW, specularW, transmissionW, clearcoatW);

    float NoL = saturate(dot(n, wi));
    float roughness = clamp(mat.roughness, 0.001, 1.0);
    float alpha = max(roughness * roughness, 0.001);
    float coatAlpha = mix(0.25, 0.03, saturate(mat.clearcoatGloss));

    return diffuseW * NoL / PI + specularW * pdfGGX(n, wo, wi, alpha) + clearcoatW * pdfGGX(n, wo, wi, coatAlpha);
}

BSDFSample makeInvalidSample() {
    return BSDFSample(vec3(0.0), vec3(0.0), 0.0, 0, false);
}

BSDFSample makeDeltaSample(vec3 wi, vec3 color, vec3 n, int flags) {
    float NoL = max(abs(dot(n, wi)), 1e-4);
    return BSDFSample(normalize(wi), color / NoL, 1.0, flags, true);
}

BSDFSample sampleBSDF(Material mat, vec3 n, vec3 wo, bool frontFace, vec3 u) {
    float diffuseW;
    float specularW;
    float transmissionW;
    float clearcoatW;
    getLobeWeights(mat, diffuseW, specularW, transmissionW, clearcoatW);

    vec3 baseColor = max(mat.baseColor, vec3(0.0));
    float lobe = u.x;
    float roughness = clamp(mat.roughness, 0.001, 1.0);
    float alpha = max(roughness * roughness, 0.001);

    if (lobe < transmissionW) {
        float eta = frontFace ? 1.0 / max(mat.ior, 0.001) : max(mat.ior, 0.001);
        float cosTheta = min(dot(wo, n), 1.0);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        bool cannotRefract = eta * sinTheta > 1.0;
        float fresnel = fresnelDielectricSchlick(cosTheta, max(mat.ior, 0.001));
        vec3 wi = (cannotRefract || u.y < fresnel) ? reflect(-wo, n) : refract(-wo, n, eta);
        int flags = dot(wi, n) >= 0.0 ? bsdf_specular : (bsdf_specular | bsdf_transmission);
        return makeDeltaSample(wi, baseColor, n, flags);
    }

    vec3 wi;
    int flags;
    if (lobe < transmissionW + diffuseW) {
        wi = cosineSampleHemisphere(u.yz, n);
        flags = bsdf_diffuse;
    } else if (lobe < transmissionW + diffuseW + specularW) {
        vec3 h = sampleGGX(u.yz, alpha, n);
        wi = reflect(-wo, h);
        flags = bsdf_glossy;
    } else {
        float coatAlpha = mix(0.25, 0.03, saturate(mat.clearcoatGloss));
        vec3 h = sampleGGX(u.yz, coatAlpha, n);
        wi = reflect(-wo, h);
        flags = bsdf_glossy;
    }

    if (dot(wi, n) <= 0.0) {
        return makeInvalidSample();
    }

    float pdf = pdfBSDF(mat, n, wo, wi);
    if (pdf <= 1e-6) {
        return makeInvalidSample();
    }

    return BSDFSample(normalize(wi), evalBSDF(mat, n, wo, wi), pdf, flags, true);
}

// Next-event estimation at a non-delta (diffuse/glossy) hit:
// deterministically connect to every emissive sphere via a cone sample + occlusion-only shadow ray,
// and accumulate the direct radiance.
// Returns the direct-lighting contribution (NOT yet multiplied by path throughput).
// Pure-delta lobes (transmission/specular fresnel) correctly receive zero direct estimate.
vec3 directLight(HitRecord hit, vec3 wo, int bounce) {
    vec3 Ld = vec3(0.0);

    for (int i = 0; i < spheres.length(); i++) {
        Sphere light = spheres[i];
        if (light.mat.emission == vec3(0.0)) continue; // not a light

        vec2 seed = hit.p.xy + hit.p.zz
                  + vec2(float(bounce) + 7.0 * float(i) + 2.2)
                  + 0.197 * float(frameCountWithoutMove);

        vec3 wi;
        float dist, pdfLight;
        sampleSphereLight(light, hit.p, seed, wi, dist, pdfLight);
        if (pdfLight <= 0.0) continue;

        float cosSurf = dot(hit.normal, wi);
        if (cosSurf <= 0.0) continue; // light is below the surface horizon

        // Full Disney BSDF value for the light direction (replaces the old albedo/PI Lambertian term)
        vec3 f = evalBSDF(hit.mat, hit.normal, wo, wi);
        if (max3(f) <= 0.0) continue; // no reflectance toward the light (e.g. pure-delta material)

        vec3 origin = hit.p + bias * hit.normal;
        if (occluded(origin, wi, dist, i)) continue; // shadowed (i = light being sampled)

        // MIS power-heuristic weight for the light-sampling strategy:
        // The competing strategy is BSDF sampling,
        // whose pdf for this same direction is the Disney pdfBSDF (not cos/PI)
        float misW = 1.0;
        if (RENDER_MODE == mode_full) {
            float pdfBsdf = pdfBSDF(hit.mat, hit.normal, wo, wi);
            misW = powerHeuristic(pdfLight, pdfBsdf);
        }

        Ld += misW * f * light.mat.emission * cosSurf / pdfLight;
    }
    return Ld;
}

vec3 castRay(Ray ray){
    Ray r = ray;
    vec3 throughput = vec3(1.0); // path throughput (product of f*cos/pdf so far)
    vec3 color = vec3(0.0);      // accumulated radiance L

    bool lastBounceWasDelta = true; // primary camera ray behaves like a delta connection

    // Previous-bounce shading info, needed to MIS-weight emission found by the BSDF ray
    vec3 prevP = vec3(0.0);
    vec3 prevN = vec3(0.0);
    vec3 prevWo = vec3(0.0);
    Material prevMat;

    for (int i = 0; i < MAX_DEPTH; i++) {
        HitRecord hit;
        if (!trace(r, hit)) {
            color += throughput * skyColor(r);
            break;
        }

        vec3 wo = normalize(-r.direction);

        // --- Emission handling (avoid double-counting against NEE) ---
        if (hit.mat.emission != vec3(0.0)) {
            if (RENDER_MODE == mode_bsdf_only || lastBounceWasDelta) {
                // BSDF-only: no NEE exists, always add
                // Delta bounce / primary ray: NEE can't connect across these,
                // so the BSDF ray is the only carrier of this emission; add it in full
                color += throughput * hit.mat.emission;
            } else if (RENDER_MODE == mode_full) {
                // Arrived from a non-delta (diffuse/glossy) bounce:
                // directLight() at the previous surface also tried to reach this emitter,
                // so MIS-weight against the light pdf to avoid double-counting
                float pdfLight = 0.0;
                for (int li = 0; li < spheres.length(); li++) {
                    if (spheres[li].mat.emission == vec3(0.0)) continue;
                    float surfErr = abs(length(hit.p - spheres[li].center) - spheres[li].radius);
                    if (surfErr > 1e-3) continue; // not the sphere we actually hit
                    pdfLight = spherePdfLight(spheres[li], prevP, r.direction);
                    break;
                }
                float pdfBsdf = pdfBSDF(prevMat, prevN, prevWo, r.direction);
                float wBsdf = powerHeuristic(pdfBsdf, pdfLight);
                color += wBsdf * throughput * hit.mat.emission;
            }
            // NEE-only never reaches here (it breaks after the direct estimate)
            break;
        }

        // --- Next event estimation at non-delta surfaces ---
        // directLight() returns 0 for pure-delta materials (evalBSDF == 0 for generic wi),
        // so calling it unconditionally on every hit is safe
        if (RENDER_MODE != mode_bsdf_only) {
            color += throughput * directLight(hit, wo, i);
            if (RENDER_MODE == mode_nee_only) {
                break; // NEE-only: terminate after the direct estimate
            }
        }

        // --- BSDF sampling for the next bounce ---
        vec2 seed = hit.p.xy + hit.p.zz + vec2(float(i) + 1.7) + 9.13 * float(frameCountWithoutMove);
        vec3 u = vec3(rand(seed), rand(seed + 2.31), rand(seed + 5.97));
        BSDFSample sample = sampleBSDF(hit.mat, hit.normal, wo, hit.frontFace, u);
        if (!sample.valid) {
            break;
        }

        float NoL = max(abs(dot(hit.normal, sample.wi)), 0.0);
        throughput *= sample.f * NoL / max(sample.pdf, 1e-6);

        if (max3(throughput) <= 1e-4) {
            break;
        }

        // A pure-delta lobe (transmission / specular Fresnel) was sampled iff makeDeltaSample tagged it bsdf_specular
        // Non-delta (diffuse (bsdf_diffuse) and glossy GGX (bsdf_glossy)) were already covered by NEE above
        lastBounceWasDelta = (sample.flags & bsdf_specular) != 0;

        // Save previous-surface info for the next iteration's emitter-MIS weight
        prevP = hit.p;
        prevN = hit.normal;
        prevWo = wo;
        prevMat = hit.mat;

        vec3 offsetN = dot(sample.wi, hit.normal) >= 0.0 ? hit.normal : -hit.normal;
        r = Ray(hit.p + bias * offsetN, normalize(sample.wi));
    }

    return color;
}

void main()
{
    if (displayOnly) {
        vec3 c = texture(accumPrev, TexCoords).rgb;
        FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);     // gamma correction
        return;
    }

    setupScene(); // fill spheres[] for the active SCENE before any tracing

    vec3 color = vec3(0);
    for (int i = 0; i < RAYS_PER_FRAME_PIXEL; i++) {
        Ray r = getRay(TexCoords, i);
        color += castRay(r);
    }
    color /= RAYS_PER_FRAME_PIXEL;

    // Optional for Figure 1(h)
    // Blend the current-frame color with accumPrev using frameCountWithoutMove.
    vec3 old_color = texture(accumPrev, TexCoords).rgb;
    float n = float(frameCountWithoutMove);
    FragColor = vec4(old_color + (color - old_color) / (n + 1.0), 1.0);     // running mean
}
