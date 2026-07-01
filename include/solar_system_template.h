#pragma once
#include <vector>
#include <string>
#include <cmath>
#include "body.h"
#include "constants.h"
#include "catalog.h"
#include "physics.h"

// ============================================================
//  Plantilla de simulacion: Sistema Solar realista
//  Coloca TODOS los cuerpos actualmente disponibles en el catalogo
//  (planetas, lunas, planetas enanos/TNOs) en sus orbitas reales
//  (semieje mayor + excentricidad, NASA/JPL -- ver
//  SOLAR_SISTEM_DISTANCES.json) y anade los tres cinturones de
//  escombros del sistema solar real (ver BELTS.json) como poblaciones
//  de polvo libre (gravedad real, NO un "anillo" bloqueado a un host).
//
//  Nota de precision (ver mensaje del usuario): las distancias son
//  semiejes mayores medios, no posiciones instantaneas de efemerides
//  -- no hay una fecha real detras de esta plantilla. La anomalia
//  verdadera de cada cuerpo se sortea al azar en cada carga para que
//  no queden todos alineados en una linea (lo cual si seria irreal).
// ============================================================

constexpr double AU_M = 149597870700.0; // 1 UA en metros (igual que astropy/JPL)

// ── Geometria orbital general: posicion/velocidad de Kepler ────────────
// Dado un foco (posicion/velocidad/masa del cuerpo CENTRAL), semieje mayor
// 'a', excentricidad 'e', anomalia verdadera 'theta' y la normal del plano
// orbital 'planeNormal' (direccion del momento angular), devuelve la
// posicion/velocidad ABSOLUTAS del cuerpo en orbita. 'refDir' fija una
// direccion de referencia DENTRO del plano orbital (dirección hacia el
// periapsis); si es paralela a 'planeNormal' se usa un eje de respaldo.
// 'retrograde' invierte el sentido de revolucion sin tocar la forma/
// inclinacion del plano (ver Triton, unica luna real con orbita
// retrograda en este catalogo).
struct KeplerState { Vector3D pos, vel; };

inline KeplerState PlaceInKeplerOrbit(
    const Vector3D& centralPos, const Vector3D& centralVel, double centralMass,
    double semiMajorAxis, double eccentricity, double trueAnomalyRad,
    Vector3D planeNormal, Vector3D refDir, bool retrograde)
{
    planeNormal = NormalizeSafe(planeNormal);

    Vector3D eHat = refDir - planeNormal * refDir.dot(planeNormal);
    if (eHat.lengthSqr() < 1e-12) {
        Vector3D fallback = (std::fabs(planeNormal.y) < 0.9) ? Vector3D{0,1,0} : Vector3D{1,0,0};
        eHat = fallback - planeNormal * fallback.dot(planeNormal);
    }
    eHat = NormalizeSafe(eHat);
    Vector3D tHat = Cross(planeNormal, eHat);
    if (retrograde) tHat = tHat * -1.0;

    double a = semiMajorAxis;
    double e = ClampD(eccentricity, 0.0, 0.97);
    double r = a * (1.0 - e*e) / (1.0 + e * std::cos(trueAnomalyRad));

    Vector3D radialDir  = eHat * std::cos(trueAnomalyRad) + tHat * std::sin(trueAnomalyRad);
    Vector3D tangentDir = tHat * std::cos(trueAnomalyRad) - eHat * std::sin(trueAnomalyRad);

    double GM = G * std::max(1.0, centralMass);
    double h  = std::sqrt(GM * a * (1.0 - e*e));
    double vR = (h > 1e-9) ? (GM / h) * e * std::sin(trueAnomalyRad) : 0.0;
    double vT = (r > 1e-9) ? h / r : 0.0;

    KeplerState s;
    s.pos = centralPos + radialDir * r;
    s.vel = centralVel + radialDir * vR + tangentDir * vT;
    return s;
}

// ── Crea un Body del catalogo y le adjunta sus anillos reales (si los
//    tiene) -- exactamente la misma logica que usaba SpawnBody (main.cpp),
//    factorizada aqui para que la plantilla de sistema solar y la
//    colocacion manual compartan una sola implementacion. ──
inline void SpawnCatalogBodyWithRings(
    std::vector<Body>& bodies, std::vector<DustParticle>& dustField,
    const CatalogItem& item, const std::string& baseName,
    const Vector3D& pos, const Vector3D& vel, bool fixed,
    const GlobalTextures& tex)
{
    Body b = SpawnFromCatalog(item, baseName, pos, vel, fixed, tex);
    b.name = item.name;
    bodies.push_back(b);

    if (baseName == "Saturno") {
        SpawnPlanetaryRings(b, dustField, {
            { 1.149, 1.314, 0.5,  GetColor(0x4a4641ff) },
            { 1.282, 1.580, 2.0,  GetColor(0x6e6259ff) },
            { 1.580, 2.019, 10.0, GetColor(0xc0b6a7ff) },
            { 2.099, 2.349, 6.0,  GetColor(0x9e9281ff) },
            { 2.395, 2.420, 1.0,  GetColor(0xd8d2c4ff) },
            { 2.851, 3.006, 0.3,  GetColor(0x55504aff) },
            { 3.092, 6.000, 0.4,  GetColor(0x4a5a6eff) },
        }, 60000);
    } else if (baseName == "Jupiter") {
        SpawnPlanetaryRings(b, dustField, {
            { 1.316, 1.753, 1.5, GetColor(0x302620ff) },
            { 1.745, 1.839, 3.0, GetColor(0x695543ff) },
            { 1.846, 2.604, 0.6, GetColor(0x1f1a17ff) },
            { 1.846, 3.233, 0.4, GetColor(0x16120fff) },
        }, 3000);
    } else if (baseName == "Urano") {
        SpawnPlanetaryRings(b, dustField, {
            { 1.058, 1.631, 1.0, GetColor(0x3a3a3aff) },
            { 1.650, 1.990, 2.0, GetColor(0x4d4d4dff) },
            { 1.990, 2.018, 4.0, GetColor(0x6b6b6bff) },
            { 2.607, 2.757, 0.5, GetColor(0x2e2e2eff) },
            { 3.391, 4.061, 0.3, GetColor(0x282828ff) },
        }, 15000);
    } else if (baseName == "Neptuno") {
        SpawnPlanetaryRings(b, dustField, {
            { 1.665, 1.746, 1.5, GetColor(0x3a3f4aff) },
            { 2.159, 2.164, 2.0, GetColor(0x555c6bff) },
            { 2.161, 2.323, 1.0, GetColor(0x3a3f4aff) },
            { 2.321, 2.325, 1.5, GetColor(0x6b7280ff) },
            { 2.595, 2.601, 3.0, GetColor(0x7a8190ff) },
        }, 10000);
    } else if (baseName == "Quaoar") {
        SpawnPlanetaryRings(b, dustField, {
            { 4.40, 4.65, 1.0, GetColor(0x9c9088ff) },
            { 7.20, 7.45, 1.5, GetColor(0xaca094ff) },
        }, 2000);
    } else if (baseName == "Haumea") {
        SpawnPlanetaryRing(b, dustField, 3.15, 3.25, {}, 1200, {{1.0, GetColor(0xb0aca6ff)}});
    }
}

inline const CatalogItem* FindCatalogItemByName(const std::vector<CatalogItem>& db, const std::string& name) {
    for (const auto& item : db) if (item.name == name) return &item;
    return nullptr;
}

// ── Poblacion de un cinturon de escombros (gravedad real, NO 'isRing'):
//    cada particula es polvo libre normal que orbita al Sol y siente la
//    gravedad de los demas cuerpos masivos (ver UpdateDustGravity,
//    physics.h) -- exactamente como asteroides/KBOs reales, capaz de ser
//    perturbado por Jupiter/Neptuno con el tiempo. 'inclMaxDeg' acota la
//    dispersion de inclinacion (cinturon principal/Kuiper: disco delgado,
//    0-30 grados); 'isotropic3D'=true ignora 'inclMaxDeg' y distribuye la
//    normal del plano orbital de cada particula UNIFORMEMENTE sobre la
//    esfera completa (Nube de Oort: distribucion esferica real, no un
//    disco). ──
inline void SpawnDebrisBelt(
    std::vector<DustParticle>& dust, const Vector3D& sunPos, const Vector3D& sunVel,
    double sunMass, double innerAU, double outerAU, double inclMaxDeg, bool isotropic3D,
    double maxEccentricity, int count, Color color, double particleRadiusM)
{
    for (int i = 0; i < count; ++i) {
        int slot = FindFreeDustSlot(dust, g_dustPoolCursor);
        if (slot < 0) break;

        double a = (innerAU + FastRand01() * (outerAU - innerAU)) * AU_M;
        double e = FastRand01() * maxEccentricity;
        double theta = FastRand01() * 2.0 * PI_D;

        Vector3D planeNormal;
        if (isotropic3D) {
            double cosT = FastRand01() * 2.0 - 1.0;
            double sinT = std::sqrt(std::max(0.0, 1.0 - cosT*cosT));
            double phi  = FastRand01() * 2.0 * PI_D;
            planeNormal = { sinT * std::cos(phi), cosT, sinT * std::sin(phi) };
        } else {
            double inclRad = FastRand01() * inclMaxDeg * PI_D / 180.0;
            double lonNode = FastRand01() * 2.0 * PI_D;
            Vector3D tiltAxis = { std::cos(lonNode), 0.0, std::sin(lonNode) };
            planeNormal = RotateAroundAxis({0.0, 1.0, 0.0}, tiltAxis, inclRad);
        }

        KeplerState st = PlaceInKeplerOrbit(sunPos, sunVel, sunMass, a, e, theta,
                                             planeNormal, Vector3D{1,0,0}, false);

        DustParticle d;
        d.pos = st.pos;
        d.vel = st.vel;
        d.radius = particleRadiusM;
        d.color  = color;
        d.heatSpike = 0.0f;
        d.seed = (float)FastRand01();
        d.active = true;
        d.isRing = false;
        d.state  = DustState::Debris;
        d.hostBodyId = -1;

        double axisCosT = 2.0 * FastRand01() - 1.0;
        double axisSinT = std::sqrt(std::max(0.0, 1.0 - axisCosT*axisCosT));
        double axisPhi  = 2.0 * PI_D * FastRand01();
        d.rotationAxis  = { (float)(axisSinT * std::cos(axisPhi)),
                             (float)axisCosT,
                             (float)(axisSinT * std::sin(axisPhi)) };
        d.rotationSpeed   = (float)((FastRand01() - 0.5) * 1.0);
        d.currentRotation = (float)(FastRand01() * 2.0 * PI_D);
        d.scale           = (float)(particleRadiusM * RENDER_SCALE);

        dust[slot] = d;
    }
}

// Tabla orbital: cada cuerpo (salvo el Sol, raiz del arbol) con su padre,
// semieje mayor real, excentricidad, INCLINACION y LONGITUD DEL NODO ASCENDENTE
// (elementos J2000, eclíptica como plano de referencia, eje +X como dirección 0°).
//
// 'tiltToParentEquator' sobrescribe la inclinación eclíptica con el ecuador del
// padre (DustHostSpinAxis) -- correcto para lunas galileanas/Titan/Triton/Caronte.
// La Luna usa la eclíptica (tiltToParentEquator=false) porque su órbita está a
// ~5° de la eclíptica, NO del ecuador terrestre (23.4°).
//
// planeNormal se calcula como:
//   n = { -sin(i)*sin(Ω),  cos(i),  sin(i)*cos(Ω) }
// (rotación de {0,1,0} por ángulo i alrededor del eje del nodo ascendente).
struct RealOrbitEntry {
    const char* baseName;
    const char* parentName;
    double semiMajorAxis_m;
    double eccentricity;
    double inclination_deg;    // inclinación orbital respecto a la eclíptica (J2000)
    double longAscNode_deg;    // longitud del nodo ascendente (J2000, desde +X)
    bool   tiltToParentEquator;
    bool   retrograde;
};

inline const std::vector<RealOrbitEntry>& GetRealOrbitTable() {
    // Fuentes: inclinaciones de ORBITAL_TILT.ison (JPL J2000 para heliocéntricos;
    // plano de Laplace del planeta para lunas — ver notas por cuerpo).
    // Ω (longitud del nodo ascendente) de elementos oscuros JPL J2000 (no en ORBITAL_TILT.ison).
    // Para lunas con tiltToParentEquator=true, i/Ω almacenados son en el frame Laplace de su
    // planeta pero el código usa DustHostSpinAxis() que ya aproxima ese plano; las pequeñas
    // inclinaciones dentro del plano de Laplace (< 1°) no se aplican para no mezclar frames.
    //   nombre         padre          semieje(m)             exc.               i(°)      Ω(°)   tiltEq  retro
    static const std::vector<RealOrbitEntry> table = {
        // ── Planetas heliocéntricos: eclíptica J2000 (JPL planetary elements) ────────────
        {"Mercurio",  "Sol",      0.387  * AU_M,   0.20563593,   7.00497902,  48.331, false, false},
        {"Venus",     "Sol",      0.723  * AU_M,   0.00677672,   3.39467605,  76.680, false, false},
        {"Tierra",    "Sol",      1.0    * AU_M,   0.01671123,   0.000,        0.000, false, false},
        // Luna: eclíptica J2000 (~5.16°) — NO usa ecuador terrestre (tiltToParentEquator=false)
        // porque su órbita está ligada a la eclíptica, no al ecuador terrestre (23.4°).
        {"Luna",      "Tierra",   384400.0*1000.0, 0.0549,       5.16,       125.08,  false, false},
        {"Marte",     "Sol",      1.524  * AU_M,   0.0933941,    1.84969142,  49.558, false, false},
        {"Ceres",     "Sol",      2.77   * AU_M,   0.0758,      10.6,         80.305, false, false},
        {"Jupiter",   "Sol",      5.203  * AU_M,   0.04838624,   1.30439695, 100.464, false, false},
        // Galileanas: plano de Laplace ≈ ecuador joviano para las interiores.
        // tiltToParentEquator=true → DustHostSpinAxis (no usa i/Ω).
        // i almacenado = inclinación dentro del plano de Laplace (JPL satellite mean elements).
        {"Io",        "Jupiter",  421700.0*1000.0, 0.0041,       0.0,          0.000,  true, false},
        {"Europa",    "Jupiter",  671100.0*1000.0, 0.0094,       0.5,          0.000,  true, false},
        {"Ganymede",  "Jupiter",  1070400.0*1000.0,0.0013,       0.2,          0.000,  true, false},
        {"Calisto",   "Jupiter",  1882700.0*1000.0,0.0074,       0.3,          0.000,  true, false},
        {"Saturno",   "Sol",      9.537  * AU_M,   0.05386179,   2.48599187, 113.665, false, false},
        // Titán: plano de Laplace saturnino ≈ ecuador. 0.3° dentro del plano (no modelado).
        {"Titan",     "Saturno",  1221870.0*1000.0,0.0288,       0.3,          0.000,  true, false},
        {"Urano",     "Sol",      19.191 * AU_M,   0.04725744,   0.77263783,  74.006, false, false},
        {"Neptuno",   "Sol",      30.07  * AU_M,   0.00859048,   1.77004347, 131.783, false, false},
        // Tritón: 156.865° en frame Laplace/ecuador de Neptuno (JPL satellite mean elements).
        // tiltToParentEquator=true + retrograde=true aproxima la órbita retrógrada ecuatorial.
        // Los ~23° de desviación respecto a 180° no se modelan por la limitación del frame.
        {"Triton",    "Neptuno",  354759.0*1000.0, 0.000016,   156.865,        0.000,  true, true},
        // ── Cuerpos transneptunianos: eclíptica J2000 (MPC / JPL SBDB) ──────────────────
        {"Pluton",    "Sol",      39.5   * AU_M,   0.2488,      17.2,         110.299, false, false},
        // Caronte: plano del sistema Plutón ≈ ecuador plutoniano. i ≈ 0° (no modelado).
        {"Caronte",   "Pluton",   19591.0*1000.0,  0.0002,       0.0,          0.000,  true, false},
        {"Haumea",    "Sol",      43.19  * AU_M,   0.1887,      28.2,         121.8,   false, false},
        {"Makemake",  "Sol",      45.48  * AU_M,   0.159,       29.0,          79.3,   false, false},
        {"Eris",      "Sol",      67.84  * AU_M,   0.44,        43.9,          35.95,  false, false},
        {"Orcus",     "Sol",      39.17  * AU_M,   0.227,       20.592,       268.5,   false, false},
        {"Quaoar",    "Sol",      43.69  * AU_M,   0.039,        7.9895,      188.6,   false, false},
        {"Gonggong",  "Sol",      67.38  * AU_M,   0.5,         30.8664,      336.9,   false, false},
        {"Sedna",     "Sol",      506.0  * AU_M,   0.85,        11.9307,      144.5,   false, false},
    };
    return table;
}

// Borra la simulacion actual y coloca el sistema solar completo (todos los
// cuerpos del catalogo con datos reales conocidos) en sus orbitas reales,
// mas los tres cinturones de escombros (cinturon principal, Kuiper, Nube
// de Oort -- ver BELTS.json). dustField YA debe estar pre-asignado a
// MAX_DUST_PARTICLES (ver main.cpp); aqui solo se desactivan los slots
// existentes antes de repoblar.
inline void LoadRealisticSolarSystem(
    std::vector<Body>& bodies, std::vector<DustParticle>& dustField,
    const std::vector<CatalogItem>& Database, const GlobalTextures& tex)
{
    bodies.clear();
    for (DustParticle& d : dustField) d.active = false;
    g_dustPoolCursor = 0;

    const CatalogItem* sunItem = FindCatalogItemByName(Database, "Sol");
    if (!sunItem) return;
    SpawnCatalogBodyWithRings(bodies, dustField, *sunItem, "Sol", {0,0,0}, {0,0,0}, false, tex);

    for (const RealOrbitEntry& orb : GetRealOrbitTable()) {
        const CatalogItem* item = FindCatalogItemByName(Database, orb.baseName);
        if (!item) continue;

        Body* parent = nullptr;
        for (Body& b : bodies) if (b.name == orb.parentName) { parent = &b; break; }
        if (!parent) continue;

        Vector3D planeNormal;
        if (orb.tiltToParentEquator) {
            planeNormal = DustHostSpinAxis(*parent);
        } else {
            // Plano orbital real desde inclinación i y longitud del nodo ascendente Ω (J2000).
            // Fórmula: rotación de {0,1,0} por ángulo i alrededor del eje del nodo ascendente
            // {cos(Ω), 0, sin(Ω)} → n = { -sin(i)*sin(Ω), cos(i), sin(i)*cos(Ω) }.
            double i_rad  = orb.inclination_deg * PI_D / 180.0;
            double om_rad = orb.longAscNode_deg  * PI_D / 180.0;
            planeNormal = {
                -std::sin(i_rad) * std::sin(om_rad),
                 std::cos(i_rad),
                 std::sin(i_rad) * std::cos(om_rad)
            };
        }
        double trueAnomaly = FastRand01() * 2.0 * PI_D;

        KeplerState st = PlaceInKeplerOrbit(parent->pos, parent->vel, parent->mass,
                                              orb.semiMajorAxis_m, orb.eccentricity, trueAnomaly,
                                              planeNormal, Vector3D{1,0,0}, orb.retrograde);

        SpawnCatalogBodyWithRings(bodies, dustField, *item, orb.baseName, st.pos, st.vel, false, tex);
    }

    Vector3D sunPos = bodies[0].pos, sunVel = bodies[0].vel;
    double   sunMass = bodies[0].mass;

    // Cinturon principal de asteroides (entre Marte y Jupiter, 2.1-3.3 UA,
    // inclinacion 0-30 grados -- ver BELTS.json): banda relativamente
    // delgada, color de roca/condrita generico.
    SpawnDebrisBelt(dustField, sunPos, sunVel, sunMass, 2.1, 3.3, 30.0, false, 0.15,
                     14000, GetColor(0x9a8d7aff), 2000.0);

    // Cinturon de Kuiper (30-50 UA, inclinacion 0-30 grados): disco mas
    // helado/oscuro que el cinturon principal.
    SpawnDebrisBelt(dustField, sunPos, sunVel, sunMass, 30.0, 50.0, 30.0, false, 0.15,
                     14000, GetColor(0x6e6a64ff), 3000.0);

    // Nube de Oort (2000-100000 UA): distribucion ESFERICA real, no un
    // disco -- 'isotropic3D'=true sortea la normal del plano orbital de
    // cada particula uniformemente sobre toda la esfera. Mucho mas
    // dispersa que los otros dos cinturones (volumen real
    // astronomicamente mayor, y son cometas/icebergs muy escasos, no una
    // banda densa) -- pocas particulas, mas que nada para que la forma
    // esferica se note si la camara se aleja lo bastante.
    SpawnDebrisBelt(dustField, sunPos, sunVel, sunMass, 2000.0, 100000.0, 0.0, true, 0.3,
                     6000, GetColor(0xc9d4e0ff), 5000.0);
}
