#version 330
out vec4 FragColor;

in vec2 TexCoords;

// You can change the code whatever you want

const int MAX_DEPTH = 10; // maximum bounce
const float PI = 3.14159265359;

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


Sphere spheres[] = Sphere[](
//     Sphere(vec3(0,-100.5,-1), 100, material_ground),        // diffuse(Lambertian)
//     Sphere(vec3(0,0,-1), 0.5, material_sphere_middle),      // diffuse(Lambertian)
//     Sphere(vec3(-1.01,0,-1), 0.5, material_sphere_left),    // refractive
//     Sphere(vec3(-1,0,-1), 0.4, material_inside_left),       // refractive
//     Sphere(vec3(1,0,-1), 0.5, material_sphere_right)        // reflective

    Sphere(vec3(0,-100.5,-1), 100, material_ground),        // diffuse(Lambertian)
    Sphere(vec3(-1.01,0,-1), 0.5, material_sphere_middle),  // diffuse(Lambertian)
    Sphere(vec3(0.01,0,-1), 0.5, material_sphere_left),     // refractive
    Sphere(vec3(0,0,-1), 0.4, material_inside_left),        // refractive
    Sphere(vec3(1,0,-1), 0.5, material_sphere_right)        // reflective
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
    return (1.0 - a) * vec3(1.0) + a * vec3(0.5, 0.7, 1.0);
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

vec3 castRay(Ray ray){
    Ray r = ray;
    vec3 throughput = vec3(1.0);
    vec3 color = vec3(0.0);

    for (int i = 0; i < MAX_DEPTH; i++) {
        HitRecord hit;
        if (!trace(r, hit)) {
            color += throughput * skyColor(r);
            break;
        }

        vec3 wo = normalize(-r.direction);
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
