// ============================================================
//  Gritador Universe — Edición Mórbida
//  main.cpp — Punto de entrada y bucle principal
// ============================================================

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

// Módulos del engine
#include "constants.h"
#include "math_utils.h"
#include "body.h"
#include "textures.h"
#include "shaders.h"
#include "physics.h"
#include "renderer.h"
#include "catalog.h"
#include "ui.h"
#include "camera_input.h"

// GUI (Dear ImGui + rlImGui)
#include "imgui.h"
#include "rlImGui.h"
#include "gui.h"

// Variable global mutable (usada en varios módulos vía extern)
double TIME_STEP = 1200.0;

// Toggle de iluminacion ambiental "falsa" (ver constants.h/UploadLightUniforms)
bool g_fakeLightEnabled = true;

// Reloj de tiempo simulado acumulado (ver constants.h/TrailPoint en body.h)
double g_simTime = 0.0;

// Origen flotante de render (ver math_utils.h): actualizado cada frame
// por OrbitCamera::Update() con el target actual de la cámara.
Vector3D g_renderOrigin = {0, 0, 0};

// ── Starfield ───────────────────────────────────────────────
static std::vector<CosmicStar> GenerateStarfield(int count = 800) {
    std::vector<CosmicStar> sf;
    sf.reserve(count);
    Color palette[] = {
        GetColor(0x8899aaff), GetColor(0xffd166cc),
        GetColor(0xbdb2ffff), GetColor(0xffffff88)
    };
    for (int i = 0; i < count; ++i) {
        float t = GetRandomValue(0, 3600) * 0.1f * DEG2RAD;
        float p = GetRandomValue(-900, 900) * 0.1f * DEG2RAD;
        float r = 6500.0f + GetRandomValue(0, 1200);
        CosmicStar s;
        s.position = { r*cosf(p)*sinf(t), r*sinf(p), r*cosf(p)*cosf(t) };
        s.color    = palette[GetRandomValue(0, 3)];
        sf.push_back(s);
    }
    return sf;
}

// ── Actualización de estado de estrellas y temperaturas ─────
static void UpdateBodiesState(std::vector<Body>& bodies) {
    struct StarCache { Vector3D pos; double lum; };
    std::vector<StarCache> stars;
    for (const Body& s : bodies)
        if (s.isStar && s.luminosity > 0.0 && s.mass > 0.0)
            stars.push_back({s.pos, s.luminosity});

    for (Body& b : bodies) {
        if (b.isStar) {
            if (b.radius > 0.0)
                b.temperature = std::pow(std::max(1e-12, b.luminosity)
                                         / (4.0*PI_D*SIGMA*b.radius*b.radius), 0.25);
        } else {
            double irr = 0.0;
            for (const auto& sc : stars)
                irr += sc.lum / (4.0*PI_D * std::max(1.0, (b.pos - sc.pos).lengthSqr()));
            // Solo fija el OBJETIVO de temperatura: 'temperature' relaja
            // hacia 'equilibriumTemp' en UpdateThermodynamics (physics.h),
            // con inercia termica -- asi un impacto puede dispararla por
            // encima de este valor sin que se sobreescriba al instante.
            b.equilibriumTemp = (irr > 0.0) ? std::pow(irr*0.7/(4.0*SIGMA), 0.25) : 3.0;
        }

        if (b.heatSpike > 0.0f) b.heatSpike = std::max(0.0f, b.heatSpike - 0.008f);
        // Periodo de wrap = 2*PI*20 / (0.0008 * 0.04). Elegido para que el
        // "salto" de spinPhase al reiniciarse equivalga a un giro de
        // exactamente -2*PI*20 en la deriva de longitud (windTime*0.04) Y a
        // -2*PI en la fase de evolucion del ruido (spinPhase*0.0000016) del
        // shader de gigantes gaseosos -> ningun "clip" visible, ni en la
        // rotacion ni en la animacion de nubes/bandas.
        b.spin = std::fmod(b.spin + TIME_STEP * 0.015, 3926990.8125);

        // Actualizar trayectoria segun tamano fisico (no etiqueta): un
        // fragmento masivo resultante de un cataclismo es tan relevante
        // como cualquier planeta.
        if (b.radius > MIN_TRAIL_RADIUS) {
            b.trail.push_back({b.pos, g_simTime});
            // Descarta puntos mas viejos que TRAIL_TIME_SPAN segundos
            // SIMULADOS (ver constants.h) -- a velocidad alta esa ventana
            // de tiempo simulado pasa en pocos frames reales (el trail se
            // desvanece rapido en pantalla); a velocidad baja, en muchos
            // frames (se desvanece lento). MAX_TRAIL_POINTS acota ademas
            // la cantidad de puntos por rendimiento a velocidades muy bajas.
            while (!b.trail.empty() && g_simTime - b.trail.front().simTime > TRAIL_TIME_SPAN)
                b.trail.erase(b.trail.begin());
            if ((int)b.trail.size() > MAX_TRAIL_POINTS) b.trail.erase(b.trail.begin());
        }
    }
}

// ── Spawn de cuerpo desde el catálogo ───────────────────────
static void SpawnBody(std::vector<Body>& bodies,
                       std::vector<DustParticle>& dustField,
                       const std::vector<CatalogItem>& db,
                       int catIdx,
                       int& objectCounter,
                       SpawnMode mode,
                       int selectedBodyIdx,
                       const Vector3D& hitPhys,
                       const Camera3D& cam,
                       const GlobalTextures& tex,
                       bool orbitRetrograde,
                       float orbitEccentricity)
{
    CatalogItem item     = db[(size_t)catIdx];
    std::string baseName = item.name;
    item.name           += " " + std::to_string(objectCounter++);

    auto Spawn = [&](Vector3D pos, Vector3D vel, bool fixed) {
        Body b = SpawnFromCatalog(item, baseName, pos, vel, fixed, tex);
        b.name = item.name;
        bodies.push_back(b);

        // Gigantes gaseosos/helados: anillo de escombros 3D en orbita
        // perpetua (isRing=true), inclinado segun el axialTilt del
        // catalogo (ver SpawnPlanetaryRing, physics.h). Geometria real
        // (Saturno/Jupiter/Urano/Neptuno): multiplicadores de radio sacados
        // de las distancias reales en km de cada anillo (Wikipedia: Rings
        // of Saturn/Jupiter/Uranus/Neptune), divididas por el radio MEDIO
        // de catalogo de cada planeta (el que la sim realmente dibuja). Los
        // huecos (Cassini/Roche en Saturno, etc.) son huecos REALES: no se
        // incluye banda alguna en esos rangos, en vez de "menos densidad".
        // 'weight' por banda aproxima el brillo/densidad real relativo
        // (p.ej. el anillo B de Saturno se lleva ~la mitad del presupuesto
        // total de particulas, igual que en la realidad es el mas denso).
        // El color es el material real del anillo (silicatos/hielo/polvo),
        // distinto del color atmosferico 'b.color' del planeta.
        if (baseName == "Saturno") {
            SpawnPlanetaryRings(b, dustField, {
                { 1.149, 1.314, 0.5,  GetColor(0x4a4641ff) }, // D: muy tenue, interior
                { 1.282, 1.580, 2.0,  GetColor(0x6e6259ff) }, // C: tenue ("crepe ring")
                { 1.580, 2.019, 10.0, GetColor(0xc0b6a7ff) }, // B: el mas denso/brillante
                // 2.019-2.099: Division de Cassini, hueco real (sin banda)
                { 2.099, 2.349, 6.0,  GetColor(0x9e9281ff) }, // A: brillante
                // 2.349-2.393: Division de Roche, hueco real (sin banda)
                { 2.395, 2.420, 1.0,  GetColor(0xd8d2c4ff) }, // F: delgado y brillante
                { 2.851, 3.006, 0.3,  GetColor(0x55504aff) }, // G: muy tenue
                { 3.092, 6.000, 0.4,  GetColor(0x4a5a6eff) }, // E: tenue/difuso (recortado de 8.24x para no diluir el presupuesto)
            }, 60000);
        } else if (baseName == "Jupiter") {
            SpawnPlanetaryRings(b, dustField, {
                { 1.316, 1.753, 1.5, GetColor(0x302620ff) }, // Halo: tenue, grueso
                { 1.745, 1.839, 3.0, GetColor(0x695543ff) }, // Main ring: el mas brillante de Jupiter
                { 1.846, 2.604, 0.6, GetColor(0x1f1a17ff) }, // Gossamer de Amaltea: muy tenue
                { 1.846, 3.233, 0.4, GetColor(0x16120fff) }, // Gossamer de Tebe: el mas tenue/externo
            }, 3000);
        } else if (baseName == "Urano") {
            // 13 anillos reales, casi todos de pocos km de ancho
            // (sub-pixel individualmente): agrupados en bandas
            // representativas conservando los huecos/anchos reales.
            SpawnPlanetaryRings(b, dustField, {
                { 1.058, 1.631, 1.0, GetColor(0x3a3a3aff) }, // zeta: tenue, ancho, el mas interior
                { 1.650, 1.990, 2.0, GetColor(0x4d4d4dff) }, // cluster 6/5/4/alfa/beta/eta/gamma/delta/lambda
                { 1.990, 2.018, 4.0, GetColor(0x6b6b6bff) }, // epsilon: el mas ancho/brillante
                // hueco real entre epsilon y nu
                { 2.607, 2.757, 0.5, GetColor(0x2e2e2eff) }, // nu: tenue, polvoso
                // hueco real entre nu y mu
                { 3.391, 4.061, 0.3, GetColor(0x282828ff) }, // mu: muy tenue, polvoso (orbita de la luna Mab)
            }, 15000);
        } else if (baseName == "Neptuno") {
            SpawnPlanetaryRings(b, dustField, {
                { 1.665, 1.746, 1.5, GetColor(0x3a3f4aff) }, // Galle: tenue, ancho, el mas interior
                // hueco real entre Galle y Le Verrier
                { 2.159, 2.164, 2.0, GetColor(0x555c6bff) }, // Le Verrier: angosto, brillante
                { 2.161, 2.323, 1.0, GetColor(0x3a3f4aff) }, // Lassell/plateau: ancho, tenue, toca a Le Verrier
                { 2.321, 2.325, 1.5, GetColor(0x6b7280ff) }, // Arago: pico brillante en el borde de Lassell
                // hueco real entre Lassell/Arago y Adams
                // Adams: simplificado SIN sus 5 arcos discretos reales
                // (Fraternite, Egalite 1&2, Liberte, Courage, mantenidos por
                // resonancia con la luna Galatea) -- no son estables como
                // disco uniforme en este motor de gravedad restringida de
                // particulas; se renderiza como anillo angosto continuo.
                { 2.595, 2.601, 3.0, GetColor(0x7a8190ff) }, // Adams: angosto, el mas brillante de Neptuno
            }, 10000);
        } else if (baseName == "Gigante Procedural") {
            // Gigante aleatorio: 50% de probabilidad de tener anillos.
            // Geometria (radio interior/exterior, huecos, densidad de
            // particulas) aleatoria dentro de los rangos que cubren los
            // 4 anillos reales de arriba (1.3..2.2 radios planetarios de
            // borde interior, ancho 0.3..1.0, 0-2 huecos, 3000..60000
            // particulas). El color del material depende de
            // b.gasGiant.iceGiant -- la misma distincion de composicion
            // que ya usa el shader atmosferico procedural: gigantes
            // "helados" (Urano/Neptuno) tienen anillos de hielo/polvo
            // gris-azulado; gigantes "gaseosos" (Saturno/Jupiter) tienen
            // anillos de silicatos/polvo tierra-marron con degradado.
            auto rnd = []() { return (double)GetRandomValue(0, 1000000) / 1000000.0; };
            if (rnd() < 0.5) {
                double inner = 1.3 + rnd() * 0.9;          // 1.3 .. 2.2 radios planetarios
                double outer = inner + 0.3 + rnd() * 0.7;  // ancho 0.3 .. 1.0 radios

                std::vector<std::pair<double,double>> gaps;
                double gapRoll = rnd();
                int gapCount = (gapRoll < 0.35) ? 0 : (gapRoll < 0.80 ? 1 : 2);
                for (int i = 0; i < gapCount; ++i) {
                    double gapStart = inner + rnd() * (outer - inner) * 0.8;
                    double gapWidth = 0.05 + rnd() * 0.10;
                    gaps.push_back({gapStart, std::min(outer, gapStart + gapWidth)});
                }

                int particleCount = (int)(3000 + rnd() * 57000.0);

                std::vector<std::pair<double, Color>> colorBands;
                if (b.gasGiant.iceGiant) {
                    float hue = 0.55f + (float)rnd() * 0.12f; // azul-gris (hielo/polvo), tipo Urano/Neptuno
                    float sat = (float)rnd() * 0.15f;
                    Vector3 c = HSVtoRGBVec(hue, sat, 0.30f + (float)rnd() * 0.20f);
                    colorBands.push_back({1.0, Vec3ToColor(c)});
                } else {
                    float hue = 0.06f + (float)rnd() * 0.06f; // tierra/marron (silicatos), tipo Saturno/Jupiter
                    float sat = 0.15f + (float)rnd() * 0.25f;
                    Vector3 c1 = HSVtoRGBVec(hue, sat,        0.25f + (float)rnd() * 0.15f);
                    Vector3 c2 = HSVtoRGBVec(hue, sat * 0.8f, 0.45f + (float)rnd() * 0.15f);
                    Vector3 c3 = HSVtoRGBVec(hue, sat * 0.6f, 0.65f + (float)rnd() * 0.15f);
                    colorBands.push_back({0.4,  Vec3ToColor(c1)});
                    colorBands.push_back({0.75, Vec3ToColor(c2)});
                    colorBands.push_back({1.0,  Vec3ToColor(c3)});
                }

                SpawnPlanetaryRing(b, dustField, inner, outer, gaps, particleCount, colorBands);
            }
        }
    };

    if (mode == MODE_STATIC) {
        Spawn(hitPhys, {0,0,0}, true);
    }
    else if (mode == MODE_ORBIT) {
        const Body* center = (selectedBodyIdx >= 0 && selectedBodyIdx < (int)bodies.size()
                              && bodies[(size_t)selectedBodyIdx].mass > 0.0)
                           ? &bodies[(size_t)selectedBodyIdx]
                           : FindNearestBodyAtPoint(bodies, hitPhys);
        if (center) {
            Vector3D rVec = hitPhys - center->pos;

            // Para estrellas grandes el plano Y=0 da posiciones dentro de la estrella.
            // Usamos el punto más cercano del rayo de cámara al centro de la estrella,
            // proyectado al plano XZ, como radio y dirección orbital.
            if (center->isStar && center->radius > 0.0) {
                Vector3  starR  = ToDrawPos(center->pos);
                Ray      mRay   = GetSafeMouseRay(cam);
                Vector3  diff   = RayHorizontalOffset(mRay, starR);
                float    xzD    = std::sqrt(diff.x*diff.x + diff.z*diff.z);
                float    rendR  = std::max((float)(center->radius * RENDER_SCALE * 1.1f), xzD);
                double   physR  = (double)rendR / RENDER_SCALE;
                double   dx = (xzD > 0.001f) ? diff.x/xzD : 1.0;
                double   dz = (xzD > 0.001f) ? diff.z/xzD : 0.0;
                rVec = { physR*dx, 0.0, physR*dz };
            }

            Vector3D spawnPos = center->pos + rVec;
            Vector3D tang = Cross({0,1,0}, rVec/rVec.length());
            if (tang.lengthSqr() < 1e-16) tang = Cross({1,0,0}, rVec/rVec.length());

            // Orbita eliptica/retrograda (vis-viva): el punto de spawn es el
            // periapsis de una orbita de excentricidad 'e'. Derivacion:
            // a = r/(1-e)  =>  v^2 = GM(2/r - 1/a) = GM(2/r - (1-e)/r) = GM(1+e)/r
            // En e=0 se reduce exactamente a sqrt(GM/r) (caso circular previo).
            double e = std::clamp(orbitEccentricity, 0.0f, 0.95f);
            double speed = std::sqrt(G * std::max(1.0, center->mass) / rVec.length() * (1.0 + e));
            double dir = orbitRetrograde ? -1.0 : 1.0;
            Vector3D orbitVel = center->vel + NormalizeSafe(tang) * (dir * speed);
            Spawn(spawnPos, orbitVel, false);
        } else {
            Spawn(hitPhys, {0,0,0}, false);
        }
    }
    else if (mode == MODE_LAUNCH) {
        // Velocidad de lanzamiento reducida (antes 45000 m/s, mas rapido
        // que la velocidad orbital de la Tierra -- casi imposible de
        // controlar/observar). 6000 m/s sigue siendo un impacto violento
        // pero da tiempo a ver la trayectoria antes del choque.
        Vector3D camPhys = ToPhysPos(cam.position);
        Spawn(camPhys, NormalizeSafe(hitPhys - camPhys) * 6000.0, false);
    }
}

// ============================================================
//  MAIN
// ============================================================
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1440, 900, "Gritador Universe");
    SetTargetFPS(60);

    // ── GUI (Dear ImGui + rlImGui) ──
    rlImGuiSetup(true);
    ApplyDarkTheme();

    // ── Recursos gráficos ──
    TextureStore texStore;
    texStore.Load();
    GlobalTextures tex = texStore.MakeRefs();

    Model planetModel = LoadModelFromMesh(BuildUVSphere(64, 64));
    Shader lightingShader = LoadShaderFromMemory(VERTEX_SHADER_SRC, FRAGMENT_SHADER_SRC);
    lightingShader.locs[SHADER_LOC_MATRIX_MODEL]  = GetShaderLocation(lightingShader, "matModel");
    lightingShader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lightingShader, "matNormal");
    lightingShader.locs[SHADER_LOC_COLOR_DIFFUSE]  = GetShaderLocation(lightingShader, "colDiffuse");
    lightingShader.locs[SHADER_LOC_VECTOR_VIEW]    = GetShaderLocation(lightingShader, "viewPos");

    // El shader custom debe estar en planetModel (usado para estrellas y planetas con textura)
    planetModel.materials[0].shader = lightingShader;

    ShaderLocs sLocs = GetShaderLocs(lightingShader);

    // ── Shader de llamaradas solares (ribbon arch, tutorial Blender) ──
    Shader flareShader = LoadShaderFromMemory(FLARE_VERT_SRC, FLARE_FRAG_SRC);
    FlareShaderLocs fLocs = GetFlareShaderLocs(flareShader);
    Mesh archMesh = GenMeshArchRibbon(0.85f, 0.55f, 0.35f, 16);
    Model archModel = LoadModelFromMesh(archMesh);
    archModel.materials[0].shader = flareShader;

    // ── Escombros 3D instanciados (campo de polvo/anillos) ──
    Shader rockShader = LoadShaderFromMemory(ROCK_INSTANCE_VERT_SRC, ROCK_INSTANCE_FRAG_SRC);
    RockShaderLocs rkLocs = GetRockShaderLocs(rockShader);
    Mesh rockMesh = BuildLowPolyRockMesh();
    Material rockMaterial = LoadMaterialDefault();
    rockMaterial.shader = rockShader;
    rockMaterial.maps[MATERIAL_MAP_DIFFUSE].color = {150, 140, 130, 255};

    // ── Skybox (Vía Láctea, panorámica 2D equirrectangular) ──
    Image skyImg = LoadImage("2k_stars_milky_way.jpg");
    Texture2D skyboxTex = LoadTextureFromImage(skyImg);
    UnloadImage(skyImg);

    // ── Datos de simulación ──
    std::vector<CatalogItem>   Database = BuildCatalog();
    std::vector<Body>          bodies;
    // Particle Pool de polvo/escombros 3D: tamano FIJO preasignado, todos
    // los slots inician con active=false (ver DustParticle::active, body.h
    // y FindFreeDustSlot/CountActiveDust en physics.h).
    std::vector<DustParticle>  dustField(MAX_DUST_PARTICLES);
    std::vector<CosmicStar>    starfield = GenerateStarfield();

    // ── Estado de la aplicación ──
    OrbitCamera orbitCam;
    InputState  input;
    GuiState    guiState;
    int catIdx       = 0;
    int objectCounter = 1;

    // Para doble-click de selección
    float lastClickTime  = -1.0f;
    int   lastClickBody  = -1;

    // ── Bucle principal ──────────────────────────────────────
    while (!WindowShouldClose()) {
        const int   sw     = GetScreenWidth();
        const int   sh     = GetScreenHeight();
        bool mouseOverUI = ImGui::GetIO().WantCaptureMouse;

        // Teclado
        input.HandleKeys(bodies, dustField);

        // Cámara
        if (!mouseOverUI) {
            orbitCam.HandleRotation();
            orbitCam.HandleZoom();
        }
        if (input.followSelected && input.selectedBodyIdx >= 0
            && input.selectedBodyIdx < (int)bodies.size()
            && bodies[(size_t)input.selectedBodyIdx].mass > 0.0)
        {
            orbitCam.TrackBody(bodies[(size_t)input.selectedBodyIdx]);
        }
        orbitCam.Update();

        // Raycast plano Y=0
        Vector3  hitWorld = {0,0,0};
        Vector3D hitPhys  = {0,0,0};
        RaycastGroundPlane(orbitCam.cam, hitWorld, hitPhys, orbitCam.cam.target.y);

        // Click izquierdo
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !mouseOverUI) {
            if (input.mode == MODE_SELECT) {
                int hitBody = PickBody(bodies, orbitCam.cam);
                float now   = GetTime();
                bool isDouble = (hitBody >= 0 && hitBody == lastClickBody && (now - lastClickTime) < 0.35f);
                if (hitBody >= 0) {
                    input.selectedBodyIdx = hitBody;
                    if (isDouble) input.followSelected = true;
                } else {
                    input.selectedBodyIdx = -1;
                    input.followSelected  = false;
                }
                lastClickTime = now;
                lastClickBody = hitBody;
            } else {
                SpawnBody(bodies, dustField, Database, catIdx, objectCounter,
                          input.mode, input.selectedBodyIdx, hitPhys,
                          orbitCam.cam, tex,
                          input.orbitRetrograde, input.orbitEccentricity);
            }
        }

        // Física
        if (!input.paused) {
            StepPhysics(bodies, dustField, TIME_STEP);
            g_simTime += TIME_STEP;

            if (input.selectedBodyIdx >= (int)bodies.size())
                input.selectedBodyIdx = (int)bodies.size() - 1;
            UpdateBodiesState(bodies);

            // Vuelve a centrar la camara con la posicion YA actualizada del
            // cuerpo seguido. Sin esto, a velocidades de simulacion altas
            // (donde el cuerpo recorre una distancia grande por frame) la
            // camara quedaba sistematicamente un frame "atrasada".
            if (input.followSelected && input.selectedBodyIdx >= 0
                && input.selectedBodyIdx < (int)bodies.size()
                && bodies[(size_t)input.selectedBodyIdx].mass > 0.0)
            {
                orbitCam.TrackBody(bodies[(size_t)input.selectedBodyIdx]);
                orbitCam.Update();
            }
        }

        // ── Render ──────────────────────────────────────────
        UploadLightUniforms(lightingShader, sLocs, bodies, orbitCam.cam);

        BeginDrawing();
        ClearBackground(BLACK);
        // nearPlane dinamico: escala con la distancia a la camara para
        // permitir zoom arbitrariamente cercano (asteroides, etc.) sin
        // que la geometria se recorte contra el plano cercano.
        float nearPlane = std::max(0.0001f, orbitCam.radius * 0.001f);
        rlSetClipPlanes(nearPlane, 80000.0f);

        // Skybox Vía Láctea (2D equirrectangular, sigue la orientación de la cámara)
        {
            Vector3 fwd = Vector3Normalize(Vector3Subtract(orbitCam.cam.target, orbitCam.cam.position));
            float yaw     = atan2f(fwd.x, -fwd.z);
            float pitch   = asinf(std::max(-0.999f, std::min(0.999f, fwd.y)));
            float vFovRad = orbitCam.cam.fovy * DEG2RAD;
            float hFovRad = 2.0f * atanf(tanf(vFovRad * 0.5f) * (float)sw / (float)sh);
            float uC = fmodf(yaw / (2.0f * 3.14159265f) + 1.5f, 1.0f);
            float vC = 0.5f - pitch / 3.14159265f;
            float uS = hFovRad / (2.0f * 3.14159265f);
            float vS = vFovRad / 3.14159265f;
            float uL = uC - uS * 0.5f,  uR = uL + uS;
            float srcY = (vC - vS * 0.5f) * (float)skyboxTex.height;
            float srcH = vS * (float)skyboxTex.height;
            auto drawStrip = [&](float u0, float u1, float x0, float x1) {
                DrawTexturePro(skyboxTex,
                    {u0*(float)skyboxTex.width, srcY, (u1-u0)*(float)skyboxTex.width, srcH},
                    {x0, 0.0f, x1-x0, (float)sh}, {0.0f,0.0f}, 0.0f, WHITE);
            };
            if      (uL < 0.0f) { float s = (-uL)/uS; drawStrip(uL+1.0f,1.0f,0.0f,s*sw); drawStrip(0.0f,uR,s*sw,(float)sw); }
            else if (uR > 1.0f) { float s = (1.0f-uL)/uS; drawStrip(uL,1.0f,0.0f,s*sw); drawStrip(0.0f,uR-1.0f,s*sw,(float)sw); }
            else                { drawStrip(uL, uR, 0.0f, (float)sw); }
        }

        BeginMode3D(orbitCam.cam);

        // Fondo estelar (puntos de paralaje encima del skybox)
        for (const auto& s : starfield)
            DrawPoint3D(Vector3Add(orbitCam.cam.position, s.position), s.color);

        // Cuadrícula adaptativa infinita, FIJA en espacio físico (plano
        // y=0 del universo, no en espacio de dibujado). Las líneas viven
        // en múltiplos absolutos de 'spacingPhys' en coordenadas físicas;
        // cada frame solo se elige qué ventana de esa rejilla infinita cae
        // cerca de la cámara (g_renderOrigin) para dibujarla.
        //
        // Antes la grilla se reconstruía centrada en cam.target (siempre
        // {0,0,0} en draw-space por el origen flotante == posición física
        // del cuerpo seguido): al seguir un cuerpo en movimiento la grilla
        // se desplazaba CON él, dando la ilusión óptica de que el cuerpo
        // seguido estaba quieto y el resto del universo se movía.
        {
            // Sin piso fijo: el tamano de celda escala siempre con el radio
            // de la camara (igual que el nearPlane dinamico), asi que al
            // enfocar un cuerpo pequeno (Marte, Ceres) las celdas se achican
            // con el. halfExt = spacing*50 mantiene el numero de lineas
            // dibujadas constante (~101 por eje) sea cual sea spacing.
            float  halfRaw     = orbitCam.radius * 5.0f;
            float  spacing     = std::pow(10.0f, std::ceil(std::log10(halfRaw / 50.0f) - 0.001f));
            double spacingPhys = (double)spacing / RENDER_SCALE;
            double halfExtPhys = spacingPhys * 50.0;

            double cxPhys = std::floor(g_renderOrigin.x / spacingPhys) * spacingPhys;
            double czPhys = std::floor(g_renderOrigin.z / spacingPhys) * spacingPhys;

            Color gc = Fade(GetColor(0x4a5fc9ff), 0.35f);
            rlSetLineWidth(1.5f);
            for (double gx = -halfExtPhys; gx <= halfExtPhys; gx += spacingPhys) {
                DrawLine3D(ToDrawPos({cxPhys + gx, 0.0, czPhys - halfExtPhys}),
                           ToDrawPos({cxPhys + gx, 0.0, czPhys + halfExtPhys}), gc);
                DrawLine3D(ToDrawPos({cxPhys - halfExtPhys, 0.0, czPhys + gx}),
                           ToDrawPos({cxPhys + halfExtPhys, 0.0, czPhys + gx}), gc);
            }
            rlSetLineWidth(1.0f);
        }

        // Trayectorias: el fade usa la EDAD real de cada punto (segundos
        // simulados desde que se registro, sobre TRAIL_TIME_SPAN) en vez
        // de su indice/posicion en el arreglo -- con el trimming por
        // tiempo (ver UpdateBodiesState) la cantidad de puntos varia mucho
        // con la velocidad de simulacion (pocos puntos a velocidad alta,
        // muchos a velocidad baja); un fade por indice se veria "a saltos"
        // con pocos puntos, mientras que por edad real siempre da un
        // degradado continuo sin importar cuantos puntos haya.
        for (const Body& b : bodies) {
            if (b.radius > MIN_TRAIL_RADIUS && b.trail.size() >= 2) {
                for (size_t t = 1; t < b.trail.size(); ++t) {
                    float age = (float)ClampD((g_simTime - b.trail[t].simTime) / TRAIL_TIME_SPAN, 0.0, 1.0);
                    DrawLine3D(ToDrawPos(b.trail[t-1].pos), ToDrawPos(b.trail[t].pos),
                               Fade(b.color, (1.0f - age) * 0.45f));
                }
            }
        }

        // Estrellas primero (escriben depth): planetas en frente dibujan encima,
        // planetas físicamente detrás quedan tapados correctamente.
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (bodies[i].isStar)
                DrawBody(bodies[i], orbitCam.cam, bodies, planetModel, *tex.blank,
                         input.selectedBodyIdx, i, input.paused, lightingShader, sLocs);
        // Planetas y demás después: se dibujan encima de la estrella si están más cerca.
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (!bodies[i].isStar)
                DrawBody(bodies[i], orbitCam.cam, bodies, planetModel, *tex.blank,
                         input.selectedBodyIdx, i, input.paused, lightingShader, sLocs);

        // Llamaradas solares: ribbon arches aditivos sobre la estrella
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (bodies[i].isStar)
                DrawStarFlares(bodies[i], flareShader, fLocs, archModel, 12);

        // Campo de escombros 3D: rocas low-poly instanciadas (un draw call)
        DrawDustField3D(dustField, bodies, rockMesh, rockMaterial, rkLocs, orbitCam.cam);

        // Preview del cursor
        if (!mouseOverUI && input.mode != MODE_SELECT)
            DrawCursorPreview(input.mode, Database[(size_t)catIdx],
                              hitWorld, hitPhys, orbitCam.cam, bodies, input.selectedBodyIdx,
                              input.orbitEccentricity);

        EndMode3D();

        // UI (Dear ImGui / rlImGui)
        rlImGuiBegin();
        DrawTimeControlsHUD(guiState, input.paused);
        DrawStatusOverlay(bodies, CountActiveDust(dustField), input);

        // Si el menu de catalogo se cierra este frame (boton "+" o "X" de
        // la ventana), volver a modo Seleccionar: sin esto, tras elegir un
        // modo de colocacion (Fase 2) era imposible volver a seleccionar
        // cuerpos sin un boton "Seleccionar" dedicado (ya retirado en Fase 6).
        bool catalogOpenBefore = guiState.showCatalogMenu;
        DrawAddButton(guiState);
        DrawCatalogMenu(guiState, input, Database, catIdx);
        if (catalogOpenBefore && !guiState.showCatalogMenu)
            input.mode = MODE_SELECT;

        if (input.selectedBodyIdx >= 0 && input.selectedBodyIdx < (int)bodies.size())
            DrawObjectInspector(guiState, bodies[(size_t)input.selectedBodyIdx], bodies, input.selectedBodyIdx);
        DrawMainMenu(guiState, bodies, dustField, orbitCam, input);
        rlImGuiEnd();

        EndDrawing();
    }

    // ── Limpieza ──
    UnloadModel(archModel);
    UnloadShader(flareShader);
    UnloadMaterial(rockMaterial);
    UnloadMesh(rockMesh);
    UnloadTexture(skyboxTex);
    UnloadModel(planetModel);
    UnloadShader(lightingShader);
    texStore.Unload();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
