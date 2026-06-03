#version 330
out vec4 FragColor;

in vec2 TexCoords;

// You can change the code whatever you want

const int MAX_DEPTH = 10; // maximum bounce

// Render mode selector for NEE/MIS validation
//   0: BSDF-only      — original path tracer, lit only by BSDF rays hitting
//   1: NEE-only       — direct estimate at the first diffuse hit, then terminate
//   2: full (NEE+MIS) — direct estimate at every diffuse hit AND keep bouncing
const int RENDER_MODE = 0;
const int mode_bsdf_only = 0;
const int mode_nee_only  = 1;
const int mode_full      = 2;

const float PI = 3.14159265358979323846;

// Optional for Figure 1(h): accumulation support.
uniform sampler2D accumPrev;
uniform int frameCountWithoutMove;
uniform bool displayOnly;

struct Ray {
    vec3 origin;
    vec3 direction;
};

struct Material {
    int material_type; // 0: diffusion, 1: reflect, 2: refractive

    vec3 albedo;

    // parameters for reflective
    float fuzz; // fuzziness for reflective

    // parameters for refractive
    float ior; // index of refraction

    vec3 emission; // radiance emitted by the surface (black = non-emitter)
};

const int mat_diffuse = 0;
const int mat_reflective = 1;
const int mat_refractive = 2;

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


Sphere spheres[] = Sphere[](
    Sphere(vec3(0,-100.5,-1), 100, material_ground),        // diffuse(Lambertian)
    Sphere(vec3(0,2,-1), 0.2, material_sphere_middle),      // diffuse(Lambertian), emitting
    Sphere(vec3(-1.01,0,-1), 0.5, material_sphere_left),    // refractive
    Sphere(vec3(-1,0,-1), 0.4, material_inside_left),       // refractive
    Sphere(vec3(1,0,-1), 0.5, material_sphere_right),       // reflective
    Sphere(vec3(-0.2,0,0), 0.5, material_sphere_diffuse)    // diffuse(Lambertian), occluder

    // Sphere(vec3(0,-100.5,-1), 100, material_ground),        // diffuse(Lambertian)
    // Sphere(vec3(-1.01,0,-1), 0.5, material_sphere_middle),  // diffuse(Lambertian)
    // Sphere(vec3(0.01,0,-1), 0.5, material_sphere_left),     // refractive
    // Sphere(vec3(0,0,-1), 0.4, material_inside_left),        // refractive
    // Sphere(vec3(1,0,-1), 0.5, material_sphere_right)        // reflective
);


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


uniform vec3 cameraPosition;
uniform mat3 cameraToWorldRotMatrix;
uniform float fovY; //set to 45
uniform float H;
uniform float W;

Ray getRay(vec2 uv, int sampleId) {
    // TODO
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
    // TODO
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

float schlick(float cosine, float r0) {
    // TODO
    float f0 = (1.0 - r0) / (1.0 + r0);
    f0 = f0 * f0;
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

bool trace(Ray r, out HitRecord hit){
    // TODO
    hit.t = 1.0 / 0.0; // +inf
    bool anyHit = false;
    for (int i = 0; i < spheres.length(); i++) {
        if (sphereIntersect(spheres[i], r, hit)) {
            anyHit = true;
        }
    }
    return anyHit;
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
// Handles arbitrary wi (not just sampled ones) so the same formula serves MIS later.
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
//
// `lightIndex` (the light sphere being sampled) is excluded from the test.
// `maxDist` is computed analytically in sampleSphereLight(),
// while this function re-intersects the same sphere with a different formula;
// if the two disagree by more than `bias` near the cone edge,
// the light spuriously occludes its own shadow ray and drops the sample to black.
// The light surface is the *target*, never an occluder. Pass -1 to test all spheres.
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

vec3 skyColor(Ray ray) {
    vec3 dir = normalize(ray.direction);
    float a = 0.5 * (dir.y + 1.0);
    return 0.01 * ((1.0 - a) * vec3(1.0) + a * vec3(0.5, 0.7, 1.0));
}

// Next-event estimation at a diffuse hit:
// deterministically connect to every emissive sphere via a cone sample + occlusion-only shadow ray,
// and accumulate the direct radiance.
// Returns the direct-lighting contribution (NOT yet multiplied by path throughput `beta`).
vec3 directLight(HitRecord hit, int bounce) {
    vec3 Ld = vec3(0.0);
    vec3 f = hit.mat.albedo / PI; // Lambertian surface

    for (int i = 0; i < spheres.length(); i++) {
        Sphere light = spheres[i];
        if (light.mat.emission == vec3(0.0)) continue; // not a light

        vec2 seed = hit.p.xy + hit.p.zz
                  + vec2(float(bounce) + 7.0 * float(i) + 2.2)
                  + 0.197 * float(frameCountWithoutMove);

        vec3 wi;
        float dist, pdf;
        sampleSphereLight(light, hit.p, seed, wi, dist, pdf);
        if (pdf <= 0.0) continue;

        float cosSurf = dot(hit.normal, wi);
        if (cosSurf <= 0.0) continue; // light is below the surface horizon

        vec3 origin = hit.p + bias * hit.normal;
        if (occluded(origin, wi, dist, i)) continue; // shadowed (i = light being sampled)

        Ld += f * light.mat.emission * cosSurf / pdf; // we don't need extra 1/dist^2 or light-cosine term
    }
    return Ld;
}

vec3 castRay(Ray ray){
    // TODO
    Ray r = ray;
    vec3 L = vec3(0);    // accumulated radiance
    vec3 beta = vec3(1); // path throughput (product of albedos so far)

    for (int i = 0; i < MAX_DEPTH; i++) {
        HitRecord hit;
        if (!trace(r, hit)) {
            L += beta * skyColor(r);
            break;
        }

        if (hit.mat.emission != vec3(0.0)) {
            // A BSDF ray landed on an emitter
            // When NEE is on, the direct light is already accounted for by directLight();
            // re-adding it here double-counts
            // Gated off for now; TODO later
            if (RENDER_MODE == mode_bsdf_only) {
                L += beta * hit.mat.emission;
            }
            break;
        }

        if (hit.mat.material_type == mat_diffuse) {
            // Next event estimation: add the direct-lighting estimate
            if (RENDER_MODE != mode_bsdf_only) {
                L += beta * directLight(hit, i);
                if (RENDER_MODE == mode_nee_only) {
                    break; // NEE-only: terminate after the direct estimate
                }
            }
            vec2 seed = hit.p.xy + hit.p.zz + vec2(float(i)) + 0.131 * float(frameCountWithoutMove);
            vec3 dir = normalize(hit.normal + rand_unit_vec3(seed));
            beta *= hit.mat.albedo;
            r = Ray(hit.p + bias * hit.normal, dir);
        }
        else if (hit.mat.material_type == mat_reflective) {
            vec3 reflected = reflect(normalize(r.direction), hit.normal);
            vec2 seed = hit.p.xy + hit.p.zz + vec2(float(i) + 0.5) + 13.37 * float(frameCountWithoutMove);
            vec3 dir = normalize(reflected + hit.mat.fuzz * rand_unit_vec3(seed));
            if (dot(dir, hit.normal) <= 0.0) {
                break; // ray absorbed below surface; contributes no radiance
            }
            beta *= hit.mat.albedo;
            r = Ray(hit.p + bias * hit.normal, dir);
        }
        else { // refractive
            // assume one of the two adjacent media is always air (IOR = 1)
            float iorRatio = hit.frontFace ? (1.0 / hit.mat.ior) : hit.mat.ior;
            float cosTheta = min(dot(-r.direction, hit.normal), 1.0);
            float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

            vec2 seed = hit.p.xy + hit.p.zz + vec2(float(i) + 1.3) + 4.7 * float(frameCountWithoutMove);
            vec3 dir;
            vec3 offsetN;
            if (iorRatio * sinTheta > 1.0 || rand(seed) < schlick(cosTheta, hit.mat.ior)) {
                dir = reflect(r.direction, hit.normal);
                offsetN = hit.normal;
            } else {
                dir = refract(r.direction, hit.normal, iorRatio);
                offsetN = -hit.normal;
            }
            beta *= hit.mat.albedo;
            r = Ray(hit.p + bias * offsetN, dir);
        }
    }

    return L;
}

void main()
{
    if (displayOnly) {
        vec3 c = texture(accumPrev, TexCoords).rgb;
        FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);     // gamma correction
        return;
    }

    // TODO
    const int nsamples = 8;
    vec3 color = vec3(0);
    for (int i = 0; i < nsamples; i++) {
        Ray r = getRay(TexCoords, i);
        color += castRay(r);
    }
    color /= nsamples;

    // Optional for Figure 1(h)
    // Blend the current-frame color with accumPrev using frameCountWithoutMove.
    vec3 old_color = texture(accumPrev, TexCoords).rgb;
    float n = float(frameCountWithoutMove);
    FragColor = vec4(old_color + (color - old_color) / (n + 1.0), 1.0);     // running mean
}
