#pragma once
#include <string>
#include <unordered_map>
#include <algorithm>
#include "body.h"

// ============================================================
//  Composicion quimica/geologica: anclas reales + interpolacion
//
//  Las "anclas" son los inventarios de composicion (fraccion de masa,
//  ver solid_composition/atmospheric_composition en body.h) de los
//  cuerpos reales del catalogo -- los mismos valores que asigna
//  AssignComposition (catalog.h) a partir de values.json. Los cuerpos
//  procedurales (Gigante Procedural, Planeta Rocoso) no tienen una
//  composicion real medible: en su lugar, se INTERPOLA linealmente
//  entre dos anclas reales con un factor 't' aleatorio (mismo patron
//  que GasGiantRadiusFromMass en gas_giants.h, que interpola radio
//  entre Neptuno/Saturno/Jupiter por masa). Como cada ancla suma ~1.0,
//  cualquier combinacion convexa de dos anclas tambien suma ~1.0 --
//  ningun elemento "inventado", solo mezclas de composiciones medidas.
// ============================================================

using CompMap = std::unordered_map<std::string, float>;

// Interpola dos inventarios de composicion elemento a elemento; una
// clave ausente en uno de los dos se trata como fraccion 0 en ese.
inline CompMap InterpolateComposition(const CompMap& a, const CompMap& b, float t) {
    CompMap out;
    for (const auto& kv : a) out[kv.first] = kv.second * (1.0f - t);
    for (const auto& kv : b) out[kv.first] += kv.second * t;
    return out;
}

// ---- Anclas reales (mismos valores que AssignComposition, catalog.h) ----
inline CompMap CompSolidJupiter() { return {{"Oxigeno",0.450f},{"Hierro",0.200f},{"Silicio",0.150f},{"Magnesio",0.100f},{"Agua_Hielo",0.100f}}; }
inline CompMap CompAtmJupiter()   { return {{"Hidrogeno",0.898f},{"Helio",0.102f},{"Metano",0.003f},{"Amoniaco",0.00026f}}; }
inline CompMap CompSolidSaturno() { return {{"Oxigeno",0.400f},{"Hierro",0.200f},{"Silicio",0.150f},{"Agua_Hielo",0.150f},{"Magnesio",0.100f}}; }
inline CompMap CompAtmSaturno()   { return {{"Hidrogeno",0.963f},{"Helio",0.0325f},{"Metano",0.0045f},{"Amoniaco",0.00012f}}; }
inline CompMap CompSolidUrano()   { return {{"Agua_Hielo",0.500f},{"Metano_Hielo",0.200f},{"Amoniaco_Hielo",0.100f},{"Oxigeno",0.100f},{"Silicio",0.050f},{"Hierro",0.050f}}; }
inline CompMap CompAtmUrano()     { return {{"Hidrogeno",0.825f},{"Helio",0.152f},{"Metano",0.023f}}; }
inline CompMap CompSolidNeptuno() { return {{"Agua_Hielo",0.500f},{"Amoniaco_Hielo",0.150f},{"Metano_Hielo",0.150f},{"Oxigeno",0.080f},{"Hierro",0.060f},{"Silicio",0.060f}}; }
inline CompMap CompAtmNeptuno()   { return {{"Hidrogeno",0.800f},{"Helio",0.190f},{"Metano",0.010f}}; }

inline CompMap CompSolidMercurio(){ return {{"Hierro",0.700f},{"Oxigeno",0.150f},{"Silicio",0.070f},{"Magnesio",0.060f},{"Aluminio",0.010f},{"Calcio",0.010f}}; }
inline CompMap CompSolidTierra()  { return {{"Hierro",0.321f},{"Oxigeno",0.301f},{"Silicio",0.151f},{"Magnesio",0.139f},{"Azufre",0.029f},{"Niquel",0.018f},{"Calcio",0.015f},{"Aluminio",0.014f},{"Agua_Liquida",0.0002f}}; }
inline CompMap CompAtmMarte()     { return {{"Dioxido_de_Carbono",0.953f},{"Nitrogeno",0.027f},{"Argon",0.016f},{"Oxigeno",0.0013f},{"Monoxido_de_Carbono",0.0008f}}; }
inline CompMap CompAtmTierra()    { return {{"Nitrogeno",0.7808f},{"Oxigeno",0.2095f},{"Argon",0.0093f},{"Dioxido_de_Carbono",0.0004f}}; }
inline CompMap CompAtmVenus()     { return {{"Dioxido_de_Carbono",0.965f},{"Nitrogeno",0.035f},{"Dioxido_de_Azufre",0.00015f},{"Argon",0.00007f},{"Vapor_de_Agua",0.00002f}}; }

// Mezcla dos inventarios de composicion ponderando por un "peso" (masa
// o masa-proxy) de cada uno -- usada por MergeBodies (physics.h) cuando
// un cuerpo absorbe a otro: el resultado es la combinacion convexa
// InterpolateComposition(a, b, t) con t = wb/(wa+wb). Casos limite, sin
// ramas especiales:
//   - wa=0, wb>0  -> t=1 -> resultado = b sin cambios (el progenitor 'a'
//                    no aporto nada de ese inventario, p.ej. un impactor
//                    sin atmosfera no diluye la atmosfera del cuerpo mayor).
//   - wa>0, wb=0  -> t=0 -> resultado = a sin cambios (simetrico).
//   - wa=wb=0     -> sin peso total: inventario vacio (ninguno de los
//                    dos progenitores tenia ese inventario).
inline CompMap MixComposition(const CompMap& a, double wa, const CompMap& b, double wb) {
    double total = wa + wb;
    if (total <= 0.0) return CompMap{};
    return InterpolateComposition(a, b, (float)(wb / total));
}

// Nucleo metalico desnudo (aleacion hierro-niquel de un meteorito de
// hierro, Wasson 1985). Es la composicion de referencia del catalogo
// "Asteroide" (tipo M) Y la composicion hacia la que converge
// solid_composition de un planeta rocoso/helado diferenciado
// (coreMaterial==MAT_METALLIC) a medida que la marea le arranca el
// manto (ver ApplyTidesAndRoche en physics.h): un nucleo metalico
// expuesto es, en esencia, un asteroide metalico del tamano de un
// planeta.
inline CompMap CompMetallicCore() { return {{"Hierro",0.85f},{"Niquel",0.08f},{"Cobalto",0.01f},{"Silicio",0.03f},{"Oxigeno",0.02f},{"Azufre",0.01f}}; }

// Especies "volatiles" (agua, en cualquier fase) que UpdateThermodynamics
// (physics.h) puede hacer escapar PERMANENTEMENTE al espacio durante un
// episodio de escape de Jeans (jeansEscape, volatileBudget -> 0).
inline constexpr const char* VOLATILE_COMPOSITION_KEYS[] = {"Agua_Liquida", "Agua_Hielo", "Vapor_de_Agua"};

// Quita una fraccion 'lostFrac' de la masa de especies volatiles
// (VOLATILE_COMPOSITION_KEYS) de un inventario de composicion y
// renormaliza el resto para que la suma total se mantenga ~1.0. El
// material volatil perdido se va del cuerpo POR COMPLETO (no se mueve a
// otro inventario), asi que lo que queda pasa a representar una mayor
// proporcion de la masa restante -- igual que destilar agua de una
// mezcla concentra el resto de solutos. Llamada desde
// UpdateThermodynamics con la misma 'lostFrac' que reduce
// b.volatileBudget, asi ambos inventarios (visual y composicion) pierden
// volatiles en sincronia con el mismo evento fisico (escape de Jeans).
inline void RemoveVolatileFraction(CompMap& comp, float lostFrac) {
    if (comp.empty() || lostFrac <= 0.0f) return;
    lostFrac = std::min(lostFrac, 1.0f);

    double volatileMass = 0.0;
    for (const char* key : VOLATILE_COMPOSITION_KEYS) {
        auto it = comp.find(key);
        if (it != comp.end()) volatileMass += it->second;
    }
    if (volatileMass <= 0.0) return;

    double newTotal = 1.0 - volatileMass * lostFrac; // suma original ~1.0
    if (newTotal <= 1.0e-6) return;

    for (auto& kv : comp) {
        bool isVolatile = false;
        for (const char* key : VOLATILE_COMPOSITION_KEYS) {
            if (kv.first == key) { isVolatile = true; break; }
        }
        double newValue = isVolatile ? (kv.second * (1.0 - lostFrac)) : kv.second;
        kv.second = (float)(newValue / newTotal);
    }
}

// ── Gigante gaseoso/helado procedural ("Gigante Procedural") ──
// Interpola entre los dos gigantes reales de la misma familia: gaseosos
// (Jupiter/Saturno) si !iceGiant, helados (Urano/Neptuno) si iceGiant --
// la misma distincion que ya usa MakeProceduralGasGiantProfile/
// RandomizeGasGiantPhysics para perfil visual y radio. Resultado: cada
// gigante procedural cae en algun punto del espectro real entre ambos
// planetas de referencia, en vez de copiar siempre el mismo.
inline void RandomizeGasGiantComposition(Body& b, unsigned int seed) {
    unsigned int s = seed;
    auto rnd = [&s]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1u << 24);
    };
    float t = rnd();
    if (b.gasGiant.iceGiant) {
        b.solid_composition       = InterpolateComposition(CompSolidUrano(),   CompSolidNeptuno(), t);
        b.atmospheric_composition = InterpolateComposition(CompAtmUrano(),     CompAtmNeptuno(),   t);
    } else {
        b.solid_composition       = InterpolateComposition(CompSolidJupiter(), CompSolidSaturno(), t);
        b.atmospheric_composition = InterpolateComposition(CompAtmJupiter(),   CompAtmSaturno(),   t);
    }
}

// ── Planeta rocoso/helado procedural ("Planeta Rocoso") ──
// Solido: interpola entre Mercurio (extremo seco, nucleo metalico
// dominante, 70% Hierro) y Tierra (extremo diferenciado/humedo,
// balance Hierro/Oxigeno/Silicio + agua liquida) -- el mismo espectro
// real que cubre ROCKY_MR_EXPONENT (constants.h) para masa/radio.
//
// Atmosfera: 'atmosphereDensity' ya fue asignado por RandomizeRockyPlanet
// (rocky_planets.h) ANTES de llamar a esta funcion (ver orden en
// SpawnFromCatalog), asi que se reutiliza ese valor -- ya correlacionado con
// el color/halo atmosferico -- para interpolar entre las 3 atmosferas
// reales del catalogo segun SU PROPIA densidad real: Marte (0.08, CO2
// fina), Tierra (0.80, N2/O2) y Venus (1.0, CO2 densa). Sin atmosfera
// (< 0.001, ~35% de los mundos generados) = sin inventario gaseoso,
// igual que Mercurio/Luna.
inline void RandomizeRockyComposition(Body& b, unsigned int seed) {
    unsigned int s = seed;
    auto rnd = [&s]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1u << 24);
    };

    b.solid_composition = InterpolateComposition(CompSolidMercurio(), CompSolidTierra(), rnd());

    float atm = b.atmosphereDensity;
    if (atm < 0.001f) {
        b.atmospheric_composition = CompMap{};
    } else if (atm <= 0.08f) {
        b.atmospheric_composition = CompAtmMarte();
    } else if (atm <= 0.80f) {
        float t = (atm - 0.08f) / (0.80f - 0.08f);
        b.atmospheric_composition = InterpolateComposition(CompAtmMarte(), CompAtmTierra(), t);
    } else {
        float t = std::min(1.0f, (atm - 0.80f) / (1.0f - 0.80f));
        b.atmospheric_composition = InterpolateComposition(CompAtmTierra(), CompAtmVenus(), t);
    }
}
