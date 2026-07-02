#pragma once
#include <cmath>
#include <algorithm>
#include "body.h"
#include "constants.h"
#include "math_utils.h"
// NOTA: NO incluir physics.h aquí (circular dep). SpawnSupernovaEjecta se
// define en physics.h (donde vive toda la infraestructura de DustParticle).

// ============================================================
//  Sistema de evolución estelar
//  Calcula ciclo de vida, pulsaciones y propiedades físicas
//  de estrellas según su masa, metalicidad y edad simulada.
//
//  Timescales REALES: el Sol tarda ~3×10^17 s en salir de MS.
//  El usuario controla la fase/edad desde el inspector (GUI).
//  La evolución automática avanza con TIME_STEP pero es
//  imperceptiblemente lenta sin intervención manual.
// ============================================================

// ── Tiempo de secuencia principal (segundos simulados) ──────
inline double StellarMSLifetime(double massKg, float metallicityZ = 0.014f) {
    double mRatio = std::max(0.08, massKg / M_SUN);
    double tBase  = 1.0e10 * 3.156e7 * std::pow(mRatio, -2.5);
    // Baja metalicidad → estrella más eficiente → vida ligeramente más larga
    double zFactor = 1.0 + 0.15 * (0.014f - std::max(0.001f, metallicityZ)) / 0.014f;
    return tBase * zFactor;
}

// ── Curva de luz de supernova (cinematográfica) ──────────────
// Subida rápida hasta pico, decaimiento exponencial suave.
inline double SupernovaLightCurve(double x) {
    auto smoothstep3 = [](double e0, double e1, double v) -> double {
        double t = std::max(0.0, std::min(1.0, (v - e0) / (e1 - e0)));
        return t * t * (3.0 - 2.0 * t);
    };
    double rise  = smoothstep3(0.0, 0.08, x);
    double decay = std::exp(-6.0 * std::max(0.0, x - 0.08));
    return rise * decay;
}

// ── Fórmula de Eggleton para el radio del lóbulo de Roche ───
// q = M_donante / M_acretora, a = separación binaria (metros)
// Devuelve: R_L (metros), con precisión ~1% en todo q ∈ (0, ∞)
inline double EggletonsRocheLobe(double q, double separationM) {
    double q23 = std::pow(std::max(1e-9, q), 2.0 / 3.0);
    double rl  = 0.49 * q23 / (0.6 * q23 + std::log(1.0 + std::pow(std::max(1e-9, q), 1.0 / 3.0)));
    return rl * separationM;
}

// ── Propiedades target por fase ──────────────────────────────
struct StellarTarget {
    double radiusFactor;    // multiplicador respecto a MS nominal
    double luminosityW;     // Watts (0 = usar fórmula de masa)
    double temperature;     // K (0 = dejar que Stefan-Boltzmann calcule)
};

inline StellarTarget GetStellarPhaseTarget(const Body& b) {
    double mRatio  = std::max(0.08, b.initialStellarMass / M_SUN);
    double msRadius = R_SUN * std::pow(mRatio, 0.8); // relación masa-radio MS
    double msLum    = L_SUN * std::pow(mRatio, 3.5);
    // T^4 de secuencia principal (Stefan-Boltzmann, a partir de msLum/msRadius).
    // Varias fases de abajo la usan para RESOLVER la luminosidad exacta que
    // produce una temperatura objetivo fisicamente razonable, en vez de un
    // multiplicador de luminosidad independiente del radio -- la T que
    // realmente importa es la que recalcula main.cpp cada frame desde
    // luminosity/radius (Stefan-Boltzmann), NO el campo 'temperature' de
    // este struct (ignorado para toda fase con b.isStar==true); multiplicar
    // radio y luminosidad por separado sin esta restriccion podia dar T
    // autoconsistentes muy distintas del objetivo documentado (ver fixes
    // de RED_GIANT/PLANETARY_NEBULA mas abajo).
    double msT4 = msLum / (4.0 * PI_D * SIGMA * msRadius * msRadius);

    switch (b.stellarPhase) {
    case StellarPhase::PROTOSTAR:
        return { 5.0, msLum * 2.0, 0.0 };

    case StellarPhase::MAIN_SEQUENCE:
        return { 1.0, msLum, 0.0 };

    case StellarPhase::SUBGIANT:
        // ANTES: lFac=10 con rFac=3 daba T autoconsistente ~1.03x la de
        // MS (mas CALIENTE) -- al reves de un subgigante real (se enfria
        // ~10-15% al expandirse cruzando el hueco de Hertzsprung). lFac=5
        // da ~0.86x T_MS, calibrado contra la trayectoria evolutiva real
        // del Sol (subgigante: R~2-3 R☉, L~2-5 L☉, T~5000-5300K).
        return { 3.0, msLum * 5.0, 0.0 };

    case StellarPhase::RED_GIANT: {
        // Radio 50–200× MS según masa; más masiva → más expandida.
        // ANTES: lFac=100-900x (independiente del radio) daba T
        // autoconsistente de hasta ~1859K en el extremo de baja masa
        // (demasiado frio, ni siquiera una M-dwarf esta tan fria) --
        // la T real de una gigante roja es bastante insensible a la
        // masa (~3000-4500K en la mayoria de casos), asi que aqui se
        // RESUELVE lFac para fijar la T autoconsistente en ese rango,
        // en vez de dejar que derive de dos multiplicadores sueltos.
        double rFac    = 50.0 + 150.0 * std::min(1.0, (mRatio - 0.5) / 7.5);
        double targetT = 3800.0;
        double lFac    = (rFac * rFac) * std::pow(targetT, 4.0) / msT4;
        return { rFac, msLum * lFac, targetT };
    }
    case StellarPhase::SUPERGIANT: {
        // ANTES: lFac llegaba a 1e4-1e5, multiplicando 'msLum' que YA
        // escala como M^3.5 (osea, ya es grande para estrellas masivas)
        // -- para 16 M☉ eso daba ~4x10^8 L☉, cuando una supergigante
        // real de esa masa (p.ej. Betelgeuse, ~16-19 M☉, R≈764 R☉,
        // T≈3600K) tiene solo ~1.26x10^5 L☉ (verificado con Stefan-
        // Boltzmann: L≈4πσR²T⁴). msLum ya captura el escalado fuerte
        // con la masa; el factor de fase solo debe representar el
        // incremento MODESTO post-MS (decenas/cientos de veces el
        // radio, unas pocas veces la luminosidad de MS), no miles.
        // Calibrado para que mRatio≈16 caiga cerca de Betelgeuse.
        double t    = std::min(1.0, (mRatio - 8.0) / 50.0);
        double rFac = 60.0 + 60.0 * t;  // ~60-120x el radio de MS
        double lFac = 5.0  + 10.0 * t;  // ~5-15x la luminosidad de MS
        return { rFac, msLum * lFac, 5000.0 };
    }
    case StellarPhase::HELIUM_FLASH: {
        // Ignicion del helio en el nucleo degenerado: la estrella se
        // contrae bruscamente desde gigante roja (~50-200 R☉) a una
        // "estrella gigante pequena" caliente. Aunque el "flash" real
        // dura solo unos miles de anios (inapreciable a escala cosmica),
        // en la simulacion esta fase sirve como transicion visual clara
        // de la contraccion post-gigante roja. 8-15 R☉ para masa solar.
        double rFac    = 8.0 + 7.0 * std::min(1.0, (mRatio - 0.5) / 3.0);
        double targetT = 4700.0; // 4500-5000 K
        double lFac    = (rFac * rFac) * std::pow(targetT, 4.0) / msT4;
        return { rFac, msLum * lFac, targetT };
    }
    case StellarPhase::HORIZONTAL_BRANCH: {
        // Fusion estable de helio en nucleo + hidrogeno en capa.
        // Pseudoestable visualmente (~100 Myr); algo mas caliente y
        // pequena que la gigante roja, similar al flash de helio.
        double rFac    = 8.0 + 4.0 * std::min(1.0, (mRatio - 0.5) / 3.0);
        double targetT = 5000.0; // 4800-5200 K
        double lFac    = (rFac * rFac) * std::pow(targetT, 4.0) / msT4;
        return { rFac, msLum * lFac, targetT };
    }
    case StellarPhase::AGB: {
        // Radio ABSOLUTO en R☉ (no multiplicado por msRadius). El bug
        // anterior usaba rFac × msRadius (que escala con la masa MS),
        // dando 1400+ R☉ para una AGB de 7 M☉ -- territorio de
        // hipergigante real, absurdo para una estrella de masa
        // intermedia. Con radio absoluto: la AGB de 5 M☉ cae en
        // ~250 R☉, similar a la de 1 M☉ (~200 R☉), mucho mas real.
        //
        // Ademas, las AGB "respiran" incluso antes de los pulsos
        // termicos (Miras tipo I) -- la estrella oscila ±20-25 R☉
        // alrededor del radio base en cada ciclo de pulsacion (el mismo
        // pulsationPhase que ya actualiza StellarPulsationUpdate cada
        // frame para el brillo), haciendo que se vea "viva".
        double puls       = 0.5 + 0.5 * std::sin(b.pulsationPhase);
        double baseRsol   = 180.0 + 70.0 * std::min(1.0, (mRatio - 0.5) / 4.5);
        double targetRsol = baseRsol + 40.0 * puls; // respira ~±20 R☉
        double targetR    = targetRsol * R_SUN;
        double rFac       = targetR / msRadius; // rFac = absolute/msRadius
        double targetT    = 3000.0;
        double targetL    = 4.0 * PI_D * SIGMA * targetR * targetR * std::pow(targetT, 4.0);
        return { rFac, targetL, targetT };
    }
    case StellarPhase::THERMAL_PULSES: {
        // Radio absoluto oscilante con amplitud MAYOR que AGB (la estrella
        // esta a punto de perder las capas externas). Mismo bug que AGB
        // corregido: rFac ahora apunta a un radio absoluto de ~220-380 R☉
        // independientemente de la masa de la estrella.
        double puls       = 0.5 + 0.5 * std::sin(b.pulsationPhase);
        double baseRsol   = 220.0 + 80.0 * std::min(1.0, (mRatio - 0.5) / 4.5);
        double targetRsol = baseRsol + 80.0 * puls; // oscila ~±40 R☉
        double targetR    = targetRsol * R_SUN;
        double rFac       = targetR / msRadius;
        double targetT    = 3000.0;
        double targetL    = 4.0 * PI_D * SIGMA * targetR * targetR * std::pow(targetT, 4.0);
        return { rFac, targetL, targetT };
    }
    case StellarPhase::NEUTRON_STAR: {
        // Radio: preservar el del cuerpo (igual que PULSAR/MAGNETAR) para que
        // los NS de catalogo con radio real (p.ej. 12 km) no sean reescalados
        // hacia el valor generico de 11 km por el lerp de ApplyStellarPhaseProperties.
        double R_NS    = std::max(10000.0, b.radius > 0.0 ? b.radius : 12000.0);
        double rFac    = R_NS / msRadius;
        double targetT = std::max(600000.0, b.temperature > 0.0 ? b.temperature : 600000.0);
        double targetL = 4.0 * PI_D * SIGMA * R_NS * R_NS * std::pow(targetT, 4.0);
        return { rFac, targetL, targetT };
    }
    case StellarPhase::BLACK_HOLE: {
        constexpr double c = 3.0e8;
        double R_BH = std::max(5000.0, 2.0 * G * b.mass / (c * c));
        double rFac = R_BH / msRadius;
        return { rFac, 0.0, 0.0 };
    }
    case StellarPhase::PULSAR: {
        // Targets FIJOS: evita que max(600000, b.temperature) deje al pulsar
        // bloqueado a la temperatura residual de la SN (~90 millones K) para siempre.
        // La temperatura irá convergiendo desde el estado inicial (caliente) hacia
        // 1.9 MK via lerp — enfriamiento suave, no salto instantáneo.
        constexpr double R_PS  = 11000.0;  // 11 km
        constexpr double T_PS  = 1.9e6;    // ~2 millones K (pulsar joven)
        double rFac   = R_PS / msRadius;
        double targetL = 4.0 * PI_D * SIGMA * R_PS * R_PS * std::pow(T_PS, 4.0);
        return { rFac, targetL, T_PS };
    }
    case StellarPhase::MAGNETAR: {
        constexpr double R_MG  = 12000.0;  // 12 km
        constexpr double T_MG  = 8.0e8;    // campo extremo, muy caliente
        double rFac   = R_MG / msRadius;
        double targetL = 4.0 * PI_D * SIGMA * R_MG * R_MG * std::pow(T_MG, 4.0);
        return { rFac, targetL, T_MG };
    }
    case StellarPhase::PLANETARY_NEBULA: {
        // Núcleo caliente expuesto: radio terrestre, temperatura altísima.
        // ANTES: lFac=0.1 fijo daba T autoconsistente de solo ~34,000K
        // para una ex-estrella tipo Sol (3x mas frio que el objetivo
        // documentado de 100,000K; recien a mRatio~8 se acercaba) --
        // se RESUELVE lFac para garantizar el objetivo real sin
        // importar la masa, igual que en RED_GIANT arriba.
        double rFac    = 6.371e6 / R_SUN;
        double targetT = 100000.0;
        double lFac    = (rFac * rFac) * std::pow(targetT, 4.0) / msT4;
        return { rFac, msLum * lFac, targetT };
    }
    case StellarPhase::BLACK_DWARF: {
        // Enana blanca completamente enfriada: mismo radio compacto,
        // temperatura y luminosidad practicamente nulas. Hipotetica.
        double rFac    = 6.371e6 / R_SUN; // mismo radio que enana blanca
        double Rbd     = R_SUN * rFac;
        double targetT = 800.0; // muy fria, casi apagada
        double targetL = 4.0 * PI_D * SIGMA * Rbd * Rbd * std::pow(targetT, 4.0);
        return { rFac, targetL, targetT };
    }

    case StellarPhase::SUPERNOVA:
        // Shell expansiva: valores base antes/entre explosiones. La
        // luminosidad REAL durante la explosion activa la fija
        // directamente el switch de UpdateStellarEvolution (curva
        // cinematica de snPeak) -- ApplyStellarPhaseProperties ahora
        // hace return temprano para esta fase y NUNCA llega a usar
        // este target (ver mas abajo), asi que estos valores solo
        // importan como fallback si algo mas consulta este target.
        return { 1.0, msLum, 0.0 };

    case StellarPhase::WHITE_DWARF: {
        // Radio fijo (tipo Tierra). ANTES, T y L se enfriaban con DOS
        // formulas independientes: T a razon de 1e-6 K/s (llegaba al
        // piso de 4000K en solo ~2400 anios simulados, absurdamente
        // rapido) y L a escala de 3 Gyr -- totalmente desacopladas. Y
        // como la T REAL (recalculada cada frame en main.cpp desde
        // L/R via Stefan-Boltzmann) depende de L, no de este campo
        // 'temperature', la enana blanca en la practica solo se
        // enfriaba hasta ~19,000K (el piso de la formula de L) y NUNCA
        // se acercaba al objetivo documentado de 4000K. Ahora se
        // define la curva de T deseada en escala de Gyr (realista:
        // ~80,000K -> ~4,000K en ~10 Gyr, igual que enanas blancas
        // reales) y se RESUELVE L con Stefan-Boltzmann para que
        // coincida exactamente.
        double rFac    = 6.371e6 / R_SUN;
        double Rwd     = R_SUN * rFac;
        double ageGyr  = b.stellarPhaseAge / 3.156e16; // 1 Gyr = 3.156e16 s
        double targetT = std::max(4000.0, 80000.0 - ageGyr * 7600.0);
        double targetL = 4.0 * PI_D * SIGMA * Rwd * Rwd * std::pow(targetT, 4.0);
        return { rFac, targetL, targetT };
    }

    case StellarPhase::SUPERNOVA_REMNANT:
        return { 0.0001, L_SUN * 0.0001, 10000.0 };

    case StellarPhase::BLUE_DWARF: {
        // Hipotetico: tras fusionar TODO su hidrogeno (mezclado por
        // conveccion total), se contrae Y se calienta MUCHO -- por algo
        // se llama "enana AZUL": tiene que verse realmente azul, no solo
        // tibia. b.temperature se recalcula cada frame en main.cpp via
        // Stefan-Boltzmann a partir de luminosity/radius (ignora
        // cualquier valor explicito de 'temperature' aqui), asi que para
        // garantizar la T deseada se fija un radio objetivo y se
        // RESUELVE la luminosidad necesaria con L = 4*pi*sigma*R²*T⁴
        // para que ese recalculo posterior coincida con el objetivo.
        double rFac = 0.4;
        double Rbd  = msRadius * rFac;
        double Tbd  = 22000.0; // azul-blanco, tipo B
        double Lbd  = 4.0 * PI_D * SIGMA * Rbd * Rbd * (Tbd * Tbd * Tbd * Tbd);
        return { rFac, Lbd, Tbd };
    }

    default:
        return { 1.0, msLum, 0.0 };
    }
}

// ── Actualizar composición estelar estimada ──────────────────
// Usa b.atmospheric_composition para que la GUI existente la muestre.
inline void UpdateStellarComposition(Body& b) {
    double mRatio = std::max(0.08, b.initialStellarMass / M_SUN);
    double tMS    = StellarMSLifetime(b.initialStellarMass, b.metallicityZ);
    double ageFrac = ClampD(b.stellarAge / tMS, 0.0, 1.0);

    auto& c = b.atmospheric_composition;
    c.clear();

    switch (b.stellarPhase) {
    case StellarPhase::PROTOSTAR:
    case StellarPhase::MAIN_SEQUENCE:
    case StellarPhase::SUBGIANT: {
        float H  = (float)(0.73f * (1.0 - ageFrac * 0.45));
        float He = 1.0f - H - 0.02f;
        c["Hidrogeno"] = H;
        c["Helio"]     = He;
        c["Metales"]   = 0.02f;
        break;
    }
    case StellarPhase::RED_GIANT:
        c["Helio_nucleo"]      = 0.60f;
        c["Hidrogeno_capa"]    = 0.35f;
        c["Carbono_Oxigeno"]   = 0.05f;
        break;
    case StellarPhase::SUPERGIANT:
        c["Oxigeno"]    = 0.40f;
        c["Neon"]       = 0.20f;
        c["Silicio"]    = 0.20f;
        c["Hierro"]     = 0.15f;  // capas cebolla, valor promedio global
        c["Otros"]      = 0.05f;
        break;
    case StellarPhase::PLANETARY_NEBULA:
        c["Carbono"]    = 0.50f;
        c["Oxigeno"]    = 0.40f;
        c["Nitrogeno"]  = 0.10f;
        break;
    case StellarPhase::HELIUM_FLASH:
        c["Helio_nucleo"]   = 0.70f;
        c["Hidrogeno_capa"] = 0.25f;
        c["Carbono"]        = 0.05f;
        break;
    case StellarPhase::HORIZONTAL_BRANCH:
        c["Helio_nucleo"]   = 0.60f;
        c["Hidrogeno_capa"] = 0.30f;
        c["Carbono"]        = 0.08f;
        c["Oxigeno"]        = 0.02f;
        break;
    case StellarPhase::AGB:
    case StellarPhase::THERMAL_PULSES:
        c["Carbono_Oxigeno"] = 0.60f;
        c["Helio_capa"]      = 0.25f;
        c["Hidrogeno_capa"]  = 0.15f;
        break;
    case StellarPhase::WHITE_DWARF:
    case StellarPhase::BLACK_DWARF:
        c["Carbono"]    = 0.50f;
        c["Oxigeno"]    = 0.50f;
        break;
    case StellarPhase::BLUE_DWARF:
        c["Helio"]      = 0.90f;
        c["Hidrogeno"]  = 0.08f;
        c["Metales"]    = 0.02f;
        break;
    case StellarPhase::NEUTRON_STAR:
    case StellarPhase::PULSAR:
        c["Neutrones"]  = 0.95f;
        c["Protones"]   = 0.05f;
        break;
    case StellarPhase::MAGNETAR:
        c["Neutrones"]  = 0.95f;
        c["Protones"]   = 0.04f;
        c["Plasma"]     = 0.01f;
        break;
    case StellarPhase::BLACK_HOLE:
        break;
    case StellarPhase::SUPERNOVA:
    case StellarPhase::SUPERNOVA_REMNANT:
        c["Oxigeno"]    = 0.30f;
        c["Neon"]       = 0.15f;
        c["Magnesio"]   = 0.12f;
        c["Silicio"]    = 0.15f;
        c["Azufre"]     = 0.08f;
        c["Calcio"]     = 0.05f;
        c["Hierro"]     = 0.15f;
        break;
    default: break;
    }
}

// ── Fases sin llamaradas: fusión terminada o evento de muerte ────────────
// Objetos compactos y enanas (sin convección/campo magnético renovado que
// sostenga arcos) + el propio evento de supernova (las flares deben
// reabsorberse durante la explosión). Al entrar en cualquiera de estas, las
// llamaradas vivas ejecutan su animación de muerte (ver flareDeathProgress).
inline bool StarPhaseHasNoFlares(StellarPhase phase) {
    switch (phase) {
    case StellarPhase::SUPERNOVA:
    case StellarPhase::WHITE_DWARF:
    case StellarPhase::BLACK_DWARF:
    case StellarPhase::NEUTRON_STAR:
    case StellarPhase::PULSAR:
    case StellarPhase::MAGNETAR:
    case StellarPhase::BLACK_HOLE:
    case StellarPhase::BLUE_DWARF:
        return true;
    default:
        return false;
    }
}

// ── Validez de fase según masa inicial Y actual (combinadas) ─────────────
// Ruta de supernova: habilitada si la estrella ALGUNA VEZ tuvo masa suficiente
// (inicial O actual) -- una estrella que ganó masa por Roche puede calificar
// aunque no naciera masiva; una que la perdió conserva la capacidad si nació
// masiva. Ruta de gigante: habilitada si en algún momento (inicial O actual)
// tuvo masa por DEBAJO del umbral de supernova -- una estrella que nació
// masiva pero perdió mucha masa puede terminar como enana blanca en vez de
// explotar. Enanas "permanentes" (ambas masas < 0.5 M☉) nunca evolucionan
// más allá de secuencia principal, ni siquiera manualmente.
inline bool IsStellarPhaseValid(const Body& b, StellarPhase phase) {
    double massHigh = std::max(b.initialStellarMass, b.mass) / M_SUN; // para ruta SN
    double massLow  = std::min(b.initialStellarMass, b.mass) / M_SUN; // para ruta gigante
    // "Enana para siempre" se decide por la masa ACTUAL, no la maxima
    // historica: usar massHigh aqui dejaba que una estrella que alguna vez
    // (por fusion o transferencia Roche) tuvo mas masa siguiera habilitada
    // para supernova/gigante PARA SIEMPRE, aunque ahora sea, a todos los
    // efectos (lo que el jugador ve y selecciona), una enana roja real --
    // de ahi el boton "Detonar Supernova" apareciendo habilitado en una
    // enana roja, sin sentido para el jugador.
    bool isDwarfForever    = (b.mass / M_SUN) < 0.5;
    bool canSupernovaRoute = !isDwarfForever && massHigh >= b.effectiveSNThreshold;
    bool canGiantRoute     = !isDwarfForever && (massLow < b.effectiveSNThreshold);

    switch (phase) {
    case StellarPhase::PROTOSTAR:
    case StellarPhase::MAIN_SEQUENCE:
        return true;
    case StellarPhase::SUBGIANT:
        return !isDwarfForever;
    // Ruta hipotetica, EXCLUSIVA de enanas rojas reales (ver enum BLUE_DWARF
    // en body.h): nunca observada, pero es el destino final teorico aceptado.
    case StellarPhase::BLUE_DWARF:
        return isDwarfForever;
    case StellarPhase::RED_GIANT:
    case StellarPhase::HELIUM_FLASH:
    case StellarPhase::HORIZONTAL_BRANCH:
    case StellarPhase::AGB:
    case StellarPhase::THERMAL_PULSES:
    case StellarPhase::PLANETARY_NEBULA:
        return canGiantRoute;
    // Enana blanca: alcanzable por la ruta de gigante real O por la ruta
    // hipotetica de enana roja (tras pasar, manualmente, por BLUE_DWARF).
    // Enana negra: mismo criterio que enana blanca (es el destino final
    // de cualquier enana blanca, independientemente de su progenitor).
    case StellarPhase::WHITE_DWARF:
    case StellarPhase::BLACK_DWARF:
        return canGiantRoute || isDwarfForever;
    case StellarPhase::SUPERGIANT:
    case StellarPhase::SUPERNOVA:
    case StellarPhase::SUPERNOVA_REMNANT:
    case StellarPhase::NEUTRON_STAR:
    case StellarPhase::BLACK_HOLE:
    case StellarPhase::PULSAR:
    case StellarPhase::MAGNETAR:
        return canSupernovaRoute;
    default:
        return false;
    }
}

// ── Fase válida más cercana (para auto-corrección) ───────────────────────
inline StellarPhase NearestValidStellarPhase(const Body& b, StellarPhase desired) {
    if (IsStellarPhaseValid(b, desired)) return desired;
    switch (desired) {
    case StellarPhase::SUBGIANT:
        return StellarPhase::MAIN_SEQUENCE;
    case StellarPhase::RED_GIANT:
    case StellarPhase::HELIUM_FLASH:
    case StellarPhase::HORIZONTAL_BRANCH:
    case StellarPhase::AGB:
    case StellarPhase::THERMAL_PULSES:
    case StellarPhase::PLANETARY_NEBULA:
    case StellarPhase::WHITE_DWARF:
    case StellarPhase::BLACK_DWARF:
        // Sin ruta de gigante disponible: intentar ruta SN, sino MS.
        if (IsStellarPhaseValid(b, StellarPhase::SUPERGIANT)) return StellarPhase::SUPERGIANT;
        return StellarPhase::MAIN_SEQUENCE;
    case StellarPhase::SUPERGIANT:
    case StellarPhase::SUPERNOVA:
    case StellarPhase::SUPERNOVA_REMNANT:
    case StellarPhase::NEUTRON_STAR:
    case StellarPhase::BLACK_HOLE:
    case StellarPhase::PULSAR:
    case StellarPhase::MAGNETAR:
        if (IsStellarPhaseValid(b, StellarPhase::RED_GIANT)) return StellarPhase::RED_GIANT;
        return StellarPhase::MAIN_SEQUENCE;
    default:
        return StellarPhase::MAIN_SEQUENCE;
    }
}

// ── Pérdida de masa por viento estelar ───────────────────────────────────
// Devuelve la tasa de pérdida de masa en kg/s (valor positivo = masa perdida).
//
// Escala temporal OBSERVABLE: calibrada para que la pérdida sea visible en
// minutos de tiempo real a velocidad máxima (x32768 ≈ 75 años/segundo real):
//   RGB:  ~40 min a x32768 para perder 5%
//   AGB:  ~18 min a x32768 para perder 20%
//   TP:   ~7  min a x32768 para perder 18% (superviento)
//   PN:   ~3  min a x32768 para perder 5%  (expulsión rápida)
//
// La escala tMS-based del intento anterior era 100-200× demasiado lenta porque
// las fases del juego duran cientos de millones de años de simulación.
inline double StellarMassLossRate(const Body& b) {
    if (!b.isStar || b.mass <= 0.0 || b.stellarManualOverride) return 0.0;

    const double M0 = std::max(0.1, b.initialStellarMass / M_SUN);
    if (b.mass <= 0.0) return 0.0;

    // lossFrac: fracción de masa_actual a perder durante la fase
    // timescale: escala temporal observable [sim-segundos]
    //   A x32768 y 60fps: 1 real-min ≈ 4500 años sim ≈ 1.42e11 sim-s
    //   → timescale 1e12 s ≈ 7 min a x32768 para perder lossFrac completo
    double lossFrac = 0.0, timescale = 1.0;

    switch (b.stellarPhase) {
    case StellarPhase::RED_GIANT:
        lossFrac = 0.05; timescale = 5.0e12; break; // ~40 min × x32768
    case StellarPhase::HELIUM_FLASH:
    case StellarPhase::HORIZONTAL_BRANCH:
        lossFrac = 0.01; timescale = 1.0e12; break;
    case StellarPhase::AGB:
        lossFrac = 0.20; timescale = 2.5e12; break; // ~18 min × x32768
    case StellarPhase::THERMAL_PULSES:
        lossFrac = 0.18; timescale = 1.0e12; break; // ~7  min × x32768
    case StellarPhase::PLANETARY_NEBULA:
        lossFrac = 0.05; timescale = 5.0e11; break; // ~3  min × x32768
    case StellarPhase::SUPERGIANT: {
        // Vientos LBV/OB: más agresivos cuanto mayor es la masa
        double boost = 1.0 + std::max(0.0, (M0 - 8.0) / 20.0);
        lossFrac  = 0.20 * boost;
        timescale = 1.0e12 / boost;
        break;
    }
    default: return 0.0; // MS, SUBGIANT, WD, NS, BH: sin pérdida apreciable
    }

    // No bajar del núcleo compacto esperado (relación inicial-final de Kalirai 2008)
    const double mWD     = std::max(0.1, 0.109 * M0 + 0.394);
    const double minMass = (M0 < b.effectiveSNThreshold) ? mWD * M_SUN : 1.4 * M_SUN;
    if (b.mass <= minMass * 1.01) return 0.0;

    // rate [kg/s] = lossFrac × masa_actual / timescale_observable
    return (lossFrac * b.mass) / timescale;
}

// ── Aplicar propiedades de la fase con interpolación suave ───
// 'dt' (segundos SIMULADOS): la tasa de convergencia hacia los valores
// objetivo de la fase se liga al tiempo simulado transcurrido, no al
// framerate real -- antes lerpAlpha era una constante POR LLAMADA
// (0.3%), y como esta funcion se llama una vez por frame sin importar
// TIME_STEP, el colapso planetaria->enana blanca o supergigante->
// supernova (y los saltos de temperatura que disparan al shader de
// llamaradas) avanzaba a la MISMA velocidad en tiempo real sin
// importar la velocidad de simulacion elegida -- bajar la velocidad
// no frenaba nada, de ahi las llamaradas "locas" en fases finales aun
// yendo lento. dt<0 (default) = snap inmediato, usado por ediciones
// manuales puntuales desde la GUI (gui.h), que no se repiten cada
// frame y por lo tanto deben aplicar el valor completo de una vez.
inline void ApplyStellarPhaseProperties(Body& b, double dt = -1.0) {
    if (!b.isStar && !b.isSupernovaRemnant) return;

    // Recalcular siempre (independiente de fase)
    b.effectiveSNThreshold = 8.0
        + (double)((b.id * 2654435761ull % 300)) / 100.0 - 1.5
        - 0.3 * (b.metallicityZ - 0.014f) / 0.014f * 8.0;
    if (b.radius > 0.0 && b.mass > 0.0) {
        double omegaCritical = std::sqrt(G * b.mass / (b.radius * b.radius * b.radius));
        double omegaActual   = (double)b.spinRateDeg * PI_D / 180.0 / 1200.0;
        b.criticalRotationFraction = (float)ClampD(omegaActual / omegaCritical, 0.0, 0.99);
    }
    UpdateStellarComposition(b);

    // Auto-corrección: si la fase actual ya no es sostenible por la masa
    // (inicial NI actual -- p.ej. perdió mucha masa por transferencia Roche),
    // reajustar a la fase válida más cercana. Corre SIEMPRE, incluso en modo
    // manual, porque la masa puede cambiar por física sin que el usuario
    // intervenga directamente.
    if (b.isStar && !IsStellarPhaseValid(b, b.stellarPhase)) {
        StellarPhase corrected = NearestValidStellarPhase(b, b.stellarPhase);
        if (corrected != b.stellarPhase) {
            b.stellarPhase    = corrected;
            b.stellarPhaseAge = 0.0;
        }
    }

    // ── MAIN_SEQUENCE: NO modificar radio ni luminosidad cada frame ──
    // El radio y luminosidad del spawn ya son correctos (valores del catálogo).
    // Si los modificamos via lerpAlpha, el radio converge a 'msRadius = R_sun * M^0.8'
    // que puede diferir del catalog radius → temperatura Stefan-Boltzmann cambia →
    // mDwarf/gDwarf en el shader de llamaradas cambia → velocidad/color de llamaradas
    // cambia incorrectamente (bug). Solo asegurar que baseLuminosity esté inicializado.
    if (b.stellarPhase == StellarPhase::MAIN_SEQUENCE) {
        if (b.baseLuminosity < 1.0) { // guard: no inicializado
            b.baseLuminosity   = b.luminosity;
            b.visualLuminosity = b.luminosity;
        }
        // REJUVENECIMIENTO: si el radio/luminosidad actuales estan MUY por
        // encima del nominal de MS, esta estrella viene de ser gigante y el
        // jugador la regreso manualmente a secuencia principal -- sin esto
        // se quedaba con el tamaño de gigante PARA SIEMPRE (este bloque
        // retornaba antes de tocar nada). Solo converge cuando esta lejos
        // (>1.3x), para no introducir drift permanente en estrellas que
        // NUNCA dejaron MS (su radio de catalogo puede diferir levemente
        // de la formula generica R_sun*M^0.8 usada aqui).
        double mRatioMS   = std::max(0.08, b.initialStellarMass / M_SUN);
        double msRadiusMS = R_SUN  * std::pow(mRatioMS, 0.8);
        double msLumMS    = L_SUN  * std::pow(mRatioMS, 3.5);
        if (b.radius > msRadiusMS * 1.3 || b.baseLuminosity > msLumMS * 1.3) {
            double lerpAlpha2 = (dt < 0.0) ? 1.0 : (1.0 - std::exp(-2.5037e-6 * dt));
            b.radius         = b.radius         + (msRadiusMS - b.radius)         * lerpAlpha2;
            b.baseLuminosity = b.baseLuminosity + (msLumMS    - b.baseLuminosity) * lerpAlpha2;
        }
        return;
    }

    // ── SUPERNOVA: NO competir con la curva de luminosidad dedicada ──
    // UpdateStellarEvolution fija b.baseLuminosity/visualLuminosity/luminosity
    // directamente con la curva cinematica (snPeak * SupernovaLightCurve)
    // momentos antes de llamar a esta funcion. Si seguiamos hasta el lerp
    // generico de abajo, este intentaba arrastrar baseLuminosity hacia
    // 'msLum' (el target de GetStellarPhaseTarget para esta fase) CADA
    // FRAME -- a velocidades de simulacion altas (TIME_STEP grande,
    // lerpAlpha hasta ~30%/frame) eso erosionaba visiblemente el pico de
    // brillo de la supernova en vez de dejar que la curva cinematica
    // dicte el brillo por completo.
    if (b.stellarPhase == StellarPhase::SUPERNOVA) return;

    StellarTarget tgt = GetStellarPhaseTarget(b);
    double mRatio    = std::max(0.08, b.initialStellarMass / M_SUN);
    double msRadius  = R_SUN * std::pow(mRatio, 0.8);

    double targetRadius = (tgt.radiusFactor > 0.0) ? msRadius * tgt.radiusFactor : b.radius;
    double targetLum    = (tgt.luminosityW  > 0.0) ? tgt.luminosityW             : b.baseLuminosity;

    // SUPERGIGANTE: el target de arriba escala con 'msRadius' (formula
    // generica de MASA, ~6-9 R☉ para 10-15 M☉) -- valido para una
    // supergigante que LLEGA a esta fase organicamente desde subgigante
    // (donde el radio de partida es chico y el target SIEMPRE es mayor,
    // osea crece). Pero las supergigantes reales del catalogo (UY Scuti,
    // Betelgeuse, Stephenson 2-18) entran a esta fase YA con su radio
    // real (700-2150 R☉), muy por ENCIMA de ese target generico -- sin
    // este piso, el lerp las iba "desinflando" poco a poco hacia el
    // target generico (p.ej. Stephenson 2-18 caia de 2150 a ~515 R☉).
    // Nunca debe ENCOGER una supergigante por debajo de su tamaño
    // actual; solo crecer si el target organico es mayor.
    if (b.stellarPhase == StellarPhase::SUPERGIANT) {
        targetRadius = std::max(targetRadius, b.radius);
        targetLum    = std::max(targetLum,    b.baseLuminosity);
    }

    // Interpolación exponencial hacia los valores target de la fase, en
    // tiempo SIMULADO: k ajustado para igualar el viejo 0.3%/frame a 1x
    // (TIME_STEP=1200) -- a otras velocidades de simulacion escala
    // proporcionalmente en vez de quedar fija en tiempo real.
    double lerpAlpha;
    if (dt < 0.0) {
        lerpAlpha = 1.0; // snap inmediato (edicion manual desde gui.h)
    } else {
        const double k = 2.5037e-6; // /s
        lerpAlpha = 1.0 - std::exp(-k * dt);
    }
    double oldRadius = b.radius;
    b.radius         = b.radius         + (targetRadius - b.radius)         * lerpAlpha;
    b.baseLuminosity = b.baseLuminosity  + (targetLum    - b.baseLuminosity) * lerpAlpha;

    // Conservación de momento angular (L = I·ω, I ∝ M·R²): sin esto, al
    // crecer 50-1000x en fases gigantes el spin quedaba fijo y
    // RotationalOblateness (∝ ω²R³, renderer.h) se disparaba al tope
    // visual (achatamiento extremo); al colapsar de vuelta (enana blanca)
    // pasaba lo opuesto. ω_nuevo = ω_viejo · (R_viejo/R_nuevo)².
    if (oldRadius > 1.0 && b.radius > 1.0) {
        double ratio = oldRadius / b.radius;
        b.spinRateDeg = (float)(b.spinRateDeg * ratio * ratio);
    }

    if (tgt.temperature > 0.0)
        b.temperature = b.temperature + (tgt.temperature - b.temperature) * lerpAlpha;
}

// ── Transición a una nueva fase ──────────────────────────────
inline void StellarTransitionTo(Body& b, StellarPhase newPhase, double dt) {
    b.stellarPhase    = newPhase;
    b.stellarPhaseAge = 0.0;
    ApplyStellarPhaseProperties(b, dt);
}

// ── Eyectar material de supernova ────────────────────────────
// Declarada aquí; DEFINIDA en physics.h (donde vive DustParticle infrastructure)
inline void SpawnSupernovaEjecta(std::vector<Body>& bodies, std::vector<DustParticle>& dust,
                                  const Body& b);
// (La definición en physics.h, después de #include "stellar_evolution.h")

// ── Expansión del remanente de supernova ─────────────────────
inline void UpdateRemnantExpansion(Body& b, double dt) {
    // El remanente sigue expandiéndose, pero mucho más lento que la SN
    // (shock desacelerado por el medio circundante)
    const double REMNANT_SPEED = 1.5e6; // ~1500 km/s (10× más lento que SN)
    b.supernovaRadius += REMNANT_SPEED * dt;
}

// ── Actualización de pulsaciones estelares ───────────────────
inline void StellarPulsationUpdate(Body& b, double dt) {
    if (!b.isStar || b.mass <= 0.0) return;

    double mRatio = std::max(0.08, b.initialStellarMass / M_SUN);
    double period = 0.0;

    if (b.stellarPhase == StellarPhase::MAIN_SEQUENCE
        && b.temperature >= 6500.0 && b.temperature <= 8500.0
        && mRatio >= 1.5 && mRatio <= 3.0) {
        // Delta Scuti / pulsadores tipo MS
        b.pulsationAmplitude = 0.03f;
        period               = 6.0 * 3600.0; // ~6 horas
    }
    else if ((b.stellarPhase == StellarPhase::SUPERGIANT ||
              b.stellarPhase == StellarPhase::SUBGIANT)
             && b.temperature >= 5000.0 && b.temperature <= 7500.0) {
        // Cefeidas: supergigantes/subgigantes cruzando banda de inestabilidad
        b.pulsationAmplitude = 0.15f;
        period               = 5.0 * 86400.0; // ~5 días (rango 1–50 días)
    }
    else if (b.stellarPhase == StellarPhase::RED_GIANT
             || b.stellarPhase == StellarPhase::AGB) {
        // Miras: gigantes rojas / AGB pulsantes
        b.pulsationAmplitude = 0.25f;
        period               = 300.0 * 86400.0; // ~300 días
    }
    else if (b.stellarPhase == StellarPhase::THERMAL_PULSES) {
        // Pulsos termicos: pulsacion MASIVA, periodo mas largo (1-3 anios).
        // La amplitud se refleja en el radio directamente via GetStellarPhaseTarget
        // (que usa pulsationPhase para oscilar entre 200 y 320 R☉).
        b.pulsationAmplitude = 0.40f;
        period               = 730.0 * 86400.0; // ~2 años por ciclo
    }
    else {
        b.pulsationAmplitude = 0.0f;
    }

    if (period > 0.0)
        b.pulsationPhase += dt * (2.0 * PI_D / period);
}

// ── Función principal de evolución estelar ───────────────────
// Llamar una vez por frame para cada estrella (y remanente).
inline void UpdateStellarEvolution(Body& b, double dt,
                                    std::vector<Body>& bodies,
                                    std::vector<DustParticle>& dust)
{
    if (!b.isStar || b.mass <= 0.0) return;

    // Recalcula umbral SN, rotación crítica, composición Y corrige la fase si
    // ya no es válida para la masa actual/inicial -- corre SIEMPRE, incluso en
    // modo manual, porque Roche/fusión pueden cambiar la masa sin que el
    // usuario lo controle directamente.
    ApplyStellarPhaseProperties(b, dt);

    if (b.stellarManualOverride) return; // edad y fase congeladas por el usuario

    b.stellarAge      += dt;
    b.stellarPhaseAge += dt;

    // Pérdida de masa por viento estelar (Reimers calibrado a duraciones del juego)
    {
        double mdot = StellarMassLossRate(b); // kg/s
        if (mdot > 0.0)
            b.mass = std::max(b.mass - mdot * dt, 0.1 * M_SUN);
    }

    double mRatio = std::max(0.08, b.initialStellarMass / M_SUN);

    // Enanas rojas y muy pequeñas no evolucionan automáticamente
    if (mRatio < 0.5) return; // propiedades ya refrescadas arriba

    double tMS = StellarMSLifetime(b.initialStellarMass, b.metallicityZ);

    switch (b.stellarPhase) {
    case StellarPhase::PROTOSTAR:
        if (b.stellarPhaseAge > tMS * 0.005)
            StellarTransitionTo(b, StellarPhase::MAIN_SEQUENCE, dt);
        break;

    case StellarPhase::MAIN_SEQUENCE:
        if (b.stellarAge >= tMS * 0.90)
            StellarTransitionTo(b, StellarPhase::SUBGIANT, dt);
        break;

    case StellarPhase::SUBGIANT:
        if (b.stellarPhaseAge > tMS * 0.05) {
            bool goSN = mRatio >= b.effectiveSNThreshold;
            StellarTransitionTo(b, goSN ? StellarPhase::SUPERGIANT : StellarPhase::RED_GIANT, dt);
        }
        break;

    case StellarPhase::RED_GIANT:
        if (b.stellarPhaseAge > tMS * 0.15) {
            // Flash de Helio solo en M < ~2.2 M☉: solo ahi el nucleo de
            // helio esta electron-degenerado al ignicion, lo que provoca
            // la inestabilidad termica explosiva caracteristica del flash.
            // Estrellas 2.2–8 M☉ encienden el helio de forma no degenerada
            // ("silenciosamente") y van directo a la Rama Horizontal.
            if (mRatio < 2.2)
                StellarTransitionTo(b, StellarPhase::HELIUM_FLASH, dt);
            else
                StellarTransitionTo(b, StellarPhase::HORIZONTAL_BRANCH, dt);
        }
        break;

    case StellarPhase::HELIUM_FLASH:
        // Brevísimo en términos reales (~1000 años) pero visible como
        // transicion de contraccion en la simulacion.
        if (b.stellarPhaseAge > tMS * 0.001)
            StellarTransitionTo(b, StellarPhase::HORIZONTAL_BRANCH, dt);
        break;

    case StellarPhase::HORIZONTAL_BRANCH:
        // ~100 Myr de fusion estable de helio.
        if (b.stellarPhaseAge > tMS * 0.01)
            StellarTransitionTo(b, StellarPhase::AGB, dt);
        break;

    case StellarPhase::AGB:
        if (b.stellarPhaseAge > tMS * 0.04)
            StellarTransitionTo(b, StellarPhase::THERMAL_PULSES, dt);
        break;

    case StellarPhase::THERMAL_PULSES:
        if (b.stellarPhaseAge > tMS * 0.02)
            StellarTransitionTo(b, StellarPhase::PLANETARY_NEBULA, dt);
        break;

    case StellarPhase::PLANETARY_NEBULA:
        if (b.stellarPhaseAge > tMS * 0.02)
            StellarTransitionTo(b, StellarPhase::WHITE_DWARF, dt);
        break;

    case StellarPhase::SUPERGIANT:
        if (b.stellarPhaseAge > tMS * 0.05)
            StellarTransitionTo(b, StellarPhase::SUPERNOVA, dt);
        break;

    case StellarPhase::SUPERNOVA: {
        // PRIMERA VEZ: guardar radio inicial y determinar tipo de remanente.
        // kickVelocity reutilizado temporalmente para almacenar el radio de la
        // supergigante antes del colapso (se restaura a 0 al finalizar la SN).
        if (b.stellarPhaseAge <= dt * 1.5) {
            b.kickVelocity      = b.radius;
            b.supernovaProgress = 0.0;
            b.supernovaRadius   = 0.0;
            if (mRatio < 20.0) {
                b.compactRemnantType = 1;           // estrella de neutrones
                b.remnantMass        = 1.4 * M_SUN;
            } else {
                b.compactRemnantType = 2;           // agujero negro
                b.remnantMass        = std::max(3.0, mRatio * 0.08) * M_SUN;
            }
        }

        b.supernovaProgress = std::min(1.0, b.stellarPhaseAge / 6e5);
        // La onda de choque se lanza cuando el núcleo YA está casi completamente
        // colapsado (progress ~0.045; collapseT=progress/0.05 → 90% comprimido):
        // el núcleo implosiona, rebota y la explosión REVIENTA hacia afuera.
        // Velocidad inicial alta (blast) que decelera -- expansión violenta.
        if (b.supernovaProgress >= 0.045)
            b.supernovaRadius += 7e7 * dt * (1.0 - b.supernovaProgress * 0.6);

        // IMPLOSIÓN: el radio colapsa al tamaño del objeto compacto durante el
        // primer 5% de la SN (≈primer 8h simuladas). La supergigante se ve
        // contraer dramáticamente mientras la onda de choque sale hacia afuera.
        {
            constexpr double c = 3.0e8;
            double compactR  = (b.compactRemnantType == 2)
                ? std::max(5000.0, 2.0 * G * b.remnantMass / (c * c))
                : 11000.0;
            double collapseT = std::min(1.0, b.supernovaProgress / 0.05);
            double initialR  = (b.kickVelocity > 0.0) ? b.kickVelocity : b.radius;
            b.radius = initialR + (compactR - initialR) * collapseT;
        }

        // Luminosidad con curva cinematográfica
        double snPeak      = L_SUN * 1e9 * std::sqrt(std::max(1.0, mRatio));
        b.visualLuminosity = snPeak * SupernovaLightCurve(b.supernovaProgress);
        b.baseLuminosity   = b.visualLuminosity;
        b.luminosity       = b.visualLuminosity;

        // Eyección de material (solo primer frame)
        if (b.stellarPhaseAge < dt * 1.5)
            SpawnSupernovaEjecta(bodies, dust, b);

        if (b.supernovaProgress >= 1.0) {
            // Transición DIRECTA al objeto compacto — sin SUPERNOVA_REMNANT intermedio.
            // La supernova es un evento de muerte, no una fase evolutiva separada.
            constexpr double c = 3.0e8;
            b.isSupernovaRemnant = false;
            b.supernovaProgress  = 0.0;
            b.supernovaRadius    = 0.0;
            b.kickVelocity       = 0.0;
            b.gravityEnabled     = true;
            b.collisionEnabled   = true;

            if (b.compactRemnantType == 1) {
                // Pulsar: NS recién formada con spin rápido (conservación de L angular)
                // NO tocar b.radius (ya en 11 km por el lerp de implosión).
                // NO tocar b.luminosity/temperature: ApplyStellarPhaseProperties las
                // lerpeará desde el estado residual de la SN hacia los targets del
                // pulsar — transición suave sin "POP" de luminosidad.
                b.mass        = b.remnantMass;
                b.intactMass  = b.remnantMass;
                b.isStar      = true;
                b.spinRateDeg = 3600.0f;  // ~600 RPM al nacer
                // Eje inclinado: crea el efecto visual de haz del pulsar girando.
                // Determinístico por ID para reproducibilidad entre reinicios.
                b.axialTilt   = 20.0f + (float)(b.id % 61); // 20-80 grados
                StellarTransitionTo(b, StellarPhase::PULSAR, dt);
            } else {
                // Agujero negro
                // b.radius ya está en el radio de Schwarzschild por el lerp de implosión.
                b.mass             = b.remnantMass;
                b.intactMass       = b.remnantMass;
                b.isStar           = false;
                b.temperature      = 0.0;
                b.luminosity       = 0.0;
                b.baseLuminosity   = 0.0;
                b.visualLuminosity = 0.0;
                StellarTransitionTo(b, StellarPhase::BLACK_HOLE, dt);
            }
        }
        break;
    }

    case StellarPhase::SUPERNOVA_REMNANT:
        break; // no longer used: SN ahora transiciona directamente a PULSAR/BLACK_HOLE
    case StellarPhase::WHITE_DWARF:
        // Enfriamiento continuo, sin transición posterior
        break;

    default: break;
    }

    ApplyStellarPhaseProperties(b, dt);
}
