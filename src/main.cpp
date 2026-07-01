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
#include "solar_system_template.h"
#include "stellar_evolution.h"
#include "ui.h"
#include "camera_input.h"
#include "blackhole_renderer.h"
#include "pulsar_renderer.h"

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

// Reloj de llamaradas, escalado por velocidad de simulacion (ver constants.h)
double g_flareClock = 0.0;

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
static void UpdateBodiesState(std::vector<Body>& bodies,
                               std::vector<DustParticle>& dust) {
    // Limpiar remanentes de supernova expirados (mass=-1.0 es señal interna)
    bodies.erase(std::remove_if(bodies.begin(), bodies.end(),
        [](const Body& b){ return b.mass < 0.0; }), bodies.end());

    struct StarCache { Vector3D pos; double lum; };
    std::vector<StarCache> stars;
    for (const Body& s : bodies)
        if ((s.isStar || s.isSupernovaRemnant) && s.luminosity > 0.0)
            stars.push_back({s.pos, s.luminosity});

    for (Body& b : bodies) {
        // Evolución estelar y pulsaciones ANTES de temperatura
        if (b.isStar || b.isSupernovaRemnant) {
            UpdateStellarEvolution(b, TIME_STEP, bodies, dust);
            if (b.isStar) {
                StellarPulsationUpdate(b, TIME_STEP);
                // Aplicar pulsación en visualLuminosity (NUNCA acumular en base)
                if (b.pulsationAmplitude > 0.001f)
                    b.visualLuminosity = b.baseLuminosity
                                       * (1.0 + b.pulsationAmplitude * std::sin(b.pulsationPhase));
                else
                    b.visualLuminosity = b.baseLuminosity;
                b.luminosity = b.visualLuminosity;
            }
        }

        if (b.isStar) {
            // Temperatura DESPUÉS de evolución: usa radius/luminosity actualizados
            if (b.radius > 0.0 && b.luminosity > 0.0)
                b.temperature = std::pow(std::max(1e-12, b.luminosity)
                                         / (4.0*PI_D*SIGMA*b.radius*b.radius), 0.25);
        } else if (!b.isSupernovaRemnant) {
            double irr = 0.0;
            for (const auto& sc : stars)
                irr += sc.lum / (4.0*PI_D * std::max(1.0, (b.pos - sc.pos).lengthSqr()));
            // Equilibrio de cuerpo negro puro (sin atmosfera): para la
            // Tierra a 1 UA esto da ~255K, NO los ~288K reales -- la
            // diferencia es el efecto invernadero de la atmosfera, que
            // antes no se modelaba aqui. Sin este termino, 'temperature'
            // (que relaja hacia 'equilibriumTemp' en UpdateThermodynamics,
            // physics.h) cae por debajo de MELT_POINT (273K) para
            // CUALQUIER planeta rocoso en zona habitable sin importar su
            // atmosfera, congelando oceanos que deberian ser liquidos.
            double blackbodyTeq = (irr > 0.0) ? std::pow(irr*0.7/(4.0*SIGMA), 0.25) : 3.0;

            // Invernadero: usa baseAtmosphereDensity (inventario ESTATICO
            // de gases del planeta, ver body.h) en vez de atmosphereDensity
            // dinamica, para no crear un bucle de retroalimentacion con el
            // vapor/invernadero descontrolado de UpdateThermodynamics.
            // Constante calibrada para que la Tierra (baseAtmosphereDensity
            // 0.80) llegue a ~288K partiendo de un equilibrio de cuerpo
            // negro de ~255K.
            constexpr double GREENHOUSE_K = 0.16;
            double greenhouseBoost = 1.0 + GREENHOUSE_K * std::max(0.0, (double)b.baseAtmosphereDensity);

            // Solo fija el OBJETIVO de temperatura: 'temperature' relaja
            // hacia 'equilibriumTemp' en UpdateThermodynamics (physics.h),
            // con inercia termica -- asi un impacto puede dispararla por
            // encima de este valor sin que se sobreescriba al instante.
            b.equilibriumTemp = blackbodyTeq * greenhouseBoost;
        } // end else if (!b.isSupernovaRemnant)

        // Física de rotación crítica: mass shedding cuando criticalRotation
        // supera el umbral del tipo de objeto. Solo si hay duración simulada
        // real (dt = TIME_STEP > 0). Excluye remanentes SN ya disueltos.
        if (!b.isSupernovaRemnant && b.mass > 0.0 && b.criticalRotationFraction > 0.0f)
            ApplyRotationalBreakup(b, dust, TIME_STEP);

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
                       SpawnMode mode,
                       int selectedBodyIdx,
                       const Vector3D& hitPhys,
                       const Camera3D& cam,
                       const GlobalTextures& tex,
                       bool orbitRetrograde,
                       float orbitEccentricity)
{
    // El nombre mostrado es el del catalogo TAL CUAL (sin sufijo
    // numerico): varios cuerpos pueden compartir nombre sin problema --
    // lo que los distingue internamente es Body::id (unico, asignado al
    // construirse, ver body.h), no el nombre. SpawnCatalogBodyWithRings
    // ya copia b.name = item.name por su cuenta.
    const CatalogItem& item     = db[(size_t)catIdx];
    std::string        baseName = item.name;

    auto Spawn = [&](Vector3D pos, Vector3D vel, bool fixed) {
        // Crea el Body y le adjunta sus anillos reales si los tiene (ver
        // SpawnCatalogBodyWithRings, solar_system_template.h) -- misma
        // logica compartida con LoadRealisticSolarSystem, para que ambos
        // caminos de spawn (manual y la plantilla de sistema solar) nunca
        // diverjan.
        SpawnCatalogBodyWithRings(bodies, dustField, item, baseName, pos, vel, fixed, tex);
        Body& b = bodies.back();

        if (baseName == "Gigante Procedural") {
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

#if defined(__APPLE__)
static void SyncImGuiMouseForMacOS()
{
    ImGuiIO& io = ImGui::GetIO();

    Vector2 mouse = GetMousePosition();
    Vector2 dpi   = GetWindowScaleDPI();

    // En macOS/Retina, rlImGui/raylib pueden quedar desalineados según
    // cómo se inicialice el framebuffer. Forzamos a ImGui a usar las
    // coordenadas lógicas actuales de raylib para hit-testing.
    io.DisplaySize = ImVec2((float)GetScreenWidth(), (float)GetScreenHeight());
    io.DisplayFramebufferScale = ImVec2(dpi.x, dpi.y);

    io.MousePos = ImVec2(mouse.x, mouse.y);

    io.MouseDown[0] = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    io.MouseDown[1] = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    io.MouseDown[2] = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

    io.MouseWheel = GetMouseWheelMove();
}
#endif

// ============================================================
//  MAIN
// ============================================================
int main() {
    int windowWidth  = 1440;
    int windowHeight = 900;

#if defined(__APPLE__)
    // Tamaño inicial seguro para macOS/Retina.
    // Evita que la ventana arranque más grande que el área útil del monitor
    // y quede en un estado pseudo-fullscreen hasta redimensionar manualmente.
    windowWidth  = 1200;
    windowHeight = 760;
#endif

    unsigned int windowFlags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE;

#if defined(__APPLE__)
    windowFlags |= FLAG_WINDOW_HIGHDPI;
#endif

    SetConfigFlags(windowFlags);
    InitWindow(windowWidth, windowHeight, "Gritador Universe");

#if defined(__APPLE__)
    SetWindowMinSize(900, 560);

    Vector2 dpiScale = GetWindowScaleDPI();
    TraceLog(LOG_INFO, "macOS DPI scale: %.2f %.2f | screen: %d x %d",
             dpiScale.x, dpiScale.y, GetScreenWidth(), GetScreenHeight());
#endif

    SetTargetFPS(60);

    // ── GUI (Dear ImGui + rlImGui) ──
    rlImGuiSetup(true);
    ApplyDarkTheme();

    // ── Renderer de Pulsares / Magnetares (jets volumetricos + haz) ──
    GetPulsarRenderer().Init();

    // ── Renderer de Agujero Negro (BH shader + cubemap + bloom + ACES) ──
    // Cargado DESPUES de rlImGuiSetup para que el contexto OpenGL este
    // completamente inicializado antes de crear los FBOs y shaders.
    GetBlackholeRenderer().Init(windowWidth, windowHeight);

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
    flareShader.locs[SHADER_LOC_VERTEX_POSITION]   = GetShaderLocationAttrib(flareShader, "vertexPosition");
    flareShader.locs[SHADER_LOC_VERTEX_TEXCOORD01] = GetShaderLocationAttrib(flareShader, "vertexTexCoord");
    flareShader.locs[SHADER_LOC_MATRIX_MVP]        = GetShaderLocation(flareShader, "mvp");
    FlareShaderLocs fLocs = GetFlareShaderLocs(flareShader);

    // GenMeshArchTube reemplaza a GenMeshArchRibbon: seccion circular
    // (tubo 3D) en lugar de ribbon plano. Mismo arco, mismo shader —
    // las llamaradas estelares y los filamentos del magnetar se ven
    // redondos desde cualquier angulo en vez de planos.
    // Parametros: archHeight=0.85, phiHalf=0.55, tubeRadius=0.08,
    //             segs=16, sides=8 (octogono → muy circular con glow)
    Mesh archMesh = GenMeshArchTube(0.85f, 0.55f, 0.08f, 16, 8);
    Model archModel = LoadModelFromMesh(archMesh);
    archModel.materials[0].shader = flareShader;

    Mesh jetMesh = GenMeshRadialJet();
    Model jetModel = LoadModelFromMesh(jetMesh);
    jetModel.materials[0].shader = flareShader;

    Mesh puffMesh = GenMeshFlarePuff();
    Model puffModel = LoadModelFromMesh(puffMesh);
    puffModel.materials[0].shader = flareShader;

    // ── Escombros 3D instanciados (campo de polvo/anillos) ──
    Shader rockShader = LoadShaderFromMemory(ROCK_INSTANCE_VERT_SRC, ROCK_INSTANCE_FRAG_SRC);
    RockShaderLocs rkLocs = GetRockShaderLocs(rockShader);
    Mesh rockMesh = BuildLowPolyRockMesh();
    Material rockMaterial = LoadMaterialDefault();
    rockMaterial.shader = rockShader;
    rockMaterial.maps[MATERIAL_MAP_DIFFUSE].color = {150, 140, 130, 255};

    // ── Skybox (Vía Láctea, panorámica 2D equirrectangular) ──
    Image skyImg = LoadImage("2k_stars_milky_way.png");
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
        // HandleRotation corre SIEMPRE (uiBlocksStart solo impide EMPEZAR
        // un arrastre nuevo sobre la UI) -- así nunca se queda el cursor
        // capturado si el mouse pasa sobre un panel a mitad de un
        // arrastre ya iniciado. HandleZoom si se queda gateado: hacer
        // zoom mientras el mouse esta sobre un panel (p.ej. arrastrando
        // un slider) no debe mover la camara 3D.
        orbitCam.HandleRotation(mouseOverUI);
        if (!mouseOverUI) {
            orbitCam.HandleZoom();
        }
        // Vuelo libre (WASD/QE): independiente del mouse, se bloquea si el
        // teclado esta "ocupado" por un campo de texto de ImGui (p.ej.
        // renombrando un cuerpo en el inspector) O si se esta siguiendo
        // un cuerpo (ese cuerpo es el pivot -- ver HandlePan,
        // camera_input.h -- mirar alrededor sigue andando con
        // boton-derecho + rueda).
        orbitCam.HandlePan(input.followSelected, ImGui::GetIO().WantCaptureKeyboard);
        if (input.followSelected && input.selectedBodyIdx >= 0
            && input.selectedBodyIdx < (int)bodies.size()
            && bodies[(size_t)input.selectedBodyIdx].mass > 0.0)
        {
            // Mientras dura el vuelo de enfoque (FocusOn), redirigir el
            // objetivo al cuerpo (que puede seguir moviendose) en vez de
            // pisarlo con un snap exacto -- una vez que el vuelo termina
            // (orbitCam.flying == false), volver al seguimiento exacto
            // cuadro a cuadro de siempre (sin esto, un cuerpo en orbita
            // rapida se "escapa" de un seguimiento suave por lag real).
            if (orbitCam.flying) orbitCam.flyTargetGoal = bodies[(size_t)input.selectedBodyIdx].pos;
            else                 orbitCam.TrackBody(bodies[(size_t)input.selectedBodyIdx]);
        }
        orbitCam.UpdateFlight(GetFrameTime());
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
                    input.selectedBodyId  = bodies[(size_t)hitBody].id;
                    if (isDouble) input.followSelected = true;
                    orbitCam.FocusOn(bodies[(size_t)hitBody].pos, bodies[(size_t)hitBody].radius);
                } else {
                    input.selectedBodyIdx = -1;
                    input.selectedBodyId  = 0;
                    input.followSelected  = false;
                }
                lastClickTime = now;
                lastClickBody = hitBody;
            } else {
                SpawnBody(bodies, dustField, Database, catIdx,
                          input.mode, input.selectedBodyIdx, hitPhys,
                          orbitCam.cam, tex,
                          input.orbitRetrograde, input.orbitEccentricity);
                // SpawnBody siempre agrega exactamente 1 Body (los anillos
                // van a dustField, no a 'bodies') -- ver
                // SpawnCatalogBodyWithRings, asi que el recien colocado
                // siempre es bodies.back().
                if (!bodies.empty())
                    orbitCam.FocusOn(bodies.back().pos, bodies.back().radius);
            }
        }

        // Física
        if (!input.paused) {
            StepPhysics(bodies, dustField, TIME_STEP);
            g_simTime += TIME_STEP;
            // Tope x4: una llamarada real dura minutos/horas, una escala TOTALMENTE
            // distinta a la evolucion estelar (millones de años) -- escalar el reloj
            // de llamaradas 1:1 con TIME_STEP funciona en cámara lenta (las frena,
            // bien) pero en FAST-FORWARD alto (128x+) comprime el nacimiento/muerte
            // de una llamarada (35% del periodo, ~5-20s) a una fraccion de UN frame
            // real -- aparecen y desaparecen "de la nada" en vez de animarse. Un
            // tope de aceleracion (sin tope de frenado) evita el flash sin perder el
            // frenado correcto en cámara lenta.
            g_flareClock += GetFrameTime() * std::min(4.0, TIME_STEP / 1200.0);

            // Re-resolver la seleccion por ID estable: StepPhysics puede
            // haber borrado cuerpos (fusiones, supernovas, limpieza de
            // remanentes) en CUALQUIER posicion del vector, desplazando los
            // indices posteriores -- selectedBodyIdx ya no es de fiar tal
            // cual. Buscar por id evita que el inspector/camara terminen
            // mirando un cuerpo distinto al que el jugador eligio.
            if (input.selectedBodyId != 0) {
                int foundIdx = -1;
                for (int i = 0; i < (int)bodies.size(); ++i) {
                    if (bodies[(size_t)i].id == input.selectedBodyId) { foundIdx = i; break; }
                }
                input.selectedBodyIdx = foundIdx;
                if (foundIdx < 0) {
                    input.selectedBodyId  = 0;
                    input.followSelected  = false;
                }
            }
            UpdateBodiesState(bodies, dustField);

            // Vuelve a centrar la camara con la posicion YA actualizada del
            // cuerpo seguido. Sin esto, a velocidades de simulacion altas
            // (donde el cuerpo recorre una distancia grande por frame) la
            // camara quedaba sistematicamente un frame "atrasada".
            if (input.followSelected && input.selectedBodyIdx >= 0
                && input.selectedBodyIdx < (int)bodies.size()
                && bodies[(size_t)input.selectedBodyIdx].mass > 0.0)
            {
                // Igual que arriba: si todavia esta en vuelo de enfoque, solo
                // refrescar el objetivo (la posicion post-fisica mas fresca)
                // sin pisar el suavizado con un snap exacto a mitad de vuelo.
                if (orbitCam.flying) {
                    orbitCam.flyTargetGoal = bodies[(size_t)input.selectedBodyIdx].pos;
                } else {
                    orbitCam.TrackBody(bodies[(size_t)input.selectedBodyIdx]);
                    orbitCam.Update();
                }
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
        // Far plane adaptativo: la grilla se extiende ~radius*5*sqrt(2) draw units desde
        // la cámara en la diagonal, más el offset por la posición de la cámara (~radius).
        // radius*10 cubre ese máximo con margen holgado sin degradar la precisión del Z-buffer
        // (ratio near/far ~10000 para radius grande → 24-bit Z-buffer sin artefactos).
        float farPlane  = std::max(80000.0f, orbitCam.radius * 10.0f + 80000.0f);
        rlSetClipPlanes(nearPlane, farPlane);

        // ── Captura de la escena a textura para post-proceso BH ──
        // Toda la escena (skybox + 3D) se renderiza al sceneRT de
        // BlackholeRenderer. Despues de EndSceneCapture, BlitWithBHDistortion
        // la vuelca al framebuffer principal con la distorsion UV alrededor
        // de cualquier agujero negro presente, o sin distorsion si no hay BH.
        // El GUI/HUD se dibuja ENCIMA del resultado, sin distorsionarse.
        GetBlackholeRenderer().BeginSceneCapture();

        // Skybox cubemap (nebulosa oscura) — siempre activo como fondo
        GetBlackholeRenderer().DrawCubemapSkybox(orbitCam.cam);

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
            // halfExt = spacing*50: ~101 líneas por eje, constante sin importar el spacing.
            float  halfRaw     = orbitCam.radius * 5.0f;
            float  spacing     = std::pow(10.0f, std::ceil(std::log10(halfRaw / 50.0f) - 0.001f));
            double spacingPhys = (double)spacing / RENDER_SCALE;
            double halfExtPhys = spacingPhys * 50.0;

            double cxPhys = std::floor(g_renderOrigin.x / spacingPhys) * spacingPhys;
            double czPhys = std::floor(g_renderOrigin.z / spacingPhys) * spacingPhys;

            // Grilla con fade 2D: cada línea se divide en GRID_SEGS segmentos por mitad para
            // aplicar dos fades independientes:
            //   fadePos: por la POSICIÓN de la línea en la grilla (lejos del centro = transparente)
            //   fadeDepth: por la PROFUNDIDAD del segmento a lo largo de la línea (lejos del centro
            //              a lo largo de la propia línea = transparente)
            // Sin esto, DrawLine3D acepta un solo color por línea, así que las líneas en una
            // dirección se desvanecen (las lejanas en posición) pero las de la otra dirección
            // llegan al horizonte a plena opacidad aunque estén centradas en la grilla.
            constexpr int GRID_SEGS = 8;  // por mitad de línea (16 segmentos totales)
            double segLen = halfExtPhys / GRID_SEGS;

            rlSetLineWidth(1.5f);
            for (double gx = -halfExtPhys; gx <= halfExtPhys; gx += spacingPhys) {
                float normPos = (float)(std::abs(gx) / halfExtPhys);
                float tPos    = std::max(0.0f, 1.0f - normPos * 1.5f);
                float fadePos = tPos * tPos;
                if (fadePos < 0.001f) continue;

                // Líneas paralelas al eje Z (posicionadas en X = cxPhys+gx)
                for (int si = -GRID_SEGS; si < GRID_SEGS; si++) {
                    double zS   = czPhys + si * segLen;
                    float normZ = (float)(std::abs(zS + segLen * 0.5 - czPhys) / halfExtPhys);
                    float tZ    = std::max(0.0f, 1.0f - normZ * 1.5f);
                    DrawLine3D(ToDrawPos({cxPhys + gx, 0.0, zS}),
                               ToDrawPos({cxPhys + gx, 0.0, zS + segLen}),
                               Fade(GetColor(0x4a5fc9ff), 0.35f * fadePos * tZ * tZ));
                }

                // Líneas paralelas al eje X (posicionadas en Z = czPhys+gx)
                for (int si = -GRID_SEGS; si < GRID_SEGS; si++) {
                    double xS   = cxPhys + si * segLen;
                    float normX = (float)(std::abs(xS + segLen * 0.5 - cxPhys) / halfExtPhys);
                    float tX    = std::max(0.0f, 1.0f - normX * 1.5f);
                    DrawLine3D(ToDrawPos({xS, 0.0, czPhys + gx}),
                               ToDrawPos({xS + segLen, 0.0, czPhys + gx}),
                               Fade(GetColor(0x4a5fc9ff), 0.35f * fadePos * tX * tX));
                }
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
        // Los agujeros negros (BLACK_HOLE) se OMITEN aqui: el shader de post-proceso
        // pinta el horizonte negro directamente en el framebuffer, evitando que la
        // esfera negra de la escena se propague hacia fuera al distorsionar la textura.
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (!bodies[i].isStar
                && bodies[i].stellarPhase != StellarPhase::BLACK_HOLE)
                DrawBody(bodies[i], orbitCam.cam, bodies, planetModel, *tex.blank,
                         input.selectedBodyIdx, i, input.paused, lightingShader, sLocs);

        // Llamaradas solares: ribbon arches aditivos sobre la estrella
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (bodies[i].isStar)
                DrawStarFlares(bodies[i], flareShader, fLocs, archModel, jetModel, puffModel, 12);

        // Jets polares de pulsares: dos jets permanentes alineados con el eje
        // de rotacion real del pulsar (cambia con axialTilt y rotationAngle).
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (bodies[i].isStar)
                DrawPulsarPolarJets(bodies[i], flareShader, fLocs, jetModel);

        // Campo magnetico + jets polares del magnetar: cientos de filamentos
        // curvos dipolares con turbulencia + jets anchos y cortos.
        for (int i = 0; i < (int)bodies.size(); ++i)
            if (bodies[i].isStar)
                DrawMagnetarEffect(bodies[i], flareShader, fLocs, archModel, jetModel);


        // Campo de escombros 3D: rocas low-poly instanciadas (un draw call)
        DrawDustField3D(dustField, bodies, rockMesh, rockMaterial, rkLocs, orbitCam.cam);

        // Preview del cursor
        if (!mouseOverUI && input.mode != MODE_SELECT)
            DrawCursorPreview(input.mode, Database[(size_t)catIdx],
                              hitWorld, hitPhys, orbitCam.cam, bodies, input.selectedBodyIdx,
                              input.orbitEccentricity);

        EndMode3D();

        GetBlackholeRenderer().EndSceneCapture();

        // Vuelca la escena al framebuffer — si hay un BH aplica la
        // distorsion de UV; si no, blit directo sin shader.
        GetBlackholeRenderer().BlitWithBHDistortion(bodies, orbitCam.cam);

        // Rotulos de nombre por cuerpo (ver DrawBodyNameLabels, renderer.h):
        // texto 2D, debe ir DESPUES de EndMode3D(). No se distorsionan.
        DrawBodyNameLabels(bodies, orbitCam.cam);


        // UI (Dear ImGui / rlImGui)
        rlImGuiBegin();

        #if defined(__APPLE__)
        SyncImGuiMouseForMacOS();
        #endif

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
            DrawObjectInspector(guiState, bodies[(size_t)input.selectedBodyIdx], bodies);

        DrawMainMenu(guiState, bodies, dustField, orbitCam, input);
        DrawObjectSearchPanel(guiState, input, orbitCam, bodies);

        // Plantilla "Sistema Solar Realista" confirmada (ver DrawMainMenu,
        // gui.h): se ejecuta aqui, no en gui.h, porque necesita Database/
        // tex (gui.h se mantiene sin esa dependencia). Reinicia camara/
        // seleccion igual que ResetSimulation.
        if (guiState.requestLoadRealisticSystem) {
            LoadRealisticSolarSystem(bodies, dustField, Database, tex);
            input.selectedBodyIdx = -1;
            input.selectedBodyId  = 0;
            input.followSelected  = false;
            orbitCam.radius = 30.0f;
            orbitCam.angleX = 0.4f;
            orbitCam.angleY = 0.35f;
            orbitCam.target = {0, 0, 0};
            orbitCam.Update();
            guiState.requestLoadRealisticSystem = false;
        }

        rlImGuiEnd();

        EndDrawing();
    }

    // ── Limpieza ──
    GetPulsarRenderer().Unload();
    GetBlackholeRenderer().Unload();
    UnloadModel(archModel);
    UnloadModel(jetModel);
    UnloadModel(puffModel);
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
