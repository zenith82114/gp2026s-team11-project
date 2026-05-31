#version 330
out vec4 FragColor;

in vec2 TexCoords;

// You can change the code whatever you want

const int MAX_DEPTH = 10; // maximum bounce

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

vec3 skyColor(Ray ray) {
    vec3 dir = normalize(ray.direction);
    float a = 0.5 * (dir.y + 1.0);
    return 0.01 * ((1.0 - a) * vec3(1.0) + a * vec3(0.5, 0.7, 1.0));
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
            L += beta * hit.mat.emission;
            break;
        }

        if (hit.mat.material_type == mat_diffuse) {
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
