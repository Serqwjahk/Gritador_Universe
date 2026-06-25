#pragma once
#include <cmath>
#include <algorithm>
#include "constants.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

// ============================================================
//  Álgebra vectorial de doble precisión (para física)
// ============================================================

struct Vector3D {
    double x = 0, y = 0, z = 0;

    Vector3D operator+(const Vector3D& o) const { return { x+o.x, y+o.y, z+o.z }; }
    Vector3D operator-(const Vector3D& o) const { return { x-o.x, y-o.y, z-o.z }; }
    Vector3D operator*(double s)          const { return { x*s,   y*s,   z*s   }; }
    Vector3D operator/(double s)          const { return { x/s,   y/s,   z/s   }; }

    Vector3D& operator+=(const Vector3D& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    Vector3D& operator-=(const Vector3D& o) { x-=o.x; y-=o.y; z-=o.z; return *this; }

    double lengthSqr() const { return x*x + y*y + z*z; }
    double length()    const { return std::sqrt(lengthSqr()); }
    double dot(const Vector3D& o) const { return x*o.x + y*o.y + z*o.z; }
};

inline Vector3D NormalizeSafe(const Vector3D& v) {
    double l = v.length();
    return l > 0.0 ? v / l : Vector3D{0, 1, 0};
}

inline Vector3D Cross(const Vector3D& a, const Vector3D& b) {
    return { a.y*b.z - a.z*b.y,
             a.z*b.x - a.x*b.z,
             a.x*b.y - a.y*b.x };
}

// Utilidades generales
inline double ClampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
inline float  ClampF(float  v, float  lo, float  hi) { return std::max(lo, std::min(hi, v)); }

// Origen flotante: posicion fisica (m) que se mapea al {0,0,0} del
// espacio de dibujado. OrbitCamera::Update() la actualiza cada frame
// con el target actual de la camara (ver camera_input.h). Mantenerla
// en {0,0,0} reproduce exactamente el comportamiento anterior (sin
// "follow"); al seguir un cuerpo lejano del origen evita que `radius *
// dir` (offset de camara, puede ser tan pequeno como 1e-4) se pierda
// por precision float32 al sumarse a un `target` de magnitud grande
// (causa del desfase de raycast/colocacion al hacer zoom extremo).
extern Vector3D g_renderOrigin;

// Conversión física <-> render (relativa al origen flotante actual)
inline Vector3 ToDrawPos(const Vector3D& p) {
    Vector3D rel = p - g_renderOrigin;
    return { (float)(rel.x * RENDER_SCALE),
             (float)(rel.y * RENDER_SCALE),
             (float)(rel.z * RENDER_SCALE) };
}

inline Vector3D ToPhysPos(const Vector3& p) {
    return g_renderOrigin + Vector3D{ (double)p.x / RENDER_SCALE,
                                       (double)p.y / RENDER_SCALE,
                                       (double)p.z / RENDER_SCALE };
}

// Desplazamiento XZ desde 'center' hasta donde un rayo cruza el plano
// horizontal Y = center.y. Usado para colocar orbitas alrededor de cuerpos
// grandes (estrellas), donde el plano Y=0 puede caer dentro del cuerpo.
// Si el rayo es casi paralelo al plano, usa el punto del rayo mas cercano
// a 'center' como respaldo.
inline Vector3 RayHorizontalOffset(const Ray& ray, const Vector3& center) {
    if (std::fabs(ray.direction.y) > 1e-8f) {
        float t = (center.y - ray.position.y) / ray.direction.y;
        if (t > 0.0f) {
            Vector3 hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
            return Vector3Subtract(hit, center);
        }
    }
    Vector3 oc    = Vector3Subtract(center, ray.position);
    float   tp    = std::max(0.1f, Vector3DotProduct(oc, ray.direction));
    Vector3 close = Vector3Add(ray.position, Vector3Scale(ray.direction, tp));
    return Vector3Subtract(close, center);
}

// Rayo cámara->cursor con clip planes "seguros" para la unproyeccion.
// GetMouseRay() construye la matriz de proyeccion inversa a partir de
// rlGetCullDistanceNear/Far(). Con el nearPlane dinamico del zoom extremo
// (puede llegar a 1e-4) y far=80000, (far+near) y (far-near) colapsan al
// MISMO float32 -> el termino m10 de la matriz de perspectiva queda en
// exactamente -1.0, lo que hace que el punto lejano de la unproyeccion
// tenga w=0 (division por cero) y produce ray.direction = NaN para
// CUALQUIER posicion del mouse. RaycastGroundPlane entonces falla
// siempre (fabs(NaN) > eps es false) y todo se coloca en el {0,0,0}
// fisico por defecto -> "todo se coloca en 1 sola posicion" al hacer
// zoom extremo. La direccion del rayo es matematicamente independiente
// de los clip planes, asi que se usan valores fijos seguros solo para
// este calculo y se restauran los reales (usados por el render) despues.
inline Ray GetSafeMouseRay(const Camera3D& cam) {
    double savedNear = rlGetCullDistanceNear();
    double savedFar  = rlGetCullDistanceFar();
    rlSetClipPlanes(1.0, 100000.0);
    Ray ray = GetMouseRay(GetMousePosition(), cam);
    rlSetClipPlanes(savedNear, savedFar);
    return ray;
}

// Geometría
inline double VolumeFromRadius(double r)     { return (4.0/3.0) * PI_D * r*r*r; }
inline double DensityOf(double mass, double radius) {
    if (radius <= 0.0) return DENSITY_EARTH;
    return mass / std::max(1.0, VolumeFromRadius(radius));
}
