#version 330
out vec4 FragColor;

in vec2 TexCoords;

// You can change the code whatever you want

const int MAX_DEPTH = 10; // maximum bounce
vec3 colorStack[MAX_DEPTH];

// Optional for Figure 1(h): accumulation support.
uniform sampler2D accumPrev;
uniform int frameCountWithoutMove;
uniform bool displayOnly;
uniform int sampleMode; // 0: random, 1: Sobol, 2: Sobol with per-pixel scrambling
uniform int randomSeedOffset; // extra offset for independent random runs

uniform vec3 cameraPosition;
uniform mat3 cameraToWorldRotMatrix;
uniform float fovY; //set to 45
uniform float H;
uniform float W;

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
};

const int mat_diffuse = 0;
const int mat_reflective = 1;
const int mat_refractive = 2;

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

uint hashUint(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

uint owenScramble32(uint x, uint seed) {
    uint y = 0u;
    uint prefix = 0u;

    for (int bit = 31; bit >= 0; --bit) {
        uint shift = uint(bit);
        uint inputBit = (x >> shift) & 1u;

        uint h = hashUint(seed ^ prefix ^ uint(31 - bit) * 0x9e3779b9u);
        uint flip = h & 1u;

        uint outputBit = inputBit ^ flip;
        y |= outputBit << shift;

        prefix = (prefix << 1u) | inputBit;
    }

    return y;
}

// Reverse the bit order of a 32-bit unsigned integer (Van der Corput / bit reversal)
uint bitReverse(uint v) {
    v = ((v >> 1u) & 0x55555555u) | ((v & 0x55555555u) << 1u);
    v = ((v >> 2u) & 0x33333333u) | ((v & 0x33333333u) << 2u);
    v = ((v >> 4u) & 0x0f0f0f0fu) | ((v & 0x0f0f0f0fu) << 4u);
    v = ((v >> 8u) & 0x00ff00ffu) | ((v & 0x00ff00ffu) << 8u);
    v = (v >> 16u) | (v << 16u);
    return v;
}

// 2D Sobol (VdC-based for the first two dimensions).
// Uses bit-reversal (radical inverse base-2) for dimension X and
// the Gray-coded index for dimension Y to produce low-discrepancy 2D samples.
uvec2 sobol2D(uint index) {
    uint ix = bitReverse(index);
    // Gray code for second dimension before reversing bits
    uint gray = index ^ (index >> 1u);
    uint iy = bitReverse(gray);
    return uvec2(ix, iy);
}

vec2 sobolJitter(vec2 uv, uint sampleIndex) {
    vec2 pixel = floor(uv * vec2(W, H));
    uint pixelSeed = hashUint(uint(pixel.x) ^ (uint(pixel.y) << 16u) ^ 0x9e3779b9u);
    uvec2 sampleValue = sobol2D(sampleIndex);
    if (sampleMode == 2) {
        sampleValue.x = owenScramble32(sampleValue.x, pixelSeed ^ 0x1234567u);
        sampleValue.y = owenScramble32(sampleValue.y, pixelSeed ^ 0x68bc21ebu);
    }
    // Convert 32-bit integer sample to float in [0,1)
    vec2 samplePoint = vec2(sampleValue) * 2.3283064365386963e-10; // 1/2^32
    return uv + (samplePoint - vec2(0.5)) / vec2(W, H);
}

vec2 randomJitter(vec2 uv, uint sampleIndex) {
    vec2 pixel = floor(uv * vec2(W, H));
    vec2 seed = pixel + vec2(float(sampleIndex), float(frameCountWithoutMove) * 17.0);
    vec2 jitter = vec2(rand(seed), rand(seed + 19.19));
    return uv + (jitter - vec2(0.5)) / vec2(W, H);
}

vec2 random2D(vec2 uv, uint sampleIndex, uint dim) {
    vec2 pixel = floor(uv * vec2(W, H));
    uint seed = hashUint(
        uint(pixel.x) * 1973u ^
        uint(pixel.y) * 9277u ^
        sampleIndex * 26699u ^
        dim * 0x9e3779b9u ^
        uint(randomSeedOffset) * 0x85ebca6bu
    );

    float x = float(hashUint(seed)) * 2.3283064365386963e-10;
    float y = float(hashUint(seed ^ 0x68bc21ebu)) * 2.3283064365386963e-10;
    return vec2(x, y);
}

vec2 sample2D(vec2 uv, uint sampleIndex, uint dim) {
    if (sampleMode == 0) {
        return random2D(uv, sampleIndex, dim);
    }

    // Avoid Sobol sample 0. sobol2D(0) = (0, 0), which gives a corner-biased
    // camera sample and degenerate BSDF samples when nsamples == 1.
    uint index = sampleIndex + 1u;
    uvec2 s = sobol2D(index);

    vec2 pixel = floor(uv * vec2(W, H));
    uint pixelSeed = hashUint(
        uint(pixel.x) * 1973u ^
        uint(pixel.y) * 9277u
    );
    uint dimSeed = hashUint(dim * 0x9e3779b9u + 0x243f6a88u);

    if (sampleMode == 1) {
        // Plain Sobol for the image-plane dimensions, but decorrelate later
        // path dimensions per pixel.  Without this, every pixel makes the same
        // reflection/refraction/diffuse choice each frame, causing coherent
        // full-image flicker and ghosting.
        if (dim != 0u) {
            s.x ^= hashUint(pixelSeed ^ dimSeed ^ 0x1234567u);
            s.y ^= hashUint(pixelSeed ^ dimSeed ^ 0x68bc21ebu);
        }
    }
    else { // sampleMode == 2: per-pixel Owen-like scrambling
        s.x = owenScramble32(s.x, pixelSeed ^ dimSeed ^ 0x1234567u);
        s.y = owenScramble32(s.y, pixelSeed ^ dimSeed ^ 0x68bc21ebu);
    }

    return vec2(s) * 2.3283064365386963e-10;
}

vec3 rand_unit_vec3(vec2 seed) {
    vec3 r = vec3(rand(seed), rand(seed + 1.7), rand(seed + 3.3)) * 2.0 - 1.0;
    return normalize(r);
}

vec3 sampleHemisphereCosine(vec3 normal, vec2 u) {
    float r = sqrt(u.x);
    float theta = 6.28318530718 * u.y;

    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u.x));

    vec3 up = abs(normal.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * x + bitangent * y + normal * z);
}

float max3 (vec3 v) {
  return max (max (v.x, v.y), v.z);
}

float min3 (vec3 v) {
    return min (min (v.x, v.y), v.z);
}

Ray getRay(vec2 uv, uint sampleIndex) {
    float halfH = tan(fovY * 0.5);
    float halfW = (W / H) * halfH;

    vec2 jitter = sample2D(uv, sampleIndex, 0u);
    vec2 sampleUv = uv + (jitter - vec2(0.5)) / vec2(W, H);

    vec2 ndc = 2.0 * sampleUv - 1.0;
    vec3 dirCam = vec3(ndc.x * halfW, ndc.y * halfH, -1.0);
    vec3 dirWorld = normalize(cameraToWorldRotMatrix * dirCam);
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

vec3 skyColor(Ray ray) {
    vec3 dir = normalize(ray.direction);
    float a = 0.5 * (dir.y + 1.0);
    return (1.0 - a) * vec3(1.0) + a * vec3(0.5, 0.7, 1.0);
}


vec3 stripeGroundAlbedo(vec3 p) {
    // One-direction stripe pattern for the ground.
    // The color varies only with x, so the stripes run parallel to the z-axis.
    // Increase this value to 120.0 or 160.0 if the difference is still subtle.
    float scale = 100.0;
    float stripe = mod(floor(p.x * scale), 2.0);
    return mix(vec3(0.03), vec3(0.95), stripe);
}

vec3 surfaceAlbedo(HitRecord hit) {
    vec3 albedo = hit.mat.albedo;

    // The ground is implemented as a large sphere centered at y = -100.5
    // with radius 100, so visible ground hit points are near y = -0.5.
    if (hit.mat.material_type == mat_diffuse && hit.p.y < -0.45) {
        albedo = stripeGroundAlbedo(hit.p);
    }

    return albedo;
}

vec3 castRay(Ray ray, uint sampleIndex){
    // TODO
    Ray r = ray;
    int depth = 0;
    vec3 envColor = vec3(0);

    for (int i = 0; i < MAX_DEPTH; i++) {
        HitRecord hit;
        if (!trace(r, hit)) {
            envColor = skyColor(r);
            break;
        }

        if (hit.mat.material_type == mat_diffuse) {
            vec2 u = sample2D(TexCoords, sampleIndex, 1u + uint(i));
            vec3 dir = sampleHemisphereCosine(hit.normal, u);
            colorStack[depth++] = surfaceAlbedo(hit);
            r = Ray(hit.p + bias * hit.normal, dir);
        }
        else if (hit.mat.material_type == mat_reflective) {
            vec3 reflected = reflect(normalize(r.direction), hit.normal);
            vec2 u = sample2D(TexCoords, sampleIndex, 20u + uint(i));
            vec3 fuzzDir = sampleHemisphereCosine(hit.normal, u);
            vec3 dir = normalize(reflected + hit.mat.fuzz * fuzzDir);
            if (dot(dir, hit.normal) <= 0.0) {
                envColor = vec3(0);
                break;
            }
            colorStack[depth++] = hit.mat.albedo;
            r = Ray(hit.p + bias * hit.normal, dir);
        }
        else { // refractive
            // assume one of the two adjacent media is always air (IOR = 1)
            float iorRatio = hit.frontFace ? (1.0 / hit.mat.ior) : hit.mat.ior;
            vec3 unitDir = normalize(r.direction);
            float cosTheta = min(dot(-unitDir, hit.normal), 1.0);
            float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

            vec2 u = sample2D(TexCoords, sampleIndex, 40u + uint(i));
            vec3 dir;
            vec3 offsetN;
            if (iorRatio * sinTheta > 1.0 || u.x < schlick(cosTheta, hit.mat.ior)) {
                dir = reflect(unitDir, hit.normal);
                offsetN = hit.normal;
            } else {
                dir = refract(unitDir, hit.normal, iorRatio);
                offsetN = -hit.normal;
            }
            colorStack[depth++] = hit.mat.albedo;
            r = Ray(hit.p + bias * offsetN, dir);
        }
    }

    vec3 color = envColor;
    for (int i = depth - 1; i >= 0; i--) {
        color *= colorStack[i];
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

    // TODO
    const int nsamples = 1;
    vec3 color = vec3(0);
    uint sampleBase = uint(frameCountWithoutMove) * uint(nsamples);
    for (int i = 0; i < nsamples; i++) {
        Ray r = getRay(TexCoords, sampleBase + uint(i));
        color += castRay(r, sampleBase + uint(i));
    }
    color /= nsamples;

    // Optional for Figure 1(h)
    // Blend the current-frame color with accumPrev using frameCountWithoutMove.
    vec3 old_color = texture(accumPrev, TexCoords).rgb;
    float n = float(frameCountWithoutMove);
    FragColor = vec4(old_color + (color - old_color) / (n + 1.0), 1.0);     // running mean
}
