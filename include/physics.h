#pragma once
#include <vector>
#include <utility>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include "body.h"
#include "constants.h"
#include "math_utils.h"
#include "rocky_planets.h"
#include "composition.h"
#include "thread_pool.h"
#include "stellar_evolution.h"
#include <algorithm>

// ============================================================
//  Motor de física: gravedad, colisiones, mareas, fragmentos
// ============================================================

// ── RNG rápido (LCG) ────────────────────────────────────────
static double g_rngState = 12345.6789;
inline double FastRand01() {
    g_rngState = std::fmod(g_rngState * 1664525.0 + 1013904223.0, 4294967296.0);
    return g_rngState / 4294967296.0;
}

// ── Cola de fragmentos/polvo pendientes ─────────────────────
// Un choque a alta velocidad puede disparar una cascada de colisiones
// fragmento-fragmento dentro del MISMO frame (cada nuevo fragmento puede
// chocar con otro recien creado en un sub-paso posterior), generando
// cientos de cuerpos/particulas de golpe -- bodies.size() salta de ~3 a
// ~600 en un solo frame, concentrando ahi todo el costo O(n^2)
// (ApplyTidesAndRoche) y de dibujado (un draw call por cuerpo) del
// cataclismo entero. Para suavizar ese pico, MakeFragments respeta un
// presupuesto de creacion POR FRAME; lo que excede el presupuesto se
// encola aqui y StepPhysics lo va incorporando en los frames siguientes,
// repartiendo el cataclismo a lo largo de varios frames sin cambiar el
// total final (MAX_FRAGMENTS/MAX_DUST_PARTICLES siguen siendo el tope,
// contando tambien lo encolado).
static std::vector<Body> g_pendingFragments;
static std::vector<DustParticle> g_pendingDust;
static int g_fragSpawnedThisFrame = 0;
static int g_dustSpawnedThisFrame = 0;
constexpr int FRAG_SPAWN_BUDGET_PER_FRAME = 20;
constexpr int DUST_SPAWN_BUDGET_PER_FRAME = 150;

// ── Particle Pool: helpers de polvo (ver DustParticle::active, body.h) ──
// 'dust' (dustField en main.cpp) tiene tamano FIJO = MAX_DUST_PARTICLES,
// preasignado al arrancar -- nunca se hace push_back/erase sobre el. Un
// slot libre tiene active=false.

// Cuenta los slots ocupados (active=true). 'dust.size()' ya no sirve como
// medida de ocupacion porque el vector mantiene siempre MAX_DUST_PARTICLES
// elementos.
inline int CountActiveDust(const std::vector<DustParticle>& dust) {
    int n = 0;
    for (const DustParticle& d : dust) if (d.active) ++n;
    return n;
}

// Busca el primer slot libre a partir de 'cursor' (round-robin, envuelve al
// llegar al final): evita reescanear desde el indice 0 en cada spawn cuando
// los primeros slots del pool ya estan ocupados. Devuelve -1 si el pool
// esta lleno (no deberia ocurrir: 'availableDust' en MakeFragments ya limita
// numDust al espacio libre).
// Mas alto indice de slot jamas asignado +1 ("marca de agua"): el pool
// (dustField, ver main.cpp) se PRE-ASIGNA completo a MAX_DUST_PARTICLES
// (200000) desde el arranque, pero una partida tipica usa solo una
// fraccion pequeña de eso -- sin esta marca, UpdateDustGravity/
// UpdateDustIntelligence/UpdateDustLifecycle recorrian las 200000 casillas
// EN CADA LLAMADA (UpdateDustGravity hasta 64 veces por frame a velocidad
// alta, ver PHYS_MAX_SUBSTEPS_PER_FRAME/StepPhysics) sin importar que casi
// todas estuvieran inactivas desde el inicio de la partida -- el costo
// dominante de StepPhysics incluso con un solo cuerpo y CERO polvo
// generado, y la razon de que subir la velocidad de simulacion por encima
// de x1 hundiera el framerate. Nunca decrece (no hace falta: solo acota
// el barrido a "lo mas lejos que se ha usado alguna vez", no al recuento
// de activos actual) -- una sesion que use los anillos de Saturno una vez
// se queda con la marca alta el resto de la partida, pero sigue siendo
// muchisimo mejor que recorrer las 200000 casillas siempre.
static int g_dustHighWaterMark = 0;

inline int FindFreeDustSlot(std::vector<DustParticle>& dust, int& cursor) {
    const int n = (int)dust.size();
    for (int i = 0; i < n; ++i) {
        int idx = (cursor + i) % n;
        if (!dust[idx].active) {
            cursor = (idx + 1) % n;
            if (idx + 1 > g_dustHighWaterMark) g_dustHighWaterMark = idx + 1;
            return idx;
        }
    }
    return -1;
}
static int g_dustPoolCursor = 0;

// ── Spawner de anillos planetarios (polvo isRing=true) ──────────────
// Coloca 'particleCount' particulas en un disco plano alrededor de
// 'planet', entre [planet.radius*innerRadiusMult, planet.radius*outerRadiusMult],
// inclinado segun planet.axialTilt y con velocidad orbital circular
// kepleriana exacta -- el anillo queda en orbita perpetua, alineado con
// el ecuador inclinado del planeta.
//
// Inclinacion: la misma convencion que usa el renderer para el modelo del
// planeta -- TidalBodyTransform (renderer.h) aplica MatrixRotateX(axialTilt),
// y undoAxialTilt (shaders.h) deshace esa rotacion en X, confirmando que
// RotateX(+axialTilt) es la transformacion "hacia adelante". Para un punto
// (x,y,z) con y=0 (disco en el plano XZ):
//   RotateX(theta): (x, y*cos(theta) - z*sin(theta), y*sin(theta) + z*cos(theta))
//                 = (x, -z*sin(theta), z*cos(theta))   [y=0]
// y el "polo norte" del planeta inclinado es RotateX(theta) aplicado a
// (0,1,0) = (0, cos(theta), sin(theta)).
//
// Velocidad orbital: v = sqrt(G*planet.mass/r) (orbita circular exacta a
// distancia r), en la direccion tangencial Cross(tiltedUp, dirRadial) -- la
// unica direccion perpendicular tanto al eje de rotacion del anillo
// (tiltedUp) como al vector posicion local (dirRadial), es decir, contenida
// en el plano del anillo. 'tiltedUp' se obtiene aplicando a (0,1,0) la
// MISMA rotacion (RotateAxialTilt) usada para inclinar 'localPos' -- ambos
// vectores quedan mutuamente perpendiculares (se preserva el angulo recto
// que ya tenian (0,1,0) y 'localPos' antes de inclinar), garantizando que
// 'tangent' quede SIEMPRE dentro del plano del anillo.
inline Vector3D RotateAxialTilt(const Vector3D& v, double axialTiltRad) {
    double c = std::cos(axialTiltRad), s = std::sin(axialTiltRad);
    return { v.x, v.y*c - v.z*s, v.y*s + v.z*c };
}

inline void SpawnPlanetaryRing(const Body& planet, std::vector<DustParticle>& dust,
                                double innerRadiusMult, double outerRadiusMult,
                                const std::vector<std::pair<double, double>>& gaps,
                                int particleCount,
                                const std::vector<std::pair<double, Color>>& colorBands)
{
    const double axialTiltRad = (double)planet.axialTilt * PI_D / 180.0;
    const Vector3D baseUp   = { 0.0, 1.0, 0.0 };
    const Vector3D tiltedUp = RotateAxialTilt(baseUp, axialTiltRad);

    for (int i = 0; i < particleCount; ++i) {
        int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
        if (slot < 0) break; // Pool lleno: no quedan slots libres para mas anillo.

        // Muestreo por rechazo: 'r' se sortea uniformemente en
        // [innerRadiusMult, outerRadiusMult]*planet.radius, y se descarta
        // (resorteando) si cae dentro de cualquier hueco de 'gaps' (cada
        // par es [inicio,fin] en radios planetarios) -- asi cada planeta
        // puede tener cero, uno o varios huecos (ver main.cpp para los
        // valores por planeta, basados en referencias fotograficas reales).
        double r;
        bool inGap;
        do {
            r = planet.radius * (innerRadiusMult + FastRand01() * (outerRadiusMult - innerRadiusMult));
            inGap = false;
            for (const auto& gap : gaps) {
                if (r > planet.radius * gap.first && r < planet.radius * gap.second) { inGap = true; break; }
            }
        } while (inGap);

        double theta = FastRand01() * 2.0 * PI_D;

        // Disco plano en XZ (y=0), luego inclinado sobre el eje X.
        Vector3D localPos       = { r * std::cos(theta), 0.0, r * std::sin(theta) };
        Vector3D tiltedLocalPos = RotateAxialTilt(localPos, axialTiltRad);

        DustParticle d;
        d.pos = planet.pos + tiltedLocalPos;

        Vector3D dirRadial = NormalizeSafe(tiltedLocalPos);
        Vector3D tangent   = NormalizeSafe(Cross(tiltedUp, dirRadial));
        double   vCirc     = std::sqrt(G * planet.mass / r);
        d.vel = planet.vel + tangent * vCirc;

        // Roca pequena (100-500 m, misma cota minima que el polvo de
        // colision en SpawnParticle) -- solo afecta el tamano visual
        // ('scale'), nunca su orbita ni colisiones.
        double rockR = 100.0 + FastRand01() * 400.0;
        d.radius    = rockR;

        // Degradado radial: 'colorBands' son pares (umbral, color) con
        // umbrales ascendentes de relDist (0 = borde interior del anillo,
        // 1 = borde exterior). Se usa el color de la primera banda cuyo
        // umbral supera a relDist; como r en [inner,outer)*planet.radius
        // (muestreo por rechazo de arriba), relDist siempre cae en [0,1),
        // asi que una unica banda {1.0, color} da un color plano y N
        // bandas dan un degradado de N tonos (ver perfiles por planeta en
        // main.cpp). 'back().second' es solo un respaldo defensivo ante
        // redondeo de punto flotante (relDist == 1.0 exacto).
        double relDist = (r - planet.radius * innerRadiusMult)
                       / (planet.radius * (outerRadiusMult - innerRadiusMult));
        Color rockColor = colorBands.back().second;
        for (const auto& band : colorBands) {
            if (relDist < band.first) { rockColor = band.second; break; }
        }

        // Transicion progresiva entre bandas adyacentes: cada umbral
        // colorBands[b].first (salvo el ultimo, que es el limite exterior
        // del anillo, no una transicion) separa el color de la banda b del
        // de la banda b+1. Si relDist cae a menos de BAND_BLEND_HALF_WIDTH
        // de ese umbral, se interpola (ColorLerp) entre ambos colores en
        // vez del salto brusco de arriba -- las secciones siguen siendo
        // distinguibles, pero el borde entre ellas es gradual. Con un solo
        // par (color plano) este bucle no itera y no cambia nada.
        constexpr double BAND_BLEND_HALF_WIDTH = 0.20;
        for (size_t b = 0; b + 1 < colorBands.size(); ++b) {
            double edge = colorBands[b].first;
            if (relDist > edge - BAND_BLEND_HALF_WIDTH && relDist < edge + BAND_BLEND_HALF_WIDTH) {
                float t = (float)((relDist - (edge - BAND_BLEND_HALF_WIDTH)) / (2.0 * BAND_BLEND_HALF_WIDTH));
                rockColor = ColorLerp(colorBands[b].second, colorBands[b + 1].second, t);
                break;
            }
        }
        d.color     = rockColor;
        d.heatSpike = 0.0f;
        d.seed      = (float)FastRand01();
        d.active    = true;
        d.isRing = true;       // anillo: sin muerte por fragAge (ver UpdateDustLifecycle)
        d.state = DustState::RingParticle;
        d.stableOrbitTime = 3600.0 * 24.0;
        d.hostBodyId = (int64_t)planet.id;
        d.spawnRadiusFromHost = (float)r;

        double axisCosT = 2.0 * FastRand01() - 1.0;
        double axisSinT = std::sqrt(std::max(0.0, 1.0 - axisCosT*axisCosT));
        double axisPhi  = 2.0 * PI_D * FastRand01();
        d.rotationAxis  = { (float)(axisSinT * std::cos(axisPhi)),
                             (float)axisCosT,
                             (float)(axisSinT * std::sin(axisPhi)) };
        d.rotationSpeed   = (float)((FastRand01() - 0.5) * 1.5);
        d.currentRotation = (float)(FastRand01() * 2.0 * PI_D);
        d.scale           = (float)(rockR * RENDER_SCALE);

        dust[slot] = d;
    }
}

// Banda de anillo con densidad PROPIA (no solo color, a diferencia de
// 'colorBands' en SpawnPlanetaryRing, que solo tine y reparte particulas de
// forma uniforme en toda el area no-hueco). innerMult/outerMult son
// multiplos de planet.radius para los extremos de ESTA banda; weight es un
// peso relativo de cantidad de particulas (no necesita normalizar a 1, se
// normaliza contra la suma de pesos de todas las bandas pasadas a
// SpawnPlanetaryRings) -- asi un anillo denso/brillante (p.ej. el B de
// Saturno) puede llevarse mucho mas presupuesto de particulas que uno tenue
// (p.ej. el C), en vez de la densidad uniforme por area de antes.
struct RingBand {
    double innerMult;
    double outerMult;
    double weight;
    Color  color;
};

// Anillo multi-banda: reparte 'totalParticleCount' entre 'bands' segun el
// 'weight' relativo de cada una, reusando SpawnPlanetaryRing una vez por
// banda (hereda automaticamente orbita circular/inclinacion/tumbling/
// hostBodyId de una banda simple, sin duplicar esa logica). Bandas NO
// contiguas dejan huecos REALES entre ellas -- el caller simplemente no
// incluye una banda para el rango del hueco (a diferencia de 'gaps' en
// SpawnPlanetaryRing, que sortea y descarta: aqui no se sortea nada ahi).
inline void SpawnPlanetaryRings(const Body& planet, std::vector<DustParticle>& dust,
                                 const std::vector<RingBand>& bands, int totalParticleCount)
{
    double weightSum = 0.0;
    for (const RingBand& rb : bands) weightSum += std::max(0.0, rb.weight);
    if (weightSum <= 0.0) return;

    for (const RingBand& rb : bands) {
        int count = (int)std::round(totalParticleCount * (rb.weight / weightSum));
        if (count <= 0) continue;
        // Banda unica = sin huecos internos, color plano (el degradado real
        // de cada anillo ya esta expresado como varias RingBand separadas
        // por el caller, ver main.cpp).
        SpawnPlanetaryRing(planet, dust, rb.innerMult, rb.outerMult, {}, count, {{1.0, rb.color}});
    }
}

// Frames de inmunidad de colision para fragmentos recien creados (ver
// SpawnParticle/ResolveCollisions): suficiente para que se separen del
// cuerpo de origen tras spawnear justo en su superficie, sin permitir que
// ResolveCollisions los funda/atraviese instantaneamente en el mismo frame.
// Para fragmentos catastroficos (fromTide) con la componente tangencial
// orbital (ver MakeFragments/dominantFragMass), 3 frames son insuficientes
// para que una trayectoria casi-circular se distinga de una caida directa
// -- con TIME_STEP=1200s, 3 frames son 3600s, una fraccion minima del
// periodo orbital tipico (horas) a la distancia de spawn. 10 frames (~3.3h)
// le dan a esa orbita incipiente tiempo real para curvarse antes de que
// ResolveCollisions evalue una posible fusion con el Proto-Cuerpo dominante.
constexpr int SPAWN_GRACE_FRAMES = 10;

// ── Generador de fragmentos y polvo ─────────────────────────
// Los fragmentos solidos se agregan a 'bodies' (participan en gravedad/
// colisiones/mareas con su propio aspecto rocoso procedural). El polvo
// se agrega directamente a 'dust', el campo visual ligero gestionado por
// UpdateDustGravity/UpdateDustLifecycle/DrawDustField3D -- nunca entra en 'bodies'.
static void MakeFragments(std::vector<Body>& bodies,
                           std::vector<DustParticle>& dust,
                           const Body& tmpl,
                           const Vector3D& normal,
                           double circularVel,
                           bool fromTide,
                           const Vector3D* centerVel  = nullptr,
                           const Vector3D* impactPoint = nullptr,
                           int extraFragCount = 0,
                           double escapeMass = -1.0)
{
    int fragCount = extraFragCount;
    for (const Body& b : bodies) {
        if (b.mass <= 0.0) continue;
        if (b.isFragment) fragCount++;
    }

    // Lo encolado en g_pendingFragments/g_pendingDust (ver arriba) cuenta
    // contra el tope igual que lo ya presente en bodies/dust: aunque no
    // se vea todavia, va a aparecer pronto.
    const int availableFrags = MAX_FRAGMENTS       - fragCount             - (int)g_pendingFragments.size();
    const int availableDust  = MAX_DUST_PARTICLES  - CountActiveDust(dust) - (int)g_pendingDust.size();
    const bool isFluid = (tmpl.material == MAT_GASEOUS || tmpl.material == MAT_ICY);

    // Masa gravitatoria del cuerpo del que se desprende este material, para
    // calcular su velocidad de escape (ver escVel mas abajo). En la mayoria
    // de los llamados 'tmpl.mass' YA es esa masa (el cuerpo entero, p.ej.
    // 'doomed' en una destruccion total). Pero en el desprendimiento
    // gradual por marea 'tmpl.mass' es solo la pizca de masa desprendida
    // (shedMass) -- usar esa masa daria una velocidad de escape ridiculamente
    // baja, y los fragmentos caerian de vuelta de inmediato. 'escapeMass'
    // permite pasar la masa real del cuerpo en esos casos.
    const double gravMass = (escapeMass > 0.0) ? escapeMass : tmpl.mass;

    // Cuanto mas pequeno es el cuerpo que se fragmenta (tmpl.radius), mas
    // se favorece el polvo sobre fragmentos solidos. Por debajo de
    // SMALL_FRAG_RADIUS, 'tinyBody' fuerza numFrags=0 y fragShare=0: TODA
    // la masa expulsada se convierte en polvo, sin excepcion. Esto es
    // critico para evitar la "cascada de fragmentos" -- antes, incluso un
    // fragmento minusculo generaba un minimo de 2 fragmentos nuevos al
    // chocar, y esos 2 (aun mas pequenos) generaban 2 mas cada uno al
    // chocar entre si, etc., saturando MAX_FRAGMENTS con cientos de
    // cuerpos diminutos que disparaban el costo O(n^2) de la fisica.
    // Cuerpos grandes (radio >= LARGE_FRAG_RADIUS) mantienen el reparto y
    // conteo de siempre.
    constexpr double SMALL_FRAG_RADIUS = 1.0e4; // 10 km: cero fragmentos nuevos, todo polvo
    constexpr double LARGE_FRAG_RADIUS = 2.0e5; // 200 km: reparto normal
    const double sizeT = ClampD((tmpl.radius - SMALL_FRAG_RADIUS)
                               / (LARGE_FRAG_RADIUS - SMALL_FRAG_RADIUS), 0.0, 1.0);
    const bool tinyBody = tmpl.radius < SMALL_FRAG_RADIUS;

    // Segunda rampa de saturacion: SMALL/LARGE_FRAG_RADIUS (10km/200km) cubren
    // la transicion asteroide->protocuerpo, pero saturan en sizeT=1 para
    // CUALQUIER cuerpo >=200km -- un planeta entero recibe la misma cuenta de
    // fragmentos/polvo que un asteroide grande, haciendo su destruccion
    // catastrofica igual de modesta. sizeT2 anade una rampa 200km..R_EARTH que
    // escala numFrags/numDust hasta valores propios de escala planetaria.
    // R_EARTH (constants.h, ya derivado como ancla masa-radio de rocosos) es el
    // limite natural de "tamano planetario": cuerpos aun mas grandes (gigantes)
    // no necesitan MAS piezas, solo piezas mas masivas (ver fragShare).
    //
    // Ancla subida de 1.0x a 2.5x R_EARTH: cuerpos rocosos del catalogo mas
    // grandes que la Tierra (p.ej. "Planeta Gritador", catalog.h, radio
    // ~2.35x R_EARTH) saturaban en sizeT2=1 igual que un cuerpo apenas del
    // tamano de la Tierra -- su destruccion no escalaba con su tamano real.
    constexpr double PLANET_FRAG_RADIUS = 2.5 * R_EARTH; // ~1.593e7 m

    const double sizeT2 = ClampD((tmpl.radius - LARGE_FRAG_RADIUS)
                                / (PLANET_FRAG_RADIUS - LARGE_FRAG_RADIUS), 0.0, 1.0);

    // Regla 1/2 anti-cascada: si el cuerpo que se esta erosionando ('tmpl')
    // es EL MISMO un fragmento (isFragment=true), su eyeccion es SIEMPRE
    // polvo, nunca fragmentos solidos nuevos -- solo Protocuerpos
    // (isFragment=false tras acrecion) y Planetas pueden generar
    // fragmentos solidos al chocar. Sin esto, cada fragmento que choca
    // genera mas fragmentos, que a su vez chocan y generan mas... una
    // cascada O(n^2) que satura MAX_FRAGMENTS y mata el rendimiento.
    const bool sourceIsFragment = tmpl.isFragment;
    // Mitad del reparto que antes iba a fragmentos solidos pasa a polvo: un
    // campo de escombros real esta dominado por particulas finas, no por
    // piezas grandes -- y menos cuerpos solidos significa menos colisiones
    // fragmento-fragmento inmediatas.
    //
    // Bonus +0.10*sizeT2: sin el, fragShare solo depende de sizeT (satura a
    // 200km) y un planeta gigante recibiria la misma fraccion solido/polvo
    // (techo 0.25) que un protocuerpo de 200km, pese a tener muchas veces
    // mas masa en juego -- las destrucciones grandes se veian aun MAS
    // dominadas por polvo en vez de mostrar mas escombros solidos visibles.
    const double fragShare = (isFluid || tinyBody || sourceIsFragment) ? 0.0
                            : (0.025 + 0.225 * sizeT + 0.10 * sizeT2);

    int numFrags = 0, numDust = 0;
    if (fromTide) {
        // Un impacto catastrofico genera un campo de escombros con piezas de
        // tamanos dispares (ley de potencias, ver fragWeight mas abajo): no
        // solo 1-2 "gemelos" del mismo tamano, sino 1 pieza grande + varios
        // fragmentos medianos/pequenos. La mitad de las piezas que antes
        // eran fragmentos solidos ahora se generan como polvo (numDust
        // aumenta en la misma cantidad que se le resta a numFrags).
        // numFrags: rampa original (1..5 via sizeT) + rampa planetaria (hasta
        // +35 via sizeT2, ahora hasta 2.5x R_EARTH) = hasta ~40 -- dentro de
        // fragWeight[48] mas abajo.
        numFrags = (isFluid || tinyBody || sourceIsFragment) ? 0 : std::min((int)(1 + 4 * sizeT + 35 * sizeT2),  std::max(0, availableFrags));
        // numDust: rampa original (15..19) + rampa planetaria (hasta +100) =
        // hasta 119. Coeficiente ~3.5x menor que la ruta catastrofica (350,
        // rama !fromTide abajo) porque el desprendimiento por marea ocurre en
        // RAFAGAS REPETIDAS mientras el cuerpo se desintegra -- un coeficiente
        // igual multiplicaria el total por el numero de rafagas.
        numDust  = isFluid ? std::min(4,  std::max(0, availableDust))
                           : std::min((int)(15 + 4 * sizeT + 100 * sizeT2), std::max(0, availableDust));
    } else {
        // Regla 3: cantidad reducida a ~1/4 de lo que generaba antes
        // (4-20 -> 1-5) -- el polvo (150) se mantiene para el efecto
        // visual de "lluvia de escombros" sin el costo O(n^2) de cuerpos
        // solidos extra colisionando entre si.
        // numFrags: misma rampa extendida que el caso tidal (consistencia).
        numFrags = (isFluid || tinyBody || sourceIsFragment) ? 0 : std::min((int)(1 + 4 * sizeT + 35 * sizeT2), std::max(0, availableFrags));
        // numDust: rampa original (150, constante) + rampa planetaria (hasta
        // +350) = hasta 500. Destruccion de un planeta entero produce un campo
        // de escombros denso, no la misma "lluvia" de 150 particulas que un
        // asteroide de 200km.
        numDust  = std::min((int)(150 + 350 * sizeT2), std::max(0, availableDust));
    }
    if (numFrags == 0 && numDust == 0) return;

    const double fragBudget   = tmpl.mass * fragShare;
    const double dustBudget   = tmpl.mass * (isFluid ? 1.0 : (1.0 - fragShare));
    const double massPerDust  = numDust  > 0 ? dustBudget / numDust  : 0.0;

    // Distribucion de tamanos tipo "ley de potencias": en vez de
    // fragmentos casi-iguales (uniformes, ~fragBudget/numFrags cada uno),
    // la mayor parte de fragBudget queda concentrada en 1-2 piezas y el
    // resto son fragmentos mucho mas pequenos. Estos fragmentos pequenos
    // chocan entre si y van acreciendo gradualmente (ver
    // 'bothFragments'/accreteCount en MergeBodies) hasta convertirse en un
    // Proto-Cuerpo y, con repetidas fusiones, en una luna -- en vez de
    // aparecer ya "grandes" desde el primer impacto.
    // Tamano fijo en el stack, SIN bounds-checking: debe quedar siempre >=
    // el techo real de numFrags (1 + 4*sizeT + 35*sizeT2 <= 40 con sizeT,
    // sizeT2 <= 1). 48 deja margen sobre ese techo.
    double fragWeight[48];
    double fragWeightSum = 0.0;
    double maxFragWeight = 0.0;
    for (int i = 0; i < numFrags; ++i) {
        // Exponente bajado de 4.0 a 2.2: con 4.0 casi toda fragBudget se
        // concentraba en 1 (rara vez 2) fragmento dominante, dejando el
        // resto casi-uniforme-diminuto ("un par de trozos gruesos + arena
        // uniforme"). 2.2 sigue dando un fragmento dominante (ley de
        // potencias, no uniforme) pero con un espectro de tamanos medios
        // mucho mas visible entre el dominante y los mas chicos.
        fragWeight[i] = std::pow(FastRand01(), 2.2) + 0.01;
        fragWeightSum += fragWeight[i];
        maxFragWeight = std::max(maxFragWeight, fragWeight[i]);
    }
    // Masa del fragmento mas grande de este evento (el "Proto-Cuerpo" que
    // dominara el campo de escombros) -- usada mas abajo para darle a los
    // demas fragmentos una velocidad orbital realista alrededor de el.
    const double dominantFragMass = (fragWeightSum > 0.0) ? fragBudget * (maxFragWeight / fragWeightSum) : 0.0;

    const Vector3D baseVel = centerVel ? *centerVel : tmpl.vel;
    Vector3D n    = NormalizeSafe(normal);
    Vector3D side = NormalizeSafe(Cross(n, {0, 1, 0}));
    if (side.lengthSqr() < 1e-16) side = NormalizeSafe(Cross(n, {1, 0, 0}));
    Vector3D up = NormalizeSafe(Cross(side, n));

    auto SpawnParticle = [&](bool asDust, double m, int idx) {
        double r = std::cbrt(3.0 * m / (4.0 * PI_D * 3000.0));
        if (asDust) r = std::max(r, 100.0);

        Vector3D pos, ejectVel;

        // Margen de separacion: la superficie de cada fragmento queda al
        // menos un 15% de su propio radio 'r' fuera del radio de
        // referencia (cuerpo padre ya encogido, o radio ORIGINAL del
        // cuerpo en una explosion total) -- evita que fragmentos grandes
        // queden enterrados/solapados con el cuerpo o entre si justo al
        // generarse.
        constexpr double FRAG_CLEARANCE = 1.15;

        if (fromTide) {
            double sign = (idx % 2 == 0) ? 1.0 : -1.0;
            if (!isFluid && !asDust) {
                // Reparte los fragmentos solidos en dos grupos segun 'sign'
                // (los dos lobulos de Roche: hacia el perturbador y en
                // direccion opuesta) y, dentro de cada grupo, en sectores
                // azimutales equiespaciados alrededor del eje de marea 'n'
                // -- en vez de direcciones aleatorias independientes, que
                // tendian a amontonar varios fragmentos grandes en la misma
                // zona y a que se solaparan entre si.
                double groupSize = std::max(1.0, std::ceil(numFrags / 2.0));
                double groupIdx  = std::floor(idx / 2.0);
                double azimuth   = (2.0 * PI_D * groupIdx) / groupSize
                                 + (FastRand01() - 0.5) * (2.0 * PI_D / groupSize) * 0.5;
                double polarDev  = PI_D * 0.15 + FastRand01() * PI_D * 0.15; // ~27..54 grados
                Vector3D randomDir = NormalizeSafe(
                    n * std::cos(polarDev)
                    + (up * std::cos(azimuth) + side * std::sin(azimuth)) * std::sin(polarDev));

                double dist = sign * (tmpl.radius + r * FRAG_CLEARANCE);
                pos = tmpl.pos + randomDir * dist;

                // Velocidad de escape LOCAL desde 'b' (gravMass) a la
                // distancia REAL de aparicion 'dist' (no la superficie de
                // 'b'): material que cruza el limite de Roche se desprende
                // con una velocidad de eyeccion del orden de la velocidad
                // de escape del cuerpo que lo libera. Con menos velocidad,
                // el fragmento queda en una orbita ligada muy cercana a 'b'
                // y se acumula en un "anillo" pegado a la superficie -- algo
                // que nunca ocurre en una disrupcion de marea real.
                double escVelLocal = std::sqrt(2.0 * G * gravMass / std::max(1.0, std::abs(dist)));

                // Componente tangencial (orbital): Cross(n, randomDir) da,
                // para cada fragmento, la direccion tangencial respecto al
                // eje de marea 'n'. Sin esta componente la eyeccion es casi
                // puramente radial: cada fragmento o cae de vuelta sobre el
                // "Proto-Cuerpo" dominante (fusion casi instantanea via
                // 'bothFragments', Ley 3) o escapa, sin quedar nunca en
                // orbita. La magnitud, sqrt(G*M/dist), es la velocidad
                // orbital circular EXACTA a esta distancia alrededor de
                // 'dominantFragMass' (el fragmento mas masivo del evento).
                Vector3D tangentDir = NormalizeSafe(Cross(n, randomDir));
                double vCircLocal = std::sqrt(G * dominantFragMass / std::max(1.0, std::abs(dist)));

                ejectVel = baseVel + randomDir * (sign * escVelLocal)
                                   + tangentDir * vCircLocal;
            } else {
                double dist = sign * (tmpl.radius + r * FRAG_CLEARANCE);
                pos = tmpl.pos + n * dist;
                double escVelLocal = std::sqrt(2.0 * G * gravMass / std::max(1.0, std::abs(dist)));
                ejectVel = baseVel + n * (sign * escVelLocal);
            }
        } else {
            double spreadAngle = FastRand01() * 2.0 * PI_D;
            // pitchAngle en [PI/8, 3*PI/8]: cono de eyeccion de ~45 grados
            // respecto a la normal de impacto 'n' (clasico de crateres),
            // con sin(pitchAngle) siempre positivo -- la componente de
            // ejectDir a lo largo de 'n' SIEMPRE apunta hacia afuera del
            // cuerpo de origen. Antes el rango era [-PI/4, PI/4], que
            // permitia componentes negativas (hacia adentro): combinado con
            // el spawn justo en la superficie (ver 'pos' abajo), un
            // fragmento con velocidad radial hacia adentro de varios km/s
            // podia atravesar el cuerpo de origen durante su periodo de
            // gracia (SPAWN_GRACE_FRAMES) sin ser detectado.
            double pitchAngle = PI_D * 0.125 + FastRand01() * PI_D * 0.25;
            Vector3D ejectDir = NormalizeSafe(
                side * std::cos(spreadAngle) * std::cos(pitchAngle) +
                up   * std::sin(spreadAngle) * std::cos(pitchAngle) +
                n    * std::sin(pitchAngle));

            if (impactPoint) {
                // 'impactPoint' ya es el punto exacto en la CORTEZA del
                // cuerpo que eyecta -- pos_impacto + normal_superficie *
                // (radio_nuevo + margen), calculado por EjectAndMark sobre
                // SU PROPIA posicion/radio (nunca relativo al otro cuerpo
                // del par). Aqui solo se suma el radio de ESTE fragmento a
                // lo largo de 'n' para que tampoco se solape con 'origin'.
                pos = *impactPoint + n * (r * 1.05 + 1.0);
            } else {
                // Explosion de CUERPO COMPLETO (sin punto de impacto
                // localizado, p.ej. ruptura final por marea): reparte los
                // fragmentos/polvo en una esfera de Fibonacci alrededor del
                // radio ORIGINAL del cuerpo -- antes todos se colocaban
                // casi en el mismo punto cercano al centro
                // (tmpl.pos + n*(r*1.05+1)), amontonados entre si.
                // 'ejectDir' pasa a seguir esa misma direccion radial, para
                // que la eyeccion sea hacia afuera desde la posicion de
                // cada fragmento.
                double count = (double)std::max(1, asDust ? numDust : numFrags);
                double phi   = std::acos(1.0 - 2.0 * (idx + 0.5) / count);
                double theta = PI_D * (1.0 + std::sqrt(5.0)) * idx; // angulo dorado
                ejectDir = NormalizeSafe(
                    side * std::sin(phi) * std::cos(theta) +
                    up   * std::sin(phi) * std::sin(theta) +
                    n    * std::cos(phi));
                pos = tmpl.pos + ejectDir * (tmpl.radius + r * FRAG_CLEARANCE);
            }
            double speed = circularVel * (asDust ? 1.2 : 0.8)
                         + (FastRand01() - 0.5) * circularVel * 0.8;
            ejectVel = baseVel + ejectDir * speed;
        }

        if (asDust) {
            DustParticle d;
            d.pos       = pos;
            d.vel       = ejectVel;
            d.radius    = r;
            d.color     = tmpl.color;
            d.heatSpike = fromTide ? tmpl.heatSpike : 1.0f;
            d.seed      = (float)FastRand01();
            d.active    = true;
            
            d.isRing = false;      // polvo de colision: puede volverse orbita/anillo/acrecion
            d.state = DustState::Debris;


            // Eje y velocidad de rotacion ("tumbling") aleatorios -- visual,
            // ver DustParticle::rotationAxis/rotationSpeed/currentRotation
            // y su integracion en UpdateDustGravity.
            double axisCosT = 2.0 * FastRand01() - 1.0;
            double axisSinT = std::sqrt(std::max(0.0, 1.0 - axisCosT*axisCosT));
            double axisPhi  = 2.0 * PI_D * FastRand01();
            d.rotationAxis  = { (float)(axisSinT * std::cos(axisPhi)),
                                 (float)axisCosT,
                                 (float)(axisSinT * std::sin(axisPhi)) };
            d.rotationSpeed   = (float)((FastRand01() - 0.5) * 1.5); // ±0.75 rad/s
            d.currentRotation = (float)(FastRand01() * 2.0 * PI_D);
            d.scale           = (float)(r * RENDER_SCALE);

            if (g_dustSpawnedThisFrame < DUST_SPAWN_BUDGET_PER_FRAME) {
                int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
                if (slot >= 0) {
                    dust[slot] = d;
                    g_dustSpawnedThisFrame++;
                }
                // Pool lleno: 'availableDust' ya deberia haber puesto
                // numDust=0 en este caso; si no, se descarta la particula.
            } else {
                g_pendingDust.push_back(d);
            }
            return;
        }

        Body p;
        p.name          = tmpl.name + " frag";
        p.pos           = pos;
        p.prevPos       = pos; // ver Body::prevPos: evita un "salto" falso desde (0,0,0) en la primera CCD retrospectiva
        p.vel           = ejectVel;
        p.mass          = m;
        p.radius        = r;
        p.color         = tmpl.color;
        p.isFragment    = true;
        p.isRockyPlanet = true;
        p.rockyPlanet   = MakeAsteroidProfile((unsigned int)(FastRand01() * 4294967295.0), tmpl.material);
        p.temperature   = tmpl.temperature * 0.7;
        p.heatSpike     = fromTide ? tmpl.heatSpike : 1.0f;
        p.tideAxis      = n;
        p.spawnGraceFrames = SPAWN_GRACE_FRAMES;

        // Tumbling rapido y aleatorio (periodo 1-12 horas, rango tipico de
        // asteroides reales): un fragmento recien desprendido de un
        // cataclismo se lleva momento angular caotico de la colision, a
        // diferencia de un planeta formado que ya relajo su rotacion. Esto
        // alimenta DIRECTAMENTE el achatamiento por rotacion propia (ver
        // TidalBodyTransform, renderer.h): sin un periodo realista, todo
        // cuerpo heredaba el mismo spinRateDeg lento por defecto (Body::
        // spinRateDeg=0.5) y nunca se veia achatado pese a la formula.
        {
            double periodSec = 3600.0 * (1.0 + FastRand01() * 11.0); // 1..12 h
            double omega      = 2.0 * PI_D / periodSec; // rad/s
            p.spinRateDeg = (float)(omega * 1200.0 * (180.0 / PI_D));
        }
        if (g_fragSpawnedThisFrame < FRAG_SPAWN_BUDGET_PER_FRAME) {
            bodies.push_back(p);
            g_fragSpawnedThisFrame++;
        } else {
            g_pendingFragments.push_back(p);
        }
    };

    for (int i = 0; i < numFrags; ++i) {
        double m = (fragWeightSum > 0.0) ? fragBudget * (fragWeight[i] / fragWeightSum) : 0.0;
        SpawnParticle(false, m, i);
    }
    for (int i = 0; i < numDust;  ++i) SpawnParticle(true,  massPerDust * (0.9 + FastRand01()*0.2), i);
}

// ── Dirección mundo -> espacio-malla ────────────────────────
// Deshace, en el MISMO orden que rotateToSurfaceFrame/surfacePos en
// drawRockyPlanet (shaders.h), las dos rotaciones que el renderer aplica
// al modelo del cuerpo: primero la inclinacion axial, luego el spin.
//
// 1) Inclinacion axial: RotateAxialTilt(v,t) = {x, y*c-z*s, y*s+z*c} (mas
//    arriba) es la transformacion "hacia adelante" (MatrixRotateX en
//    renderer.h). Su inversa es RotateAxialTilt(v,-t): con c'=cos(-t)=c y
//    s'=sin(-t)=-s, queda {x, y*c+z*s, -y*s+z*c} -- exactamente
//    undoAxialTilt() en shaders.h. axialTiltDeg debe ser b.axialTilt (la
//    MISMA inclinacion que uAxialTilt).
// 2) Spin: contrarrotacion en Y por rotationAngleDeg (b.rotationAngle, en
//    grados, misma convencion que uSurfaceSpin), aplicada sobre el
//    resultado del paso 1 -- igual que rotateToSurfaceFrame aplica su
//    rotacion Y sobre undoAxialTilt(v), no sobre 'v' directamente.
//
// El resultado queda fijo sobre la malla pese a rotacion y/o inclinacion
// axial -- usado para anclar las marcas de impacto (ImpactMark) a la
// superficie en el mismo sistema de referencia que surfacePos.
inline Vector3D WorldDirToMeshLocal(const Vector3D& worldDir, float rotationAngleDeg, float axialTiltDeg) {
    // 1) Deshacer inclinacion axial (= undoAxialTilt en shaders.h).
    double tilt = (double)axialTiltDeg * (PI_D / 180.0);
    double tc = std::cos(tilt), ts = std::sin(tilt);
    Vector3D v{ worldDir.x, worldDir.y * tc + worldDir.z * ts, -worldDir.y * ts + worldDir.z * tc };

    // 2) Contrarrotacion de spin sobre el resultado del paso 1.
    double theta = std::fmod((double)rotationAngleDeg, 360.0) * (PI_D / 180.0);
    double c = std::cos(theta), s = std::sin(theta);
    return Vector3D{ v.x * c - v.z * s, v.y, v.x * s + v.z * c };
}

// ── Registro de una marca de impacto/erosion permanente ─────
// Factoriza el registro de un ImpactMark (Sistema 3, ver body.h y
// drawRockyPlanet en shaders.h), usado tanto por EjectAndMark
// (colisiones, mas abajo) como por el desprendimiento gradual por marea
// en ApplyTidesAndRoche: ambos dejan un crater permanente en la
// direccion 'ejectDir' por la que salio el material. El buffer de
// marcas es circular (nextImpactSlot): las mas viejas se sobrescriben
// al llenarse.
static void AddImpactMark(Body& b, const Vector3D& ejectDir, float radius, float energy) {
    ImpactMark mark;
    mark.localDir = WorldDirToMeshLocal(ejectDir, b.rotationAngle, b.axialTilt);
    mark.radius   = radius;
    mark.energy   = energy;
    mark.age      = 0.0f;
    b.impactMarks[b.nextImpactSlot] = mark;
    b.nextImpactSlot  = (b.nextImpactSlot + 1) % Body::MAX_IMPACT_MARKS;
    b.impactMarkCount = std::min(b.impactMarkCount + 1, Body::MAX_IMPACT_MARKS);
}

// ── Fusión de dos cuerpos ────────────────────────────────────
// Ley 1 (conservación del momento, choque inelástico perfecto): el cuerpo
// menor (minIdx) se funde EN EL ACTO. masa, volumen y momento lineal del
// par se conservan exactamente con vf = (m1*v1 + m2*v2)/(m1+m2), y minIdx
// queda con mass=0.0 (eliminado por ResolveCollisions/StepPhysics en su
// barrido de limpieza de fin de frame). Sin "fusion gradual" ni cuerpos
// fantasma que conserven su velocidad de impacto y puedan atravesar al
// cuerpo mayor -- ver el bug que esto causaba en ResolveCollisions.
static void MergeBodies(std::vector<Body>& bodies, std::vector<DustParticle>& dust,
                         size_t aIdx, size_t bIdx)
{
    if (aIdx == bIdx || bodies[aIdx].mass <= 0.0 || bodies[bIdx].mass <= 0.0) return;

    const size_t majIdx = (bodies[aIdx].mass >= bodies[bIdx].mass) ? aIdx : bIdx;
    const size_t minIdx = (bodies[aIdx].mass >= bodies[bIdx].mass) ? bIdx : aIdx;

    const double mA = bodies[majIdx].mass, mB = bodies[minIdx].mass;
    const double totalMass = mA + mB;
    const double R_maj0 = bodies[majIdx].radius;
    const double R_min0 = bodies[minIdx].radius;

    const bool bothFragments = bodies[majIdx].isFragment && bodies[minIdx].isFragment;
    const bool anyGasGiant   = bodies[majIdx].isGasGiant || bodies[minIdx].isGasGiant;
    const bool anyStar       = bodies[majIdx].isStar || bodies[minIdx].isStar;

    const Vector3D relVel       = bodies[majIdx].vel - bodies[minIdx].vel;
    const double   impactSpeed  = relVel.length();
    const double   impactEnergyPM = 0.5 * impactSpeed * impactSpeed;
    const Vector3D impactNormal = NormalizeSafe(bodies[majIdx].pos - bodies[minIdx].pos);

    // newPos: centro de masas del par EN EL MOMENTO DEL CONTACTO, no de
    // las posiciones actuales. La CCD retrospectiva (ResolveCollisions)
    // puede detectar un impacto DESPUES de que minIdx ya atraveso por
    // completo a majIdx (tunneling de un sub-paso con desplazamiento
    // mayor que el diametro combinado) -- en ese instante
    // bodies[minIdx].pos puede estar a una distancia enorme y arbitraria
    // de bodies[majIdx].pos, del lado OPUESTO. Promediar posiciones
    // ACTUALES en ese caso teletransportaria a majIdx una fraccion
    // (mB/totalMass) de esa distancia -- el "salto hacia atras a
    // velocidad absurda" reportado. En cambio, se usa la posicion que
    // minIdx tendria SI estuviera tocando la superficie de majIdx
    // (separados por R_maj0+R_min0 a lo largo de impactNormal): el
    // desplazamiento de majIdx queda acotado por su propio radio y el de
    // minIdx, como corresponde a una fusion por contacto.
    const Vector3D minPosAtContact = bodies[majIdx].pos - impactNormal * (R_maj0 + R_min0);
    const Vector3D newPos = (bodies[majIdx].pos * mA + minPosAtContact * mB) * (1.0 / totalMass);
    const Vector3D newVel = (bodies[majIdx].vel * mA + bodies[minIdx].vel * mB) * (1.0 / totalMass);

    // Ley 4 (conservación de masa / límite de erosión): la fracción de
    // masa que un cuerpo expulsa en el impacto se deriva de comparar la
    // energia cinetica del impacto (en el sistema centro-de-masa, usando
    // la masa reducida -- frame-independiente) contra la energia de
    // enlace gravitacional de ESE cuerpo (esfera uniforme: U = (3/5) G
    // M^2 / R, formula estandar de autogravitacion). Sin numeros magicos:
    // un aterrizaje suave (KE << U) no erosiona nada (acrecion total);
    // solo un impacto cuya energia se acerca o supera U erosiona una
    // fraccion creciente de la masa de ESE cuerpo, hasta el 100% si
    // KE >= U (ese cuerpo queda completamente destruido por el golpe).
    const double mu       = (mA * mB) / totalMass;
    const double impactKE = 0.5 * mu * impactSpeed * impactSpeed;
    const double bindingEnergyMaj = (3.0 / 5.0) * G * mA * mA / std::max(1.0, R_maj0);
    const double bindingEnergyMin = (3.0 / 5.0) * G * mB * mB / std::max(1.0, R_min0);
    const double erodeMaj = anyStar ? 0.0 : ClampD(impactKE / std::max(1.0, bindingEnergyMaj), 0.0, 1.0);
    const double erodeMin = anyStar ? 0.0 : ClampD(impactKE / std::max(1.0, bindingEnergyMin), 0.0, 1.0);

    // Expulsa una fracción de la masa de bodies[idx] como fragmentos/
    // polvo y deja una marca de impacto permanente en su superficie
    // (cráter/onda/magma para el shader, Sistema 3).
    auto EjectAndMark = [&](size_t idx, double fraction) {
        if (bodies[idx].mass <= 0.0) return;

        // Marcado de crater desacoplado del umbral de erosion: un impactor
        // diminuto frente a un cuerpo masivo (fraction<=0.002, p.ej. un
        // asteroide contra la Tierra) nunca le arranca el 0.2% de su masa,
        // pero FISICAMENTE si deja una cicatriz visible en la corteza -- el
        // umbral de 0.2% solo decide si HAY perdida de masa/eyecta masiva
        // de ESTE cuerpo, nunca si se dibuja el crater. El tamano de esta
        // marca "menor" se basa en el radio ORIGINAL del OTRO cuerpo del
        // par (el impactor), no en 'fraction' (que seria ~0 para un
        // proyectil diminuto contra un cuerpo masivo): un crater tipico
        // mide 5-10 veces el radio del proyectil que lo formo.
        if (fraction <= 0.002) {
            double otherR = (idx == majIdx) ? R_min0 : R_maj0;
            Vector3D ejectDirMinor = (idx == majIdx) ? (impactNormal * -1.0) : impactNormal;
            float craterRadius = (float)ClampD(otherR / std::max(1.0, bodies[idx].radius) * 7.5, 0.03, 0.6);
            AddImpactMark(bodies[idx], ejectDirMinor, craterRadius, (float)impactEnergyPM);
            return;
        }

        // Direccion de eyeccion: SIEMPRE -impactNormal (del centro de
        // majIdx hacia minIdx). Para majIdx esto es "hacia atras" a lo
        // largo del eje del choque -- fuera del crater, hacia donde vino
        // el impactor (sin cambios respecto a la version anterior). Para
        // minIdx, esto apunta hacia su cara OPUESTA al punto de contacto,
        // es decir ALEJANDOSE de majIdx: su eyecta nace en el lado de
        // minIdx que NO esta tocando al cuerpo masivo, en vez de nacer
        // empujada hacia (o dentro de) su superficie.
        Vector3D ejectDir = impactNormal * -1.0;

        // Limite de erosion no catastrofica: un impacto que no supera la
        // energia de enlace (el caso erodeMaj>=1.0 lo maneja por separado
        // la rama de destruccion total, arriba) NUNCA debe arrancarle a
        // un cuerpo mas del 90% de su masa de un solo golpe. Sin este
        // tope, 'fraction' arbitrariamente cercano a 1.0 (p.ej. 0.999999)
        // dejaria una masa/radio residual arbitrariamente pequenos --
        // un "micro-planeta" practicamente nulo que sigue existiendo con
        // la velocidad de newVel, en vez de ser absorbido/destruido
        // limpiamente.
        const double effFraction = std::min(fraction, 0.9);
        double shedMass = bodies[idx].mass * effFraction;
        bodies[idx].radius = std::cbrt(
            (3.0 * VolumeFromRadius(bodies[idx].radius) * (1.0 - effFraction)) / (4.0 * PI_D));
        bodies[idx].mass = std::max(0.0, bodies[idx].mass - shedMass);

        // Oceanos evaporados/expulsados PERMANENTEMENTE (Sistema 3 ext.):
        // un impacto que erosiona >=5% de la masa de un planeta rocoso/
        // helado con inventario de volatiles tambien se lleva una fraccion
        // proporcional de ese inventario (oceanos+hielo+vapor), vaporizada
        // o expulsada al espacio junto con la corteza. 'volatileBudget'
        // solo decrece (aqui y en el escape de Jeans de
        // UpdateThermodynamics) y nunca se repone, asi que
        // rockyPlanet.waterLevel = volatileBudget - iceFraction -
        // vaporFraction (ver UpdateThermodynamics) queda con un techo mas
        // bajo de forma irreversible -- el oceano perdido no vuelve al
        // enfriarse.
        if (bodies[idx].isRockyPlanet && effFraction >= 0.05) {
            bodies[idx].volatileBudget = (float)std::max(0.0, bodies[idx].volatileBudget * (1.0 - effFraction));
        }

        // Ley 2 (dinamica de eyeccion externa estricta): el material
        // expulsado nace EN LA CORTEZA del cuerpo que lo expulsa --
        // pos_impacto + normal_superficie * (radio_nuevo +
        // margen_seguridad) -- usando la posicion/radio de ESTE cuerpo
        // (idx) DESPUES de encogerse, nunca relativo al otro cuerpo del
        // par (que puede estar arbitrariamente lejos si la CCD detecto el
        // impacto con antelacion por alta velocidad relativa). Asi ningun
        // fragmento puede nacer dentro del nucleo ni en el lado opuesto.
        constexpr double SAFETY_MARGIN = 1.0; // 1 m de margen sobre la nueva corteza
        Vector3D surfacePoint = bodies[idx].pos + ejectDir * (bodies[idx].radius + SAFETY_MARGIN);

        Body shedTmpl = bodies[idx];
        shedTmpl.mass = shedMass;
        // vCirc: velocidad de referencia para la eyecta -- SIEMPRE la del
        // pozo gravitatorio de majIdx (el cuerpo mas masivo del par), no la
        // de 'idx'. Para majIdx esto es su propia velocidad (idx==majIdx,
        // sin cambios). Para minIdx, su propio vCirc (un fragmento de pocos
        // km) es de solo unos m/s -- insuficiente para que su eyecta se
        // aleje de la superficie de un cuerpo planetario antes de que
        // UpdateDustGravity la integre con un paso demasiado grande cerca
        // de un pozo de gravedad tan profundo (ver comentario en
        // UpdateDustGravity). La eyecta de minIdx debe escapar el pozo
        // DOMINANTE en el punto de impacto: el de majIdx.
        double vCirc = std::sqrt(G * std::max(1.0, bodies[majIdx].mass) / std::max(1.0, bodies[majIdx].radius));
        Vector3D bodyVel = bodies[idx].vel;
        MakeFragments(bodies, dust, shedTmpl, ejectDir, vCirc, false, &bodyVel, &surfacePoint);

        Body& b = bodies[idx];
        AddImpactMark(b, ejectDir, (float)ClampD(0.05 + fraction * 1.2, 0.03, 0.6), (float)impactEnergyPM);
        b.heatSpike = 1.0f;
    };

    // Rebote elástico/inelástico: solo para gigantes gaseosos/helados
    // (cuerpos fluidos, sin superficie solida que cratere). Cuerpos
    // rocosos/metalicos y fragmentos/proto-cuerpos NUNCA rebotan: si se
    // tocan, se funden (con eyeccion proporcional) o forman un crater.
    if (anyGasGiant && !anyStar && impactSpeed > 400.0)
    {
        const double elasticLimit   = 2000.0;
        const double inelasticLimit = 8000.0;
        double e = ClampD((inelasticLimit - impactSpeed) / (inelasticLimit - elasticLimit), 0.0, 1.0);
        if (e > 0.05) {
            double reducedMass = mu;
            double impulse     = -(1.0 + e) * reducedMass * relVel.dot(impactNormal);
            Vector3D J = impactNormal * impulse;
            bodies[majIdx].vel += J * (1.0 / mA);
            bodies[minIdx].vel -= J * (1.0 / mB);

            double overlap = bodies[majIdx].radius + bodies[minIdx].radius
                           - (bodies[majIdx].pos - bodies[minIdx].pos).length();
            if (overlap > 0.0) {
                bodies[majIdx].pos += impactNormal * (overlap * mB / totalMass + 1.0);
                bodies[minIdx].pos -= impactNormal * (overlap * mA / totalMass + 1.0);
            }
            bodies[majIdx].heatSpike = 1.0f;
            bodies[minIdx].heatSpike = 1.0f;

            // El calor del impacto se reparte segun la masa del otro
            // cuerpo (un impactor mas masivo calienta mas al objetivo) y
            // persiste/disipa via UpdateThermodynamics (Sistema 2).
            double heatGain = std::min(50000.0, impactEnergyPM * 5e-4);
            bodies[majIdx].temperature += heatGain * (mB / totalMass);
            bodies[minIdx].temperature += heatGain * (mA / totalMass);

            // Choque cinético directo: cada cuerpo expulsa material segun
            // SU PROPIA energia de enlace (Ley 4) -- el impactor (menor,
            // mucho menos ligado) suele perder una fraccion mucho mayor
            // que el gigante (mucho mas ligado).
            EjectAndMark(minIdx, erodeMin);
            EjectAndMark(majIdx, erodeMaj);
            return;
        }
    }

    // Ley 4, caso limite: la energia cinetica del impacto iguala o supera
    // la energia de enlace gravitacional del PLANETA mayor -> se destruye
    // por completo de un solo golpe (no queda un "cuerpo mayor
    // superviviente" perdiendo masa indefinidamente en ciclos
    // posteriores). Solo aplica cuando majIdx es un cuerpo planetario real
    // (no un fragmento): la acrecion fragmento-fragmento (bothFragments,
    // mas abajo) sigue su propia logica de erosion/dust sin "destruccion
    // total", para no impedir que los Proto-Cuerpos crezcan.
    if (erodeMaj >= 1.0 && !bodies[majIdx].isFragment && !anyStar) {
        Body doomed = bodies[majIdx];
        doomed.mass = totalMass;
        doomed.pos  = newPos;
        doomed.vel  = newVel;
        doomed.radius = std::cbrt((VolumeFromRadius(R_maj0) + VolumeFromRadius(R_min0)) * 3.0 / (4.0 * PI_D));
        doomed.temperature = std::max(bodies[majIdx].temperature, bodies[minIdx].temperature)
                            + std::min(50000.0, impactEnergyPM * 5e-4);
        doomed.heatSpike = 1.0f;

        double vCirc = std::sqrt(G * std::max(1.0, doomed.mass) / std::max(1.0, doomed.radius));
        MakeFragments(bodies, dust, doomed, impactNormal, vCirc, true, nullptr, nullptr);

        bodies[majIdx].mass = 0.0;
        bodies[minIdx].mass = 0.0;
        return;
    }

    // Fusión: SIN excepciones por etiquetas. Cualquier par de cuerpos
    // (planeta-planeta, fragmento-fragmento, fragmento-planeta) reparte
    // la energia cinetica del impacto como calor y luego expulsa una
    // fraccion continua de masa via EjectAndMark, escalando con
    // 'erodeMaj' (Ley 4) -- ningun cuerpo se funde "magicamente" sin
    // generar crater/calor/eyeccion.
    const double heatGain = std::min(50000.0, impactEnergyPM * 5e-4);

    // El calor LOCAL del crater (mark.energy = impactEnergyPM, abajo en
    // EjectAndMark) no cambia: un impacto pequeno sigue hirviendo su
    // propia zona de magma sin importar el tamano del planeta. Pero el
    // calor GLOBAL (b.temperature, "bola de lava" del shader) se escala
    // ademas por massRatio = mB/mA: un fragmento pequeno (mB << mA)
    // apenas mueve la temperatura PROMEDIO de un planeta entero -- solo
    // un impacto verdaderamente catastrofico (masas comparables, p.ej.
    // Marte vs Tierra) inyecta suficiente energia al promedio global como
    // para fundir el planeta entero.
    const double massRatio      = ClampD(mB / mA, 0.0, 1.0); // mA >= mB siempre
    const double globalHeatGain = heatGain * massRatio;

    // Estrella absorbe todo: instantaneo, sin superficie solida que
    // cratere.
    if (anyStar) {
        bodies[majIdx].pos    = newPos;
        bodies[majIdx].vel    = newVel;
        bodies[majIdx].heatSpike = 1.0f;

        // Solo se trata como FUSION ESTELAR real (con renacimiento tipo
        // "blue straggler", radio/luminosidad/fase reseteados via la
        // formula generica de masa) si el cuerpo menor aporta una masa
        // no despreciable -- mismo umbral del 1% que ya usa
        // ApplyTidesAndRoche para decidir "fuente de marea dominante".
        // SIN esto, una supergigante (radio ENORME, 700-2150 R☉ ->
        // sección transversal gigantesca) tarde o temprano choca con
        // CUALQUIER escombro/asteroide/fragmento que pase cerca, y cada
        // impacto -- sin importar cuan insignificante la masa ganada --
        // reseteaba la estrella ENTERA a una version diminuta y azul de
        // secuencia principal (R_SUN*mRatio^0.6 para 10-15 M☉ da solo
        // ~5-8 R☉, muchisimo menor que su radio real). Un impacto
        // trivial ahora solo suma masa, sin tocar fase/radio/luminosidad.
        bool meaningfulMerger = (bodies[minIdx].mass >= bodies[majIdx].mass * 0.01);
        if (!meaningfulMerger) {
            bodies[majIdx].mass += bodies[minIdx].mass;
            bodies[minIdx].mass  = 0.0;
            return;
        }

        // Radio según rango de masa (relación masa-radio más precisa)
        double mRatioNew = std::max(0.08, totalMass / M_SUN);
        double newR = (mRatioNew < 2.0)  ? R_SUN * std::pow(mRatioNew, 0.8)
                    : (mRatioNew < 50.0) ? R_SUN * std::pow(mRatioNew, 0.6)
                                          : R_SUN * std::pow(mRatioNew, 0.5);

        // Rejuvenecimiento ("blue straggler"): combustible gastado ponderado por masa
        double tMSA = StellarMSLifetime(bodies[majIdx].initialStellarMass);
        double tMSB = StellarMSLifetime(bodies[minIdx].initialStellarMass);
        double fracA = ClampD(bodies[majIdx].stellarAge / std::max(1.0, tMSA), 0.0, 1.0);
        double fracB = ClampD(bodies[minIdx].stellarAge / std::max(1.0, tMSB), 0.0, 1.0);
        double spentFrac = (fracA * bodies[majIdx].mass + fracB * bodies[minIdx].mass) / totalMass;
        double newFrac   = std::clamp(spentFrac - 0.15, 0.0, 0.98);
        double newAge    = newFrac * StellarMSLifetime(totalMass);

        // Hash combinado para efectiveSNThreshold de la nueva estrella
        uint64_t combinedId = bodies[majIdx].id ^ (bodies[minIdx].id * 2654435761ull);

        bodies[majIdx].mass               = totalMass;
        bodies[majIdx].initialStellarMass = totalMass;
        bodies[majIdx].effectiveSNThreshold = 8.0
            + (double)((combinedId % 300)) / 100.0 - 1.5;
        bodies[majIdx].radius             = newR;
        double newLum = L_SUN * std::pow(mRatioNew, 3.5);
        bodies[majIdx].baseLuminosity     = newLum;
        bodies[majIdx].visualLuminosity   = newLum;
        bodies[majIdx].luminosity         = newLum;
        bodies[majIdx].stellarAge         = newAge;
        bodies[majIdx].stellarPhase       = StellarPhase::MAIN_SEQUENCE;
        bodies[majIdx].stellarPhaseAge    = 0.0;
        bodies[majIdx].isStar             = true;
        bodies[majIdx].isFragment         = false;
        // Resetear estados residuales de SN/remanente
        bodies[majIdx].isSupernovaRemnant = false;
        bodies[majIdx].supernovaProgress  = 0.0;
        bodies[majIdx].supernovaRadius    = 0.0;
        bodies[majIdx].gravityEnabled     = true;
        bodies[majIdx].collisionEnabled   = true;
        bodies[majIdx].stellarManualOverride = false; // la nueva estrella auto-evoluciona

        // Conservar momento angular: L = I*omega, I ∝ M*R²
        // L_maj + L_min = (M_maj*R_maj² + M_min*R_min²)*omega_new
        // omega (rad/s) = spinRateDeg * PI/180 / 1200
        {
            double omegaMaj = (double)bodies[majIdx].spinRateDeg * (PI_D/180.0) / 1200.0;
            double omegaMin = (double)bodies[minIdx].spinRateDeg * (PI_D/180.0) / 1200.0;
            double R_maj = bodies[majIdx].radius; // radio ANTES de sobreescribirse
            // (bodies[majIdx].radius fue sobreescrito a newR arriba, usar el guardado)
            // En realidad newR ya está en bodies[majIdx].radius tras la linea 1007.
            // Usar R_maj0 (guardado al inicio de MergeBodies) y radio del minIdx.
            double L = mA * R_maj0 * R_maj0 * omegaMaj
                     + mB * bodies[minIdx].radius * bodies[minIdx].radius * omegaMin;
            double I_new = totalMass * newR * newR;
            double omegaNew = (I_new > 0.0) ? L / I_new : 0.0;
            bodies[majIdx].spinRateDeg = (float)(omegaNew * 1200.0 * (180.0 / PI_D));
        }
        // Recalcular criticalRotationFraction inmediatamente para evitar que
        // ApplyRotationalBreakup vea el valor obsoleto del pulsar (0.99) en
        // el primer frame y consuma toda la masa del cuerpo recien fusionado.
        {
            double omega = (double)bodies[majIdx].spinRateDeg * (PI_D/180.0) / 1200.0;
            double omegaCrit = (newR > 0 && totalMass > 0)
                ? std::sqrt(G * totalMass / (newR * newR * newR)) : 1.0;
            bodies[majIdx].criticalRotationFraction =
                (float)ClampD(omega / omegaCrit, 0.0, 0.99);
        }
        bodies[majIdx].axialTilt = 0.0f; // nueva estrella sin inclinacion heredada

        // Temperatura recalculada por Stefan-Boltzmann en UpdateBodiesState
        bodies[minIdx].mass   = 0.0;
        return;
    }

    // Ley 1bis: antes de fusionarse, minIdx TAMBIEN se erosiona segun su
    // propia energia de impacto relativa a su propia energia de enlace
    // (erodeMin) -- igual que majIdx (Ley 4) y exactamente como ya ocurre
    // para AMBOS cuerpos en el rebote de gigantes gaseosos (arriba). Sin
    // esto, un cuerpo pequeno que choca contra uno mucho mas grande
    // (erodeMaj~0, majIdx no se erosiona de forma visible) desaparecia sin
    // generar NINGUN fragmento/polvo/crater propio, sin importar que para
    // EL la colision fuera catastrofica (erodeMin~1: deberia hacerse
    // pedazos).
    //
    // Orden de operaciones: EjectAndMark(minIdx,...) muta
    // bodies[minIdx].mass/radius EN SITIO, reduciendolos a la fraccion
    // superviviente (1-effFraction) ANTES de calcular la fusion. Solo esa
    // masa RESIDUAL -- no la mB original -- se transfiere a majIdx: el
    // resto ya se fue como eyecta con su propia velocidad (vel original de
    // minIdx + dispersion radial, ver MakeFragments/SpawnParticle), asi que
    // sumarlo de nuevo al momento de majIdx duplicaria masa y energia.
    EjectAndMark(minIdx, erodeMin);
    const double mB_residual       = bodies[minIdx].mass;
    const double totalMassResidual = mA + mB_residual;
    const Vector3D newPosResidual = (bodies[majIdx].pos * mA + minPosAtContact * mB_residual) * (1.0 / totalMassResidual);
    const Vector3D newVelResidual = (bodies[majIdx].vel * mA + bodies[minIdx].vel * mB_residual) * (1.0 / totalMassResidual);

    // Acrecion de proto-cuerpos: si AMBOS eran fragmentos, esto cuenta
    // para la UI ("Acrecion: N cuerpos") y puede promover a "Proto-
    // Cuerpo". Se evalua sobre totalMassResidual (la masa final tras la
    // erosion de minIdx y la absorcion de abajo).
    if (bothFragments) {
        bodies[majIdx].accreteCount++;
        if (totalMassResidual > 1.0e20) {
            bodies[majIdx].isFragment = false;
            bodies[majIdx].name = "Proto-Cuerpo";
        }
    }

    // Ley 1: absorcion instantanea de la masa RESIDUAL de minIdx -- masa,
    // volumen (suma de volumenes, densidad media conservada) y momento
    // quedan en majIdx; minIdx desaparece en este mismo frame.
    bodies[majIdx].pos    = newPosResidual;
    bodies[majIdx].vel    = newVelResidual;
    bodies[majIdx].temperature = std::max(bodies[majIdx].temperature, bodies[minIdx].temperature) + globalHeatGain;
    bodies[majIdx].heatSpike = 1.0f;
    bodies[majIdx].mass    = totalMassResidual;
    bodies[majIdx].radius  = std::cbrt((VolumeFromRadius(R_maj0) + VolumeFromRadius(bodies[minIdx].radius)) * 3.0 / (4.0 * PI_D));

    // Composicion: el cuerpo fusionado hereda una mezcla de los
    // inventarios de ambos progenitores (ver composition.h). El solido
    // se pondera por masa total -- domina sobre la atmosfera en
    // cualquier cuerpo, igual que newPos/newVel arriba se ponderan por
    // masa. La atmosfera se pondera por una masa-proxy independiente
    // (atmosphereDensity * radio^2, proporcional a la masa de gas bajo
    // presion hidrostatica): asi un impactor sin atmosfera (peso 0) no
    // diluye la atmosfera del cuerpo mayor, y un gigante gaseoso que
    // absorbe un asteroide no pierde su composicion atmosferica.
    bodies[majIdx].solid_composition = MixComposition(
        bodies[majIdx].solid_composition, mA,
        bodies[minIdx].solid_composition, mB_residual);

    double atmWMaj = bodies[majIdx].atmosphereDensity * R_maj0 * R_maj0;
    double atmWMin = bodies[minIdx].atmosphereDensity * bodies[minIdx].radius * bodies[minIdx].radius;
    bodies[majIdx].atmospheric_composition = MixComposition(
        bodies[majIdx].atmospheric_composition, atmWMaj,
        bodies[minIdx].atmospheric_composition, atmWMin);

    bodies[minIdx].mass    = 0.0;

    // Erosion del impacto (Ley 4): aplica DESPUES de la absorcion, sobre
    // el cuerpo ya fusionado -- el crater se forma en su superficie
    // "de ahora".
    EjectAndMark(majIdx, erodeMaj);
}

// ── Radio de colisión efectivo bajo deformación de marea ────
// Devuelve el radio del elipsoide de marea de b en la dirección dir
// (unitaria, espacio físico): semieje mayor b.radius*tideElongation a lo
// largo de tideAxis, semiejes menores b.radius*tideSquash perpendiculares
// (formula exacta de radio de un elipsoide de revolucion). Sin marea
// (elongation==squash==1) se reduce exactamente a b.radius, asi que el
// volumen de colision coincide con la forma visual (TidalBodyTransform en
// renderer.h usa la misma elongacion/squash/eje).
static double EllipsoidRadiusToward(const Body& b, const Vector3D& dir) {
    if (b.tideElongation <= 1.0001f) return b.radius;
    double c = dir.dot(b.tideAxis);
    double a = b.radius * (double)b.tideElongation;
    double p = b.radius * (double)b.tideSquash;
    double denom = (c*c)/(a*a) + (1.0 - c*c)/(p*p);
    return 1.0 / std::sqrt(std::max(1e-30, denom));
}

// ── Detección de colisión continua (CCD) ───────────────────
static double SweptSphereTime(const Vector3D& pa, const Vector3D& va,
                               const Vector3D& pb, const Vector3D& vb,
                               double ra, double rb, double dt)
{
    Vector3D dp = pb - pa, dv = vb - va;
    double rSum = ra + rb;
    double a = dv.lengthSqr(), b = 2.0 * dp.dot(dv), c = dp.lengthSqr() - rSum*rSum;
    if (c <= 0.0) return 0.0;
    if (a < 1e-30 || b*b - 4.0*a*c < 0.0) return -1.0;
    double t = (-b - std::sqrt(b*b - 4.0*a*c)) / (2.0*a);
    return (t >= 0.0 && t <= dt) ? t : -1.0;
}

// ── Resolución de colisiones ────────────────────────────────
// 'dt' es la duracion del desplazamiento de posicion que se esta
// revisando retrospectivamente (el sub-paso h que acaba de correr en
// LeapfrogStep) -- ver Body::prevPos, fijado por StepPhysics justo antes
// de cada LeapfrogStep.
static void ResolveCollisions(std::vector<Body>& bodies, std::vector<DustParticle>& dust, double dt)
{
    const int n = (int)bodies.size();
    for (int i = 0; i < n; ++i) {
        if (bodies[i].mass <= 0.0) continue;
        for (int j = i+1; j < n; ++j) {
            if (bodies[j].mass <= 0.0) continue;

            // Inmunidad de colision para fragmentos recien spawneados (ver
            // SPAWN_GRACE_FRAMES/SpawnParticle): les da unos frames para
            // separarse del cuerpo de origen antes de evaluar fusion/CCD.
            if (bodies[i].spawnGraceFrames > 0 || bodies[j].spawnGraceFrames > 0) continue;

            // Radio efectivo de cada cuerpo en la direccion del otro: si
            // estan deformados por marea (gigantes gaseosos sobre todo,
            // donde el bulto puede llegar a ~4x el radio), la colision
            // sigue la forma elipsoidal alargada, igual que el dibujado
            // (TidalBodyTransform), en vez del radio esferico original.
            Vector3D dirIJ = NormalizeSafe(bodies[j].pos - bodies[i].pos);
            double rEffI   = EllipsoidRadiusToward(bodies[i],  dirIJ);
            double rEffJ   = EllipsoidRadiusToward(bodies[j], dirIJ * -1.0);
            double rSum    = rEffI + rEffJ;

            double distSqr = (bodies[i].pos - bodies[j].pos).lengthSqr();

            // Ley 3 (purga de intersecciones / absorcion absoluta): la
            // materia es impenetrable -- si dos cuerpos terminan
            // superpuestos AHORA (colocacion manual, deriva gravitacional,
            // o el desplazamiento de este sub-paso los dejo solapados), se
            // funden YA. Antes este caso se IGNORABA por completo: el
            // cuerpo menor quedaba flotando dentro del mayor, sintiendo
            // gravedad de corto alcance amplificada por SOFTENING, hasta
            // salir disparado a velocidades absurdas y disparar una fusion
            // de altisima energia (ejectedFraction al maximo) desde
            // cualquier angulo -- el origen real del "planeta que explota
            // desde adentro".
            if (distSqr <= rSum*rSum) {
                MergeBodies(bodies, dust, i, j);
                continue;
            }

            // CCD retrospectiva (anti-tunneling): 'prevPos' es la posicion
            // de cada cuerpo ANTES del avance de posicion de este sub-paso
            // (LeapfrogStep). Si el desplazamiento prevPos->pos de este
            // sub-paso cruzo a 'rSum' de distancia en algun punto, hubo una
            // colision DURANTE este sub-paso aunque, vistas solo las
            // posiciones actuales, los cuerpos esten separados (la pasaron
            // de largo) o ya superpuestos por el otro lado (ya cubierto
            // arriba). Una CCD puramente prospectiva (con pos/vel ya
            // actualizados) no puede detectar esto cuando v*h supera el
            // diametro combinado.
            Vector3D dispI = (bodies[i].pos - bodies[i].prevPos) * (1.0 / dt);
            Vector3D dispJ = (bodies[j].pos - bodies[j].prevPos) * (1.0 / dt);
            double tHit = SweptSphereTime(bodies[i].prevPos, dispI,
                                           bodies[j].prevPos, dispJ,
                                           rEffI, rEffJ, dt);
            if (tHit >= 0.0) MergeBodies(bodies, dust, i, j);
        }
    }
    bodies.erase(
        std::remove_if(bodies.begin(), bodies.end(), [](const Body& b){ return b.mass <= 0.0; }),
        bodies.end());
}

// ── Aceleraciones gravitacionales ──────────────────────────
// Cada cuerpo 'i' suma la atraccion de TODOS los demas (O(n^2) en vez del
// O(n^2/2) de antes, que actualizaba bodies[i] Y bodies[j] a la vez). El
// doble de pares se compensa de sobra al repartir las filas 'i' entre los
// hilos del pool: cada hilo solo escribe en bodies[i].acc para SUS 'i' --
// sin solapamiento, sin locks. LeapfrogStep llama a esto 2 veces por
// sub-paso (16 veces/frame), asi que con cientos de fragmentos esto suele
// ser el mayor costo de StepPhysics.
static void ComputeAccelerations(std::vector<Body>& bodies) {
    const int n = (int)bodies.size();
    Body* data = bodies.data();

    GetThreadPool().ParallelFor(n, [data, n](int i) {
        if (data[i].mass <= 0.0 || data[i].fixed) { data[i].acc = {0, 0, 0}; return; }

        Vector3D acc{0, 0, 0};
        const Vector3D pi = data[i].pos;
        for (int j = 0; j < n; ++j) {
            if (j == i || data[j].mass <= 0.0) continue;

            Vector3D delta = data[j].pos - pi;
            // Clamp de singularidad: la distancia real nunca debe usarse por
            // debajo de la suma de radios. Si los cuerpos estan superpuestos
            // (tunneling justo antes de la fusion), 1/dist^2 -> infinito y
            // produce una aceleracion (y luego velocidad) astronomica en un
            // solo sub-paso. Con el piso en rSum, la fuerza maxima posible es
            // la de "superficie a superficie", que es finita y fisica.
            double rSum = data[i].radius + data[j].radius;
            double distSqrRaw = delta.lengthSqr();
            double dist2 = std::max(distSqrRaw, rSum*rSum) + SOFTENING*SOFTENING;
            double dist  = std::sqrt(dist2);
            acc += delta * (G * data[j].mass / (dist2 * dist));
        }
        data[i].acc = acc;
    });
}

// ── Integración Leapfrog ────────────────────────────────────
static void LeapfrogStep(std::vector<Body>& bodies, double dt) {
    ComputeAccelerations(bodies);
    for (Body& b : bodies) if (!b.fixed) b.vel += b.acc * (0.5 * dt);
    for (Body& b : bodies) if (!b.fixed) b.pos += b.vel * dt;
    ComputeAccelerations(bodies);
    for (Body& b : bodies) if (!b.fixed) b.vel += b.acc * (0.5 * dt);
}

// ── Física de rotación crítica (breakup / mass shedding) ─────
// Cuando un objeto gira demasiado rápido para su gravedad superficial,
// el material del ecuador empieza a escapar. El umbral depende del tipo:
//   • NS/pulsar/magnetar: ~0.99 (ultracompactos, gravedad enorme)
//   • Estrellas:          ~0.65
//   • Gigantes gaseosos:  ~0.55
//   • Rocosos/helados:    ~0.50
// Por encima del umbral visual, la función expulsa polvo ecuatorial y
// reduce la masa progresivamente. La oblateness ya escala con
// criticalRotationFraction via RotationalOblateness (renderer.h).
inline void ApplyRotationalBreakup(Body& b, std::vector<DustParticle>& dust, double dt)
{
    // criticalRotationFraction se calcula en ApplyStellarPhaseProperties;
    // para no-estrellas lo recomputamos aqui.
    if (b.mass <= 0.0 || b.radius <= 0.0) return;

    float crf = b.criticalRotationFraction;
    if (crf <= 0.0f) {
        // Calcular para cuerpos no-estrella (estrellas lo tienen calculado ya)
        if (!b.isStar) {
            double omega = (double)b.spinRateDeg * (PI_D / 180.0) / 1200.0;
            double omegaCrit = std::sqrt(G * b.mass / (b.radius * b.radius * b.radius));
            crf = (float)ClampD(omega / omegaCrit, 0.0, 0.99);
            b.criticalRotationFraction = crf;
        }
    }

    // Umbral por tipo de objeto
    float breakupThreshold;
    if (b.stellarPhase == StellarPhase::NEUTRON_STAR ||
        b.stellarPhase == StellarPhase::PULSAR        ||
        b.stellarPhase == StellarPhase::MAGNETAR) {
        breakupThreshold = 0.99f;  // ultracompactos: casi imposible romper
    } else if (b.isStar) {
        breakupThreshold = 0.65f;  // estrellas
    } else if (b.isGasGiant) {
        breakupThreshold = 0.55f;  // gigantes gaseosos (menos densos)
    } else {
        breakupThreshold = 0.50f;  // rocosos/helados
    }

    if (crf < breakupThreshold * 0.85f) return; // bien por debajo del umbral, nada que hacer

    // Zona de shedding: entre 85% y 100% del umbral
    float severity = ClampD((crf - breakupThreshold * 0.85) /
                             (breakupThreshold * 0.15), 0.0, 1.0);

    // Tasa de pérdida de masa: exponencial con la severidad
    // Para objetos en el límite critico pierden hasta ~0.1% de masa por segundo sim.
    // Tasa conservadora: max 0.01% de masa por paso de simulacion.
    // El bug critico de "masa → 0 en segundos" ocurria cuando el
    // spinRateDeg heredado de un pulsar (1.29e7) llegaba a un cuerpo
    // 10^5 veces mayor tras una fusion, saturando criticalRotation=0.99;
    // esto queda resuelto con la conservacion de momento angular en
    // MergeBodies. El limite aqui es una segunda linea de defensa.
    double massLossRate = 0.00001 * severity * severity * b.mass;
    double massLoss = massLossRate * dt;
    massLoss = std::min(massLoss, b.mass * 0.0001); // max 0.01% por paso

    if (massLoss > 0.0) {
        b.mass        -= massLoss;
        b.intactMass   = b.mass;
        // Masa perdida → polvo ecuatorial (eyeccion en el plano ecuatorial)
        int nParticles = (int)(severity * 8.0) + 1;
        for (int k = 0; k < nParticles; k++) {
            int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
            if (slot < 0) break;
            DustParticle& d = dust[slot];
            d.active    = true;
            d.isRing    = false;
            d.hostBodyId= -1;
            d.heatSpike = 0.3f + 0.7f * (float)severity;

            // Color segun temperatura/tipo del cuerpo
            if (b.isStar)
                d.color = {255, (uint8_t)(150 + (int)(severity * 80)), 50, 255};
            else if (b.isGasGiant)
                d.color = {200, 150, 100, 255};
            else
                d.color = {180, 160, 140, 255};

            // Eyectar en el plano ecuatorial real del cuerpo:
            // el eje de rotacion esta definido por axialTilt (inclinacion
            // respecto a Y) y rotationAngle (fase actual de la rotacion).
            // El plano ecuatorial es perpendicular a ese eje.
            double tiltR  = (double)b.axialTilt  * PI_D / 180.0;
            double rotR   = (double)b.rotationAngle * PI_D / 180.0;
            // Eje de rotacion (polo norte del objeto)
            Vector3D poleAxis = {
                std::sin(tiltR) * std::cos(rotR),
                std::cos(tiltR),
                std::sin(tiltR) * std::sin(rotR)
            };
            // Vector perpendicular al polo (en el plano ecuatorial)
            Vector3D eqRef = { -poleAxis.y, poleAxis.x, 0.0 };
            double eqRefLen = eqRef.length();
            if (eqRefLen < 1e-6) eqRef = { 1.0, 0.0, 0.0 };
            else                  eqRef = eqRef / eqRefLen;
            // Rotar eqRef un angulo aleatorio alrededor del polo → punto aleatorio en ecuador
            double angle = FastRand01() * 2.0 * PI_D;
            // Rodrigues rotation formula para rotar eqRef alrededor de poleAxis
            double cosA = std::cos(angle), sinA = std::sin(angle);
            Vector3D ejectDir = eqRef * cosA
                              + Cross(poleAxis, eqRef) * sinA
                              + poleAxis * (poleAxis.dot(eqRef) * (1.0 - cosA));
            // Pequeno componente vertical aleatorio (~5%) para dar volumen al disco
            ejectDir = ejectDir + poleAxis * ((FastRand01() - 0.5) * 0.05);
            double edLen = ejectDir.length();
            if (edLen > 1e-9) ejectDir = ejectDir / edLen;

            double vEsc  = std::sqrt(G * b.mass / b.radius);
            double speed = vEsc * (0.3 + FastRand01() * 0.4);
            d.pos = b.pos + ejectDir * b.radius * 1.05;
            d.vel = b.vel + ejectDir * speed;

            double pR = b.radius * (0.005 + FastRand01() * 0.02);
            d.scale  = (float)(pR * RENDER_SCALE);
            d.radius = pR;
            d.seed   = (float)FastRand01();
            d.spawnRadiusFromHost = 0.0f;
        }
    }

    // Si supera el 99% del umbral critico, desestabilizacion severa:
    // velocidad de spin empieza a caer (el cuerpo pierde momento angular
    // con el material eyectado) — evita que siga acelerando infinitamente.
    if (crf > breakupThreshold * 0.99f) {
        b.spinRateDeg *= (float)(1.0 - 0.001 * dt / 1200.0);
    }
}

// ── Definición de SpawnSupernovaEjecta (declarada en stellar_evolution.h) ──
inline void SpawnSupernovaEjecta(std::vector<Body>& bodies, std::vector<DustParticle>& dust,
                                  const Body& b)
{
    (void)bodies; // reservado para empujar cuerpos vecinos en v2
    int ejectedCount = 0;
    constexpr int MAX_EJECTA = 200;
    for (int attempt = 0; attempt < 600 && ejectedCount < MAX_EJECTA; ++attempt) {
        int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
        if (slot < 0) break;

        DustParticle& d = dust[slot];
        d.active     = true;
        d.isRing     = false;
        d.hostBodyId = -1;
        d.heatSpike  = 1.0f;
        d.color      = {255, 180, 80, 255};
        d.seed       = (float)FastRand01();

        double cosT  = FastRand01() * 2.0 - 1.0;
        double sinT  = std::sqrt(std::max(0.0, 1.0 - cosT * cosT));
        double phi   = FastRand01() * 2.0 * PI_D;
        Vector3D dir = { sinT * std::cos(phi), cosT, sinT * std::sin(phi) };
        double speed = 1e6 + FastRand01() * 2.9e7;
        d.pos        = b.pos + dir * b.radius * 1.1;
        d.vel        = b.vel + dir * speed;
        double pR    = 2000.0 + FastRand01() * 8000.0;
        d.scale      = (float)(pR * RENDER_SCALE);
        d.radius     = pR;
        double axC   = 2.0*FastRand01()-1.0, axS = std::sqrt(std::max(0.0,1.0-axC*axC));
        double axP   = 2.0*PI_D*FastRand01();
        d.rotationAxis = {(float)(axS*std::cos(axP)),(float)axC,(float)(axS*std::sin(axP))};
        d.rotationSpeed   = (float)((FastRand01()-0.5)*2.0);
        d.currentRotation = (float)(FastRand01()*2.0*PI_D);
        d.spawnRadiusFromHost = 0.0f;
        ++ejectedCount;
    }
}

// ── Mareas y límite de Roche ────────────────────────────────
static void ApplyTidesAndRoche(std::vector<Body>& bodies, std::vector<DustParticle>& dust, double dt)
{
    std::vector<Body> newBodies;

    // Fragmentos YA existentes en bodies (MakeFragments solo cuenta
    // newBodies, asi que sin esto el limite MAX_FRAGMENTS se aplicaria
    // solo a lo nuevo de este paso, permitiendo que el total crezca sin
    // limite si ya hay muchos -> bodies.size() se dispara y la fisica
    // O(n^2) se vuelve impracticable (parece un crash).
    int existingFragCount = 0;
    for (const Body& eb : bodies) {
        if (eb.mass <= 0.0) continue;
        if (eb.isFragment) existingFragCount++;
    }

    for (Body& b : bodies) {
        if (b.mass <= 0.0) continue;

        double bestStress = 0.0;
        Vector3D bestAxis {0, 1, 0};
        const Body* bestSource = nullptr;
        double bestDist = 1e30;
        double bodyDensity = DensityOf(b.mass, b.radius);

        double rocheMult = 2.44;
        if      (b.material == MAT_ICY)      rocheMult = 2.0;
        else if (b.material == MAT_ROCKY)    rocheMult = 1.44;
        else if (b.material == MAT_METALLIC) rocheMult = 1.26;

        for (const Body& s : bodies) {
            if (&s == &b || s.mass <= 0.0 || s.isFragment) continue;

            // Solo el perturbador mas masivo/significativo cuenta para la
            // deformacion de marea: con cientos de fragmentos/Protocuerpos
            // pequenos orbitando muy cerca, bestSource (y por tanto
            // bestAxis) competia entre ellos cada frame -- direcciones
            // totalmente distintas de un frame a otro -- y el planeta
            // "convulsionaba" como gelatina pese al suavizado de tideAxis.
            // Un cuerpo con menos del 1% de la masa de 'b' no puede ser la
            // fuente dominante de marea de 'b', sin importar que tan cerca
            // orbite.
            if (s.mass < b.mass * 0.01) continue;
            Vector3D d = s.pos - b.pos;
            double dist = std::max(1.0, d.length());
            Vector3D dir = d / dist;

            // Si las mareas ya abultan a b y/o a s el uno hacia el otro
            // (tideAxis del frame anterior alineado con la direccion al
            // otro cuerpo), sus superficies efectivas estan mas cerca que
            // sus centros: restamos esos bultos de la distancia antes de
            // medir el estres. Sin esto, dos cuerpos "besandose" por sus
            // bultos de marea se quedaban estables para siempre con una
            // elongacion moderada en vez de entrar en una espiral de
            // estres creciente -> ruptura, como ocurriria en la realidad.
            double bulgeB = b.radius * std::max(0.0f, b.tideElongation - 1.0f) * std::max(0.0,  dir.dot(b.tideAxis));
            double bulgeS = s.radius * std::max(0.0f, s.tideElongation - 1.0f) * std::max(0.0, -dir.dot(s.tideAxis));
            double distEff = std::max(dist * 0.05, dist - bulgeB - bulgeS);

            double tidalAcc = G * s.mass * b.radius / (distEff*distEff*distEff);
            double ratio    = tidalAcc / std::max(1e-18, G * b.mass / std::max(1.0, b.radius*b.radius));
            double rocheRatio = (rocheMult * s.radius * std::cbrt(DensityOf(s.mass, s.radius) / bodyDensity)) / distEff;
            double stress = std::max(ratio, rocheRatio * 0.55);
            if (stress > bestStress) {
                bestStress = stress; bestAxis = dir;
                bestSource = &s;    bestDist  = dist;
            }
        }

        // Bulto de marea "fisico": magnitud impulsada por bestStress, EXACTAMENTE
        // igual que siempre (independiente de composicion/tamano). Esto alimenta
        // distEff (feedback de bulto -> estres, ver bucle de arriba),
        // EllipsoidRadiusToward (forma de colision) y rocheRatio/canRupture/
        // tidalDamage mas abajo -- el comportamiento de estres/ruptura/colision
        // de cuerpos pequenos y rigidos NO cambia.
        double maxStretchBase = (b.material == MAT_GASEOUS) ? 4.5 : ((b.material == MAT_ICY) ? 3.0 : 1.5);

        // Suaviza el eje de marea (nlerp) en vez de saltar de golpe a
        // 'bestAxis': con muchos cuerpos pequenos orbitando muy cerca,
        // bestSource (y por tanto bestAxis) puede cambiar de un cuerpo a
        // otro de un frame a otro -- sin suavizado, el bulto de marea
        // (tideElongation, ya con magnitud considerable) cambiaba de
        // direccion bruscamente cada frame, dando un efecto de
        // "contorsion" violenta. La MAGNITUD (tideStretch) sigue
        // respondiendo igual de rapido; solo la DIRECCION se amortigua.
        b.tideAxis      = NormalizeSafe(b.tideAxis * 0.9 + bestAxis * 0.1);
        b.tideStretch   = (float)ClampD(b.tideStretch * 0.95 + bestStress * 0.10, 0.0, maxStretchBase);
        b.tideElongation = 1.0f + b.tideStretch * 0.65f;
        b.tideSquash     = 1.0f / std::sqrt(b.tideElongation);

        // Rigidez VISUAL: cuanto mas pequeno y mas solido/denso es un cuerpo,
        // menos se ESTIRA AL DIBUJARSE para el MISMO bulto fisico (b.tideStretch)
        // -- ver tideVisualElongation/tideVisualSquash en body.h y
        // TidalBodyTransform en renderer.h. La rigidez por tamano usa el radio
        // terrestre como referencia: a escala planetaria (>= R_EARTH) no resta
        // nada (gigantes gaseosos/helados conservan su deformacion visual
        // "extrema"/"moderada"), y crece por debajo de esa escala hasta casi
        // anular la deformacion visual en lunas/asteroides/fragmentos pequenos.
        // El dano por marea "ablanda" la rigidez visual gradualmente: un cuerpo
        // rigido que empieza a desintegrarse SI muestra el estiramiento completo,
        // como si la estructura que antes resistia ya estuviera rota.
        constexpr double R_EARTH_REF = 6.371e6;
        double materialRigidity = (b.material == MAT_GASEOUS) ? 0.0
                                : (b.material == MAT_ICY)      ? 0.25
                                : (b.material == MAT_METALLIC) ? 0.65 : 0.55;
        double sizeRigidity = ClampD(std::log10(R_EARTH_REF / std::max(1.0, b.radius)) / 2.5, 0.0, 0.9);
        double rigidity     = ClampD(materialRigidity + sizeRigidity * (1.0 - materialRigidity), 0.0, 0.97);
        double rigidityRelief = ClampD(b.tidalDamage / 0.3, 0.0, 1.0);
        double visualStretch  = b.tideStretch * ((1.0 - rigidity) + rigidity * rigidityRelief);
        b.tideVisualElongation = 1.0f + (float)(visualStretch * 0.65);
        b.tideVisualSquash     = 1.0f / std::sqrt(b.tideVisualElongation);

        // Limite duro anti-"gelatina": un planeta solido (no gigante
        // gaseoso/helado) jamas se dibuja estirado mas de un 5% extra de su
        // radio, sin importar cuantas fuentes de marea/ruido gravitacional
        // haya cerca. Los gigantes gaseosos/helados conservan su
        // deformacion visual extrema (hasta tideVisualElongation grande)
        // porque es el efecto deseado al desintegrarse en el limite de
        // Roche.
        if (!b.isGasGiant) {
            constexpr float MAX_VISUAL_ELONGATION_SOLID = 1.05f;
            if (b.tideVisualElongation > MAX_VISUAL_ELONGATION_SOLID) {
                b.tideVisualElongation = MAX_VISUAL_ELONGATION_SOLID;
                b.tideVisualSquash     = 1.0f / std::sqrt(MAX_VISUAL_ELONGATION_SOLID);
            }
        }

        // Bloqueo por marea: cuanto mas fuerte el estres de marea, mas se
        // "engancha" la rotacion del cuerpo a su movimiento orbital
        // alrededor del cuerpo dominante (bestSource), hasta quedar en
        // resonancia 1:1 (rotacion sincrona, siempre la misma cara
        // mirando al perturbador) -- igual que la Luna con la Tierra.
        // Las estrellas también se deforman y pueden bloquearse en binarias.
        if (bestSource) {
            Vector3D d    = bestSource->pos - b.pos;
            Vector3D dvel = bestSource->vel - b.vel;
            double   r2   = d.x*d.x + d.z*d.z;
            if (r2 > 1.0) {
                // Velocidad angular orbital (rad/s) alrededor del eje Y.
                double omega = (d.x*dvel.z - d.z*dvel.x) / r2;
                // rotationAngle (grados, convencion de MatrixRotateY) gira
                // el mundo en sentido opuesto al angulo de un punto fijo
                // de la malla -> para que la malla siga la direccion hacia
                // bestSource, su velocidad debe ser -omega.
                float targetSpinDeg = (float)(-omega * 1200.0 * (180.0 / PI_D));

                // Bloqueo gradual: a partir de un estres de marea moderado
                // (muy por debajo del umbral de ruptura ~0.35-0.55) la
                // rotacion empieza a sincronizarse; con marea fuerte se
                // sincroniza casi instantaneamente.
                double lockPull = ClampD(bestStress / 0.02, 0.0, 1.0);
                b.tidalLock = (float)ClampD(b.tidalLock * 0.999 + lockPull * 0.001, 0.0, 1.0);
                b.spinRateDeg += (targetSpinDeg - b.spinRateDeg) * (float)(lockPull * 0.02);
            }
        }

        if (bestSource && !b.isStar) {
            // Caída en la estrella
            if (bestSource->isStar && bestDist < bestSource->radius * 3.0) {
                b.mass = 0.0; continue;
            }

            bool canRupture = (bestSource->mass > b.mass * 3.0) || bestSource->isStar;

            if (canRupture && bestStress > 0.35 && !b.isFragment) {
                double materialResistance = (b.material == MAT_METALLIC) ? 4.0
                                          : (b.material == MAT_ROCKY)    ? 2.0 : 0.6;
                double damageRate = (bestStress > 1.0) ? 5e-4 : 5e-5;

                b.temperature += (bestStress - 0.35) * dt * (3.0 / materialResistance);
                b.heatSpike    = std::min(b.heatSpike + 0.005f, 0.8f);
                b.tidalDamage  = ClampD(b.tidalDamage + (bestStress - 0.35) * (dt / materialResistance) * damageRate, 0.0, 1.0);
                b.isDisintegrating = (b.tidalDamage > 0.05);
                b.lastFragTime += dt;

                // Intervalo entre rafagas de desprendimiento: el tiempo dinamico
                // propio del cuerpo, T_dyn = 2*PI*sqrt(R^3/(G*M)) ~ 1/sqrt(G*rho)
                // -- la misma escala de tiempo que define el limite de Roche.
                // Con estres por encima del umbral (bestStress>1), el desprendimiento
                // se acelera proporcionalmente al exceso de estres. Antes este
                // intervalo era una constante fija de 10-30s, muchisimo menor que
                // TIME_STEP (1200s): la condicion de abajo era SIEMPRE verdadera en
                // cada paso de fisica, generando un lote de fragmentos/polvo por
                // FRAME y saturando MAX_FRAGMENTS en segundos.
                double tDyn = 2.0 * PI_D * std::sqrt((b.radius * b.radius * b.radius) / (G * b.mass));
                double streamInterval = tDyn / std::max(1.0, bestStress);
                if (b.isDisintegrating && b.lastFragTime > streamInterval) {
                    double shedRate = (bestStress > 1.0) ? 0.001 : 0.0001;

                    // Una vez revelado el nucleo (material ya no es GASEOUS/
                    // ICY), la roca/metal resiste la erosion por marea mucho
                    // mas que el gas/hielo -- reutiliza el mismo ratio
                    // 'materialResistance' que ya regula tidalDamage arriba.
                    // Para gigantes gaseosos/helados intactos no cambia nada
                    // (0.6/0.6=1); para roca/metal el shed cae a 0.3x/0.15x.
                    shedRate *= (0.6 / materialResistance);

                    double shedFrac = std::min(shedRate * b.lastFragTime, 0.02);
                    double shedMass = b.mass * shedFrac;
                    b.mass -= shedMass;

                    // Modelo de volumen compuesto (cuerpo diferenciado con
                    // nucleo, ver coreMass/shellBaseDensity en body.h): el
                    // nucleo (volumen fijo, densidad segun coreMaterial) no
                    // cambia al perder la envoltura/manto; solo se reduce el
                    // volumen de la capa restante (su densidad,
                    // shellBaseDensity, es constante). El radio se encoge de
                    // forma continua, sin saltos, convergiendo al radio del
                    // nucleo desnudo cuando toda la masa de la envoltura/manto
                    // se ha perdido. Aplica tanto a gigantes gaseosos/helados
                    // (nucleo rocoso) como a planetas rocosos/helados
                    // diferenciados (nucleo metalico, ver
                    // DIFFERENTIATION_RADIUS en constants.h).
                    bool compositeShell = (b.coreMass > 0.0 && b.shellBaseDensity > 0.0);
                    if (compositeShell) {
                        double coreDensity = (b.coreMaterial == MAT_METALLIC) ? RHO_METALLIC_CORE : RHO_ROCKY_CORE;
                        double shellMass  = std::max(0.0, b.mass - b.coreMass);
                        double coreVolume = b.coreMass / coreDensity;
                        double shellVolume = shellMass / b.shellBaseDensity;
                        b.radius = std::cbrt((coreVolume + shellVolume) * 3.0 / (4.0 * PI_D));

                        // Composicion: a medida que el manto se erosiona, el
                        // nucleo (coreMass, fijo) representa una fraccion
                        // creciente de la masa restante. solid_composition se
                        // desplaza hacia CompMetallicCore() (composition.h)
                        // con ese mismo peso -- en el limite mass->coreMass
                        // (manto agotado) converge exactamente a un nucleo
                        // metalico desnudo (ver bloque de abajo). Solo aplica
                        // a nucleos METALICOS (planetas rocosos/helados
                        // diferenciados): para gigantes gaseosos/helados
                        // (coreMaterial=MAT_ROCKY) solid_composition YA es la
                        // composicion del nucleo rocoso desde el spawn (ver
                        // AssignComposition, catalog.h), asi que no cambia.
                        if (b.coreMaterial == MAT_METALLIC) {
                            double coreFrac = ClampD(b.coreMass / std::max(1.0, b.mass), 0.0, 1.0);
                            b.solid_composition = MixComposition(
                                b.solid_composition, 1.0 - coreFrac,
                                CompMetallicCore(), coreFrac);
                        }
                    } else {
                        b.radius = std::cbrt((3.0 * VolumeFromRadius(b.radius) * (1.0 - shedFrac)) / (4.0 * PI_D));
                    }

                    Body shedTmpl = b;
                    shedTmpl.mass = shedMass;
                    double vCirc = std::sqrt(G * bestSource->mass / std::max(1.0, bestDist));
                    MakeFragments(newBodies, dust, shedTmpl, bestAxis, vCirc, true, &b.vel, nullptr,
                                   existingFragCount, b.mass);
                    b.lastFragTime = 0.0;

                    // Crateres inversos: el material que se desprende deja un
                    // hueco visible en los dos puntos de Roche por los que
                    // MakeFragments expulsa fragmentos/polvo (L1, hacia
                    // bestSource, y L2, el lado opuesto). Mismo mecanismo que
                    // un crater de impacto (AddImpactMark) pero sin energia
                    // (sin destello de magma): es material que SALIO, no que
                    // llego con energia cinetica externa. El tamano usa
                    // 'tidalDamage' (acumulado, monotono mientras el cuerpo
                    // se desintegra) en vez de 'shedFrac' (esta ultima es
                    // practicamente constante entre rafagas, porque
                    // streamInterval autorregula lastFragTime -- el cráter
                    // nunca creceria). Con tidalDamage la cicatriz crece de
                    // forma continua a medida que mas estructura del cuerpo
                    // se compromete, hasta el rango completo del clamp
                    // cuando tidalDamage se acerca al umbral de explosion (0.95).
                    float shedMarkRadius = (float)ClampD(0.05 + b.tidalDamage * 1.2, 0.03, 0.6);
                    AddImpactMark(b, bestAxis, shedMarkRadius, 0.0f);
                    AddImpactMark(b, bestAxis * -1.0, shedMarkRadius, 0.0f);

                    // Envoltura/manto agotado: el nucleo queda expuesto
                    // (planeta Chthoniano o nucleo metalico desnudo). A
                    // partir de aqui 'material' pasa a 'coreMaterial', mucho
                    // mas denso -- bodyDensity sube (ver bestStress arriba),
                    // lo que reduce 'ratio'/'rocheRatio' y puede bajar
                    // bestStress por debajo de 0.35, deteniendo la
                    // desintegracion: el nucleo sobrevive como remanente. El
                    // proximo shed (si continua) entra por la rama 'else'
                    // (densidad uniforme) con el materialResistance de
                    // roca/metal, mucho mas alto.
                    if (compositeShell && b.mass <= b.coreMass) {
                        b.material   = b.coreMaterial;
                        b.isGasGiant = false;
                        // La envoltura/atmosfera (lo unico que aportaba al
                        // inventario atmospheric_composition) ya se perdio
                        // por completo -- el remanente es un nucleo desnudo
                        // sin gas.
                        b.atmospheric_composition = CompMap{};
                    }
                }
            } else {
                b.tidalDamage = ClampD(b.tidalDamage - dt * 2e-6, 0.0, 1.0);
            }

            // Explosión final: requiere que el cuerpo este EN ESTE MOMENTO
            // bajo estres de marea significativo (bestStress > 0.35, el
            // mismo umbral que activa la acumulacion de tidalDamage arriba).
            // Sin esta condicion, 'canRupture' (cualquier cuerpo >3x mas
            // masivo en el sistema, sin importar la distancia) y
            // 'b.mass < 1e19' (cierto para cualquier asteroide/fragmento
            // pequeno) bastaban por si solos: TODO cuerpo pequeno explotaba
            // en el primer paso de fisica sin importar donde estuviera --
            // los asteroides normales jamas llegaban a atravesar la
            // atmosfera ni a impactar la corteza.
            if (canRupture && bestStress > 0.35 && (b.tidalDamage >= 0.95 || b.mass < 1.0e19)) {
                Body doomed = b;
                doomed.temperature += 2000.0;
                doomed.heatSpike   = 1.0f;
                double vCirc = std::sqrt(G * bestSource->mass / std::max(1.0, bestDist));
                MakeFragments(newBodies, dust, doomed, bestAxis, vCirc, false, nullptr, nullptr,
                               existingFragCount);
                b.mass = 0.0;
            }
        }
    }

    if (!newBodies.empty()) bodies.insert(bodies.end(), newBodies.begin(), newBodies.end());

    // ── Transferencia de masa Roche en binarias estelares (Eggleton 1983) ───
    // Acumuladores para evitar invalidación de punteros y doble conteo.
    static std::vector<double> pendingMassRoche;
    pendingMassRoche.assign(bodies.size(), 0.0);

    constexpr double ROCHE_TRANSFER_RATE = 1e-9; // fracción de masa/s (muy lento por defecto)

    for (size_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (!b.isStar || b.mass <= 0.0 || !b.gravityEnabled) continue;

        // Buscar la estrella compañera más cercana y masiva
        size_t bestJ = SIZE_MAX;
        double bestDist2 = 1e99;
        for (size_t j = 0; j < bodies.size(); ++j) {
            if (i == j || !bodies[j].isStar || bodies[j].mass <= 0.0) continue;
            double d2 = (b.pos - bodies[j].pos).lengthSqr();
            if (d2 < bestDist2) { bestDist2 = d2; bestJ = j; }
        }
        if (bestJ == SIZE_MAX) continue;

        double separation = std::sqrt(bestDist2);
        if (separation < 1.0) continue;

        // Fórmula de Eggleton (1983): R_L / a = 0.49q^(2/3) / (0.6q^(2/3) + ln(1+q^(1/3)))
        double q = b.mass / bodies[bestJ].mass;
        double rocheRadius = EggletonsRocheLobe(q, separation);

        if (b.radius < rocheRadius) continue; // estrella dentro de su lóbulo

        // Transferencia proporcional al desbordamiento
        double overflow = (b.radius - rocheRadius) / rocheRadius; // 0..1 aprox
        double massDt   = std::min(b.mass * ROCHE_TRANSFER_RATE * overflow * dt, b.mass * 0.005);
        if (massDt <= 0.0) continue;

        pendingMassRoche[i]    -= massDt;
        pendingMassRoche[bestJ] += massDt;

        // Partículas de plasma en el punto L1 (aproximación visual)
        // L1 ≈ a × (1 - q^(1/3)/3) desde la estrella más masiva
        double rl1Frac = 1.0 - std::pow(bodies[bestJ].mass / (b.mass + bodies[bestJ].mass), 1.0 / 3.0) / 3.0;
        Vector3D l1Pos = bodies[bestJ].pos + (b.pos - bodies[bestJ].pos) * rl1Frac;

        int spawned = 0;
        for (int attempt = 0; attempt < 30 && spawned < 5; ++attempt) {
            int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
            if (slot < 0) break;
            DustParticle& d = dust[slot];
            d.active        = true;
            d.isRing        = false;
            d.hostBodyId    = -1;
            d.heatSpike     = 0.8f;
            d.color         = {255, 140, 60, 255};
            d.seed          = (float)FastRand01();
            d.pos           = l1Pos;
            // Velocidad: tangencial orbital + componente hacia la receptora
            Vector3D toAccretor = NormalizeSafe(bodies[bestJ].pos - l1Pos);
            Vector3D velTan     = b.vel * 0.5 + bodies[bestJ].vel * 0.5;
            d.vel = velTan + toAccretor * (massDt * 1e6 / std::max(1.0, dt));
            double pRadiusM = 500.0 + FastRand01() * 2000.0;
            d.scale  = (float)(pRadiusM * RENDER_SCALE);
            d.radius = pRadiusM;
            double ax = 2.0*FastRand01()-1.0, aSinT = std::sqrt(std::max(0.0,1.0-ax*ax));
            double aP = 2.0*PI_D*FastRand01();
            d.rotationAxis = {(float)(aSinT*std::cos(aP)),(float)ax,(float)(aSinT*std::sin(aP))};
            d.rotationSpeed   = (float)((FastRand01()-0.5)*2.0);
            d.currentRotation = 0.0f;
            d.spawnRadiusFromHost = 0.0f;
            ++spawned;
        }
    }

    // Aplicar transferencias acumuladas
    for (size_t i = 0; i < bodies.size() && i < pendingMassRoche.size(); ++i) {
        if (pendingMassRoche[i] == 0.0) continue;
        bodies[i].mass         += pendingMassRoche[i];
        if (bodies[i].mass > 0.0 && bodies[i].isStar)
            bodies[i].baseLuminosity = L_SUN * std::pow(bodies[i].mass / M_SUN, 3.5);
    }
}

// ── Inteligencia de polvo: ligadura orbital, anillos y acrecion ─────────
// El polvo sigue siendo visual/ligero, pero ahora puede:
//  - detectar si esta ligado a un host;
//  - asentarse gradualmente en el ecuador del host;
//  - convertirse en RingParticle persistente;
//  - agruparse en protocuerpos si esta fuera del limite de Roche.

static double DustMassEstimate(const DustParticle& d) {
    constexpr double DUST_DENSITY = 3000.0; // kg/m^3, roca/regolito promedio
    return (4.0 / 3.0) * PI_D * d.radius * d.radius * d.radius * DUST_DENSITY;
}

static Vector3D DustHostSpinAxis(const Body& host) {
    return NormalizeSafe(RotateAxialTilt(
        {0.0, 1.0, 0.0},
        (double)host.axialTilt * PI_D / 180.0
    ));
}

static Vector3D RotateAroundAxis(const Vector3D& v, const Vector3D& axis, double angle)
{
    Vector3D a = NormalizeSafe(axis);
    double c = std::cos(angle);
    double s = std::sin(angle);

    return v * c + Cross(a, v) * s + a * (a.dot(v) * (1.0 - c));
}

static const Body* FindBestDustHost(
    const DustParticle& d,
    const std::vector<Body>& bodies,
    double* outEnergy = nullptr,
    double* outR = nullptr,
    double* outEcc = nullptr,
    double* outInclCos = nullptr)
{
    const Body* best = nullptr;

    double bestScore = 0.0;
    double bestEnergy = 0.0;
    double bestR = 0.0;
    double bestEcc = 1.0;
    double bestIncl = 0.0;

    for (const Body& b : bodies) {
        if (b.mass <= 0.0 || b.radius <= 0.0) continue;

        Vector3D rel = d.pos - b.pos;
        Vector3D rv  = d.vel - b.vel;

        double r = std::max(1.0, rel.length());
        double mu = G * b.mass;

        // Energia orbital especifica.
        // energy < 0 => orbita ligada al host.
        double energy = 0.5 * rv.lengthSqr() - mu / r;
        if (energy >= 0.0) continue;

        Vector3D h = Cross(rel, rv);
        double h2 = h.lengthSqr();

        double ecc = 1.0;
        if (h2 > 1e-12 && mu > 0.0) {
            double e2 = std::max(0.0, 1.0 + (2.0 * energy * h2) / (mu * mu));
            ecc = std::sqrt(e2);
        }

        Vector3D hN = NormalizeSafe(h);
        double inclCos = std::abs(hN.dot(DustHostSpinAxis(b)));

        // Preferimos el pozo que mas fuertemente liga a la particula.
        double score = -energy;

        if (!best || score > bestScore) {
            best = &b;
            bestScore = score;
            bestEnergy = energy;
            bestR = r;
            bestEcc = ecc;
            bestIncl = inclCos;
        }
    }

    if (outEnergy)  *outEnergy = bestEnergy;
    if (outR)       *outR = bestR;
    if (outEcc)     *outEcc = bestEcc;
    if (outInclCos) *outInclCos = bestIncl;

    return best;
}

struct DustGridKey {
    long long x, y, z;

    bool operator==(const DustGridKey& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
};

struct DustGridKeyHash {
    size_t operator()(const DustGridKey& k) const {
        uint64_t x = (uint64_t)k.x * 11400714819323198485ull;
        uint64_t y = (uint64_t)k.y * 14029467366897019727ull;
        uint64_t z = (uint64_t)k.z * 1609587929392839161ull;
        return (size_t)(x ^ (y >> 1) ^ (z >> 2));
    }
};

static void UpdateDustIntelligence(
    std::vector<Body>& bodies,
    std::vector<DustParticle>& dust,
    double dt)
{
    constexpr double RING_STABLE_PROMOTE_TIME = 3600.0 * 12.0;
    constexpr double RING_CANDIDATE_TIME      = 1800.0;

    constexpr double MAX_RING_RADIUS_MULT = 120.0;
    constexpr double RING_ECC_MAX         = 0.22;
    constexpr double RING_INCL_COS_MIN    = 0.88;

    constexpr double PLANE_SPRING = 2.5e-5;
    constexpr double PLANE_DAMP   = 8.0e-3;
    constexpr double RADIAL_DAMP  = 4.0e-3;

    // 1) Clasificacion orbital + asentamiento ecuatorial.
    // Acotado a g_dustHighWaterMark (ver FindFreeDustSlot mas arriba), no
    // dust.size(): el pool esta preasignado a MAX_DUST_PARTICLES completo
    // desde el arranque, recorrerlo entero aqui cuando casi todo esta
    // inactivo es puro desperdicio.
    int dustRange1 = std::min((int)dust.size(), g_dustHighWaterMark);
    for (int di = 0; di < dustRange1; ++di) {
        DustParticle& d = dust[di];
        if (!d.active) continue;

        if (d.isRing || d.state == DustState::RingParticle) {
            d.state = DustState::RingParticle;
            continue;
        }

        double energy = 0.0;
        double r = 0.0;
        double ecc = 1.0;
        double inclCos = 0.0;


        const Body* host = FindBestDustHost(d, bodies, &energy, &r, &ecc, &inclCos);

        if (!host) {
            d.state = DustState::Decaying;
            d.stableOrbitTime = 0.0;
            if (!d.isRing) d.hostBodyId = -1;
            continue;
        }

        d.hostBodyId = (int64_t)host->id;
        d.state = DustState::BoundOrbit;
        d.usefulAge += dt;

        if (!host->isStar && host->mass > 0.0 && host->radius > 0.0) {
            Vector3D rel = d.pos - host->pos;
            Vector3D rv  = d.vel - host->vel;

            Vector3D radial = NormalizeSafe(rel);
            Vector3D up = DustHostSpinAxis(*host);

            double verticalPos = rel.dot(up);
            double verticalVel = rv.dot(up);

            double roche = 2.44 * host->radius *
                std::cbrt(std::max(1e-9, DensityOf(host->mass, host->radius) / 3000.0));

            bool inRingZone =
                r > host->radius * 1.15 &&
                r < host->radius * MAX_RING_RADIUS_MULT;

            bool stableOrbit =
                inRingZone &&
                ecc < RING_ECC_MAX &&
                inclCos > RING_INCL_COS_MIN;

            if (stableOrbit) {
                d.stableOrbitTime += dt;

                d.state = (d.stableOrbitTime >= RING_CANDIDATE_TIME)
                    ? DustState::RingCandidate
                    : DustState::BoundOrbit;

                // Asentamiento suave al ecuador:
                // No teletransporta; amortigua velocidad vertical, posicion vertical
                // y excentricidad radial.
                d.vel -= up * (verticalVel * ClampD(PLANE_DAMP * dt, 0.0, 0.35));
                d.vel -= up * (verticalPos * ClampD(PLANE_SPRING * dt, 0.0, 0.20));

                double radialVel = rv.dot(radial);
                d.vel -= radial * (radialVel * ClampD(RADIAL_DAMP * dt, 0.0, 0.20));

                if (d.stableOrbitTime >= RING_STABLE_PROMOTE_TIME || r < roche * 1.20) {
                    d.state = DustState::RingParticle;
                    d.isRing = true;
                    d.heatSpike = std::max(0.0f, d.heatSpike - 0.02f);
                }
            } else {
                d.stableOrbitTime = std::max(0.0, d.stableOrbitTime - dt * 0.5);
                if (!d.isRing) d.state = DustState::BoundOrbit;
            }
        }
    }

    // 2) Acrecion ligera por grid.
    // Solo polvo util no estabilizado como anillo.
    constexpr double CELL_SIZE = 2.0e7;       // 20 000 km
    constexpr int    MIN_CLUSTER_COUNT = 10;
    constexpr double MIN_PROTO_MASS = 5.0e16;
    constexpr double MAX_DISPERSION_FRAC = 0.75;

    int dustRange2 = std::min((int)dust.size(), g_dustHighWaterMark);

    std::unordered_map<DustGridKey, std::vector<int>, DustGridKeyHash> grid;
    grid.reserve((size_t)dustRange2);

    for (int i = 0; i < dustRange2; ++i) {
        DustParticle& d = dust[i];

        if (!d.active) continue;
        if (d.isRing) continue;
        if (d.state == DustState::RingParticle) continue;
        if (d.state == DustState::Decaying) continue;

        DustGridKey key {
            (long long)std::floor(d.pos.x / CELL_SIZE),
            (long long)std::floor(d.pos.y / CELL_SIZE),
            (long long)std::floor(d.pos.z / CELL_SIZE)
        };

        grid[key].push_back(i);
    }

    for (auto& kv : grid) {
        std::vector<int>& ids = kv.second;
        if ((int)ids.size() < MIN_CLUSTER_COUNT) continue;

        double mSum = 0.0;
        Vector3D cm{0, 0, 0};
        Vector3D cv{0, 0, 0};

        int rSum = 0;
        int gSum = 0;
        int bSum = 0;

        for (int idx : ids) {
            const DustParticle& d = dust[idx];

            double m = DustMassEstimate(d);
            mSum += m;
            cm += d.pos * m;
            cv += d.vel * m;

            rSum += d.color.r;
            gSum += d.color.g;
            bSum += d.color.b;
        }

        if (mSum < MIN_PROTO_MASS) continue;

        cm = cm / mSum;
        cv = cv / mSum;

        double rProto = std::cbrt((3.0 * mSum) / (4.0 * PI_D * 3000.0));

        double vDisp2 = 0.0;
        for (int idx : ids) {
            vDisp2 += (dust[idx].vel - cv).lengthSqr();
        }

        double vDisp = std::sqrt(vDisp2 / std::max(1, (int)ids.size()));
        double vEsc = std::sqrt(2.0 * G * mSum / std::max(1.0, rProto));

        if (vDisp > vEsc * MAX_DISPERSION_FRAC) {
            for (int idx : ids) dust[idx].state = DustState::Accreting;
            continue;
        }

        // No acrecionar dentro del limite de Roche: ahi toca anillo/disco.
        DustParticle probe = dust[ids.front()];
        probe.pos = cm;
        probe.vel = cv;

        double e = 0.0;
        double rr = 0.0;
        double ec = 1.0;
        double ic = 0.0;

        const Body* host = FindBestDustHost(probe, bodies, &e, &rr, &ec, &ic);

        if (host && !host->isStar) {
            double roche = 2.44 * host->radius *
                std::cbrt(std::max(1e-9, DensityOf(host->mass, host->radius) / 3000.0));

            if ((cm - host->pos).length() < roche) {
                for (int idx : ids) {
                    dust[idx].state = DustState::RingCandidate;
                    dust[idx].isRing = true;
                }
                continue;
            }
        }

        Body p;
        p.name = (mSum > 1.0e20) ? "Proto-Cuerpo" : "Fragmento acrecido";
        p.pos = cm;
        p.prevPos = cm;
        p.vel = cv;
        p.mass = mSum;
        p.radius = rProto;

        int n = std::max(1, (int)ids.size());

        p.color = {
            (unsigned char)ClampD(rSum / n, 0, 255),
            (unsigned char)ClampD(gSum / n, 0, 255),
            (unsigned char)ClampD(bSum / n, 0, 255),
            255
        };

        p.isFragment = (mSum <= 1.0e20);
        p.isRockyPlanet = true;
        p.material = MAT_ROCKY;
        p.rockyPlanet = MakeAsteroidProfile(
            (unsigned int)(FastRand01() * 4294967295.0),
            MAT_ROCKY
        );

        p.temperature = 280.0;
        p.heatSpike = 0.25f;
        p.spawnGraceFrames = SPAWN_GRACE_FRAMES;

        bodies.push_back(p);

        for (int idx : ids) {
            dust[idx].state = DustState::ProtoBodySeed;
            dust[idx].active = false;
        }
    }
}

// ── Actualización del campo de polvo (sistema visual ligero) ───────
// El polvo NO participa en la gravedad N-cuerpo completa ni en las
// colisiones: es una capa puramente visual con una orbita simplificada
// (un paso de Euler hacia los cuerpos de 'bodies'), para mantener su
// costo en O(dust * bodies) en vez de formar parte del O(n^2) de
// ComputeAccelerations/ResolveCollisions.
//
// La gravedad se integra en los mismos sub-pasos adaptativos 'h' que usa
// LeapfrogStep (ver StepPhysics/PHYS_SAFE_SUBSTEP_DT, constants.h), porque
// el polvo eyectado por
// EjectAndMark (Sistema 1) aparece muy cerca (1.5-3.5 radios) de cuerpos
// con masa planetaria: con un unico paso de Euler usando el dt completo
// (hasta 153600s), 'acc*dt' dispara la velocidad/posicion a valores
// absurdos en un frame, lanzando el polvo fuera de la vista (invisible)
// sin que muera (DUST_MAX_LIFE).
// El polvo es puramente visual y nunca afecta a 'bodies', asi que solo
// necesita "sentir" a los cuerpos realmente masivos (estrellas y
// planetas): la atraccion de un fragmento de unos pocos km es
// despreciable frente a la de un planeta a la misma distancia. Con
// cientos de fragmentos en 'bodies' (hasta MAX_FRAGMENTS=600), recorrer
// TODOS los cuerpos por cada particula de polvo (hasta
// MAX_DUST_PARTICLES=3000) x8 sub-pasos era el costo dominante de
// StepPhysics. Se preseleccionan los DUST_GRAVITY_MAX_SOURCES cuerpos mas
// masivos UNA SOLA VEZ por sub-paso (no por particula), y el resto del
// trabajo (por particula, sin dependencias entre si) se reparte en el
// pool de hilos.
static void UpdateDustGravity(std::vector<DustParticle>& dust,
                               const std::vector<Body>& bodies, double h)
{
    constexpr int DUST_GRAVITY_MAX_SOURCES = 12;
    const Body* sources[DUST_GRAVITY_MAX_SOURCES];
    int sourceCount = 0;
    for (const Body& b : bodies) {
        if (b.mass <= 0.0) continue;
        if (sourceCount < DUST_GRAVITY_MAX_SOURCES) {
            sources[sourceCount++] = &b;
        } else {
            int minIdx = 0;
            for (int k = 1; k < DUST_GRAVITY_MAX_SOURCES; ++k)
                if (sources[k]->mass < sources[minIdx]->mass) minIdx = k;
            if (b.mass > sources[minIdx]->mass) sources[minIdx] = &b;
        }
    }

    DustParticle* data = dust.data();
    int activeRange = std::min((int)dust.size(), g_dustHighWaterMark);
    GetThreadPool().ParallelFor(activeRange, [data, &sources, sourceCount, &bodies, h](int i) {
        DustParticle& d = data[i];
        if (!d.active) return; // slot libre del pool: nada que actualizar

        if (d.isRing && d.hostBodyId >= 0) {
            const Body* host = nullptr;

            for (const Body& b : bodies) {
                if ((int64_t)b.id == d.hostBodyId && b.mass > 0.0) {
                    host = &b;
                    break;
                }
            }

            if (host && host->mass > 0.0 && host->radius > 0.0) {
                // Gravedad real (host + 'sources'), SIN forzar ninguna
                // forma de orbita: nada de circulo perfecto recalculado
                // cada paso. La particula simplemente cae bajo la atraccion
                // real de su host (que domina, por eso "es" un anillo) MAS
                // la de cualquier otro cuerpo masivo cercano -- si un
                // planeta o la estrella pasan cerca, esta MISMA suma ya la
                // perturba, dispersa o desliga con total naturalidad, sin
                // necesidad de un criterio de disrupcion aparte.
                //
                // El unico cuidado real es de RESOLUCION TEMPORAL: un
                // anillo orbita muy cerca y muy rapido (cerca del limite de
                // Roche), con un periodo mucho menor que el de cualquier
                // planeta. Integrar eso con el mismo paso 'h' del resto del
                // motor (pensado para escalas planetarias, hasta 2400s a
                // velocidad de simulacion maxima) haria que la orbita se
                // vuelva inestable (gane o pierda energia cada vuelta) NO
                // porque la fisica este mal, sino por resolucion
                // insuficiente -- exactamente igual que cualquier
                // integrador orbital real necesita mas pasos por vuelta
                // cuanto mas cerrada es la orbita. La solucion correcta no
                // es fingir la forma: es dar mas sub-pasos LOCALES, sin
                // tocar la fisica ni el resto del motor.
                Vector3D rel0 = d.pos - host->pos;
                double   r0   = std::max(1.0, rel0.length());

                double period = 2.0 * PI_D * std::sqrt(
                    (r0 * r0 * r0) / std::max(1.0, G * host->mass));

                constexpr int ORBIT_STEPS_PER_REV = 48; // resolucion angular minima por vuelta
                constexpr int MAX_MICROSTEPS      = 512; // techo de costo por particula/sub-paso
                int microSteps = (int)ClampD(std::ceil(h / (period / ORBIT_STEPS_PER_REV)), 1.0, (double)MAX_MICROSTEPS);
                double mh = h / (double)microSteps;

                // La perturbacion de otros cuerpos masivos se evalua UNA
                // sola vez por sub-paso 'h' (no en cada microstep): esos
                // cuerpos apenas se mueven en el lapso de un 'h' (segundos a
                // minutos), mientras que la orbita del anillo SI necesita
                // resolucion fina -- el mismo principio que separa la parte
                // "rapida" (host) de la "lenta" (perturbadores) en
                // cualquier integrador de perturbaciones real, y evita
                // recorrer 'sources' por cada microstep (costo
                // microSteps*sourceCount innecesario).
                Vector3D accPerturb{0, 0, 0};
                for (int k = 0; k < sourceCount; ++k) {
                    const Body* src = sources[k];
                    if (src == host) continue;

                    Vector3D toSrc = src->pos - d.pos;
                    double sd2 = toSrc.lengthSqr() + SOFTENING * SOFTENING;
                    double sd  = std::sqrt(sd2);
                    accPerturb += toSrc * (G * src->mass / (sd2 * sd));
                }

                bool collided = false;
                for (int s = 0; s < microSteps && !collided; ++s) {
                    Vector3D rel = d.pos - host->pos;
                    double r2 = rel.lengthSqr() + SOFTENING * SOFTENING;
                    double r  = std::sqrt(r2);

                    Vector3D acc = rel * (-G * host->mass / (r2 * r)) + accPerturb;

                    d.vel += acc * mh;
                    d.pos += d.vel * mh;

                    if ((d.pos - host->pos).length() <= host->radius * 1.05) collided = true;
                }

                if (collided) {
                    d.active = false;
                    return;
                }

                d.currentRotation = std::fmod(
                    d.currentRotation + d.rotationSpeed * (float)h,
                    2.0f * (float)PI_D
                );

                return;
            }

            // Host no encontrado (destruido/fusionado): se desliga para que
            // UpdateDustIntelligence la reclasifique con normalidad, y cae
            // de inmediato a la gravedad N-body normal de mas abajo en
            // este mismo sub-paso.
            d.isRing = false;
            d.state  = DustState::Decaying;
            d.stableOrbitTime = 0.0;
        }

        // Restricted N-Body: la particula SIENTE la gravedad de los cuerpos
        // rigidos (sources), pero no suma gravedad entre particulas ni
        // afecta a 'bodies' -- a = sum(G*M_body/|r|^2 * r_hat).
        //
        // 'h' aqui es el dt COMPLETO del frame (ver StepPhysics: esta
        // funcion ya no se llama una vez por sub-paso de cuerpo rigido,
        // sino una sola vez por frame -- el polvo es un sistema visual
        // ligero, no necesita la misma resolucion temporal fina que la
        // integracion N-body precisa de 'bodies'). Sin micro-pasos
        // propios, un 'h' grande (velocidades de simulacion altas) haria
        // que la trayectoria/colision de escombros sueltos se volviera
        // imprecisa de golpe -- en vez de eso, se subdivide en
        // FREE_DUST_MICROSTEPS pasos fijos, igual de baratos sin importar
        // la velocidad de simulacion elegida (a diferencia del polvo de
        // anillo, que ya se auto-resuelve por periodo orbital mas arriba).
        constexpr int FREE_DUST_MICROSTEPS = 8;
        double mh = h / (double)FREE_DUST_MICROSTEPS;
        bool diedThisCall = false;

        for (int s = 0; s < FREE_DUST_MICROSTEPS && !diedThisCall; ++s) {
            Vector3D acc{0, 0, 0};
            for (int k = 0; k < sourceCount; ++k) {
                const Body& b = *sources[k];
                Vector3D delta = b.pos - d.pos;
                double dist2 = delta.lengthSqr() + SOFTENING*SOFTENING;
                double dist  = std::sqrt(dist2);
                acc += delta * (G * b.mass / (dist2 * dist));
            }
            d.vel += acc * mh;
            d.pos += d.vel * mh;

            // Colision UNIDIRECCIONAL particula -> cuerpo (Bounding
            // Spheres): si distance(d.pos, b.pos) <= b.radius, la
            // particula se desactiva de inmediato para que el Particle
            // Pool la reutilice. b.radius es SIEMPRE el radio solido base
            // del cuerpo (nunca la atmosfera, ver hitboxes de
            // ResolveCollisions). Las particulas NO comprueban colisiones
            // entre si.
            for (int k = 0; k < sourceCount; ++k) {
                const Body& b = *sources[k];
                if ((d.pos - b.pos).length() <= b.radius) {
                    d.active = false;
                    diedThisCall = true;
                    break;
                }
            }
        }

        if (!diedThisCall) {
            // Rotacion propia ("tumbling"), puramente visual -- ver
            // DustParticle::rotationAxis/rotationSpeed/currentRotation
            // (body.h) y su uso en DrawDustField3D (renderer.h).
            d.currentRotation = std::fmod(d.currentRotation + d.rotationSpeed * (float)h,
                                           2.0f * (float)PI_D);
        }
    });
}

// Ciclo de vida del polvo: decaimiento del destello de calor y remocion
// al superar DUST_MAX_LIFE. Se llama una vez por frame con el dt
// completo (la gravedad se integra aparte, en sub-pasos).
static void UpdateDustLifecycle(std::vector<DustParticle>& dust, double dt)
{
    int dustRange = std::min((int)dust.size(), g_dustHighWaterMark);
    for (int di = 0; di < dustRange; ++di) {
        DustParticle& d = dust[di];
        if (!d.active) continue;

        d.fragAge += dt;

        // El polvo util se enfria mas rapido para evitar el look falso
        // de bolitas naranjas eternas.
        float cool =
            (d.state == DustState::RingParticle ||
             d.state == DustState::RingCandidate ||
             d.state == DustState::Accreting)
            ? 0.020f
            : 0.010f;

        if (d.heatSpike > 0.0f) {
            d.heatSpike = std::max(0.0f, d.heatSpike - cool);
        }

        const bool useful =
            d.isRing ||
            d.state == DustState::RingParticle ||
            d.state == DustState::RingCandidate ||
            d.state == DustState::Accreting ||
            d.state == DustState::ProtoBodySeed ||
            d.state == DustState::BoundOrbit;

        // Solo muere basura real.
        // Si esta ligado, formando anillo o acreciendo, NO desaparece por TTL.
        if (!useful && d.fragAge > DUST_MAX_LIFE) {
            d.active = false;
        }
    }
}


// ── Termodinámica: relajación de temperatura + transiciones de fase ──
// 'temperature' ya no se recalcula desde cero cada frame: es un estado
// persistente que relaja hacia 'equilibriumTemp' (fijado por irradiacion
// estelar en UpdateBodiesState) siguiendo una curva de enfriamiento tipo
// Stefan-Boltzmann linealizada -- la tasa de relajacion es proporcional a
// (radius^2 / mass) * T^3, asi que cuerpos pequenos/calientes se enfrian
// rapido y cuerpos masivos (gigantes gaseosos) se enfrian muy lentamente.
// El decaimiento exponencial es incondicionalmente estable sin importar
// el tamano de dt, asi que un pico de temperatura por impacto (ver
// MergeBodies) persiste y se disipa gradualmente en vez de desaparecer
// en el siguiente frame.
//
// Para planetas rocosos/helados con inventario de volatiles
// (volatileBudget > 0) tambien gestiona las transiciones de fase
// hielo/liquido/vapor y el engrosamiento o "blow-off" de la atmosfera
// segun la temperatura resultante.
static void UpdateThermodynamics(std::vector<Body>& bodies, double dt)
{
    constexpr double THERMAL_K  = 0.05;   // constante de enfriamiento (ajustable)
    constexpr double MELT_POINT = 273.0;  // K: hielo -> agua liquida
    constexpr double BOIL_POINT = 373.0;  // K: agua liquida -> vapor
    constexpr double PHASE_RATE = 1.0 / (3600.0 * 24.0 * 5.0); // ~5 dias para reequilibrar fases

    // Inercia termica de un cataclismo ("bola de lava"): la curva T^3 de
    // arriba esta calibrada para perturbaciones pequenas cerca del
    // equilibrio (~horas) y, sin freno, hace que un planeta calentado a
    // miles de K por un impacto masivo (ver MergeBodies) vuelva a su
    // equilibrio en UN SOLO frame con dt grande. Por encima de
    // MAGMA_TEMP el enfriamiento se limita a 'maxCoolRate' K/s, escalado
    // por la misma 'k' (radius^2/mass) -- un cuerpo tipo Tierra (k≈K_REF)
    // tarda ~1 anio simulado en bajar de 8000K a MAGMA_TEMP, mientras que
    // fragmentos pequenos (k >> K_REF) siguen enfriandose mucho mas
    // rapido. Por debajo de MAGMA_TEMP no se aplica ningun freno: el
    // clima normal (T cerca de Teq) es identico a antes.
    constexpr double MAGMA_TEMP          = 1500.0; // K: umbral de "bola de lava"
    constexpr double K_REF               = 3.4e-13; // k de un planeta tipo Tierra
    constexpr double MAGMA_COOL_RATE_REF = 2.0e-4;  // K/s para k == K_REF

    for (Body& b : bodies) {
        if (b.mass <= 0.0 || b.isStar) continue;

        // --- Relajación de temperatura hacia el equilibrio radiativo ---
        double T    = std::max(2.7, b.temperature);
        double Teq  = std::max(2.7, b.equilibriumTemp);
        double k    = THERMAL_K * (b.radius * b.radius) / std::max(1.0, b.mass);
        double rate = 4.0 * k * T * T * T;
        double Tnext = Teq + (T - Teq) * std::exp(-rate * dt);

        if (T > MAGMA_TEMP) {
            double maxCoolRate = MAGMA_COOL_RATE_REF * (k / K_REF);
            Tnext = std::max(Tnext, T - maxCoolRate * dt);
        }
        b.temperature = Tnext;

        // --- Envejecimiento de marcas de impacto (Sistema 3) ---
        for (int i = 0; i < b.impactMarkCount; ++i) b.impactMarks[i].age += (float)dt;

        // --- Transiciones de fase y atmósfera (solo rocosos/helados) ---
        if (b.isRockyPlanet && b.volatileBudget > 0.0f) {
            double Tn = b.temperature;

            // Fracciones objetivo "fundida" y "vapor" segun la
            // temperatura, con transicion suave alrededor de los puntos
            // de fusion/ebullicion.
            double meltFrac = ClampD((Tn - MELT_POINT) / 20.0 + 0.5, 0.0, 1.0);
            double boilFrac = ClampD((Tn - BOIL_POINT) / 30.0 + 0.5, 0.0, 1.0);

            float targetIce   = (float)(b.volatileBudget * (1.0 - meltFrac));
            float targetVapor = (float)(b.volatileBudget * meltFrac * boilFrac);

            float relax = (float)ClampD(PHASE_RATE * dt, 0.0, 1.0);
            b.iceFraction   += (targetIce   - b.iceFraction)   * relax;
            b.vaporFraction += (targetVapor - b.vaporFraction) * relax;
            b.iceFraction    = ClampF(b.iceFraction,   0.0f, b.volatileBudget);
            b.vaporFraction  = ClampF(b.vaporFraction, 0.0f, b.volatileBudget - b.iceFraction);

            float liquidFraction = ClampF(
                b.volatileBudget - b.iceFraction - b.vaporFraction,
                0.0f,
                b.volatileBudget
            );

            float surfaceIceFraction = ClampF(
                b.iceFraction,
                0.0f,
                b.volatileBudget
            );

            // Nivel hidrográfico visible: líquido + hielo superficial.
            // Así un océano congelado NO desaparece visualmente.
            float visibleHydroLevel = ClampF(
                liquidFraction + surfaceIceFraction,
                0.0f,
                1.0f
            );

            b.rockyPlanet.waterLevel = visibleHydroLevel;

            // Si predomina hielo, el "agua" visual se vuelve blanca/azulada.
            float iceVisual = (visibleHydroLevel > 1.0e-5f)
                ? ClampF(surfaceIceFraction / visibleHydroLevel, 0.0f, 1.0f)
                : 0.0f;

            const Vector3 ICE_OCEAN_COLOR = {0.78f, 0.88f, 0.95f};

            b.rockyPlanet.colorWater.x =
                b.baseColorWater.x + (ICE_OCEAN_COLOR.x - b.baseColorWater.x) * iceVisual;
            b.rockyPlanet.colorWater.y =
                b.baseColorWater.y + (ICE_OCEAN_COLOR.y - b.baseColorWater.y) * iceVisual;
            b.rockyPlanet.colorWater.z =
                b.baseColorWater.z + (ICE_OCEAN_COLOR.z - b.baseColorWater.z) * iceVisual;

            // Atmosfera: el vapor la engrosa por encima de su base; el
            // calor extremo (mas alla de la temperatura de escape de
            // Jeans) la hace hervir y disiparse de forma permanente.
            double escVel2     = 2.0 * G * b.mass / std::max(1.0, b.radius);
            double blowoffTemp = escVel2 * 2.4e-5;
            bool   jeansEscape = Tn > blowoffTemp;

            // Nivel 1 (efecto invernadero descontrolado, REVERSIBLE):
            // 'greenhouseFrac' = fraccion de los volatiles del planeta
            // actualmente vaporizada (vaporFraction/volatileBudget, en
            // [0,1]). vaporFraction ya relaja suavemente (PHASE_RATE,
            // arriba) y decae junto con volatileBudget durante el escape
            // de Jeans (Nivel 2), asi que greenhouseFrac crece con el
            // calentamiento y vuelve a 0 tanto al enfriarse como al
            // perderse la atmosfera por completo -- sin logica extra
            // para "apagar" el Nivel 1 cuando empieza el Nivel 2. Se
            // desactiva explicitamente DURANTE el escape de Jeans, que
            // tiene prioridad (fuerza todo a 0 mas abajo).
            float greenhouseFrac = (!jeansEscape && b.volatileBudget > 1.0e-4f)
                ? ClampF(b.vaporFraction / b.volatileBudget, 0.0f, 1.0f)
                : 0.0f;

            // Atmosfera hasta 3x su base (vapor en suspension + opacidad
            // por invernadero) y cobertura de nubes -> 100%.
            float targetAtm   = (b.baseAtmosphereDensity + b.vaporFraction * 0.6f) * (1.0f + 2.0f * greenhouseFrac);
            float targetCloud = b.baseCloudDensity + (1.0f - b.baseCloudDensity) * greenhouseFrac;

            // Nivel 2 (escape atmosferico de Jeans, PERMANENTE): hierve
            // y disipa la atmosfera/cobertura nubosa por completo.
            if (jeansEscape) {
                double lossRate = ClampD((Tn - blowoffTemp) / blowoffTemp, 0.0, 1.0) * 0.05;
                float requestedLoss = (float)(lossRate * dt / (3600.0 * 24.0));

                // El espacio sólo se lleva vapor disponible.
                // El hielo/líquido debe convertirse primero a vapor por la lógica de fases.
                float loss = std::min(requestedLoss, b.vaporFraction);

                if (loss > 0.0f && b.volatileBudget > 1.0e-6f) {
                    float lostFrac = ClampF(loss / b.volatileBudget, 0.0f, 1.0f);
                    RemoveVolatileFraction(b.solid_composition, lostFrac);
                    RemoveVolatileFraction(b.atmospheric_composition, lostFrac);
                }

                b.vaporFraction = std::max(0.0f, b.vaporFraction - loss);
                b.volatileBudget = std::max(0.0f, b.volatileBudget - loss);

                b.iceFraction = ClampF(b.iceFraction, 0.0f, b.volatileBudget);
                b.vaporFraction = ClampF(
                    b.vaporFraction,
                    0.0f,
                    std::max(0.0f, b.volatileBudget - b.iceFraction)
                );

                b.baseAtmosphereDensity = std::max(0.0f, b.baseAtmosphereDensity - loss);

                targetAtm = 0.0f;
                targetCloud = 0.0f;
            }
            b.atmosphereDensity += (targetAtm - b.atmosphereDensity) * relax;
            b.atmosphereDensity  = ClampF(b.atmosphereDensity, 0.0f, 1.0f);
            b.rockyPlanet.cloudDensity += (targetCloud - b.rockyPlanet.cloudDensity) * relax;
            b.rockyPlanet.cloudDensity  = ClampF(b.rockyPlanet.cloudDensity, 0.0f, 1.0f);

            // Tinte denso grisaceo-amarillento tipo Venus (Nivel 1) sobre
            // el oceano y el halo atmosferico, proporcional a
            // greenhouseFrac (que ya es 0 durante el Nivel 2 -> vuelve al
            // tono base configurado, ver baseColorWater/baseAtmosphereColor).
            const Vector3 GREENHOUSE_WATER_TINT = {0.55f, 0.50f, 0.35f};
            const Color   GREENHOUSE_ATM_TINT   = {200, 185, 140, 0};
            Vector3 hydroColor = b.baseColorWater;

            hydroColor.x = hydroColor.x + (ICE_OCEAN_COLOR.x - hydroColor.x) * iceVisual;
            hydroColor.y = hydroColor.y + (ICE_OCEAN_COLOR.y - hydroColor.y) * iceVisual;
            hydroColor.z = hydroColor.z + (ICE_OCEAN_COLOR.z - hydroColor.z) * iceVisual;

            hydroColor.x = hydroColor.x + (GREENHOUSE_WATER_TINT.x - hydroColor.x) * greenhouseFrac;
            hydroColor.y = hydroColor.y + (GREENHOUSE_WATER_TINT.y - hydroColor.y) * greenhouseFrac;
            hydroColor.z = hydroColor.z + (GREENHOUSE_WATER_TINT.z - hydroColor.z) * greenhouseFrac;

            b.rockyPlanet.colorWater = hydroColor;
            b.atmosphereColor.r = (unsigned char)((float)b.baseAtmosphereColor.r + ((float)GREENHOUSE_ATM_TINT.r - (float)b.baseAtmosphereColor.r) * greenhouseFrac);
            b.atmosphereColor.g = (unsigned char)((float)b.baseAtmosphereColor.g + ((float)GREENHOUSE_ATM_TINT.g - (float)b.baseAtmosphereColor.g) * greenhouseFrac);
            b.atmosphereColor.b = (unsigned char)((float)b.baseAtmosphereColor.b + ((float)GREENHOUSE_ATM_TINT.b - (float)b.baseAtmosphereColor.b) * greenhouseFrac);
        }

        // --- Gigantes gaseosos/helados: turbulencia por exceso de calor ---
        // (en vez del antiguo tinte naranja por heatSpike). El exceso de
        // calor relativo al equilibrio intensifica temporalmente
        // bandas/tormentas, y relaja de vuelta a la normalidad con el
        // enfriamiento (ya muy lento por su masa). El radio NO se ve
        // afectado: b.radius es siempre el radio visual de un gigante
        // gaseoso, sin multiplicador de expansion termica.
        if (b.isGasGiant) {
            double excessHeat = ClampD((b.temperature - b.equilibriumTemp) / std::max(1.0, b.equilibriumTemp), 0.0, 3.0);
            float targetTurbBoost = (float)ClampD(excessHeat, 0.0, 1.0);

            float relaxVis = (float)ClampD(dt / 1800.0, 0.0, 1.0); // ~30 min de reaccion visual
            b.turbulenceBoost += (targetTurbBoost - b.turbulenceBoost) * relaxVis;
        }
    }
}

// ── Paso completo de física ─────────────────────────────────
static void StepPhysics(std::vector<Body>& bodies, std::vector<DustParticle>& dust, double dt)
{
    // Reincorpora fragmentos/polvo encolados en frames anteriores (ver
    // g_pendingFragments/g_pendingDust arriba), respetando el mismo
    // presupuesto por frame que la creacion nueva de este frame.
    g_fragSpawnedThisFrame = 0;
    g_dustSpawnedThisFrame = 0;
    while (!g_pendingFragments.empty() && g_fragSpawnedThisFrame < FRAG_SPAWN_BUDGET_PER_FRAME) {
        bodies.push_back(g_pendingFragments.back());
        g_pendingFragments.pop_back();
        g_fragSpawnedThisFrame++;
    }
    while (!g_pendingDust.empty() && g_dustSpawnedThisFrame < DUST_SPAWN_BUDGET_PER_FRAME) {
        int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
        if (slot < 0) break; // pool lleno
        dust[slot] = g_pendingDust.back();
        g_pendingDust.pop_back();
        g_dustSpawnedThisFrame++;
    }

    // Cuenta regresiva de inmunidad de colision (ver SPAWN_GRACE_FRAMES):
    // una vez por frame, no por sub-paso.
    for (Body& b : bodies)
        if (b.spawnGraceFrames > 0) b.spawnGraceFrames--;

    // Sub-pasos ADAPTATIVOS (ver constants.h): el numero de sub-pasos
    // escala con 'dt' (=TIME_STEP) para mantener el tamano de paso real
    // 'h' cerca de PHYS_SAFE_SUBSTEP_DT sin importar la velocidad de
    // simulacion elegida. A la velocidad 1x por defecto (dt=1200) esto da
    // substeps=8 y h=150 -- EXACTAMENTE el comportamiento de siempre, sin
    // cambios. A velocidades mas altas 'substeps' crece (hasta el techo
    // PHYS_MAX_SUBSTEPS_PER_FRAME) en vez de dejar que 'h' crezca sin
    // limite como antes.
    const int substeps = (int)ClampD(std::ceil(dt / PHYS_SAFE_SUBSTEP_DT), 1.0, (double)PHYS_MAX_SUBSTEPS_PER_FRAME);
    const double h = dt / (double)substeps;
    for (int s = 0; s < substeps; ++s) {
        // Guarda la posicion de partida de este sub-paso para la CCD
        // retrospectiva de ResolveCollisions (ver Body::prevPos).
        for (Body& b : bodies) b.prevPos = b.pos;
        LeapfrogStep(bodies, h);
        ResolveCollisions(bodies, dust, h);
    }

    // El polvo se actualiza UNA SOLA VEZ por frame con el dt completo (no
    // una vez por sub-paso de cuerpo rigido, ver comentario en
    // UpdateDustGravity mas arriba): a velocidad alta 'substeps' puede
    // llegar a 64 (PHYS_MAX_SUBSTEPS_PER_FRAME), y repetir el barrido del
    // pool de polvo esas 64 veces por frame -- incluso ya acotado a
    // g_dustHighWaterMark -- multiplicaba el costo de cualquier anillo o
    // cinturon de escombros activo (p.ej. los ~60000 particulas del
    // anillo de Saturno) sin necesidad: el polvo no necesita la misma
    // resolucion temporal fina que la integracion N-body precisa de
    // 'bodies'. UpdateDustGravity ya se auto-resuelve por dentro (anillos:
    // micro-pasos segun periodo orbital; polvo libre: FREE_DUST_MICROSTEPS
    // fijos), asi que sigue siendo preciso con un 'h' grande.
    UpdateDustGravity(dust, bodies, dt);

    ApplyTidesAndRoche(bodies, dust, dt);
    ResolveCollisions(bodies, dust, dt);

    UpdateThermodynamics(bodies, dt);
    UpdateDustIntelligence(bodies, dust, dt);
    UpdateDustLifecycle(dust, dt);


    // Cálculo del centroide y radio del sistema
    Vector3D sysCentroid{0, 0, 0};
    double   sysRadius = 0.0;
    int majorCount = 0;
    for (const Body& b : bodies)
        if (!b.isFragment && b.mass > 0.0) { sysCentroid += b.pos; ++majorCount; }
    if (majorCount > 0) sysCentroid = sysCentroid * (1.0 / majorCount);
    for (const Body& b : bodies)
        if (!b.isFragment && b.mass > 0.0)
            sysRadius = std::max(sysRadius, (b.pos - sysCentroid).length());

    // Índices de cuerpos principales (para comprobar si un fragmento está ligado)
    std::vector<size_t> mIdx;
    for (size_t i = 0; i < bodies.size(); ++i)
        if (!bodies[i].isFragment && bodies[i].mass > 0.0)
            mIdx.push_back(i);

    // Ciclo de vida de fragmentos
    for (Body& b : bodies) {
        if (!b.isFragment) continue;
        b.fragAge += dt;

        double escapeThreshold = std::max(sysRadius * 12.0, 8.0e11);
        if ((b.pos - sysCentroid).length() > escapeThreshold ||
            b.fragAge > 3600.0 * 24.0 * 365.0 * 50.0)
        {
            bool bound = false;
            for (size_t mi : mIdx) {
                double kePlusGpe = 0.5 * (b.vel - bodies[mi].vel).lengthSqr()
                                 - G * bodies[mi].mass / std::max(1.0, (b.pos - bodies[mi].pos).length());
                if (kePlusGpe < 0.0) { bound = true; break; }
            }
            if (!bound) b.mass = 0.0;
        }
    }

    bodies.erase(
        std::remove_if(bodies.begin(), bodies.end(), [](const Body& b){ return b.mass <= 0.0; }),
        bodies.end());
}

// ── Búsqueda de cuerpo más cercano ─────────────────────────
inline const Body* FindNearestBodyAtPoint(const std::vector<Body>& bodies, const Vector3D& point) {
    const Body* best = nullptr;
    double bestDist  = DBL_MAX;
    for (const Body& b : bodies) {
        if (b.mass <= 0.0) continue;
        double d = (b.pos - point).length();
        if (d < bestDist) { bestDist = d; best = &b; }
    }
    return best;
}
