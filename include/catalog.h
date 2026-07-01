#pragma once
#include <vector>
#include "body.h"
#include "constants.h"
#include "textures.h"   // Acceso a las texturas globales
#include "gas_giants.h" // Perfiles atmosfericos procedurales
#include "rocky_planets.h" // Perfiles de superficie procedurales
#include "composition.h" // Composicion quimica/geologica (real + procedural)

// ============================================================
//  Catálogo de cuerpos celestes y función de spawn
// ============================================================

inline std::vector<CatalogItem> BuildCatalog() {
    std::vector<CatalogItem> db = {
        // Valores fisicos (masa/radio/inclinacion axial) ajustados a NASA/JPL
        // Planetary Physical Parameters (ver NEW_values.json) -- radio = el
        // "radius_km" (volumetrico medio) de cada cuerpo, no el ecuatorial,
        // por consistencia con el resto del catalogo.
        {"Sol",        M_SUN,        R_SUN,         GOLD,                    true,  0.0f, {255,200,100,0}, MAT_GASEOUS, 3.0f, 7.25f},

        // ── Estrellas reales (FAT_STARS.json): masa/radio observados/medidos
        // tomados directamente de ese archivo (Rsun=695700km, Msun=1 -- mismas
        // unidades que M_SUN/R_SUN aqui). La luminosidad real se inyecta por
        // separado en SpawnFromCatalog (la formula generica M^3.5 de abajo es
        // solo para estrellas procedurales) para que la temperatura, que
        // main.cpp recalcula cada frame via Stefan-Boltzmann a partir de
        // luminosity/radius, salga fisicamente correcta sin tener que tocar
        // 'temperature' a mano.
        {"Alpha Centauri A", 1.0788*M_SUN, 1.2175*R_SUN, GetColor(0xfff1d6ff), true, 0.0f, {255,225,180,0}, MAT_GASEOUS, 3.0f, 0.0f},
        {"Alpha Centauri B", 0.9092*M_SUN, 0.8591*R_SUN, GetColor(0xffce8aff), true, 0.0f, {255,190,120,0}, MAT_GASEOUS, 3.0f, 0.0f},
        // Antes "Enana Roja" (valores genericos 0.15 M☉/0.20 R☉) -- ahora la
        // enana roja real mas cercana al Sol, con sus valores medidos reales
        // (M5.5Ve, estrella de llamaradas conocida).
        {"Proxima Centauri", 0.1221*M_SUN, 0.1542*R_SUN, GetColor(0xff5533ff), true, 0.0f, {255, 90, 50,0}, MAT_GASEOUS},
        // UY Scuti: masa actual estimada 7-10 M☉ (alta incertidumbre); se usa
        // el extremo superior (10 M☉) para que SIEMPRE caiga, sin importar el
        // ruido aleatorio de 'effectiveSNThreshold' por estrella, del lado
        // "puede acabar en supernova" -- ver override de fase mas abajo en
        // SpawnFromCatalog, que la coloca directamente en SUPERGIANT (si se
        // dejara en MAIN_SEQUENCE con este radio, la logica de "rejuvenecimiento"
        // la encogeria de vuelta a su radio nominal de secuencia principal).
        {"UY Scuti",   10.0*M_SUN,  909.0*R_SUN, GetColor(0xff4a1aff), true, 0.0f, {255, 80, 30,0}, MAT_GASEOUS},
        {"Betelgeuse", 15.0*M_SUN,  700.0*R_SUN, GetColor(0xff6a3cff), true, 0.0f, {255,110, 60,0}, MAT_GASEOUS},
        {"Stephenson 2-18", 12.0*M_SUN, 2150.0*R_SUN, GetColor(0xd9300aff), true, 0.0f, {255, 70, 20,0}, MAT_GASEOUS},

        // ── Sagitario A* — agujero negro supermasivo del centro de la Vía Láctea ──
        // Datos: Event Horizon Telescope Collaboration (2022), Genzel/Ghez Nobel 2020.
        // Masa:   4.154 millones de M☉ (medicion orbital de estrellas-S, precision ~0.5%).
        // Radio:  radio de Schwarzschild Rs = 2GM/c² ≈ 12.2 millones km ≈ 17.5 R☉.
        //         El disco de acrecion en el shader (r=2.6-12 Rs) esta en escala relativa.
        // isStar: false — no es una estrella en fusion; se inicializa directamente
        //         como BLACK_HOLE en SpawnFromCatalog (ver bloque "Sagitario A*" abajo).
        //         El shader de agujero negro (blackhole_renderer.h) se activa al
        //         seleccionarlo en la GUI, mostrando el lensing gravitacional completo.
        {"Sagitario A*", 4.154e6*M_SUN, 12.2e9, BLACK, false, 0.0f, {0,0,0,0}, MAT_ROCKY},

        // ── Pulsar del Cangrejo (PSR B0531+21) ──────────────────────
        // Remanente de la supernova SN 1054 (observada en China/Arabia).
        // Uno de los pulsares mas estudiados: emite en radio, optico,
        // rayos X y gamma. Los jets polares son su caracteristica visual
        // mas notable. Datos: Hester et al. 2008 / HEASARC.
        // isStar=false: se inicializa como PULSAR en SpawnFromCatalog.
        {"Pulsar de Cangrejo", 2.78e30, 12000.0, GetColor(0xaaccffff), true,  0.0f, {100,150,255,0}, MAT_GASEOUS},

        // ── SGR 1806-20 — Magnetar (Soft Gamma Repeater) ────────────────────
        // Uno de los magnetares mas energeticos conocidos: campo magnetico
        // ~2×10^11 T (el mas intenso medido). Emitio la mayor rafaga de
        // rayos gamma extrasolar registrada (27 dic. 2004). Rotation lenta
        // caracteristica de magnetares (vs pulsares): T=7.56s.
        // Datos: Hurley et al. 1999 / Kosugi et al. 2005 / SGR catalog.
        // isStar=true: usa pipeline de estrella (sin jets, sin llamaradas).
        // Distorsion espaciotiempo activada (ver blackhole_renderer.h).
        {"SGR 1806-20", 2.78e30, 12000.0, GetColor(0xff9955ff), true, 0.0f, {255,140,60,0}, MAT_GASEOUS},

        {"Jupiter",    1.898125e27,  69911000.0,    ORANGE,                  false, 0.35f,{200,150,100,0}, MAT_GASEOUS, 3.0f, 3.13f},
        {"Saturno",    5.68317e26,   58232000.0,    GetColor(0xe7d79bff),    false, 0.45f,{230,210,160,0}, MAT_GASEOUS, 3.0f, 26.73f},
        {"Urano",      8.68099e25,   25362000.0,    GetColor(0x9fd8ffff),    false, 0.40f,{184,240,240,0}, MAT_ICY, 3.0f, 97.77f},
        {"Neptuno",    1.024092e26,  24622000.0,    GetColor(0x3a86ffff),    false, 0.50f,{ 80,120,255,0}, MAT_ICY, 3.0f, 28.32f},
        {"Gigante Procedural", 1.6e27, 60000000.0,   GetColor(0xc9b79cff),    false, 0.40f,{200,180,150,0}, MAT_GASEOUS},
        {"Mercurio",   3.30103e23,   2439400.0,     GetColor(0x8c8a82ff),    false, 0.0f, {150,150,145,0}, MAT_ROCKY, 3.0f, 0.034f},
        // atmosphereDensity 1.8 (vs 0.80 Tierra / 0.08 Marte): la atmosfera
        // de CO2 de Venus tiene ~93x la masa por unidad de superficie de
        // la terrestre -- el valor mas alto del catalogo, "extremadamente
        // densa". atmosphereColor 0xE8DBB7 = amarillo toxico palido (haz
        // de la atmosfera; el color de la capa de nubes en si se define
        // en MakeVenusProfile/cloudColor, rocky_planets.h).
        // atmosphereFalloff 4.0 (antes 1.2): con el exponente bajo, el halo
        // de Fresnel (pow(rimFactor,falloff)*density, ver drawAtmosphereShell)
        // se extendia muy adentro del disco -- a 45 grados del centro
        // (rimFactor=1-cos(45)~0.29), alpha=0.29^1.2*1.8~0.41: una neblina
        // amarilla con su propio degradado de brillo se mezclaba con el
        // relieve/bump-mapping de la corteza, lavando su contraste. Con
        // falloff=4.0, a 45 grados alpha=0.29^4*1.8~0.013 (~3% de lo
        // anterior): el halo queda confinado al limbo real.
        {"Venus",      4.86731e24,   6051800.0,      GetColor(0xe6d8b3ff),    false, 1.8f, {232,219,183,0}, MAT_ROCKY, 4.0f, 177.36f},
        // atmosphereFalloff 4.5 (antes 3.0, mismo razonamiento que Venus
        // arriba): a 60 grados del centro del disco (rimFactor=1-cos(60)
        // =0.5), alpha=0.5^4.5*0.8~0.035 (vs 0.5^3*0.8=0.1 con falloff=3.0,
        // ~3x mas) -- el halo azul ya no se superpone a cicatrices de
        // impacto/relieve fuera del limbo real.
        {"Tierra",     5.97217e24,   6371008.4,      SKYBLUE,                 false, 0.80f,{100,160,255,0}, MAT_ROCKY, 4.5f, 23.44f},
        {"Marte",      6.41691e23,  3389500.0,      GetColor(0xcc4422ff),    false, 0.08f,{200, 80, 40,0}, MAT_ROCKY, 4.5f, 25.19f},
        {"Luna",       7.346e22,    1737400.0,      LIGHTGRAY,               false, 0.0f, {200,200,200,0}, MAT_ROCKY, 3.0f, 1.542f},
        {"Ceres",      9.38416e20,  469700.0,       DARKGRAY,                false, 0.0f, {180,180,160,0}, MAT_ROCKY, 3.0f, 4.0f},

        // ── Lunas reales (satelites de los gigantes, ver categoria MOON
        // mas abajo): masa/radio/temperatura iniciales de NASA/JPL (ver
        // NEW_values.json), apariencia procedural (sin textura fotografica
        // real, ver rocky_planets.h) ajustada a fotografias de referencia.
        {"Io",          8.9319e22,   1821600.0,     GetColor(0xd9b14cff),    false, 0.0f,  {0,0,0,0}, MAT_ROCKY},
        {"Europa",      4.7998e22,   1560800.0,     GetColor(0xd9c9a0ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        {"Ganymede",    1.4819e23,   2631200.0,     GetColor(0x8b8378ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        {"Calisto",     1.0759e23,   2410300.0,     GetColor(0x5b5147ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        // Titan: la unica luna del catalogo con atmosfera propia densa
        // (1.45 bar, mas que la Tierra) -- bruma anaranjada que oculta casi
        // por completo la superficie real, igual que en las fotos de la
        // sonda Cassini/Huygens.
        {"Titan",       1.3452e23,   2574700.0,     GetColor(0xc98a4bff),    false, 1.10f, {210,150, 80,0}, MAT_ICY, 3.5f},
        // Triton: orbita retrograda alrededor de Neptuno (no modelado aqui,
        // solo apariencia/masa); "terreno de melon" + casquete de nitrogeno
        // con estrias oscuras de los geiseres de nitrogeno.
        {"Triton",      2.14e22,     1353400.0,     GetColor(0xe6d2d2ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        // Caronte: la luna de Pluton, casi tan grande como el propio
        // Pluton (sistema binario real) -- gris neutro con el polo norte
        // (Mordor Macula) teñido de rojo por tolinas migradas desde Pluton.
        {"Caronte",     1.586e21,    606000.0,      GetColor(0x8c8680ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},

        {"Asteroide",  2.614e20,     200000.0,       GetColor(0x998877ff),    false, 0.0f, {180,170,150,0}, MAT_METALLIC},
        {"Planeta Rocoso", 3.0e23,   2200000.0,      GetColor(0xa08868ff),    false, 0.0f, {180,170,150,0}, MAT_ROCKY},

        // ── Planetas enanos / objetos transneptunianos (categoria MINOR,
        // ver mas abajo): masa/radio de NASA/NSSDCA y The Planetary Society
        // (ver NEW_values.json). Todos MAT_ICY (cuerpos helados, no
        // diferenciados como un rocoso puro) salvo que se indique lo
        // contrario.
        {"Pluton",      1.30246e22,  1188300.0,     GetColor(0xc9a789ff),    false, 0.06f, {180,200,220,0}, MAT_ICY, 6.0f, 122.53f},
        {"Haumea",      4.006e21,    715000.0,      GetColor(0xd8d4ceff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        {"Makemake",    3.1e21,      714000.0,      GetColor(0xb97a56ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        {"Eris",        1.66e22,     1200000.0,     GetColor(0xe8e4dcff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        {"Orcus",       6.4e20,      473000.0,      GetColor(0xafafa8ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        // Quaoar: tiene un anillo real (descubierto en 2023, demasiado
        // lejos de su superficie para deberse a la fisica de Roche
        // conocida) -- ver SpawnPlanetaryRing en main.cpp.
        {"Quaoar",      1.4e21,      555000.0,      GetColor(0x9c6e52ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        {"Gonggong",    1.75e21,     615000.0,      GetColor(0xa85c45ff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},
        // Sedna: masa no medida directamente (orbita sin satelite conocido);
        // estimada aqui a partir de un radio de 500 km y una densidad tipica
        // de objeto helado del disco disperso (~2.0 g/cm3).
        {"Sedna",       1.05e21,     500000.0,      GetColor(0x9c3b2eff),    false, 0.0f,  {0,0,0,0}, MAT_ICY},

        // Caso especial: "mega-Tierra" rocosa en el limite superior de masa/radio
        // para seguir siendo un cuerpo solido (sin envoltura de H/He). Valores
        // basados en Kepler-10c, el "mega-Earth" mas masivo confirmado:
        // ~17 masas terrestres, ~2.35 radios terrestres, densidad ~7.2 g/cm3
        // (mas denso que la Tierra por compresion gravitacional, consistente
        // con un interior roca/metal). No tiene perfil procedural (ver
        // AssignRockyPlanetProfile) ni atmosfera renderizada: usa el metodo
        // legacy de renderizado por textura (AssignTextures -> tex.gritador).
        {"Planeta Gritador", 1.0152e26, 1.49719e7,   GetColor(0xb5483cff),   false, 0.0f, {180,170,150,0}, MAT_ROCKY},
    };

    // Categorias para las pestanas del menu de catalogo de la GUI (gui.h):
    // el resto de entradas conserva el default BodyCategory::PLANET.
    for (auto& item : db) {
        if (item.name == "Sol" || item.name == "Alpha Centauri A" || item.name == "Alpha Centauri B"
              || item.name == "Proxima Centauri" || item.name == "UY Scuti"
              || item.name == "Betelgeuse" || item.name == "Stephenson 2-18"
              || item.name == "Sagitario A*"
              || item.name == "Pulsar de Cangrejo"
              || item.name == "SGR 1806-20")
            item.category = BodyCategory::STAR;
        else if (item.name == "Luna" || item.name == "Io" || item.name == "Europa"
              || item.name == "Ganymede" || item.name == "Calisto" || item.name == "Titan"
              || item.name == "Triton" || item.name == "Caronte")
            item.category = BodyCategory::MOON;
        else if (item.name == "Ceres" || item.name == "Asteroide"
              || item.name == "Pluton" || item.name == "Haumea" || item.name == "Makemake"
              || item.name == "Eris" || item.name == "Orcus" || item.name == "Quaoar"
              || item.name == "Gonggong" || item.name == "Sedna")
            item.category = BodyCategory::MINOR;
    }

    return db;
}

// Asigna texturas al Body según el nombre base del catálogo
inline void AssignTextures(Body& b, const std::string& baseName, BodyMaterial mat,
                            const GlobalTextures& tex)
{
    if (baseName == "Tierra") {
        b.diffuseTex  = tex.earthBase;
        b.normalTex   = tex.earthNorm;
        b.specularTex = tex.earthSpec;
        b.emissionTex = tex.earthNight;
        b.cloudTex    = tex.earthCloud;
    }
    else if (baseName == "Planeta Gritador")             { b.diffuseTex = tex.gritador; }
    // Luna, Marte y cuerpos MAT_ROCKY/MAT_METALLIC: el shader drawRockyPlanet solo activa
    // texturas cuando los TRES punteros (normalTex + specularTex + diffuseTex) son no-nulos.
    // Sin un mapa de normales y especular propios, la textura difusa no se usaría nunca —
    // el shader usa generación procedural. Se eliminó la asignación para no mantener
    // punteros a texturas que nunca se renderizan (las imágenes .jpg también se borraron).
}

// Asigna la composicion quimica/geologica segun el nombre base del
// catalogo, separada en DOS inventarios (ver solid_composition /
// atmospheric_composition en body.h): manto/nucleo SOLIDO vs. envoltura
// GASEOSA -- valores reales de bulk composition (fraccion de masa, ver
// values.json), cada inventario suma ~1.0 POR SEPARADO. Informativo
// (lectura externa para densidad/terraformacion), no afecta la
// simulacion fisica.
inline void AssignComposition(Body& b, const std::string& baseName)
{
    if (baseName == "Sol") {
        // Estrella: sin "solido" propiamente dicho (nucleo de fusion, no
        // un manto diferenciado) -- toda la masa se reporta como la
        // envoltura/plasma.
        b.solid_composition       = {};
        b.atmospheric_composition = {{"Hidrogeno", 0.7346f}, {"Helio", 0.2485f}, {"Oxigeno", 0.0077f},
                                      {"Carbono", 0.0029f}, {"Hierro", 0.0016f}, {"Neon", 0.0012f},
                                      {"Nitrogeno", 0.0009f}};
    }
    else if (baseName == "Mercurio") {
        b.solid_composition       = {{"Hierro", 0.700f}, {"Oxigeno", 0.150f}, {"Silicio", 0.070f},
                                      {"Magnesio", 0.060f}, {"Aluminio", 0.010f}, {"Calcio", 0.010f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Venus") {
        b.solid_composition       = {{"Hierro", 0.315f}, {"Oxigeno", 0.300f}, {"Silicio", 0.150f},
                                      {"Magnesio", 0.140f}, {"Azufre", 0.025f}, {"Niquel", 0.018f},
                                      {"Calcio", 0.015f}, {"Aluminio", 0.014f}};
        b.atmospheric_composition = {{"Dioxido_de_Carbono", 0.965f}, {"Nitrogeno", 0.035f},
                                      {"Dioxido_de_Azufre", 0.00015f}, {"Argon", 0.00007f},
                                      {"Vapor_de_Agua", 0.00002f}};
    }
    else if (baseName == "Tierra") {
        b.solid_composition       = {{"Hierro", 0.321f}, {"Oxigeno", 0.301f}, {"Silicio", 0.151f},
                                      {"Magnesio", 0.139f}, {"Azufre", 0.029f}, {"Niquel", 0.018f},
                                      {"Calcio", 0.015f}, {"Aluminio", 0.014f}, {"Agua_Liquida", 0.0002f}};
        b.atmospheric_composition = {{"Nitrogeno", 0.7808f}, {"Oxigeno", 0.2095f},
                                      {"Argon", 0.0093f}, {"Dioxido_de_Carbono", 0.0004f}};
    }
    else if (baseName == "Luna") {
        b.solid_composition       = {{"Oxigeno", 0.430f}, {"Silicio", 0.200f}, {"Magnesio", 0.190f},
                                      {"Hierro", 0.100f}, {"Calcio", 0.030f}, {"Aluminio", 0.030f},
                                      {"Titanio", 0.018f}, {"Cromo", 0.002f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Marte") {
        b.solid_composition       = {{"Oxigeno", 0.420f}, {"Hierro", 0.210f}, {"Silicio", 0.160f},
                                      {"Magnesio", 0.110f}, {"Azufre", 0.040f}, {"Calcio", 0.030f},
                                      {"Aluminio", 0.020f}, {"Niquel", 0.010f}};
        b.atmospheric_composition = {{"Dioxido_de_Carbono", 0.953f}, {"Nitrogeno", 0.027f},
                                      {"Argon", 0.016f}, {"Oxigeno", 0.0013f},
                                      {"Monoxido_de_Carbono", 0.0008f}};
    }
    else if (baseName == "Jupiter") {
        b.solid_composition       = {{"Oxigeno", 0.450f}, {"Hierro", 0.200f}, {"Silicio", 0.150f},
                                      {"Magnesio", 0.100f}, {"Agua_Hielo", 0.100f}};
        b.atmospheric_composition = {{"Hidrogeno", 0.898f}, {"Helio", 0.102f},
                                      {"Metano", 0.003f}, {"Amoniaco", 0.00026f}};
    }
    else if (baseName == "Saturno") {
        b.solid_composition       = {{"Oxigeno", 0.400f}, {"Hierro", 0.200f}, {"Silicio", 0.150f},
                                      {"Agua_Hielo", 0.150f}, {"Magnesio", 0.100f}};
        b.atmospheric_composition = {{"Hidrogeno", 0.963f}, {"Helio", 0.0325f},
                                      {"Metano", 0.0045f}, {"Amoniaco", 0.00012f}};
    }
    else if (baseName == "Urano") {
        b.solid_composition       = {{"Agua_Hielo", 0.500f}, {"Metano_Hielo", 0.200f}, {"Amoniaco_Hielo", 0.100f},
                                      {"Oxigeno", 0.100f}, {"Silicio", 0.050f}, {"Hierro", 0.050f}};
        b.atmospheric_composition = {{"Hidrogeno", 0.825f}, {"Helio", 0.152f}, {"Metano", 0.023f}};
    }
    else if (baseName == "Neptuno") {
        b.solid_composition       = {{"Agua_Hielo", 0.500f}, {"Amoniaco_Hielo", 0.150f}, {"Metano_Hielo", 0.150f},
                                      {"Oxigeno", 0.080f}, {"Hierro", 0.060f}, {"Silicio", 0.060f}};
        b.atmospheric_composition = {{"Hidrogeno", 0.800f}, {"Helio", 0.190f}, {"Metano", 0.010f}};
    }
    else if (baseName == "Proxima Centauri" || baseName == "Alpha Centauri A"
          || baseName == "Alpha Centauri B") {
        // Estrellas de secuencia principal reales: misma composicion primordial
        // H/He que el Sol (X~0.73 H, Y~0.25 He, Z~0.02 metales -- nucleosintesis
        // primordial + metalicidad galactica tipica de Poblacion I). NOTA: este
        // valor de spawn lo sobreescribe UpdateStellarComposition (stellar_evolution.h)
        // en el primer frame de simulacion (recalcula segun fase/edad) -- es
        // solo el estado visible antes de despausar.
        b.solid_composition       = {};
        b.atmospheric_composition = {{"Hidrogeno", 0.7346f}, {"Helio", 0.2485f}, {"Oxigeno", 0.0077f},
                                      {"Carbono", 0.0029f}, {"Hierro", 0.0016f}, {"Neon", 0.0012f},
                                      {"Nitrogeno", 0.0009f}};
    }
    else if (baseName == "Ceres") {
        // Nucleo/manto de condrita carbonacea tipo CI (Lodders 2003) +
        // ~27% de hielo de agua por masa (modelos de estructura interna,
        // Castillo-Rogez & McCord 2010). Sin atmosfera.
        b.solid_composition       = {{"Agua_Hielo", 0.27f}, {"Oxigeno", 0.34f}, {"Hierro", 0.14f},
                                      {"Silicio", 0.08f}, {"Magnesio", 0.07f}, {"Azufre", 0.04f},
                                      {"Carbono", 0.03f}, {"Niquel", 0.01f}, {"Calcio", 0.01f},
                                      {"Aluminio", 0.01f}};
        b.atmospheric_composition = {};
    }
    // ── Lunas reales (Galileanas + Titan/Triton/Caronte) ──
    else if (baseName == "Io") {
        b.solid_composition       = {{"Oxigeno", 0.27f}, {"Hierro", 0.25f}, {"Silicio", 0.13f},
                                      {"Magnesio", 0.10f}, {"Azufre", 0.18f}, {"Niquel", 0.02f},
                                      {"Calcio", 0.03f}, {"Aluminio", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Europa") {
        b.solid_composition       = {{"Agua_Hielo", 0.55f}, {"Oxigeno", 0.14f}, {"Silicio", 0.08f},
                                      {"Magnesio", 0.07f}, {"Hierro", 0.08f}, {"Calcio", 0.03f},
                                      {"Aluminio", 0.03f}, {"Sodio", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Ganymede") {
        b.solid_composition       = {{"Agua_Hielo", 0.50f}, {"Oxigeno", 0.14f}, {"Silicio", 0.08f},
                                      {"Magnesio", 0.07f}, {"Hierro", 0.13f}, {"Calcio", 0.03f},
                                      {"Aluminio", 0.03f}, {"Carbono", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Calisto") {
        b.solid_composition       = {{"Agua_Hielo", 0.50f}, {"Oxigeno", 0.16f}, {"Silicio", 0.10f},
                                      {"Magnesio", 0.08f}, {"Hierro", 0.08f}, {"Calcio", 0.02f},
                                      {"Aluminio", 0.01f}, {"Dioxido_de_Carbono_Hielo", 0.03f},
                                      {"Carbono", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Titan") {
        b.solid_composition       = {{"Agua_Hielo", 0.45f}, {"Metano_Hielo", 0.10f}, {"Oxigeno", 0.17f},
                                      {"Silicio", 0.11f}, {"Magnesio", 0.09f}, {"Hierro", 0.06f},
                                      {"Calcio", 0.01f}, {"Aluminio", 0.01f}};
        b.atmospheric_composition = {{"Nitrogeno", 0.984f}, {"Metano", 0.015f}, {"Hidrogeno", 0.001f}};
    }
    else if (baseName == "Triton") {
        b.solid_composition       = {{"Nitrogeno_Hielo", 0.35f}, {"Agua_Hielo", 0.35f}, {"Metano_Hielo", 0.05f},
                                      {"Oxigeno", 0.10f}, {"Silicio", 0.06f}, {"Magnesio", 0.04f},
                                      {"Hierro", 0.03f}, {"Carbono", 0.02f}};
        b.atmospheric_composition = {{"Nitrogeno", 0.99f}, {"Metano", 0.01f}};
    }
    else if (baseName == "Caronte") {
        b.solid_composition       = {{"Agua_Hielo", 0.55f}, {"Amoniaco_Hielo", 0.05f}, {"Oxigeno", 0.16f},
                                      {"Silicio", 0.09f}, {"Magnesio", 0.07f}, {"Hierro", 0.03f},
                                      {"Carbono", 0.05f}};
        b.atmospheric_composition = {};
    }
    // ── Planetas enanos / objetos transneptunianos ──
    else if (baseName == "Pluton") {
        b.solid_composition       = {{"Agua_Hielo", 0.45f}, {"Nitrogeno_Hielo", 0.10f}, {"Metano_Hielo", 0.06f},
                                      {"Monoxido_de_Carbono_Hielo", 0.04f}, {"Oxigeno", 0.16f}, {"Silicio", 0.09f},
                                      {"Magnesio", 0.06f}, {"Hierro", 0.03f}, {"Calcio", 0.01f}};
        b.atmospheric_composition = {{"Nitrogeno", 0.99f}, {"Metano", 0.005f}, {"Monoxido_de_Carbono", 0.005f}};
    }
    else if (baseName == "Haumea") {
        b.solid_composition       = {{"Agua_Hielo", 0.70f}, {"Oxigeno", 0.12f}, {"Silicio", 0.07f},
                                      {"Magnesio", 0.04f}, {"Hierro", 0.02f}, {"Carbono", 0.03f},
                                      {"Metano_Hielo", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Makemake") {
        b.solid_composition       = {{"Metano_Hielo", 0.55f}, {"Nitrogeno_Hielo", 0.05f}, {"Agua_Hielo", 0.15f},
                                      {"Carbono", 0.10f}, {"Oxigeno", 0.06f}, {"Silicio", 0.04f},
                                      {"Magnesio", 0.03f}, {"Hierro", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Eris") {
        b.solid_composition       = {{"Metano_Hielo", 0.45f}, {"Nitrogeno_Hielo", 0.20f}, {"Agua_Hielo", 0.15f},
                                      {"Oxigeno", 0.08f}, {"Silicio", 0.05f}, {"Magnesio", 0.03f},
                                      {"Hierro", 0.02f}, {"Carbono", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Orcus") {
        b.solid_composition       = {{"Agua_Hielo", 0.45f}, {"Metano_Hielo", 0.05f}, {"Oxigeno", 0.18f},
                                      {"Silicio", 0.11f}, {"Magnesio", 0.09f}, {"Hierro", 0.05f},
                                      {"Carbono", 0.05f}, {"Calcio", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Quaoar") {
        b.solid_composition       = {{"Agua_Hielo", 0.35f}, {"Metano_Hielo", 0.05f}, {"Carbono", 0.10f},
                                      {"Oxigeno", 0.20f}, {"Silicio", 0.12f}, {"Magnesio", 0.10f},
                                      {"Hierro", 0.06f}, {"Calcio", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Gonggong") {
        b.solid_composition       = {{"Agua_Hielo", 0.40f}, {"Metano_Hielo", 0.10f}, {"Carbono", 0.10f},
                                      {"Oxigeno", 0.16f}, {"Silicio", 0.10f}, {"Magnesio", 0.08f},
                                      {"Hierro", 0.04f}, {"Calcio", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Sedna") {
        b.solid_composition       = {{"Metano_Hielo", 0.30f}, {"Nitrogeno_Hielo", 0.20f}, {"Agua_Hielo", 0.20f},
                                      {"Carbono", 0.10f}, {"Oxigeno", 0.09f}, {"Silicio", 0.06f},
                                      {"Magnesio", 0.03f}, {"Hierro", 0.02f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Asteroide") {
        // Asteroide metalico (tipo M): aleacion hierro-niquel de un
        // meteorito de hierro (Wasson 1985) con trazas de silicatos --
        // ver CompMetallicCore() en composition.h.
        b.solid_composition       = CompMetallicCore();
        b.atmospheric_composition = {};
    }
    else if (baseName == "Planeta Gritador") {
        // Mega-Tierra (Kepler-10c): densidad ~7.2 g/cm3 vs 5.514 de la
        // Tierra -- compresion gravitacional incrementa la fraccion de
        // nucleo metalico respecto al manto silicatado de la Tierra.
        b.solid_composition       = {{"Hierro", 0.42f}, {"Oxigeno", 0.27f}, {"Silicio", 0.13f},
                                      {"Magnesio", 0.11f}, {"Niquel", 0.025f}, {"Azufre", 0.025f},
                                      {"Calcio", 0.01f}, {"Aluminio", 0.01f}};
        b.atmospheric_composition = {};
    }
    else if (baseName == "Gigante Procedural") {
        RandomizeGasGiantComposition(b, (unsigned int)GetRandomValue(0, 0x7fffffff));
    }
    else if (baseName == "Planeta Rocoso") {
        RandomizeRockyComposition(b, (unsigned int)GetRandomValue(0, 0x7fffffff));
    }
}

// Crea un Body a partir de un CatalogItem.
// baseName: el nombre del catálogo sin sufijo numérico (para asignar texturas correctamente).
inline Body SpawnFromCatalog(const CatalogItem& item, const std::string& baseName,
                              Vector3D pos, Vector3D vel, bool fixed,
                              const GlobalTextures& tex)
{
    Body b;
    b.name               = item.name;
    b.pos                = pos;
    b.vel                = vel;
    b.mass               = item.mass;
    b.radius             = item.radius;
    b.color              = item.color;
    b.isStar             = item.isStar;
    b.fixed              = fixed;
    b.atmosphereDensity     = item.atmosphereDensity;
    b.baseAtmosphereDensity = item.atmosphereDensity;
    b.atmosphereColor    = item.atmosphereColor;
    b.atmosphereFalloff  = item.atmosphereFalloff;
    b.axialTilt          = item.axialTilt;
    b.material           = item.material;
    b.intactMass         = item.mass;
    b.tidalDamage        = 0;
    b.tideSquash         = 1.0f;
    b.tideElongation     = 1.0f;
    b.tideVisualSquash     = 1.0f;
    b.tideVisualElongation = 1.0f;
    b.accreteCount       = 0;

    if (b.isStar) {
        b.luminosity  = L_SUN  * std::pow(std::max(0.05, b.mass / M_SUN), 3.5);
        b.temperature = 3000.0 + 4500.0 * std::pow(std::max(0.05, b.mass / M_SUN), 0.35);

        // Estrellas reales catalogadas (FAT_STARS.json): la ley de potencia
        // M^3.5 de arriba es una aproximacion para estrellas procedurales --
        // aqui se usa la luminosidad real medida/estimada directamente. La
        // temperatura que de verdad se ve la recalcula main.cpp cada frame
        // via Stefan-Boltzmann a partir de (luminosity, radius), asi que
        // basta con la luminosidad real (el radio ya viene del catalogo)
        // para que la T tambien salga correcta sin tocar 'temperature' aqui.
        bool isRealCatalogStar = true;
        if      (baseName == "Alpha Centauri A") b.luminosity = L_SUN * 1.5059;
        else if (baseName == "Alpha Centauri B") b.luminosity = L_SUN * 0.4981;
        else if (baseName == "Proxima Centauri") b.luminosity = L_SUN * 0.001567; // bolometrica
        else if (baseName == "UY Scuti")         b.luminosity = L_SUN * 124000.0;
        else if (baseName == "Stephenson 2-18")  b.luminosity = L_SUN * 436516.0;
        else if (baseName == "Betelgeuse")
            // El rango "luminosity_solar_range: [7500,14000]" del JSON fuente
            // es INCONSISTENTE con su propio radio/temperatura dados (700 R☉,
            // ~3600K): por Stefan-Boltzmann (L=4πσR²T⁴) esos valores dan
            // ~74,000 L☉, no 7500-14000 -- probablemente el dato original sea
            // luminosidad en banda visual (no bolometrica; una supergigante
            // roja fria como Betelgeuse emite la mayoria de su energia en
            // infrarrojo, igual que el propio JSON distingue para Proxima
            // "luminosity_solar_visual": 0.00005 vs "_bolometric": 0.001567,
            // ~31x menos). Se usa el valor bolometrico autoconsistente con su
            // R/T, ademas mucho mas cercano a los ~100,000-150,000 L☉
            // bolometricos comunmente citados para Betelgeuse, y coherente
            // con el resto de la fisica del simulador (la luminosidad que
            // alimenta sombreado/iluminacion es bolometrica, no visual).
            b.luminosity = L_SUN * 74088.0;
        else isRealCatalogStar = false;

        // Para estas estrellas reales, recalcular 'temperature' YA (via
        // Stefan-Boltzmann, con la luminosidad real recien fijada y el radio
        // real del catalogo) en vez de esperar al primer frame de main.cpp --
        // sin esto, la clasificacion de actividad estelar de abajo usaba la
        // T placeholder generica de la formula M^0.35 (p.ej. ~7620K para
        // Alpha Centauri A, una A-tipo "poca convección" en vez de su G2V
        // real ~5804K, actividad solar tipica).
        if (isRealCatalogStar && b.radius > 0.0 && b.luminosity > 0.0)
            b.temperature = std::pow(b.luminosity / (4.0 * PI_D * SIGMA * b.radius * b.radius), 0.25);

        // Actividad estelar según tipo espectral aproximado por temperatura
        double T = b.temperature;
        if      (T < 3500.0) b.stellarActivity = 0.75f; // Enanas M: muy activas
        else if (T < 5000.0) b.stellarActivity = 0.45f; // K: moderadamente activas
        else if (T < 7500.0) b.stellarActivity = 0.30f; // G/F: actividad solar
        else if (T < 15000.0)b.stellarActivity = 0.15f; // A: poca convección
        else                 b.stellarActivity = 0.65f; // B/O/WR: vientos intensos
        // Gigantes y supergigantes: convección prominente
        if (b.mass > 8.0 * M_SUN) b.stellarActivity = std::max(b.stellarActivity, 0.50f);

        // ── Inicialización del ciclo de vida estelar ──────────────
        b.initialStellarMass   = b.mass;
        // Umbral de SN con ruido seeded (±1.5 M☉) para que dos estrellas de la
        // misma masa no evolucionen exactamente igual (metalicidad/masa perdida)
        b.effectiveSNThreshold = 8.0
            + (double)((b.id * 2654435761ull % 300)) / 100.0 - 1.5;
        b.baseLuminosity       = b.luminosity;
        b.visualLuminosity     = b.luminosity;
        // UY Scuti/Betelgeuse/Stephenson 2-18 son supergigantes REALES, no
        // estrellas de secuencia principal con un radio anomalo -- si se
        // dejaran en MAIN_SEQUENCE, el bloque de "rejuvenecimiento" de
        // ApplyStellarPhaseProperties (stellar_evolution.h) las encogeria de
        // vuelta a su radio nominal de MS (p.ej. Betelgeuse de 700 R☉ a
        // ~9 R☉) por estar a >1.3x de 'msRadius'.
        b.stellarPhase = (baseName == "UY Scuti" || baseName == "Betelgeuse"
                        || baseName == "Stephenson 2-18")
                       ? StellarPhase::SUPERGIANT : StellarPhase::MAIN_SEQUENCE;
        b.stellarPhaseAge      = 0.0;
        b.gravityEnabled       = true;
        b.collisionEnabled     = true;
        b.pulsationAmplitude   = 0.0f;
        b.pulsationPhase       = 0.0;
        b.isSupernovaRemnant   = false;
        b.supernovaProgress    = 0.0;
        b.supernovaRadius      = 0.0;
        b.stellarManualOverride = false;

        // Metalicidad: Sol y estrellas reales catalogadas fijas en 0.014
        // (el JSON fuente no da metalicidad real para ellas; fijarla evita
        // que el ruido aleatorio de abajo empuje 'effectiveSNThreshold' por
        // encima de la masa de UY Scuti/Stephenson 2-18/Betelgeuse y las
        // saque de la ruta de supernova). Estrellas procedurales varían ±0.005.
        if (baseName == "Sol" || isRealCatalogStar)
            b.metallicityZ = 0.014f;
        else
            b.metallicityZ = 0.014f + (float)((b.id * 1664525ull % 100) - 50) * 0.0001f;

        // Edad inicial: posicionar en mitad de la MS para el Sol (~4.6 Gyr),
        // estrellas más masivas son más jóvenes. Fórmula: sol→ tMS*0.48.
        {
            double mR  = std::max(0.08, b.mass / M_SUN);
            double tMS = 1.0e10 * 3.156e7 * std::pow(mR, -2.5);
            b.stellarAge = std::min(tMS * 0.48,
                4.6e9 * 3.156e7 * std::sqrt(M_SUN / std::max(b.mass, 0.001 * M_SUN)));
        }
    } else {
        b.temperature = 280.0;
        // Temperaturas medias reales de superficie: punto de partida del
        // equilibrio termico (ver UpdateBodiesState/UpdateThermodynamics en
        // main.cpp/physics.h, que luego relajan 'temperature' hacia
        // 'equilibriumTemp' segun la irradiacion estelar real).
        if      (baseName == "Mercurio") b.temperature = 440.0;
        else if (baseName == "Venus")    b.temperature = 737.0;
        else if (baseName == "Ceres")    b.temperature = 167.0;
        else if (baseName == "Io")       b.temperature = 130.0;
        else if (baseName == "Europa")   b.temperature = 102.0;
        else if (baseName == "Ganymede") b.temperature = 110.0;
        else if (baseName == "Calisto")  b.temperature = 134.0;
        else if (baseName == "Titan")    b.temperature = 94.0;
        else if (baseName == "Triton")   b.temperature = 38.0;
        else if (baseName == "Pluton")   b.temperature = 44.0;
        else if (baseName == "Caronte")  b.temperature = 53.0;
        else if (baseName == "Haumea")   b.temperature = 50.0;
        else if (baseName == "Makemake") b.temperature = 40.0;
        else if (baseName == "Eris")     b.temperature = 42.0;
        else if (baseName == "Orcus")    b.temperature = 44.0;
        else if (baseName == "Quaoar")   b.temperature = 44.0;
        else if (baseName == "Gonggong") b.temperature = 40.0;
        else if (baseName == "Sedna")    b.temperature = 20.0;
    }

    // ── Cuerpos compactos del catalogo (agujeros negros, etc.) ──────────────
    // Los agujeros negros catalogados (p.ej. Sagitario A*) son isStar=false
    // pero necesitan inicialización especial distinta a la de un planeta:
    // stellarPhase=BLACK_HOLE, sin luminosidad propia, sin evolucion estelar.
    // Se inicializan ANTES del bloque de rotacion para que spinRateDeg = 0
    // (ningun efecto de achatamiento ni criticalRotationFraction).
    // ── Pulsar de Cangrejo: inicializacion especial ──────────────
    if (baseName == "Pulsar de Cangrejo") {
        b.stellarPhase          = StellarPhase::PULSAR;
        b.stellarManualOverride = true;  // no auto-evoluciona
        b.isStar                = true;  // usa el pipeline de estrella (shader, lighting)
        b.isSupernovaRemnant    = false;
        b.gravityEnabled        = true;
        b.collisionEnabled      = true;
        b.temperature           = 1.9e6;
        b.luminosity            = 4.0 * PI_D * SIGMA * 12000.0 * 12000.0
                                  * std::pow(1.9e6, 4.0);
        b.baseLuminosity        = b.luminosity;
        b.visualLuminosity      = b.luminosity;
        b.stellarActivity       = 0.0f;  // sin llamaradas (excluido en renderer.h)
        // initialStellarMass = masa del PROGENITOR de la supernova (no la
        // masa remanente actual). IsStellarPhaseValid usa massHigh =
        // max(initialStellarMass, mass) para decidir si la ruta SN es
        // valida. Con la masa remanente actual (~1.4 M☉) < threshold
        // (~8 M☉), la fase PULSAR se consideraba invalida y ApplyStellar-
        // PhaseProperties la autocorregía a RED_GIANT en cada frame.
        // El progenitor del Cangrejo fue una estrella de ~8-11 M☉.
        b.initialStellarMass    = 10.0 * M_SUN; // masa del progenitor
        b.remnantMass           = b.mass;        // masa remanente actual
        b.compactRemnantType    = 1;
        b.effectiveSNThreshold  = 8.0;
        b.metallicityZ          = 0.014f;
        b.stellarAge            = 969.0 * 3.156e7;
        b.stellarPhaseAge       = 0.0;
        b.axialTilt             = 62.0f;
        {
            double periodSec  = 1.0 / 29.9;
            double omega      = 2.0 * PI_D / periodSec;
            b.spinRateDeg     = (float)(omega * 1200.0 * (180.0 / PI_D));
        }
        // criticalRotationFraction la calcula ApplyStellarPhaseProperties
        // a partir del spin/masa/radio reales — no se fuerza a mano.
        b.color = GetColor(0xddeeffff); // azul-blanco frio
    }

    // ── SGR 1806-20 (Magnetar) ───────────────────────────────────────────
    if (baseName == "SGR 1806-20") {
        b.stellarPhase          = StellarPhase::MAGNETAR;
        b.stellarManualOverride = true;
        b.isStar                = true;
        b.isSupernovaRemnant    = false;
        b.gravityEnabled        = true;
        b.collisionEnabled      = true;
        b.temperature           = 5.0e6;
        b.luminosity            = 4.0 * PI_D * SIGMA * 12000.0 * 12000.0
                                  * std::pow(5.0e6, 4.0);
        b.baseLuminosity        = b.luminosity;
        b.visualLuminosity      = b.luminosity;
        b.stellarActivity       = 0.0f;  // sin llamaradas ni jets (magnetar)
        b.initialStellarMass    = 10.0 * M_SUN; // masa del progenitor (como Crab)
        b.remnantMass           = b.mass;
        b.compactRemnantType    = 1;
        b.effectiveSNThreshold  = 8.0;
        b.metallicityZ          = 0.014f;
        b.stellarAge            = 650.0 * 3.156e7; // ~650 anios
        b.stellarPhaseAge       = 0.0;
        b.axialTilt             = 35.0f; // inclinacion observable del eje de rotacion
        // Periodo de rotacion: 7.56 s (magnetar gira MUCHO mas lento que pulsar)
        // omega = 2*PI/7.56 = 0.831 rad/s
        // spinRateDeg = 0.831 * 1200 * (180/PI) = 57139
        {
            double periodSec = 7.56;
            double omega     = 2.0 * PI_D / periodSec;
            b.spinRateDeg    = (float)(omega * 1200.0 * (180.0 / PI_D));
        }
        b.color = GetColor(0xff9955ff); // naranja-blanco (mas caliente que Crab)
    }

    if (baseName == "Sagitario A*") {
        b.stellarPhase          = StellarPhase::BLACK_HOLE;
        b.stellarManualOverride = true;   // no auto-evoluciona
        b.isStar                = false;
        b.isSupernovaRemnant    = false;
        b.gravityEnabled        = true;
        b.collisionEnabled      = true;
        b.luminosity            = 0.0;
        b.baseLuminosity        = 0.0;
        b.visualLuminosity      = 0.0;
        b.temperature           = 0.0;
        b.stellarActivity       = 0.0f;
        b.initialStellarMass    = b.mass;
        b.remnantMass           = b.mass;
        b.compactRemnantType    = 2;      // tipo agujero negro
        b.effectiveSNThreshold  = 8.0;
        b.metallicityZ          = 0.014f;
        b.spinRateDeg           = 0.0f;   // sin efecto de achatamiento
        b.criticalRotationFraction = 0.0f;
        b.stellarAge            = 1.3e10 * 3.156e7; // ~13 Gyr (edad estimada de SgrA*)
        b.stellarPhaseAge       = 0.0;
        b.color                 = BLACK;  // horizonte de eventos: negro absoluto
    }

    // Velocidad de rotacion real (alimenta RotationalOblateness/
    // TidalBodyTransform en renderer.h, ya implementado: q =
    // omega^2*R^3/(G*M), Maclaurin/Darwin-Radau de primer orden). SIN
    // esto, todo cuerpo del catalogo heredaba el spin generico lento por
    // defecto (Body::spinRateDeg=0.5) -- la formula de achatamiento por
    // rotacion propia ya existia, pero nunca recibia un omega real, asi
    // que ningun cuerpo se veia achatado en los polos sin importar que
    // tan rapido rotara en la realidad (Saturno, Haumea...). Periodos en
    // DIAS reales (NASA/JPL, ver NEW_values.json); signo negativo =
    // rotacion retrograda (Venus, Urano, Pluton) -- se preserva en
    // 'omega', el achatamiento (que usa omega^2) no depende del signo,
    // pero el sentido de giro visible si. 0.0 = sin dato real conocido
    // (lunas/TNOs tidalmente bloqueados u objetos sin periodo medido):
    // conserva el spin generico por defecto.
    {
        double rotPeriodDays = 0.0;
        if      (baseName == "Mercurio")  rotPeriodDays = 58.6462;
        else if (baseName == "Venus")     rotPeriodDays = -243.018;
        else if (baseName == "Tierra")    rotPeriodDays = 0.99726968;
        else if (baseName == "Marte")     rotPeriodDays = 1.02595676;
        else if (baseName == "Jupiter")   rotPeriodDays = 0.41354;
        else if (baseName == "Saturno")   rotPeriodDays = 0.44401;
        else if (baseName == "Urano")     rotPeriodDays = -0.71833;
        else if (baseName == "Neptuno")   rotPeriodDays = 0.67125;
        else if (baseName == "Ceres")     rotPeriodDays = 0.37809042;
        // Pluton/Caronte: sistema binario real con rotacion sincrona
        // mutua (ambos muestran siempre la misma cara al otro) -- mismo
        // periodo retrogrado que su orbita mutua.
        else if (baseName == "Pluton")    rotPeriodDays = -6.3872;
        else if (baseName == "Caronte")   rotPeriodDays = -6.3872;
        // Haumea: el caso EXTREMO del catalogo -- ~3.9 horas, la rotacion
        // mas rapida conocida de un cuerpo >100km, por eso es un
        // elipsoide triaxial muy marcado en la realidad (no solo achatado
        // en los polos, tambien alargado en el ecuador).
        else if (baseName == "Haumea")    rotPeriodDays = 0.1631;
        else if (baseName == "Makemake")  rotPeriodDays = 0.937;
        else if (baseName == "Eris")      rotPeriodDays = 1.079;

        // Estrellas reales (FAT_STARS.json): periodos medidos para el
        // sistema Alpha Centauri. Sin esto, las 3 componentes heredaban
        // el spin generico (Body::spinRateDeg=0.5) -- inofensivo para
        // ellas porque su radio es cercano al solar (omega_critico no se
        // dispara), pero ahora son correctas tambien.
        else if (baseName == "Alpha Centauri A") rotPeriodDays = 28.3;
        else if (baseName == "Alpha Centauri B") rotPeriodDays = 36.7;
        else if (baseName == "Proxima Centauri") rotPeriodDays = 84.9;
        // Supergigantes/hipergigantes rojas: NO existe un periodo de
        // rotacion medido y confiable para estas estrellas en la
        // realidad (envolturas extendidas, dificiles de resolver). Se
        // estima un periodo ORDEN DE MAGNITUD a partir de una velocidad
        // de rotacion superficial tipica y conservadora para
        // supergigantes rojas (v ~ 5 km/s) y el radio real de catalogo:
        // periodo = 2*pi*R/v. Sin esto, heredaban el spin generico
        // (omega fijo) pero con un radio cientos de veces mayor al
        // solar -> omega_critico (~R^-1.5) se desplomaba y
        // criticalRotationFraction (stellar_evolution.h) se saturaba en
        // su tope de 0.99, deformando el render como si giraran a
        // velocidad de ruptura (achatamiento/alargamiento ecuatorial
        // extremo) pese a ser, en la realidad, rotadores lentos.
        else if (baseName == "Betelgeuse")        rotPeriodDays = 7085.0;  // ~19.4 anos
        else if (baseName == "UY Scuti")          rotPeriodDays = 9197.0;  // ~25.2 anos
        else if (baseName == "Stephenson 2-18")   rotPeriodDays = 21753.0; // ~59.6 anos

        if (rotPeriodDays != 0.0) {
            double periodSec = rotPeriodDays * 86400.0;
            double omega     = 2.0 * PI_D / periodSec;
            b.spinRateDeg = (float)(omega * 1200.0 * (180.0 / PI_D));
        }
    }

    // Gigantes gaseosos/helados y planetas rocosos/helados: perfiles
    // procedurales (sin textura). El resto recibe texturas estaticas.
    if (!AssignGasGiantProfile(b, baseName) && !AssignRockyPlanetProfile(b, baseName, tex))
        AssignTextures(b, baseName, item.material, tex);

    AssignComposition(b, baseName);

    // Valores BASE para el efecto invernadero (Nivel 1, ver
    // UpdateThermodynamics en physics.h): se capturan aqui, justo despues
    // de aplicar el perfil rocoso, para que reflejen la configuracion
    // propia de cada mundo (p.ej. cloudDensity=0.55 en la Tierra, 0.0 en
    // Marte/Luna) en vez de un valor generico.
    if (b.isRockyPlanet) {
        b.baseCloudDensity    = b.rockyPlanet.cloudDensity;
        b.baseColorWater      = b.rockyPlanet.colorWater;
        b.baseAtmosphereColor = b.atmosphereColor;
    }

    // Nucleo rocoso de gigantes gaseosos/helados (planeta Chthoniano al
    // perder la envoltura, ver coreMass/shellBaseDensity en body.h y el
    // modelo de volumen compuesto en ApplyTidesAndRoche/physics.h). El
    // nucleo es el 10% de la masa total; 'shellBaseDensity' se fija una sola
    // vez a partir del radio/masa iniciales para que la envoltura conserve
    // su densidad propia mientras se reduce su volumen.
    if (b.isGasGiant) {
        b.coreMass     = b.mass * 0.10;
        b.coreMaterial = MAT_ROCKY;
        double coreVolume  = b.coreMass / RHO_ROCKY_CORE;
        double gasVolume   = std::max(1.0, VolumeFromRadius(b.radius) - coreVolume);
        double gasMassInit = b.mass - b.coreMass;
        b.shellBaseDensity = gasMassInit / gasVolume;
    }

    // Nucleo metalico de planetas rocosos/helados diferenciados (ver
    // DIFFERENTIATION_RADIUS/ROCKY_CORE_MASS_FRACTION/RHO_METALLIC_CORE en
    // constants.h y coreMass/shellBaseDensity en body.h): cuerpos lo
    // bastante grandes como para haberse fundido y separado un nucleo de
    // hierro-niquel del manto/corteza de roca. Igual que con los gigantes
    // gaseosos, 'shellBaseDensity' (aqui la densidad del MANTO) se fija una
    // sola vez para que el manto restante conserve su densidad propia
    // mientras se reduce su volumen (ver ApplyTidesAndRoche). Cuerpos por
    // debajo del umbral (asteroides, lunas menores) quedan homogeneos
    // (coreMass=0) y se desintegran por completo si cruzan el limite de
    // Roche.
    if (b.isRockyPlanet && !b.isGasGiant
        && (b.material == MAT_ROCKY || b.material == MAT_ICY)
        && b.radius >= DIFFERENTIATION_RADIUS) {
        b.coreMass     = b.mass * ROCKY_CORE_MASS_FRACTION;
        b.coreMaterial = MAT_METALLIC;
        double coreVolume    = b.coreMass / RHO_METALLIC_CORE;
        double mantleVolume  = std::max(1.0, VolumeFromRadius(b.radius) - coreVolume);
        double mantleMassInit = b.mass - b.coreMass;
        b.shellBaseDensity   = mantleMassInit / mantleVolume;
    }

    // Inventario inicial de volatiles para transiciones de fase (solo
    // planetas rocosos/helados, ver UpdateThermodynamics en physics.h).
    // Se reparte entre hielo/liquido/vapor de forma coherente con el
    // waterLevel inicial del perfil y la temperatura de partida (280 K).
    if (b.isRockyPlanet) {
        if (baseName == "Tierra") {
            b.volatileBudget = 0.60f;
            b.iceFraction    = 0.10f;
            b.vaporFraction  = 0.05f;
        } else if (baseName == "Marte") {
            b.volatileBudget = 0.15f;
            b.iceFraction    = 0.15f;
            b.vaporFraction  = 0.0f;
        }
    }

    return b;
}
