#pragma once
#include <vector>
#include <cmath>
#include "raylib.h"
#include "rlgl.h"
#include "body.h"
#include "constants.h"
#include "math_utils.h"
#include "textures.h"
#include "catalog.h"   // SpawnFromCatalog + CatalogItem
#include "renderer.h"  // DrawBody, UploadLightUniforms, RENDER_SCALE
#include "imgui.h"

// ============================================================
//  PreviewRenderer — vista isometrica 3D del cuerpo seleccionado
//  para el panel inspector.
// ============================================================
struct PreviewRenderer {
    RenderTexture2D inspRT = {};
    bool ready = false;

    void Init() {
        if (ready) return;
        inspRT = LoadRenderTexture(320, 240);
        SetTextureFilter(inspRT.texture, TEXTURE_FILTER_BILINEAR);
        ready  = true;
    }

    void Unload() {
        if (!ready) return;
        UnloadRenderTexture(inspRT);
        ready = false;
    }

    // Render del body al RT del inspector.
    // Debe llamarse ANTES de rlImGuiBegin() cada frame que haya cuerpo seleccionado.
    void RenderInspector(Body& b,
                         Model& model, const Texture2D& blank,
                         Shader& shader, const ShaderLocs& locs)
    {
        if (!ready) return;
        Body proxy = b;
        proxy.pos = {0, 0, 0};
        proxy.vel = {0, 0, 0};

        float rR = (float)(proxy.radius * RENDER_SCALE);
        // Objetos compactos (NS/Pulsar/Magnetar) tienen rR ≈ 1e-4:
        // la cámara quedaría en 9e-4 draw-units, detrás del near-plane
        // fijo de raylib (0.01). Normalizar a mínimo 0.025 para que
        // la cámara siempre esté por delante del near-plane.
        if (rR < 0.025f) {
            proxy.radius      = 0.025 / (double)RENDER_SCALE;
            rR                = 0.025f;
            // Evitar achatamiento falso por RotationalOblateness con radio inflado
            proxy.spinRateDeg              = 0.0f;
            proxy.criticalRotationFraction = 0.0f;
        }
        Camera3D cam = MakeIsoCamera(rR, proxy.isStar);

        // Fix 1: forzar iluminacion artificial en el preview independiente
        // del toggle global. El usuario puede desactivarla en el simulador
        // pero la miniatura del inspector siempre necesita verse iluminada.
        bool savedFakeLight = g_fakeLightEnabled;
        g_fakeLightEnabled  = true;

        // Fix 2: main.cpp ajusta rlSetClipPlanes dinamicamente segun
        // orbitCam.radius (nearPlane = radius * 0.001). Al alejar mucho la
        // camara, nearPlane puede superar la distancia camara-cuerpo en el
        // preview (rR * 3.5), haciendo que el cuerpo caiga detras del near
        // plane y quede invisible. Restaurar planos propios para el preview.
        double savedNear   = rlGetCullDistanceNear();
        double savedFar    = rlGetCullDistanceFar();
        float  previewFar  = std::max(2000.0f, rR * 12.0f);
        rlSetClipPlanes(0.001, (double)previewFar);

        Vector3D savedOrigin = g_renderOrigin;
        g_renderOrigin = {0, 0, 0};

        std::vector<Body> fakeBodies = {proxy};

        BeginTextureMode(inspRT);
        ClearBackground({5, 5, 8, 255});
        BeginMode3D(cam);
        UploadLightUniforms(shader, locs, fakeBodies, cam);
        DrawBody(proxy, cam, fakeBodies, model, blank, -1, 0, true, shader, locs);
        EndMode3D();
        EndTextureMode();

        g_renderOrigin    = savedOrigin;
        rlSetClipPlanes(savedNear, savedFar);
        g_fakeLightEnabled = savedFakeLight;
    }

    // Mostrar en ImGui (UV invertida verticalmente — quirk de raylib RT)
    void ShowInspector(float w, float h) {
        if (!ready) return;
        ImGui::Image((ImTextureID)(uintptr_t)inspRT.texture.id,
                     ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
    }

private:
    static Camera3D MakeIsoCamera(float drawRadius, bool isStar) {
        Vector3 dir = Vector3Normalize({1.0f, 0.75f, 1.0f});
        // Estrellas necesitan más espacio para que el halo (hasta 3.5× radio) quepa
        float dist = drawRadius * (isStar ? 9.0f : 3.5f);
        Camera3D cam{};
        cam.position   = Vector3Scale(dir, dist);
        cam.target     = {0.0f, 0.0f, 0.0f};
        cam.up         = {0.0f, 1.0f, 0.0f};
        cam.fovy       = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
        return cam;
    }
};

inline PreviewRenderer& GetPreviewRenderer() {
    static PreviewRenderer inst;
    return inst;
}

// ============================================================
//  CatalogPreviewCache — miniaturas 3D por cada item del catálogo.
//
//  Una RenderTexture2D de THUMB_SIZE × THUMB_SIZE por cada CatalogItem.
//  Se pre-renderizan TODAS en RenderAll(), llamado UNA SOLA VEZ antes
//  del bucle principal (después de que model/shader/tex estén listos).
//  Durante el juego, ShowEntry() simplemente muestra el RT cacheado.
//
//  Framing: todos los cuerpos se normalizan a radius = 1.0 / RENDER_SCALE
//  para que rR = 1.0 en draw-units. Así la cámara fija a dist=3.5 / 9.0
//  enmarca correctamente cualquier cuerpo sin importar su tamaño real,
//  y los near/far por defecto de raylib (0.01 / 1000) nunca producen
//  clipping (la esfera siempre tiene radio 1.0 draw-unit, bien dentro
//  de esa ventana).
// ============================================================
struct CatalogPreviewCache {
    static constexpr int THUMB_SIZE = 128;  // píxeles cuadrados por miniatura

    std::vector<RenderTexture2D> rts;
    bool initialized = false;

    // Referencias prestadas de main (válidas todo el ciclo de vida del programa)
    Model*                          storedModel  = nullptr;
    const GlobalTextures*           storedTex    = nullptr;
    Shader*                         storedShader = nullptr;
    const ShaderLocs*               storedLocs   = nullptr;
    const std::vector<CatalogItem>* storedDb     = nullptr;

    // Renderiza todas las miniaturas. Llamar UNA vez, antes del main loop,
    // después de que model/tex/shader/sLocs estén listos.
    void RenderAll(const std::vector<CatalogItem>& db,
                   Model& model, const GlobalTextures& tex,
                   Shader& shader, const ShaderLocs& locs)
    {
        // Guardar referencias para re-renders futuros (items aleatorios)
        storedModel  = &model;
        storedTex    = &tex;
        storedShader = &shader;
        storedLocs   = &locs;
        storedDb     = &db;

        // Liberar si ya había datos anteriores
        if (initialized) Unload();

        rts.resize(db.size());
        for (size_t i = 0; i < db.size(); ++i) {
            rts[i] = LoadRenderTexture(THUMB_SIZE, THUMB_SIZE);
            SetTextureFilter(rts[i].texture, TEXTURE_FILTER_BILINEAR);
        }

        Vector3D savedOrigin = g_renderOrigin;
        g_renderOrigin = {0, 0, 0};

        for (size_t i = 0; i < db.size(); ++i) {
            RenderEntry((int)i, db[i], model, tex, shader, locs);
            rlDrawRenderBatchActive();
        }

        g_renderOrigin = savedOrigin;
        initialized = true;
    }

    // Re-renderiza un item concreto (para previews aleatorias animadas).
    // Llamar ANTES de rlImGuiBegin(); usa las referencias guardadas en RenderAll.
    void RerenderEntry(int idx) {
        if (!initialized || !storedModel || !storedTex || !storedShader
                         || !storedLocs  || !storedDb) return;
        if (idx < 0 || idx >= (int)rts.size()) return;

        Vector3D savedOrigin = g_renderOrigin;
        g_renderOrigin = {0, 0, 0};
        RenderEntry(idx, (*storedDb)[(size_t)idx],
                    *storedModel, *storedTex, *storedShader, *storedLocs);
        rlDrawRenderBatchActive();
        g_renderOrigin = savedOrigin;
    }

    void Unload() {
        for (auto& rt : rts)
            UnloadRenderTexture(rt);
        rts.clear();
        storedModel = nullptr; storedTex = nullptr;
        storedShader = nullptr; storedLocs = nullptr; storedDb = nullptr;
        initialized = false;
    }

    // Muestra la miniatura idx en ImGui con el tamaño pedido.
    // UV invertida verticalmente (quirk de raylib render textures).
    void ShowEntry(int idx, float w, float h) const {
        if (!initialized || idx < 0 || idx >= (int)rts.size()) return;
        ImGui::Image((ImTextureID)(uintptr_t)rts[(size_t)idx].texture.id,
                     ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
    }

    // ID de textura OpenGL para uso con ImDrawList::AddImage (posición manual)
    unsigned int GetTextureId(int idx) const {
        if (!initialized || idx < 0 || idx >= (int)rts.size()) return 0;
        return rts[(size_t)idx].texture.id;
    }

private:
    void RenderEntry(int idx, const CatalogItem& item,
                     Model& model, const GlobalTextures& tex,
                     Shader& shader, const ShaderLocs& locs)
    {
        RenderTexture2D& rt = rts[(size_t)idx];

        // ── Construir proxy con datos visuales completos ──────────────
        // SpawnFromCatalog popula todas las propiedades visuales: texturas
        // (Earth), parámetros de gas giant, composición, temperatura, etc.
        Body proxy = SpawnFromCatalog(item, item.name,
                                       {0, 0, 0}, {0, 0, 0}, true, tex);
        proxy.pos = {0, 0, 0};
        proxy.vel = {0, 0, 0};

        // Calcular achatamiento real ANTES de normalizar el radio.
        // q = omega²·R³/(G·M) con los valores reales del cuerpo.
        // Luego back-calculamos el spinRateDeg que produce el mismo q
        // con el radio normalizado (1e8 m) y la misma masa real.
        // Esto preserva la oblatitud visual de Haumea y cuerpos similares.
        double q_real = 0.0;
        if (proxy.mass > 0.0 && proxy.radius > 0.0 && proxy.spinRateDeg != 0.0f) {
            double omega_real = (double)proxy.spinRateDeg * (PI_D / 180.0) / 1200.0;
            double R3_real    = proxy.radius * proxy.radius * proxy.radius;
            q_real = (omega_real * omega_real * R3_real) / (G * proxy.mass);
            q_real = std::min(q_real, 0.5); // mismo clamp que RotationalOblateness
        }

        // Normalizar el radio a 1.0 draw-unit DESPUÉS de SpawnFromCatalog para
        // que el framing de cámara sea uniforme entre todos los cuerpos.
        // Esto preserva todas las propiedades visuales (shader, textura, color,
        // isGasGiant/isRockyPlanet, etc.) — solo cambia la escala geométrica.
        proxy.radius = 1.0 / (double)RENDER_SCALE;

        // Back-calcular spinRateDeg para que RotationalOblateness devuelva
        // el mismo q_real pero con el radio normalizado.
        // omega_needed = sqrt(q_real * G * M / R_norm³)
        // spinRateDeg  = omega_needed * (180/PI) * 1200
        if (q_real > 1e-9 && proxy.mass > 0.0) {
            double R3_norm = proxy.radius * proxy.radius * proxy.radius;
            double omega_needed = std::sqrt(q_real * G * proxy.mass / R3_norm);
            proxy.spinRateDeg = (float)(omega_needed * (180.0 / PI_D) * 1200.0);
        } else {
            proxy.spinRateDeg = 0.0f;
        }
        proxy.criticalRotationFraction = 0.0f;
        proxy.rotationAngle           = 25.0f;  // ángulo fijo para vista isométrica agradable
        proxy.tideVisualSquash        = 1.0f;
        proxy.tideVisualElongation    = 1.0f;

        // ── Cámara isométrica ─────────────────────────────────────────
        // rR = 1.0 siempre (por la normalización de radio de arriba).
        const float rR_norm = 1.0f;
        bool needsHaloRoom  = proxy.isStar;  // NS/Pulsar/Magnetar también son isStar
        float dist = rR_norm * (needsHaloRoom ? 9.0f : 3.5f);

        Camera3D cam{};
        cam.position   = Vector3Scale(Vector3Normalize({1.0f, 0.75f, 1.0f}), dist);
        cam.target     = {0.0f, 0.0f, 0.0f};
        cam.up         = {0.0f, 1.0f, 0.0f};
        cam.fovy       = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;

        // ── Vector de cuerpos para UploadLightUniforms ────────────────
        // Para estrellas: el proxy se usa a sí mismo como fuente de luz (self-lit).
        // Para no-estrellas: vector vacío → AddFakeLight() activa
        //   automáticamente la luz direccional de relleno fija (renderer.h).
        std::vector<Body> lightBodies;
        if (proxy.isStar) lightBodies = {proxy};

        // ── Render ───────────────────────────────────────────────────
        // Igual que RenderInspector: forzar fake light y clip planes propios
        // para que RerenderEntry (llamado en el loop) no sufra los mismos bugs.
        bool savedFakeLight = g_fakeLightEnabled;
        g_fakeLightEnabled  = true;
        double savedNear = rlGetCullDistanceNear();
        double savedFar  = rlGetCullDistanceFar();
        rlSetClipPlanes(0.001, 2000.0); // dist max catalog = 9.0 draw-units

        BeginTextureMode(rt);
        ClearBackground({5, 5, 8, 255});
        BeginMode3D(cam);

        UploadLightUniforms(shader, locs, lightBodies, cam);
        DrawBody(proxy, cam, lightBodies, model, *tex.blank, -1, 0, true, shader, locs);

        // Agujeros negros: DrawBody devuelve una esfera negra invisible.
        // Añadir un sutil halo naranja para que sean reconocibles en la miniatura.
        if (proxy.stellarPhase == StellarPhase::BLACK_HOLE) {
            BeginBlendMode(BLEND_ADDITIVE);
            rlDisableDepthMask();
            DrawSphereEx({0.0f, 0.0f, 0.0f}, rR_norm * 2.8f, 10, 10,
                         Fade({255, 140, 30, 255}, 0.12f));
            DrawSphereEx({0.0f, 0.0f, 0.0f}, rR_norm * 1.5f, 10, 10,
                         Fade({200, 100, 20, 255}, 0.08f));
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            EndBlendMode();
        }

        EndMode3D();
        EndTextureMode();

        rlSetClipPlanes(savedNear, savedFar);
        g_fakeLightEnabled = savedFakeLight;
    }
};

inline CatalogPreviewCache& GetCatalogCache() {
    static CatalogPreviewCache inst;
    return inst;
}
