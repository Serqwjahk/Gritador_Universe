#pragma once
#include <vector>
#include <string>
#include "raylib.h"
#include "body.h"
#include "constants.h"
#include "math_utils.h"

// ============================================================
//  Interfaz de usuario
// ============================================================

enum SpawnMode { MODE_SELECT, MODE_STATIC, MODE_ORBIT, MODE_LAUNCH };

// ── Botón básico ─────────────────────────────────────────────
inline bool DrawButton(Rectangle rect, const char* text, bool active) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), rect);
    Color bg   = active     ? GetColor(0x4361eeff)
               : hover      ? GetColor(0x3f3f5aff)
                            : GetColor(0x1a1a2eff);
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 1, active ? WHITE : GetColor(0x8899aaff));
    DrawText(text, (int)rect.x + 10, (int)rect.y + 8, 12, RAYWHITE);
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// ── Preview 3D del cursor ───────────────────────────────────
inline void DrawCursorPreview(SpawnMode currentMode,
                               const CatalogItem& catalogEntry,
                               const Vector3& hitWorld,
                               const Vector3D& hitPhysical,
                               const Camera3D& camera,
                               const std::vector<Body>& bodies,
                               int selectedBodyIdx,
                               float orbitEccentricity)
{
    // Mismo calculo de radio "inflado" que DrawBody() en renderer.h: si
    // el radio real proyectaria menos de 3px en pantalla, se infla para
    // mantener un tamano minimo visible. El preview DEBE usar exactamente
    // esta formula para que el tamano coincida con el cuerpo ya "spawneado".
    float rawR    = (float)(catalogEntry.radius * RENDER_SCALE);
    float dist3d  = Vector3Distance(camera.position, hitWorld);
    float screenR = (rawR / dist3d) * (float)GetScreenHeight() / tanf(camera.fovy * 0.5f * DEG2RAD);
    float pR = rawR;
    if (screenR < 3.0f && !catalogEntry.isStar)
        pR = dist3d * 3.0f * tanf(camera.fovy * 0.5f * DEG2RAD) / (float)GetScreenHeight();
    pR = std::max(0.0001f, pR);

    // Inclinacion axial del "fantasma": misma rotacion FIJA alrededor del
    // eje X de mundo que TidalBodyTransform (renderer.h) aplica al cuerpo ya
    // spawneado (MatrixRotateX(axialTilt*DEG2RAD)) -- rlRotatef toma grados,
    // igual que catalogEntry.axialTilt, asi que el preview queda orientado
    // exactamente como el cuerpo real al hacer clic.
    auto DrawTiltedPreview = [&](Vector3 center, Color wireColor) {
        rlPushMatrix();
        rlTranslatef(center.x, center.y, center.z);
        rlRotatef(catalogEntry.axialTilt, 1.0f, 0.0f, 0.0f);
        DrawSphere({0,0,0}, pR, Fade(catalogEntry.color, 0.6f));
        DrawSphereWires({0,0,0}, pR, 10, 10, wireColor);
        rlPopMatrix();
    };

    // Dibuja la elipse real de la orbita que producira SpawnBody (vis-viva,
    // ver MODE_ORBIT en main.cpp): el punto de spawn es el PERIAPSIS de una
    // orbita de excentricidad 'ecc' alrededor de 'center'.
    //   a = r_p / (1-e)          (semi-eje mayor)
    //   b = a * sqrt(1-e^2)      (semi-eje menor)
    //   centro_elipse = center - a*e*u   (u = direccion centro->periapsis)
    // En e=0: a=b=r_p y centro_elipse=center -> circulo previo, sin cambios.
    auto DrawOrbitEllipsePreview = [&](Vector3 center, Vector3 periapsisDir, float rPeriapsis, float ecc) {
        Vector3 u = Vector3Normalize(periapsisDir);
        Vector3 v = Vector3CrossProduct({0,1,0}, u);
        if (Vector3LengthSqr(v) < 1e-12f) v = Vector3CrossProduct({1,0,0}, u);
        v = Vector3Normalize(v);

        float a = rPeriapsis / (1.0f - ecc);
        float b = a * std::sqrt(1.0f - ecc*ecc);
        Vector3 ellipseCenter = Vector3Subtract(center, Vector3Scale(u, a * ecc));

        const int N = 72;
        Vector3 prev{};
        for (int i = 0; i <= N; ++i) {
            float theta = 2.0f * PI * (float)i / (float)N;
            Vector3 pt = Vector3Add(ellipseCenter,
                           Vector3Add(Vector3Scale(u, a * cosf(theta)), Vector3Scale(v, b * sinf(theta))));
            if (i > 0) DrawLine3D(prev, pt, Fade(GREEN, 0.28f));
            prev = pt;
        }
    };

    if (currentMode == MODE_STATIC) {
        DrawTiltedPreview(hitWorld, Fade(WHITE, 0.8f));
    }
    else if (currentMode == MODE_ORBIT) {
        const Body* c = (selectedBodyIdx >= 0 && selectedBodyIdx < (int)bodies.size() && bodies[(size_t)selectedBodyIdx].mass > 0.0)
                      ? &bodies[(size_t)selectedBodyIdx]
                      : FindNearestBodyAtPoint(bodies, hitPhysical);
        if (c) {
            Vector3 cR        = ToDrawPos(c->pos);
            float   orbitR    = (float)((hitPhysical - c->pos).length() * RENDER_SCALE);
            Vector3 previewPos = hitWorld;

            if (c->isStar && c->radius > 0.0) {
                // Mismo algoritmo que SpawnBody: interseccion del rayo con el
                // plano horizontal que pasa por el centro de la estrella
                Ray     mRay  = GetSafeMouseRay(camera);
                Vector3 diff  = RayHorizontalOffset(mRay, cR);
                float   xzD   = std::sqrt(diff.x*diff.x + diff.z*diff.z);
                orbitR        = std::max((float)(c->radius * RENDER_SCALE * 1.1f), xzD);
                float dx = (xzD > 0.001f) ? diff.x/xzD : 1.0f;
                float dz = (xzD > 0.001f) ? diff.z/xzD : 0.0f;
                previewPos = { cR.x + dx*orbitR, cR.y, cR.z + dz*orbitR };
            }

            float ecc = std::clamp(orbitEccentricity, 0.0f, 0.95f);
            DrawOrbitEllipsePreview(cR, Vector3Subtract(previewPos, cR), orbitR, ecc);
            DrawTiltedPreview(previewPos, GREEN);
        } else {
            DrawTiltedPreview(hitWorld, GREEN);
        }
    }
    else if (currentMode == MODE_LAUNCH) {
        // pR ya replica el radio "inflado" minimo con el que se dibuja el
        // cuerpo una vez lanzado (ver arriba) -- antes este preview usaba
        // un radio fijo (0.5) sin relacion con catalogEntry.radius, mucho
        // mayor que el cuerpo real para la mayoria de objetos.
        DrawLine3D(camera.position, hitWorld, RED);
        DrawSphereWires(hitWorld, pR, 8, 8, RED);
    }
}
