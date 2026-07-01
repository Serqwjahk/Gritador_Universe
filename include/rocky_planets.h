#pragma once
#include <string>
#include "raylib.h"
#include "body.h"
#include "gas_giants.h" // HexColV3 / HSVtoRGBVec
#include "textures.h"   // GlobalTextures (mapas reales de la Tierra)

// ============================================================
//  Perfiles de superficie de planetas rocosos/helados
//  Cada preset alimenta el shader procedural unificado
//  (drawRockyPlanet en shaders.h) con su propio nivel de
//  agua, densidad de crateres, nubes, luces nocturnas y paleta.
// ============================================================

// ── Tierra: oceanos, nubes dinamicas con sombra, luces nocturnas ──
inline RockyPlanetProfile MakeEarthProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.45f;
    p.craterDensity = 0.0f;
    p.cloudDensity  = 0.55f;
    p.hasCityLights = 1.0f;
    p.seed          = 0.0f;
    p.polarIceSize  = 1.0f; // casquetes polares grandes
    p.colorLow   = HexColV3(0x3a5f2a); // continentes a nivel del mar (verde/marron)
    p.colorHigh  = HexColV3(0xcfc8ad); // tierras altas / cumbres
    p.colorWater = HexColV3(0x0a2a6e); // oceano profundo
    p.cloudColor = HexColV3(0xf5f6fa);
    return p;
}

// ── Mercurio: mundo muerto e inerte, sin atmosfera/nubes, la mayor
//    densidad de crateres del catalogo (sin erosion atmosferica que los
//    suavice ni vulcanismo que los borre) ──
inline RockyPlanetProfile MakeMercuryProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 1.0f; // maxima densidad de impactos: sin atmosfera, nada los protege
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 5.0f;
    p.polarIceSize  = 0.0f; // sin casquetes visibles a esta escala
    p.mountainStrength = 0.35f; // escarpes de contraccion global (Discovery Rupes, etc.)
    // terrainScale alto (vs 1.0 generico): sube la frecuencia de TODO el
    // campo de relieve, incluida craterField (terrainHeight, shaders.h) --
    // crateres mas pequenos y numerosos por unidad de superficie,
    // superpuestos entre si (regolito saturado de impactos, real).
    p.terrainScale     = 1.8f;
    // Gris carbon / marron muy oscuro: mucho mas oscuro que el regolito
    // lunar (ver MakeMoonProfile) para diferenciarlos a simple vista.
    p.colorLow   = HexColV3(0x2A2724); // cuencas/fondos de crater
    p.colorHigh  = HexColV3(0x4A443D); // tierras altas
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Venus: corteza basaltica/oxidada bajo una capa de nubes de acido
//    sulfurico totalmente opaca y en super-rotacion (ver
//    atmosphereDensity/atmosphereColor en catalog.h). Pocos crateres: la
//    atmosfera densa destruye los impactadores pequenos antes de que
//    lleguen al suelo; en cambio el terreno esta intensamente deformado
//    (tesserae: crestas/grietas entrecruzadas) ──
inline RockyPlanetProfile MakeVenusProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f; // oceanos evaporados por el efecto invernadero (sin agua liquida)
    p.craterDensity = 0.05f; // la densa atmosfera filtra los impactadores pequenos (real)
    // Cobertura de nubes 100%: smoothstep(lo,hi,cloudNoise) con
    // lo=mix(0.78,0.30,d), hi=mix(0.96,0.55,d) (shaders.h). hi<=0 cuando
    // d >= 0.96/0.41 = 2.3415..., y para todo d>=0 se cumple hi>lo
    // (hi-lo = 0.18+0.07d > 0), asi que smoothstep nunca queda en estado
    // indefinido (edge0<edge1 siempre). Con cloudNoise>=0>=hi, el termino
    // (cloudNoise-lo)/(hi-lo) >= 1 y la cobertura satura a 1.0 en TODO el
    // planeta. d=2.5 deja margen sobre el umbral 2.3415.
    p.cloudDensity  = 2.5f;
    // Super-rotacion (~4 dias el periodo de la capa de nubes vs 243 dias
    // del solido): bandas estiradas en longitud + cizalla en "V"/"Y"
    // hacia el ecuador (ver cloudField en shaders.h).
    p.cloudBandStrength = 1.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 6.0f;
    p.polarIceSize  = 0.0f;
    // Tesserae: el terreno mas deformado del sistema solar (crestas y
    // grietas por compresion tectonica). mountainStrength = la amplitud
    // MAS ALTA del catalogo (crestas profundas). terrainScale BAJO (vs
    // ~1.0-1.1 generico/Marte) -> frecuencia espacial baja: crestas
    // ANCHAS en vez de una red densa de surcos estrechos ("gusanos") --
    // ver derivacion cuantitativa en TERRAIN_BUMP_SCALE (shaders.h).
    p.mountainStrength = 0.70f;
    p.terrainScale     = 0.9f;

    // Bioma "infierno basaltico": ridge noise de alta frecuencia cubre
    // TODO el planeta (terrainHeight, shaders.h) y el color usa el
    // degradado de 3 bandas por elevacion definido abajo, en vez del
    // simple colorLow->colorHigh del bioma generico.
    p.terrainBiome = 1.0f;

    // Paleta por elevacion (bandas 0.0-0.3 / 0.3-0.7 / 0.7-1.0, ver
    // drawRockyPlanet): grietas/llanuras de lava en naranja volcanico
    // intenso, planicies en rojo oxido profundo, picos/tesserae en
    // basalto quemado casi negro.
    p.colorLow   = HexColV3(0xD95A11); // 0.0-0.3: naranja volcanico (grietas/llanuras)
    p.colorMid   = HexColV3(0x5E1F09); // 0.3-0.7: rojo oxido profundo (planicies)
    p.colorHigh  = HexColV3(0x110501); // 0.7-1.0: basalto quemado casi negro (picos)
    p.colorWater = HexColV3(0x404040); // sin oceanos: no se usa
    p.cloudColor = HexColV3(0xDDD1AC); // capa de nubes: amarillo toxico palido
    return p;
}

// ── Marte: seco, polvoriento, crateres moderados, sin nubes ──
inline RockyPlanetProfile MakeMarsProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.18f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 1.0f;
    p.polarIceSize  = 0.6f; // casquetes pequenos, solo en los polos
    p.mountainStrength = 0.35f; // Tharsis/Valles Marineris: relieve marcado pero no extremo
    p.terrainScale     = 1.1f;
    p.colorLow   = HexColV3(0x7a3318); // llanuras de oxido oscuro
    p.colorHigh  = HexColV3(0xd9966a); // tierras altas mas claras/polvorientas
    p.colorWater = HexColV3(0x404040); // sin oceanos: no se usa
    p.cloudColor = HexColV3(0xe8d8c8);
    return p;
}

// ── Luna: mundo muerto, crateres densos, sin atmosfera/nubes ──
inline RockyPlanetProfile MakeMoonProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.90f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 2.0f;
    // mountainStrength bajo: tierras altas suaves, sin cordilleras
    // marcadas -- el contraste visual lo dan los mares (llanuras lisas,
    // mountainMask~0 en terrainHeight) frente a las tierras altas claras.
    p.mountainStrength = 0.10f;
    // terrainScale bajo (vs 1.0 generico): baja la frecuencia de
    // craterField (terrainHeight, shaders.h) -- crateres mas grandes y
    // mejor separados ("cráteres gigantes"), en vez de la saturacion fina
    // de Mercurio.
    p.terrainScale     = 0.6f;
    // Grises claros (sin tinte azulado/marron): diferencia clara frente al
    // gris carbon de Mercurio y el azul palido de Ceres.
    p.colorLow   = HexColV3(0x888888); // mares lunares (basalto oscuro)
    p.colorHigh  = HexColV3(0xCCCCCC); // tierras altas (anortosita clara)
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Ceres: planeta enano helado, crateres densos, gris-parduzco ──
inline RockyPlanetProfile MakeCeresProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.65f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 3.0f;
    p.mountainStrength = 0.10f; // cuerpo pequeno y helado: relieve muy suave
    p.terrainScale     = 0.75f;
    // Gris azulado palido (regolito carbonaceo), distinto del gris neutro
    // de la Luna y del marron oscuro de Mercurio.
    p.colorLow   = HexColV3(0x3E4248); // regolito oscuro
    p.colorHigh  = HexColV3(0x767C86); // tierras altas mas claras
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);

    // Bioma Ceres: terreno igual que el generico + "manchas de sal" tipo
    // Occator (parches Voronoi brillantes ocasionales, ver
    // drawRockyPlanet en shaders.h).
    p.terrainBiome = 2.0f;
    return p;
}

// ── Io: luna volcanica de Jupiter, resurfacing constante por erupciones
//    de azufre -- crateres casi inexistentes (se borran solapados por
//    coladas nuevas), montanas reales hasta 17 km (las mas altas del
//    sistema solar fuera de cuerpos mucho mas grandes) ──
inline RockyPlanetProfile MakeIoProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.04f; // resurfacing volcanico constante
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 7.0f;
    p.polarIceSize     = 0.15f; // parches de escarcha de SO2
    p.mountainStrength = 0.55f; // Boosaule/Euboea Montes
    p.terrainScale     = 1.0f;
    p.colorLow   = HexColV3(0xC9622A); // coladas de azufre fundido/oxidado
    p.colorHigh  = HexColV3(0xE8DCB0); // escarcha de SO2 / depositos claros
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Europa: corteza de hielo joven, casi sin crateres grandes, surcada
//    por "lineae" rojizas (fracturas con material del oceano subsuperficial) ──
inline RockyPlanetProfile MakeEuropaProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.03f; // corteza de hielo geologicamente muy joven
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 8.0f;
    p.mountainStrength = 0.05f; // superficie casi plana
    p.terrainScale     = 0.65f;
    p.colorLow   = HexColV3(0x8B6F47); // lineae (fracturas tenidas de marron)
    p.colorHigh  = HexColV3(0xE8DCC0); // hielo limpio
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Ganimedes: mezcla de terreno oscuro muy antiguo (cratereado) y
//    terreno surcado mas joven y brillante ──
inline RockyPlanetProfile MakeGanymedeProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.45f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 9.0f;
    p.mountainStrength = 0.22f;
    p.terrainScale     = 0.90f;
    p.colorLow   = HexColV3(0x5B5147); // terreno oscuro antiguo
    p.colorHigh  = HexColV3(0x9C9488); // terreno surcado brillante
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Calisto: la superficie mas antigua y cratereada del sistema solar
//    (sin resurfacing significativo desde su formacion) ──
inline RockyPlanetProfile MakeCallistoProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.85f; // saturacion casi total de impactos
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 10.0f;
    p.mountainStrength = 0.15f;
    p.terrainScale     = 0.85f;
    p.colorLow   = HexColV3(0x4A4038); // regolito oscuro contaminado de roca
    p.colorHigh  = HexColV3(0x7A7268);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Titan: superficie de dunas e hidrocarburos casi oculta bajo una
//    bruma anaranjada espesa (ver atmosphereDensity en catalog.h) --
//    pocos crateres (erosion por lluvia de metano) ──
inline RockyPlanetProfile MakeTitanProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.08f; // erosion por ciclo del metano
    p.cloudDensity  = 0.0f;  // la opacidad real la aporta atmosphereDensity, no nubes propias
    p.hasCityLights = 0.0f;
    p.seed          = 11.0f;
    p.mountainStrength = 0.10f;
    p.terrainScale     = 0.80f;
    p.colorLow   = HexColV3(0x8B5A2B); // lagos/dunas de hidrocarburos
    p.colorHigh  = HexColV3(0xC9914F);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Triton: "terreno de melon" (textura punteada unica) + casquete de
//    nitrogeno con estrias oscuras de geiseres criovolcanicos ──
inline RockyPlanetProfile MakeTritonProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.25f; // aproxima la textura punteada del "terreno de melon"
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 12.0f;
    p.polarIceSize     = 1.0f; // gran casquete de nitrogeno
    p.mountainStrength = 0.10f;
    p.terrainScale     = 0.70f;
    p.colorLow   = HexColV3(0xE3C9C9); // hielo de nitrogeno rosado
    p.colorHigh  = HexColV3(0xF0E8E8);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Pluton: corazon de nitrogeno brillante (Tombaugh Regio) sobre
//    terreno de tolinas rojizo-marron (Cthulhu Macula); montanas reales
//    de hielo de agua (Norgay/Hillary Montes) ──
inline RockyPlanetProfile MakePlutoProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.35f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 13.0f;
    p.polarIceSize     = 0.5f; // aproxima el "corazon" de Tombaugh Regio
    p.mountainStrength = 0.30f;
    p.terrainScale     = 0.75f;
    p.colorLow   = HexColV3(0x9C6648); // Cthulhu Macula (tolinas oscuras)
    p.colorHigh  = HexColV3(0xE8D9C4); // Tombaugh Regio (hielo de nitrogeno)
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Caronte: gris neutro, muy cratereado, con el polo norte (Mordor
//    Macula) tenido de rojo por tolinas migradas desde la atmosfera de
//    Pluton ──
inline RockyPlanetProfile MakeCharonProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.70f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 14.0f;
    p.mountainStrength = 0.20f;
    p.terrainScale     = 0.80f;
    p.colorLow   = HexColV3(0x8C8680); // gris neutro
    p.colorHigh  = HexColV3(0xACA49C);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Haumea: hielo de agua cristalina, albedo muy alto, forma elipsoidal
//    real (rotacion rapidisima de ~4h) no modelada aqui -- solo apariencia ──
inline RockyPlanetProfile MakeHaumeaProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.20f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 15.0f;
    p.mountainStrength = 0.10f;
    p.terrainScale     = 0.70f;
    p.colorLow   = HexColV3(0xC8C4BC); // hielo cristalino, brillo casi uniforme
    p.colorHigh  = HexColV3(0xE8E4DC);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Makemake: tolinas rojizas + parches de hielo de metano/etano ──
inline RockyPlanetProfile MakeMakemakeProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.15f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 16.0f;
    p.mountainStrength = 0.20f;
    p.terrainScale     = 0.75f;
    p.colorLow   = HexColV3(0x7A4A36); // tolinas oscuras
    p.colorHigh  = HexColV3(0xB97A56); // hielo de metano teñido
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Eris: el objeto transneptuniano mas brillante conocido (albedo~0.84,
//    comparable a la nieve fresca) -- hielo de metano casi puro en superficie ──
inline RockyPlanetProfile MakeErisProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.15f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 17.0f;
    p.mountainStrength = 0.10f;
    p.terrainScale     = 0.70f;
    p.colorLow   = HexColV3(0xC8C4BC);
    p.colorHigh  = HexColV3(0xECE8E0);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Orcus: a veces llamado "anti-Pluton" por su superficie mucho mas
//    neutra/gris (mucha menos tolina roja que Pluton) ──
inline RockyPlanetProfile MakeOrcusProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.40f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 18.0f;
    p.mountainStrength = 0.15f;
    p.terrainScale     = 0.75f;
    p.colorLow   = HexColV3(0x9A968E);
    p.colorHigh  = HexColV3(0xC4C0B8);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Quaoar: hielo de agua cristalina + tolinas rojizas; posee un anillo
//    real descubierto en 2023 (ver SpawnPlanetaryRing en main.cpp) ──
inline RockyPlanetProfile MakeQuaoarProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.35f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 19.0f;
    p.mountainStrength = 0.25f;
    p.terrainScale     = 0.80f;
    p.colorLow   = HexColV3(0x6E4636);
    p.colorHigh  = HexColV3(0x9C6E52);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Gonggong: tolinas rojizas, superficie similar a Quaoar/Makemake ──
inline RockyPlanetProfile MakeGonggongProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.30f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 20.0f;
    p.mountainStrength = 0.20f;
    p.terrainScale     = 0.75f;
    p.colorLow   = HexColV3(0x6E3A2C);
    p.colorHigh  = HexColV3(0xA85C45);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Sedna: uno de los objetos mas rojos conocidos del sistema solar
//    (casi tan rojo como Marte), orbita extrema en el disco disperso ──
inline RockyPlanetProfile MakeSednaProfile() {
    RockyPlanetProfile p;
    p.waterLevel    = 0.0f;
    p.craterDensity = 0.10f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;
    p.seed          = 21.0f;
    p.mountainStrength = 0.10f;
    p.terrainScale     = 0.70f;
    p.colorLow   = HexColV3(0x5C2018);
    p.colorHigh  = HexColV3(0x9C3B2E);
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Asteroide/fragmento procedural: irregular, cratereado, sin agua/
//    nubes. La paleta varia segun el material (rocoso/helado/metalico)
//    ademas de la semilla, para que escombros y proto-cuerpos generados
//    en tiempo real (ver MakeFragments en physics.h) tengan un aspecto
//    coherente con su composicion sin necesidad de texturas.
inline RockyPlanetProfile MakeAsteroidProfile(unsigned int seed, BodyMaterial mat = MAT_ROCKY) {
    RockyPlanetProfile p;
    unsigned int s = seed;
    auto rnd = [&s]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1u << 24);
    };

    p.waterLevel    = 0.0f;
    p.cloudDensity  = 0.0f;
    p.hasCityLights = 0.0f;

    // Semilla espacial: desplaza TODO el campo de ruido (terreno,
    // cordilleras, crateres -- ver terrainHeight en shaders.h) a una
    // region completamente distinta. Rango amplio (4..124) derivado de
    // rnd() -- que usa los bits ALTOS de la LCG, bien mezclados -- en
    // vez del antiguo 'seed % 1000' (bits bajos, mas correlados entre
    // semillas consecutivas de una misma cascada de fragmentos). Asi
    // dos fragmentos de la MISMA explosion (semillas consecutivas)
    // terminan leyendo zonas muy distintas del ruido: montanas y
    // crateres en lugares distintos.
    p.seed = 4.0f + rnd() * 120.0f;

    // Jitter de color por canal RGB: ademas de variar matiz/saturacion/
    // valor en HSV (por material, abajo), desplaza cada canal R/G/B un
    // poco al azar -- asi ningun par de fragmentos comparte EXACTAMENTE
    // el mismo tono aunque su HSV base coincida.
    auto jitterRGB = [&](Vector3 c, float amount) -> Vector3 {
        c.x = ClampF(c.x + (rnd() - 0.5f) * amount, 0.0f, 1.0f);
        c.y = ClampF(c.y + (rnd() - 0.5f) * amount, 0.0f, 1.0f);
        c.z = ClampF(c.z + (rnd() - 0.5f) * amount, 0.0f, 1.0f);
        return c;
    };

    if (mat == MAT_ICY) {
        // Hielo sucio: crateres moderados, paleta azul-blanquecina, relieve
        // suavizado por la propia naturaleza del hielo (menos rigido que la roca)
        p.craterDensity = 0.30f + rnd() * 0.35f;
        p.mountainStrength = 0.30f + rnd() * 0.55f;
        p.terrainScale     = 0.70f + rnd() * 0.80f;
        float hue = 0.46f + rnd() * 0.22f;  // azul-cian .. azul-violeta, hielo sucio
        float sat = 0.03f + rnd() * 0.20f;
        p.colorLow  = jitterRGB(HSVtoRGBVec(hue, sat,        0.35f + rnd() * 0.25f), 0.10f);
        p.colorHigh = jitterRGB(HSVtoRGBVec(hue, sat * 0.6f, 0.65f + rnd() * 0.30f), 0.10f);
    } else if (mat == MAT_METALLIC) {
        // Nucleo metalico: muy cratereado, gris-azulado oscuro/plateado,
        // y el mas irregular de los tres -- esquirlas de un nucleo fracturado
        p.craterDensity = 0.55f + rnd() * 0.35f;
        p.mountainStrength = 0.60f + rnd() * 1.00f;
        p.terrainScale     = 0.80f + rnd() * 1.30f;
        float hue = 0.50f + rnd() * 0.26f;  // azul-acero .. violeta-grafito
        float sat = 0.01f + rnd() * 0.12f;
        p.colorLow  = jitterRGB(HSVtoRGBVec(hue, sat,        0.12f + rnd() * 0.20f), 0.08f);
        p.colorHigh = jitterRGB(HSVtoRGBVec(hue, sat * 0.7f, 0.40f + rnd() * 0.35f), 0.08f);
    } else {
        // Rocoso: gama de oxidos/basaltos (rojo->naranja->marron->gris),
        // relieve muy variable de un fragmento a otro (escombros de
        // tamanos/formas dispares)
        p.craterDensity = 0.45f + rnd() * 0.40f;
        p.mountainStrength = 0.45f + rnd() * 0.85f;
        p.terrainScale     = 0.60f + rnd() * 1.20f;
        float hue = rnd() * 0.13f;          // 0.00-0.13: rojo -> naranja -> marron
        float sat = 0.04f + rnd() * 0.36f;  // casi gris (basalto) .. tierra muy saturada (oxido)
        p.colorLow  = jitterRGB(HSVtoRGBVec(hue, sat,        0.10f + rnd() * 0.30f), 0.12f);
        p.colorHigh = jitterRGB(HSVtoRGBVec(hue, sat * 0.7f, 0.38f + rnd() * 0.37f), 0.12f);
    }
    p.colorWater = HexColV3(0x404040);
    p.cloudColor = HexColV3(0xe0e0e0);
    return p;
}

// ── Planeta rocoso aleatorio ("Planeta Rocoso" en BuildCatalog) ──
// Genera un mundo rocoso/helado completo: masa/radio dentro del rango
// fisico real de un cuerpo rocoso (ver ROCKY_RANDOM_MASS_MIN/MAX y
// ROCKY_MR_EXPONENT en constants.h), inclinacion axial, atmosfera
// (densidad/color/halo), oceanos, casquetes polares, nubes y el
// inventario de volatiles que alimenta UpdateThermodynamics
// (physics.h) -- todo coherente entre si y derivado de la misma
// semilla, para que cada spawn sea un mundo distinto pero
// internamente consistente.
inline void RandomizeRockyPlanet(Body& b, unsigned int seed) {
    unsigned int s = seed;
    auto rnd = [&s]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1u << 24);
    };

    // Masa log-uniforme: cubre ordenes de magnitud de forma equiprobable,
    // igual que el espaciado real de los rocosos del sistema solar
    // (Mercurio..mega-Earth) en escala logaritmica.
    double logMin = std::log(ROCKY_RANDOM_MASS_MIN);
    double logMax = std::log(ROCKY_RANDOM_MASS_MAX);
    b.mass       = std::exp(logMin + (double)rnd() * (logMax - logMin));
    b.intactMass = b.mass;

    // Radio: relacion masa-radio real para rocosos de composicion tipo
    // Tierra (ver ROCKY_MR_EXPONENT).
    double massEarths = b.mass / M_EARTH;
    b.radius = R_EARTH * std::pow(massEarths, ROCKY_MR_EXPONENT);

    // Inclinacion axial: uniforme 0-180 grados -- cubre desde casi nula
    // (Mercurio, 0.03 grados) hasta retrograda extrema (Venus, 177.4
    // grados), pasando por inclinaciones moderadas (Tierra/Marte/
    // Saturno, 23-27 grados) y extremas de lado (Urano, 97.8 grados).
    b.axialTilt = rnd() * 180.0f;

    // Atmosfera: densidad 0 (Mercurio/Luna, sin atmosfera) .. 1.2 (mas
    // densa que Venus=1.0). El exponente de Fresnel del halo
    // (atmosphereFalloff) se correlaciona inversamente con la densidad
    // -- a mayor profundidad optica de columna, el halo atmosferico es
    // algo mas ancho. ANTES el rango era 1.2 (mas denso) .. 4.5 (menos
    // denso), pero con falloff=1.2 el halo de Fresnel (pow(rimFactor,
    // falloff)*density, ver drawAtmosphereShell) invade gran parte del
    // disco (a 45 grados del centro, alpha~0.41) y lava el contraste del
    // relieve/bump-mapping -- el mismo problema diagnosticado para Venus
    // (ver catalog.h). El rango ahora es 4.0 (mas denso, igual que el
    // Venus del catalogo) .. 7.0 (menos denso): incluso el extremo mas
    // denso mantiene el halo confinado cerca del limbo real.
    float atmDensity = rnd() * 1.2f;
    b.atmosphereDensity     = atmDensity;
    b.baseAtmosphereDensity = atmDensity;
    b.atmosphereFalloff     = 7.0f - std::min(1.0f, atmDensity / 1.2f) * 3.0f;

    // Color de atmosfera: tono aleatorio entre familias reales (azul
    // N2/O2 tipo Tierra, amarillo/naranja CO2-azufre tipo Venus, rojizo
    // polvo tipo Marte, grisaceo).
    float atmHue = rnd();
    float atmSat = 0.15f + rnd() * 0.55f;
    Vector3 atmRGB = HSVtoRGBVec(atmHue, atmSat, 0.65f + rnd() * 0.30f);
    b.atmosphereColor     = { (unsigned char)(atmRGB.x * 255.0f), (unsigned char)(atmRGB.y * 255.0f),
                               (unsigned char)(atmRGB.z * 255.0f), 0 };
    b.baseAtmosphereColor = b.atmosphereColor;

    // Perfil de superficie
    RockyPlanetProfile p;
    p.seed          = 4.0f + rnd() * 120.0f;
    p.hasCityLights = 0.0f;

    // Terreno: paleta de oxidos/basaltos/regolito/vegetacion (rango de
    // matiz rojo->naranja->amarillo->verde) y relieve variable, mismo
    // estilo que MakeAsteroidProfile pero con menor densidad media de
    // crateres (un planeta diferenciado sufre erosion/resurfacing,
    // a diferencia de un asteroide inerte).
    p.craterDensity    = 0.05f + rnd() * 0.50f;
    p.mountainStrength = 0.20f + rnd() * 0.80f;
    p.terrainScale     = 0.70f + rnd() * 0.80f;
    float terrHue = rnd() * 0.40f;
    float terrSat = 0.04f + rnd() * 0.36f;
    p.colorLow  = HSVtoRGBVec(terrHue, terrSat,        0.10f + rnd() * 0.30f);
    p.colorHigh = HSVtoRGBVec(terrHue, terrSat * 0.7f, 0.38f + rnd() * 0.37f);

    // Oceanos: 35% de mundos secos (waterLevel=0, como Mercurio/Marte);
    // el resto, cobertura entre 15% y 70% (Tierra=45%).
    p.waterLevel = (rnd() < 0.35f) ? 0.0f : 0.15f + rnd() * 0.55f;
    if (p.waterLevel > 0.0f) {
        float waterHue = 0.52f + rnd() * 0.12f; // azul-cian .. azul profundo (agua liquida)
        p.colorWater = HSVtoRGBVec(waterHue, 0.55f + rnd() * 0.35f, 0.15f + rnd() * 0.35f);
    } else {
        p.colorWater = HexColV3(0x404040); // sin oceanos: no se usa
    }

    // Casquetes polares: 0 = ninguno, 1 = grandes (Tierra).
    p.polarIceSize = rnd();

    // Nubes: requieren atmosfera apreciable -- 30% sin nubes
    // (atmosferas muy finas o nulas), el resto 0.10-0.85 de cobertura.
    p.cloudDensity = (atmDensity < 0.05f || rnd() < 0.30f) ? 0.0f : 0.10f + rnd() * 0.75f;
    p.cloudColor   = HSVtoRGBVec(terrHue + 0.40f + rnd() * 0.20f, 0.05f + rnd() * 0.15f, 0.85f + rnd() * 0.15f);

    b.rockyPlanet   = p;
    b.isRockyPlanet = true;

    // Inventario de volatiles (ver UpdateThermodynamics, physics.h):
    // oceanos + casquetes polares alimentan el presupuesto total; el
    // vapor inicial es proporcional a la densidad atmosferica (una
    // atmosfera mas densa parte con mas vapor en equilibrio). La
    // proporcion casquetes->iceFraction (0.12) es un punto medio entre
    // Marte (polarIceSize=0.6 -> iceFraction=0.15, ratio 0.25) y Tierra
    // (polarIceSize=1.0 -> iceFraction=0.10, ratio 0.10).
    b.iceFraction   = p.polarIceSize * 0.12f;
    b.vaporFraction = 0.02f * atmDensity;
    b.volatileBudget = ClampF(p.waterLevel + b.iceFraction + b.vaporFraction, 0.0f, 1.0f);
}

// Asigna el perfil de superficie procedural a un Body segun el nombre
// base del catalogo. Devuelve true si el cuerpo es un planeta rocoso/
// helado renderizado con drawRockyPlanet (en cuyo caso no lleva textura).
inline bool AssignRockyPlanetProfile(Body& b, const std::string& baseName, const GlobalTextures& tex) {
    if (baseName == "Tierra") {
        b.rockyPlanet   = MakeEarthProfile();
        b.isRockyPlanet = true;
        // Mapas reales de albedo/relieve/biomas: solo la Tierra los usa
        // (uHasSurfaceTex en el shader se activa segun estos punteros).
        b.diffuseTex  = tex.earthBase;
        b.normalTex   = tex.earthNorm;
        b.specularTex = tex.earthSpec;
        return true;
    }
    if (baseName == "Mercurio")  { b.rockyPlanet = MakeMercuryProfile();  b.isRockyPlanet = true; return true; }
    if (baseName == "Venus")     { b.rockyPlanet = MakeVenusProfile();    b.isRockyPlanet = true; return true; }
    if (baseName == "Marte")     { b.rockyPlanet = MakeMarsProfile();     b.isRockyPlanet = true; return true; }
    if (baseName == "Luna")      { b.rockyPlanet = MakeMoonProfile();     b.isRockyPlanet = true; return true; }
    if (baseName == "Ceres")     { b.rockyPlanet = MakeCeresProfile();    b.isRockyPlanet = true; return true; }
    if (baseName == "Io")        { b.rockyPlanet = MakeIoProfile();       b.isRockyPlanet = true; return true; }
    if (baseName == "Europa")    { b.rockyPlanet = MakeEuropaProfile();  b.isRockyPlanet = true; return true; }
    if (baseName == "Ganymede")  { b.rockyPlanet = MakeGanymedeProfile(); b.isRockyPlanet = true; return true; }
    if (baseName == "Calisto")   { b.rockyPlanet = MakeCallistoProfile(); b.isRockyPlanet = true; return true; }
    if (baseName == "Titan")     { b.rockyPlanet = MakeTitanProfile();   b.isRockyPlanet = true; return true; }
    if (baseName == "Triton")    { b.rockyPlanet = MakeTritonProfile();  b.isRockyPlanet = true; return true; }
    if (baseName == "Pluton")    { b.rockyPlanet = MakePlutoProfile();   b.isRockyPlanet = true; return true; }
    if (baseName == "Caronte")   { b.rockyPlanet = MakeCharonProfile();  b.isRockyPlanet = true; return true; }
    if (baseName == "Haumea")    { b.rockyPlanet = MakeHaumeaProfile();  b.isRockyPlanet = true; return true; }
    if (baseName == "Makemake")  { b.rockyPlanet = MakeMakemakeProfile(); b.isRockyPlanet = true; return true; }
    if (baseName == "Eris")      { b.rockyPlanet = MakeErisProfile();    b.isRockyPlanet = true; return true; }
    if (baseName == "Orcus")     { b.rockyPlanet = MakeOrcusProfile();   b.isRockyPlanet = true; return true; }
    if (baseName == "Quaoar")    { b.rockyPlanet = MakeQuaoarProfile();  b.isRockyPlanet = true; return true; }
    if (baseName == "Gonggong")  { b.rockyPlanet = MakeGonggongProfile(); b.isRockyPlanet = true; return true; }
    if (baseName == "Sedna")     { b.rockyPlanet = MakeSednaProfile();   b.isRockyPlanet = true; return true; }
    if (baseName == "Asteroide") {
        b.rockyPlanet = MakeAsteroidProfile((unsigned int)GetRandomValue(0, 0x7fffffff));
        b.isRockyPlanet = true;
        return true;
    }
    if (baseName == "Planeta Rocoso") {
        // Planeta rocoso aleatorio: cada spawn es un mundo distinto --
        // masa/radio, inclinacion axial, atmosfera, oceanos, casquetes
        // polares y nubes cubren todo el rango fisico real de un
        // rocoso diferenciado (ver RandomizeRockyPlanet arriba).
        RandomizeRockyPlanet(b, (unsigned int)GetRandomValue(0, 0x7fffffff));
        return true;
    }
    return false;
}
