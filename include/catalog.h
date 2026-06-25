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
        {"Sol",        M_SUN,        R_SUN,         GOLD,                    true,  0.0f, {255,200,100,0}, MAT_GASEOUS},
        {"Enana Roja", 0.15*M_SUN,   0.20*R_SUN,    RED,                     true,  0.0f, {255,100, 60,0}, MAT_GASEOUS},
        {"Jupiter",    1.898e27,     69911000.0,     ORANGE,                  false, 0.35f,{200,150,100,0}, MAT_GASEOUS, 3.0f, 3.13f},
        {"Saturno",    5.683e26,     58232000.0,     GetColor(0xe7d79bff),    false, 0.45f,{230,210,160,0}, MAT_GASEOUS, 3.0f, 26.73f},
        {"Urano",      8.681e25,     25362000.0,     GetColor(0x9fd8ffff),    false, 0.40f,{184,240,240,0}, MAT_ICY, 3.0f, 97.77f},
        {"Neptuno",    1.024e26,     24622000.0,     GetColor(0x3a86ffff),    false, 0.50f,{ 80,120,255,0}, MAT_ICY, 3.0f, 28.32f},
        {"Gigante Procedural", 1.6e27, 60000000.0,   GetColor(0xc9b79cff),    false, 0.40f,{200,180,150,0}, MAT_GASEOUS},
        {"Mercurio",   3.3011e23,    2439700.0,      GetColor(0x8c8a82ff),    false, 0.0f, {150,150,145,0}, MAT_ROCKY, 3.0f, 0.034f},
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
        {"Venus",      4.8675e24,    6051800.0,      GetColor(0xe6d8b3ff),    false, 1.8f, {232,219,183,0}, MAT_ROCKY, 4.0f, 177.36f},
        // atmosphereFalloff 4.5 (antes 3.0, mismo razonamiento que Venus
        // arriba): a 60 grados del centro del disco (rimFactor=1-cos(60)
        // =0.5), alpha=0.5^4.5*0.8~0.035 (vs 0.5^3*0.8=0.1 con falloff=3.0,
        // ~3x mas) -- el halo azul ya no se superpone a cicatrices de
        // impacto/relieve fuera del limbo real.
        {"Tierra",     5.972e24,     6371000.0,      SKYBLUE,                 false, 0.80f,{100,160,255,0}, MAT_ROCKY, 4.5f, 23.44f},
        {"Marte",      6.417e23,     3389000.0,      GetColor(0xcc4422ff),    false, 0.08f,{200, 80, 40,0}, MAT_ROCKY, 4.5f, 25.19f},
        {"Luna",       7.342e22,     1737000.0,      LIGHTGRAY,               false, 0.0f, {200,200,200,0}, MAT_ROCKY},
        {"Ceres",      9.393e20,     473000.0,       DARKGRAY,                false, 0.0f, {180,180,160,0}, MAT_ROCKY},
        {"Asteroide",  2.614e20,     200000.0,       GetColor(0x998877ff),    false, 0.0f, {180,170,150,0}, MAT_METALLIC},
        {"Planeta Rocoso", 3.0e23,   2200000.0,      GetColor(0xa08868ff),    false, 0.0f, {180,170,150,0}, MAT_ROCKY},

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
        if (item.name == "Sol" || item.name == "Enana Roja")
            item.category = BodyCategory::STAR;
        else if (item.name == "Luna")
            item.category = BodyCategory::MOON;
        else if (item.name == "Ceres" || item.name == "Asteroide")
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
    else if (baseName == "Luna")                         { b.diffuseTex = tex.luna;     }
    else if (baseName == "Marte")                        { b.diffuseTex = tex.marte;    }
    else if (baseName == "Planeta Gritador")             { b.diffuseTex = tex.gritador; }
    // Gigantes gaseosos/helados (Jupiter, Saturno, Urano, Neptuno, procedurales)
    // usan el shader atmosferico procedural en lugar de una textura estatica.
    // Estrellas tampoco usan textura — el shader procedural genera su apariencia completa.
    else if (mat == MAT_ROCKY || mat == MAT_METALLIC)    { b.diffuseTex = tex.mercury;  }
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
    else if (baseName == "Enana Roja") {
        // Estrella de secuencia principal: misma composicion primordial
        // H/He que el Sol (X~0.73 H, Y~0.25 He, Z~0.02 metales -- nucleosintesis
        // primordial + metalicidad galactica tipica de Poblacion I).
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

        // Actividad estelar según tipo espectral aproximado por temperatura
        double T = b.temperature;
        if      (T < 3500.0) b.stellarActivity = 0.75f; // Enanas M: muy activas
        else if (T < 5000.0) b.stellarActivity = 0.45f; // K: moderadamente activas
        else if (T < 7500.0) b.stellarActivity = 0.30f; // G/F: actividad solar
        else if (T < 15000.0)b.stellarActivity = 0.15f; // A: poca convección
        else                 b.stellarActivity = 0.65f; // B/O/WR: vientos intensos
        // Gigantes y supergigantes: convección prominente
        if (b.mass > 8.0 * M_SUN) b.stellarActivity = std::max(b.stellarActivity, 0.50f);
    } else {
        b.temperature = 280.0;
        // Temperaturas medias reales de superficie: punto de partida del
        // equilibrio termico (ver UpdateBodiesState/UpdateThermodynamics en
        // main.cpp/physics.h, que luego relajan 'temperature' hacia
        // 'equilibriumTemp' segun la irradiacion estelar real).
        if      (baseName == "Mercurio") b.temperature = 440.0;
        else if (baseName == "Venus")    b.temperature = 737.0;
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
