#pragma once
#include "raylib.h"
#include "raymath.h"
#include "body.h"
#include "math_utils.h"
#include "ui.h"

// ============================================================
//  Cámara orbital e input de teclado/ratón
// ============================================================

struct OrbitCamera {
    Camera3D cam;
    float radius     = 30.0f;
    float angleX     = 0.4f;  // azimut
    float angleY     = 0.35f; // elevación
    Vector3D target  = {0, 0, 0};

    OrbitCamera() {
        cam.up         = {0, 1, 0};
        cam.fovy       = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
    }

    // Actualiza la posición de la cámara respecto a su target.
    // Origen flotante: el target de la cámara SIEMPRE se mapea al
    // {0,0,0} del espacio de dibujado (g_renderOrigin = target), asi
    // que cam.position = offset puro (radius * direccion), sin sumarle
    // un `t` potencialmente grande -> sin perdida de precision float32
    // del offset cuando radius es muy pequeño y target esta lejos del
    // origen del universo (p.ej. siguiendo un planeta en orbita).
    void Update() {
        g_renderOrigin = target;
        cam.target   = {0.0f, 0.0f, 0.0f};
        cam.position = {
            radius * cosf(angleY) * sinf(angleX),
            radius * sinf(angleY),
            radius * cosf(angleY) * cosf(angleX)
        };
    }

    // Arrastra el botón derecho para rotar
    void HandleRotation() {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 delta = GetMouseDelta();
            angleX += delta.x * 0.005f;
            angleY  = ClampF(angleY + delta.y * 0.005f, -1.45f, 1.45f);
        }
    }

    // Rueda del ratón para zoom: factor multiplicativo constante por
    // "tick" de rueda -> el paso ABSOLUTO se reduce automaticamente a
    // medida que radius disminuye (zoom logaritmico, frena al acercarse).
    // Sin clamp inferior atado al radio del planeta: junto con el
    // nearPlane dinamico (ver main.cpp) permite zoom infinito sobre
    // objetos pequenos como asteroides sin clipping.
    void HandleZoom() {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            radius = ClampF(radius * powf(0.88f, wheel), 0.0001f, 50000.0f);
    }

    // Si se está siguiendo un cuerpo, lo tomamos como target
    void TrackBody(const Body& b) {
        target = b.pos;
    }
};

// ── Procesamiento de teclas de atajos ───────────────────────
struct InputState {
    bool paused          = true;
    SpawnMode mode       = MODE_SELECT;
    int  selectedBodyIdx = -1;
    bool followSelected  = false;

    // Parametros de orbita para MODE_ORBIT (ver Fase 3, main.cpp): el punto
    // de spawn se trata como periapsis de una orbita de excentricidad
    // 'orbitEccentricity' (vis-viva); 'orbitRetrograde' invierte el sentido
    // de la velocidad tangencial. Controlados desde el panel "Modos de
    // Colocacion" del menu de catalogo (gui.h).
    bool  orbitRetrograde   = false;
    float orbitEccentricity = 0.0f;

    // Llama cada frame ANTES del bucle de render
    void HandleKeys(std::vector<Body>& bodies, std::vector<DustParticle>& dust) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (IsKeyPressed(KEY_SPACE))        paused = !paused;
        if (IsKeyPressed(KEY_ONE))          mode = MODE_SELECT;
        if (IsKeyPressed(KEY_TWO))          mode = MODE_STATIC;
        if (IsKeyPressed(KEY_THREE))        mode = MODE_ORBIT;
        if (IsKeyPressed(KEY_FOUR))         mode = MODE_LAUNCH;
        if (!ctrl && IsKeyPressed(KEY_F) && selectedBodyIdx >= 0) followSelected = !followSelected;
        if (IsKeyPressed(KEY_ESCAPE))     { selectedBodyIdx = -1; followSelected = false; }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) TIME_STEP = std::min(TIME_STEP*2.0, 1200.0*128.0);
        if (IsKeyPressed(KEY_LEFT_BRACKET))  TIME_STEP = std::max(TIME_STEP*0.5, 1200.0/64.0);
        if (IsKeyPressed(KEY_BACKSLASH))     TIME_STEP = 1200.0;

        bool del = IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE);
        if (del && selectedBodyIdx >= 0 && selectedBodyIdx < (int)bodies.size()) {
            bodies.erase(bodies.begin() + selectedBodyIdx);
            selectedBodyIdx = -1;
            followSelected  = false;
        }

        // CTRL+D: vaciar el campo de polvo (escombros/dust trails). 'dust'
        // es un Particle Pool de tamano FIJO (ver DustParticle::active,
        // body.h) -- "vaciar" significa liberar todos los slots, nunca
        // dust.clear() (eso destruiria la preasignacion).
        if (ctrl && IsKeyPressed(KEY_D)) {
            for (DustParticle& d : dust) d.active = false;
        }

        // CTRL+F: eliminar SOLO escombros (isFragment==true) que no
        // orbitan nada ni tienen nada orbitandolos. Un par (i,j) se
        // considera "ligado" si su energia orbital especifica es negativa
        // (orbita cerrada): 0.5*v_rel^2 - G*(m_i+m_j)/r < 0. Un fragmento
        // sin ningun par ligado es errante y se elimina.
        //
        // IMPORTANTE: se restringe a isFragment==true a proposito.
        // Planetas y lunas colocados por el usuario (isFragment==false)
        // NUNCA se borran con esta tecla, sin importar lo que diga la
        // formula de energia -- un "Proto-Cuerpo"/luna recien acrecionado
        // tambien pasa a isFragment==false (ver MergeBodies) y queda a
        // salvo. "Objetos errantes" se referia a escombros sueltos, no a
        // cuerpos celestes legitimos.
        if (ctrl && IsKeyPressed(KEY_F)) {
            std::vector<bool> bound(bodies.size(), false);
            for (size_t i = 0; i < bodies.size(); ++i) {
                if (bodies[i].mass <= 0.0) continue;
                for (size_t j = i + 1; j < bodies.size(); ++j) {
                    if (bodies[j].mass <= 0.0) continue;
                    Vector3D relPos = bodies[i].pos - bodies[j].pos;
                    Vector3D relVel = bodies[i].vel - bodies[j].vel;
                    double r = relPos.length();
                    if (r < 1.0) continue;
                    double specificEnergy = 0.5 * relVel.lengthSqr()
                                          - G * (bodies[i].mass + bodies[j].mass) / r;
                    if (specificEnergy < 0.0) { bound[i] = true; bound[j] = true; }
                }
            }
            for (int i = (int)bodies.size() - 1; i >= 0; --i) {
                size_t ui = (size_t)i;
                if (bodies[ui].mass <= 0.0 || !bodies[ui].isFragment || bound[ui]) continue;
                bodies.erase(bodies.begin() + i);
                if (i == selectedBodyIdx)      { selectedBodyIdx = -1; followSelected = false; }
                else if (i < selectedBodyIdx)  selectedBodyIdx--;
            }
        }
    }
};

// ── Raycast sobre un plano horizontal ───────────────────────
inline bool RaycastGroundPlane(const Camera3D& cam, Vector3& outWorld, Vector3D& outPhys,
                                float planeY = 0.0f) {
    Ray ray = GetSafeMouseRay(cam);
    if (std::fabs(ray.direction.y) > 1e-8f) {
        float t = (planeY - ray.position.y) / ray.direction.y;
        if (t > 0.0f) {
            outWorld = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
            outPhys  = ToPhysPos(outWorld);
            return true;
        }
    }
    return false;
}

// ── Selección de cuerpo por click ───────────────────────────
inline int PickBody(const std::vector<Body>& bodies, const Camera3D& cam) {
    Vector3 camFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    float closestDist = FLT_MAX;
    int hitBody = -1;

    for (int i = 0; i < (int)bodies.size(); ++i) {
        if (bodies[i].mass <= 0.0) continue;
        Vector3 p  = ToDrawPos(bodies[i].pos);
        if (Vector3DotProduct(camFwd, Vector3Subtract(p, cam.position)) <= 0.0f) continue;
        Vector2 sp  = GetWorldToScreen(p, cam);
        Vector2 sp2 = GetWorldToScreen(Vector3Add(p, {(float)std::max(0.01, bodies[i].radius * RENDER_SCALE), 0, 0}), cam);
        float dtm   = Vector2Distance(GetMousePosition(), sp);
        float sr    = Vector2Distance(sp, sp2);
        if (dtm <= std::max(18.0f, sr) && dtm < closestDist) {
            closestDist = dtm;
            hitBody = i;
        }
    }
    return hitBody;
}
