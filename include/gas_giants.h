#pragma once
#include <string>
#include "raylib.h"
#include "body.h"

// ============================================================
//  Perfiles atmosfericos de gigantes gaseosos/helados
//  Cada preset alimenta el shader procedural multicapa
//  (drawGasGiant en shaders.h) con su propia paleta y
//  parametros de bandas/turbulencia/tormentas.
// ============================================================

// Convierte un color hexadecimal 0xRRGGBB a Vector3 (0..1)
inline Vector3 HexColV3(unsigned int hex) {
    return {
        ((hex >> 16) & 0xFF) / 255.0f,
        ((hex >> 8)  & 0xFF) / 255.0f,
        ( hex        & 0xFF) / 255.0f
    };
}

// Convierte HSV (h,s,v en 0..1) a Vector3 RGB (0..1)
inline Vector3 HSVtoRGBVec(float h, float s, float v) {
    Color c = ColorFromHSV(fmodf(h, 1.0f) * 360.0f, s, v);
    return { c.r/255.0f, c.g/255.0f, c.b/255.0f };
}

// Convierte Vector3 RGB (0..1) a Color (0..255)
inline Color Vec3ToColor(Vector3 c, unsigned char a = 255) {
    return { (unsigned char)(ClampF(c.x, 0.0f, 1.0f) * 255.0f),
             (unsigned char)(ClampF(c.y, 0.0f, 1.0f) * 255.0f),
             (unsigned char)(ClampF(c.z, 0.0f, 1.0f) * 255.0f), a };
}

// ── Jupiter: tonos terrosos calidos pastel, Gran Mancha Roja ──
inline GasGiantProfile MakeJupiterProfile() {
    GasGiantProfile p;
    p.bandCount          = 9.0f;
    p.bandStrength       = 1.0f;
    p.turbulenceStrength = 0.85f;
    p.jetStreamStrength  = 0.90f;
    p.stormFrequency     = 0.10f;
    p.stormSize          = 1.0f;
    p.cloudContrast      = 0.60f;
    p.colorVariance      = 0.25f;
    p.seed               = 1.0f;
    p.bandColors[0] = HexColV3(0x9C7C63); // terracota oscuro (bandas oscuras)
    p.bandColors[1] = HexColV3(0xBAA188); // tostado medio
    p.bandColors[2] = HexColV3(0xC5B4A1); // tostado claro
    p.bandColors[3] = HexColV3(0xD9CDBB); // crema palido
    p.bandColors[4] = HexColV3(0xE6E0D5); // blanco calido
    p.highCloudColor = HexColV3(0xE6E0D5);
    p.hasMajorStorm    = true;
    p.majorStormLat    = -0.32f;
    p.majorStormLon    = 1.2f;
    p.majorStormSize   = 0.22f;
    p.majorStormColor  = HexColV3(0xA65E42); // mancha roja
    p.majorStormBorder = HexColV3(0xE6E0D5); // borde claro
    return p;
}

// ── Saturno: aspecto difuso/neblinoso pero con tono dorado visible ──
inline GasGiantProfile MakeSaturnProfile() {
    GasGiantProfile p;
    p.bandCount          = 7.0f;
    // bandStrength/cloudContrast subidos (0.35->0.55, 0.18->0.35): las
    // imagenes reales de Cassini muestran bandas y zonas claramente
    // visibles (mas sutiles que Jupiter, pero lejos de la bola casi lisa
    // que daban los valores anteriores).
    p.bandStrength       = 0.55f;
    p.turbulenceStrength = 0.15f;
    p.jetStreamStrength  = 0.40f;
    p.stormFrequency     = 0.02f;
    p.stormSize          = 0.8f;
    p.cloudContrast      = 0.35f;
    p.colorVariance      = 0.08f;
    p.seed               = 2.0f;
    p.bandColors[0] = HexColV3(0xB4975A); // dorado oscuro
    p.bandColors[1] = HexColV3(0xC6A664); // dorado medio
    p.bandColors[2] = HexColV3(0xD7B576); // dorado claro
    p.bandColors[3] = HexColV3(0xE4C78B); // crema dorado
    p.bandColors[4] = HexColV3(0xF0D69A); // crema claro
    p.highCloudColor = HexColV3(0xFBEFCE);
    p.hasMajorStorm = false;
    return p;
}

// ── Urano: gigante de hielo, bola casi uniforme, absorbe el rojo, turbulencia minima ──
inline GasGiantProfile MakeUranusProfile() {
    GasGiantProfile p;
    p.iceGiant           = true;
    p.bandCount          = 4.0f;
    // bandStrength/cloudContrast subidos un poco (0.20->0.30, 0.08->0.15):
    // Urano sigue siendo el gigante MAS uniforme de los 4 (asi es en la
    // realidad, por la neblina de metano), pero con los valores anteriores
    // quedaba completamente liso/sin estructura -- Voyager 2/Hubble/JWST
    // muestran bandas tenues y alguna tormenta brillante ocasional.
    p.bandStrength       = 0.30f;
    p.turbulenceStrength = 0.02f;
    p.jetStreamStrength  = 0.08f;
    // stormFrequency pequena (antes 0.0, sin tormentas nunca): Urano tiene
    // tormentas brillantes ocasionales (p.ej. la de 2014 observada por
    // Hubble/Keck), poco frecuentes pero no inexistentes.
    p.stormFrequency     = 0.015f;
    p.stormSize          = 0.5f;
    p.cloudContrast      = 0.15f;
    p.colorVariance      = 0.04f;
    p.seed               = 3.0f;
    p.bandColors[0] = HexColV3(0x279BC2); // azul hielo oscuro
    p.bandColors[1] = HexColV3(0x3EB4DD); // azul hielo medio
    p.bandColors[2] = HexColV3(0x51C2E8); // cian hielo
    p.bandColors[3] = HexColV3(0x5FC5E6); // cian hielo claro
    p.bandColors[4] = HexColV3(0x7BD3F0); // cian hielo muy claro
    p.highCloudColor = HexColV3(0xC9ECF7);
    p.hasMajorStorm = false;
    return p;
}

// ── Neptuno: gigante de hielo, azul cobalto vibrante casi solido,
//    cirros de metano esporadicos y Gran Mancha Oscura (ovalo relleno) ──
inline GasGiantProfile MakeNeptuneProfile() {
    GasGiantProfile p;
    p.iceGiant           = true;
    p.bandCount          = 6.0f;
    p.bandStrength       = 0.35f;  // bandas ecuatoriales muy sutiles
    p.turbulenceStrength = 0.45f;
    p.jetStreamStrength  = 1.0f;
    p.stormFrequency     = 0.06f;
    p.stormSize          = 0.9f;
    p.cloudContrast      = 0.45f;  // controla el brillo de los cirros de metano
    p.colorVariance      = 0.08f;  // casi azul solido, ligeras variaciones de tono
    p.seed               = 4.0f;
    p.bandColors[0] = HexColV3(0x081A4D); // azul marino/cobalto muy oscuro
    p.bandColors[1] = HexColV3(0x0D33CC); // cobalto vibrante (azur profundo)
    p.bandColors[2] = HexColV3(0x123FE0); // cobalto-azur
    p.bandColors[3] = HexColV3(0x2659F2); // azur
    p.bandColors[4] = HexColV3(0x4A7BFF); // azur claro
    p.highCloudColor = HexColV3(0xFFFFFF); // cirros de metano blancos
    p.hasMajorStorm    = true;
    // La Gran Mancha Oscura real (Voyager 2, 1989) estaba en el
    // HEMISFERIO SUR (~-30 grados de latitud), no en el norte -- el signo
    // estaba invertido (quedaba en +0.28, hemisferio norte). Calibrado con
    // la misma escala que Jupiter (majorStormLat=-0.32 para -22 grados).
    p.majorStormLat    = -0.45f;
    p.majorStormLon    = -0.8f;
    p.majorStormSize   = 0.20f;
    p.majorStormColor  = HexColV3(0x040A1F); // Gran Mancha Oscura: casi negro azulado
    p.majorStormBorder = HexColV3(0xFFFFFF); // nubes de metano en el borde sur
    return p;
}

// ── Gigante procedural: paleta y parametros generados desde una semilla ──
inline GasGiantProfile MakeProceduralGasGiantProfile(unsigned int seed) {
    GasGiantProfile p;
    unsigned int s = seed;
    auto rnd = [&s]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1u << 24);
    };

    p.bandCount          = 4.0f  + rnd() * 8.0f;
    p.bandStrength       = 0.25f + rnd() * 1.0f;
    p.turbulenceStrength = 0.15f + rnd() * 0.85f;
    p.jetStreamStrength  = 0.20f + rnd() * 0.90f;
    p.stormFrequency     = rnd() * 0.18f;
    p.stormSize          = 0.6f  + rnd() * 0.8f;
    p.cloudContrast      = 0.20f + rnd() * 0.55f;
    p.colorVariance      = 0.10f + rnd() * 0.35f;
    p.seed               = (float)(seed % 1000) * 0.01f;
    p.iceGiant           = rnd() > 0.55f;

    // Paleta tonal coherente: un matiz base + variaciones de luminosidad.
    // Los gigantes de hielo usan un matiz azul/cobalto vibrante y bandas
    // mucho mas sutiles (ver shader: ggIceGiant comprime paletteT).
    float hue, sat;
    if (p.iceGiant) {
        hue = 0.58f + rnd() * 0.12f; // azul-cian a azul-cobalto profundo
        sat = 0.55f + rnd() * 0.35f; // vibrante, no grisaceo
        p.bandStrength  *= 0.35f;
        p.colorVariance *= 0.35f;
    } else {
        hue = rnd();
        sat = 0.20f + rnd() * 0.45f;
    }
    for (int i = 0; i < 5; i++) {
        float val = 0.40f + (float)i * 0.135f + (rnd() - 0.5f) * 0.05f;
        float h   = hue + (rnd() - 0.5f) * 0.05f;
        p.bandColors[i] = HSVtoRGBVec(h, sat, ClampF(val, 0.05f, 1.0f));
    }
    p.highCloudColor = HSVtoRGBVec(hue, sat * 0.35f, 0.97f);

    p.hasMajorStorm = rnd() > 0.45f;
    if (p.hasMajorStorm) {
        p.majorStormLat    = (rnd() - 0.5f) * 1.2f;
        p.majorStormLon    = (rnd() - 0.5f) * 6.28318f;
        p.majorStormSize   = 0.10f + rnd() * 0.18f;
        if (p.iceGiant) {
            // Gran Mancha Oscura: ovalo casi negro/azul marino + cirros
            // de metano blancos en su borde sur (ver drawGasGiant).
            p.majorStormColor  = HSVtoRGBVec(hue, sat, 0.06f);
            p.majorStormBorder = HexColV3(0xFFFFFF);
        } else {
            p.majorStormColor  = HSVtoRGBVec(fmodf(hue + 0.45f + rnd() * 0.15f, 1.0f), 0.45f + rnd()*0.3f, 0.50f);
            p.majorStormBorder = p.bandColors[4];
        }
    }
    return p;
}

// ── Relacion masa-radio de gigantes gaseosos/helados ──
// Interpolacion log-log por tramos, anclada en cuerpos reales (Neptuno,
// Saturno, Jupiter) mas un cuarto punto que captura el aplanamiento del
// radio para masas "super-Jupiter": mas alla de ~1 M_Jup, la presion de
// degeneracion electronica del interior comprime el planeta casi tanto
// como lo expande la gravedad creciente, asi que el radio deja de crecer
// e incluso decrece levemente (Hatzes & Rauer 2015; Fortney et al. 2007).
// Un "super-Jupiter" de 5 M_Jup es por tanto MUCHO mas masivo/denso que
// Jupiter pero de tamano visual similar (ligeramente menor) -- es el
// comportamiento fisico real, no un error de la formula.
inline double GasGiantRadiusFromMass(double massKg) {
    struct Anchor { double mass, radius; };
    static const Anchor anchors[] = {
        {1.024e26,           2.4622e7},          // Neptuno
        {5.683e26,           5.8232e7},          // Saturno
        {M_JUPITER,          R_JUPITER},         // Jupiter
        {5.0 * M_JUPITER,    0.94 * R_JUPITER},  // ~5 M_Jup: leve disminucion por degeneracion
    };
    constexpr int n = (int)(sizeof(anchors) / sizeof(anchors[0]));

    double logM = std::log(std::max(massKg, 1.0));

    int seg = n - 2;
    for (int i = 0; i < n - 1; ++i) {
        if (logM <= std::log(anchors[i + 1].mass)) { seg = i; break; }
    }

    double logM0 = std::log(anchors[seg].mass),     logM1 = std::log(anchors[seg + 1].mass);
    double logR0 = std::log(anchors[seg].radius),   logR1 = std::log(anchors[seg + 1].radius);
    double t = (logM - logM0) / (logM1 - logM0);
    return std::exp(logR0 + t * (logR1 - logR0));
}

// ── Gigante gaseoso/helado aleatorio: masa, radio, inclinacion axial ──
// Masa log-uniforme entre "mini-Neptuno" y "super-Jupiter" (ver
// GASGIANT_RANDOM_MASS_MIN/MAX en constants.h); el radio se deriva de la
// masa con GasGiantRadiusFromMass (arriba). Inclinacion axial uniforme
// 0-180 grados, igual rango que los rocosos (Urano=97.77 es un ejemplo
// real de inclinacion "de lado").
inline void RandomizeGasGiantPhysics(Body& b, unsigned int seed) {
    unsigned int s = seed;
    auto rnd = [&s]() -> float {
        s = s * 1664525u + 1013904223u;
        return (float)(s >> 8) / (float)(1u << 24);
    };

    double logMin = std::log(GASGIANT_RANDOM_MASS_MIN);
    double logMax = std::log(GASGIANT_RANDOM_MASS_MAX);
    b.mass       = std::exp(logMin + (double)rnd() * (logMax - logMin));
    b.intactMass = b.mass;
    b.radius     = GasGiantRadiusFromMass(b.mass);
    b.axialTilt  = rnd() * 180.0f;
}

// Asigna el perfil atmosferico procedural a un Body segun el nombre base
// del catalogo. Devuelve true si el cuerpo es un gigante gaseoso/helado
// renderizado con el shader procedural (en cuyo caso no debe llevar textura).
inline bool AssignGasGiantProfile(Body& b, const std::string& baseName) {
    if (baseName == "Jupiter") { b.gasGiant = MakeJupiterProfile(); b.isGasGiant = true; return true; }
    if (baseName == "Saturno") { b.gasGiant = MakeSaturnProfile();  b.isGasGiant = true; return true; }
    if (baseName == "Urano")   { b.gasGiant = MakeUranusProfile();  b.isGasGiant = true; return true; }
    if (baseName == "Neptuno") { b.gasGiant = MakeNeptuneProfile(); b.isGasGiant = true; return true; }
    if (baseName == "Gigante Procedural") {
        b.gasGiant = MakeProceduralGasGiantProfile((unsigned int)GetRandomValue(0, 0x7fffffff));
        b.isGasGiant = true;
        RandomizeGasGiantPhysics(b, (unsigned int)GetRandomValue(0, 0x7fffffff));
        return true;
    }
    return false;
}
