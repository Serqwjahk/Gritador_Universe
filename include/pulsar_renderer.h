#pragma once
// ============================================================
//  Pulsar / Magnetar — Solo estrella 2D + dos jets
//
//  La estrella de neutrones es TAN PEQUEÑA (12 km) que cualquier
//  esfera 3D se vuelve enorme cuando la camara hace auto-zoom al
//  radio fisico del cuerpo. Solucion: glow 2D en coordenadas de
//  pantalla (GetWorldToScreen) — siempre el mismo tamaño en pixels
//  sin importar la distancia de la camara.
//
//  Los jets son planos billboard orientados axialmente hacia la
//  camara. El shader usa fragTexCoord (UV) para la gaussiana
//  radial, no la posicion del vertice del cilindro (que siempre
//  tiene rXZ=1 en la superficie y hacia discard todo).
// ============================================================
#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "math_utils.h"
#include "body.h"
#include "constants.h"
#include <cmath>
#include <algorithm>

// ── Vertex ───────────────────────────────────────────────────
static const char* PSR_VERT = R"GLSL(
#version 330 core
layout(location=0) in vec3 vertexPosition;
layout(location=1) in vec2 vertexTexCoord;
out vec2 fragUV;
uniform mat4 mvp;
void main(){
    fragUV      = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

// ── Fragment del jet ──────────────────────────────────────────
// fragUV.x = lateral (0=borde, 0.5=eje, 1=borde)
// fragUV.y = a lo largo del jet (0=estrella, 1=punta)
static const char* PSR_JET_FRAG = R"GLSL(
#version 330 core
in vec2 fragUV;
out vec4 fragColor;
uniform float time;
uniform float visualSpin;

float hash(vec3 p){ p=fract(p*0.3183099+.1);p*=17.;return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }
float n3(vec3 x){
    vec3 i=floor(x),f=fract(x);f=f*f*(3.-2.*f);
    return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float fbm(vec3 p){float v=0.,a=.5,fr=1.;for(int i=0;i<5;i++){v+=a*n3(p*fr);fr*=2.;a*=.5;}return v;}

void main(){
    float h   = fragUV.y;
    float lat = (fragUV.x - 0.5) * 2.0;  // -1 centro eje, ±1 borde

    // Gaussiana radial: ajustada para un chorro muy colimado
    float density = exp(-lat * lat * 14.0);

    // FBM: filamentos de plasma fluyendo
    vec3 np = vec3(lat * 2.0, h * 7.0 - time * 2.5, time * 0.4);
    density *= 0.35 + 0.85 * fbm(np);

    // Atenuacion longitudinal
    density *= exp(-h * 0.5);

    // Pulsacion sutil
    density *= 1.0 + 0.1 * sin(time * visualSpin * 2.0);

    if(density < 0.005) discard;

    // Color HDR
    vec3 cCore = vec3(5.0, 8.0, 18.0);
    vec3 cMid  = vec3(0.5, 1.0,  4.0);
    vec3 cEdge = vec3(0.08, 0.15, 0.8);
    float a    = abs(lat);
    vec3 color = mix(cCore, cMid, a);
    color      = mix(color, cEdge, a*a);
    color     += exp(-lat*lat*70.) * cCore * 1.8;  // halo brillante en el eje
    color     *= exp(-h * 0.20);

    fragColor = vec4(color * density, clamp(density * 0.92, 0., 0.92));
}
)GLSL";

// ─────────────────────────────────────────────────────────────
struct PulsarRenderer {
    Shader jetShader  = {};
    Model  planeModel = {};
    bool   ready      = false;
    int    jLoc_time  = -1;
    int    jLoc_spin  = -1;

    void Init(){
        jetShader  = LoadShaderFromMemory(PSR_VERT, PSR_JET_FRAG);
        jLoc_time  = GetShaderLocation(jetShader, "time");
        jLoc_spin  = GetShaderLocation(jetShader, "visualSpin");
        Mesh m     = GenMeshPlane(1.f, 1.f, 1, 1);
        planeModel = LoadModelFromMesh(m);
        ready      = true;
    }

    // Matriz de transformacion para el billboard axial del jet.
    //
    // IMPORTANTE — layout del struct Matrix de raylib (column-major OpenGL):
    //   La primera línea del struct (m0,m4,m8,m12) NO es la primera COLUMNA,
    //   son los elementos [Row0,Col0], [Row0,Col1], [Row0,Col2], [Row0,Col3].
    //   Es decir, el struct almacena los elementos ROW por ROW aunque la
    //   GPU los interpreta como COLUMN-MAJOR. Para poner el vector 'v' en la
    //   COLUMNA k de la matriz (= eje local k → espacio mundo), hay que
    //   repartir sus componentes en la posicion de COLUMNA k de cada fila:
    //     Col0 row0 = m0, Col0 row1 = m1, Col0 row2 = m2, Col0 row3 = m3
    //     Col1 row0 = m4, Col1 row1 = m5, ...
    //   Entonces la inicializacion correcta para (Col0=X, Col1=Y, Col2=Z, Col3=T) es:
    //     {X.x, Y.x, Z.x, T.x,   <- m0  m4  m8  m12   (Row0)
    //      X.y, Y.y, Z.y, T.y,   <- m1  m5  m9  m13   (Row1)
    //      X.z, Y.z, Z.z, T.z,   <- m2  m6  m10 m14   (Row2)
    //      0,   0,   0,   1  }   <- m3  m7  m11 m15   (Row3)
    //
    //  Mapeo de ejes locales del plano → mundo:
    //    local X (fragUV.x) → right  (ancho del jet, perpendicular a su eje)
    //    local Y             → normal (hacia la camara, no importa en additive)
    //    local Z (fragUV.y) → jetAxis (a lo largo del jet, de estrella a punta)
    static Matrix JetMatrix(Vector3 jetOrigin, Vector3 jetAxis,
                              Vector3 right,
                              float jetLen, float jetWidth){
        Vector3 norm   = Vector3Normalize(Vector3CrossProduct(right, jetAxis));
        // Centro del plano: mitad del jet desde su origen
        Vector3 center = Vector3Add(jetOrigin, Vector3Scale(jetAxis, jetLen * 0.5f));
        // Col0=right*w, Col1=norm, Col2=jetAxis*len, Col3=center
        Matrix m = {
            right.x*jetWidth, norm.x, jetAxis.x*jetLen, center.x,  // m0,m4,m8,m12  (Row0)
            right.y*jetWidth, norm.y, jetAxis.y*jetLen, center.y,  // m1,m5,m9,m13  (Row1)
            right.z*jetWidth, norm.z, jetAxis.z*jetLen, center.z,  // m2,m6,m10,m14 (Row2)
            0.f,              0.f,    0.f,              1.f        // m3,m7,m11,m15 (Row3)
        };
        return m;
    }

    // Dibuja la estrella de neutrones en 3D (dentro de BeginMode3D).
    // Radio FIJO en draw-units para que escale correctamente con el zoom:
    //   - Al acercarse → se ve más grande (como cualquier objeto 3D)
    //   - Al alejarse  → se ve más pequeño
    // El zoom mínimo ya está garantizado en ~3 draw-units por main.cpp,
    // así que starR=0.08 da ~35px a esa distancia — visible pero no enorme.
    // Antes era 2D (DrawCircleGradient en pixeles fijos) → siempre igual de
    // pequeño sin importar el zoom, y relativamente más grande al alejarse.
    void DrawPulsarStar3D(const Body& b) const {
        if(!ready) return;
        if(b.stellarPhase != StellarPhase::PULSAR &&
           b.stellarPhase != StellarPhase::MAGNETAR) return;

        Vector3 pos = ToDrawPos(b.pos);
        DrawSphere(pos, 0.08f, {220, 235, 255, 255});
    }

    // Dibuja los jets 3D. Llamar dentro de BeginMode3D.
    void DrawPulsarJets(const Body& b, const Camera3D& cam, float flareClock){
        if(!ready) return;
        if(b.stellarPhase != StellarPhase::PULSAR &&
           b.stellarPhase != StellarPhase::MAGNETAR) return;

        Vector3 pos       = ToDrawPos(b.pos);
        float   distToCam = Vector3Distance(pos, cam.position);
        float   t         = (float)flareClock;
        float   vSpin     = (b.spinRateDeg > 1e4f) ? 6.f : 1.5f;

        // Eje de rotacion del pulsar
        float tiltR = b.axialTilt  * DEG2RAD;
        float rotR  = b.rotationAngle * DEG2RAD;
        Vector3 poleN = Vector3Normalize({
            sinf(tiltR)*cosf(rotR), cosf(tiltR), sinf(tiltR)*sinf(rotR)
        });
        Vector3 poleS = Vector3Negate(poleN);

        // Escala de los jets: adaptable a la distancia de camara,
        // nunca menos de 1 draw-unit para ser visibles de cerca
        float jetLen  = std::clamp(distToCam * 0.45f, 1.0f, 100.f);
        float jetWidth= jetLen * 0.15f;

        // Direccion lateral del billboard: perp al jet y a la camara
        Vector3 camDir   = Vector3Normalize(Vector3Subtract(cam.position, pos));
        Vector3 camRight = Vector3Normalize(Vector3CrossProduct(poleN, camDir));
        if(Vector3Length(camRight) < 0.01f)
            camRight = Vector3Normalize(Vector3CrossProduct(poleN, {1.f,0.f,0.f}));

        SetShaderValue(jetShader, jLoc_time, &t,     SHADER_UNIFORM_FLOAT);
        SetShaderValue(jetShader, jLoc_spin, &vSpin, SHADER_UNIFORM_FLOAT);
        planeModel.materials[0].shader = jetShader;

        // Segunda direccion perpendicular al jet Y a camRight: rotar 90° alrededor
        // del eje del jet. Con 2 planos a 90° en BLEND_ADDITIVE el jet parece
        // un haz 3D circular en vez de una cinta plana.
        Vector3 camRight2 = Vector3Normalize(Vector3CrossProduct(camRight, poleN));

        BeginBlendMode(BLEND_ADDITIVE);
            for(int s = 0; s < 2; s++){
                Vector3 axis = (s==0) ? poleN : poleS;
                // Plano 1: perpendicular a la camara (siempre visible)
                planeModel.transform = JetMatrix(pos, axis, camRight, jetLen, jetWidth);
                DrawModel(planeModel, {0,0,0}, 1.f, WHITE);
                // Plano 2: girado 90° alrededor del eje del jet → aspecto 3D/volumetrico
                planeModel.transform = JetMatrix(pos, axis, camRight2, jetLen, jetWidth);
                DrawModel(planeModel, {0,0,0}, 1.f, WHITE);
            }
        EndBlendMode();
        planeModel.transform = MatrixIdentity();
    }

    // Dibuja el glow 2D de la NS. Llamar FUERA de BeginMode3D (en 2D).
    // Siempre el mismo tamaño en pantalla sin importar la distancia de camara.
    void DrawPulsarStar2D(const Body& b, const Camera3D& cam) const {
        if(!ready) return;
        if(b.stellarPhase != StellarPhase::PULSAR &&
           b.stellarPhase != StellarPhase::MAGNETAR) return;

        Vector3 pos = ToDrawPos(b.pos);
        Vector2 sp  = GetWorldToScreen(pos, cam);

        // Recortar si esta detras de la camara o muy fuera de pantalla
        if(sp.x < -200.f || sp.y < -200.f) return;

        // Glow fijo en pixels: neutron star = punto blanco-azul brillante
        DrawCircleGradient(sp, 28.f, {80,140,255,60},  {0,0,0,0});
        DrawCircleGradient(sp, 12.f, {180,210,255,140},{0,0,0,0});
        DrawCircle((int)sp.x, (int)sp.y,  4, {230,245,255,220});
        DrawCircle((int)sp.x, (int)sp.y,  2, {255,255,255,255});
    }

    void Unload(){
        if(!ready) return;
        UnloadShader(jetShader);
        UnloadModel(planeModel);
        ready = false;
    }
};

inline PulsarRenderer& GetPulsarRenderer(){
    static PulsarRenderer inst;
    return inst;
}
