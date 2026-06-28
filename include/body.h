#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include "raylib.h"
#include "math_utils.h"

// ============================================================
//  Tipos de cuerpos y estructuras de datos
// ============================================================

enum BodyMaterial {
    MAT_ROCKY    = 0,
    MAT_ICY      = 1,
    MAT_GASEOUS  = 2,
    MAT_METALLIC = 3
};

// ============================================================
//  Perfil atmosferico de un gigante gaseoso/helado procedural
//  Controla las 5 capas del shader: macro-bandas, corrientes,
//  turbulencia regional, microdetalle y tormentas.
// ============================================================
struct GasGiantProfile {
    float bandCount          = 8.0f;  // num. de bandas latitudinales
    float bandStrength       = 1.0f;  // contraste/nitidez de las bandas
    float turbulenceStrength = 0.5f;  // deformacion regional (vortices, pliegues)
    float jetStreamStrength  = 0.5f;  // cizalla por rotacion diferencial
    float stormFrequency     = 0.3f;  // densidad de tormentas menores (0..1)
    float stormSize          = 1.0f;  // escala de las tormentas
    float cloudContrast      = 0.5f;  // visibilidad del microdetalle/nubes altas
    float colorVariance      = 0.3f;  // variacion cromatica dentro de cada banda
    float seed                = 0.0f; // variacion procedural por cuerpo
    bool  iceGiant            = false; // Urano/Neptuno: bandas sutiles, cirros de
                                        // metano por umbral, tormenta = ovalo oscuro

    // Paleta: 5 colores muestreados de oscuro/frio a claro/calido
    Vector3 bandColors[5] = {
        {0.55f,0.55f,0.55f}, {0.65f,0.65f,0.65f}, {0.75f,0.75f,0.75f},
        {0.85f,0.85f,0.85f}, {0.95f,0.95f,0.95f}
    };
    Vector3 highCloudColor = {1.0f, 1.0f, 1.0f};

    // Tormenta principal (tipo Gran Mancha Roja / Gran Mancha Oscura)
    bool    hasMajorStorm    = false;
    float   majorStormLat    = -0.3f;  // -1..1
    float   majorStormLon    = 0.0f;   // radianes
    float   majorStormSize   = 0.18f;
    Vector3 majorStormColor  = {0.64f, 0.29f, 0.20f};
    Vector3 majorStormBorder = {0.85f, 0.76f, 0.62f};
};

// ============================================================
//  Perfil de un planeta rocoso/helado procedural
//  Alimenta drawRockyPlanet(): terreno+oceanos, crateres,
//  nubes con sombra proyectada, atmosfera/terminador y
//  luces nocturnas (efecto Tierra).
// ============================================================
struct RockyPlanetProfile {
    float waterLevel    = 0.0f; // 0 = planeta seco (todo "tierra firme")
    float craterDensity = 0.0f; // 0..1: intensidad de crateres tipo Voronoi
    float cloudDensity  = 0.0f; // 0..1: cobertura de la capa de nubes
    float hasCityLights = 0.0f; // 0/1: luces nocturnas en el lado oscuro
    float seed          = 0.0f; // variacion procedural (terreno/crateres/ciudades)
    float polarIceSize  = 0.0f; // 0 = sin casquetes (Luna/Ceres/asteroides),
                                 // 1 = casquetes grandes (Tierra), valores
                                 // intermedios = casquetes pequenos (Marte)

    // Relieve real (ver terrainHeight/bump-mapping en drawRockyPlanet,
    // shaders.h): 'mountainStrength' anade una capa de ruido "ridged"
    // (picos afilados, valles anchos -- cordilleras) al campo de altura,
    // y 'terrainScale' multiplica la frecuencia de TODO el relieve base
    // (terreno + cordilleras). 0 / 1.0 = sin cordilleras, escala normal
    // (comportamiento previo, Tierra).
    float mountainStrength = 0.0f;
    float terrainScale     = 1.0f;

    Vector3 colorLow   = {0.35f, 0.32f, 0.28f}; // tierras bajas / cuencas
    Vector3 colorHigh  = {0.55f, 0.52f, 0.48f}; // tierras altas / montanas
    Vector3 colorWater = {0.02f, 0.12f, 0.35f}; // oceano/liquido
    Vector3 cloudColor = {0.92f, 0.93f, 0.95f}; // capa de nubes

    // 3a banda de elevacion, solo usada si terrainBiome>0.5 (ver mas
    // abajo). Ignorada por el shader en el bioma generico.
    Vector3 colorMid = {0.45f, 0.30f, 0.20f};

    // Solo afecta la PALETA de color en drawRockyPlanet -- terrainHeight
    // (forma del relieve) es la MISMA formula para todos los rocosos
    // (incluido Venus), ver shaders.h.
    // 0 = paleta generica: degradado colorLow->colorHigh por elevacion --
    // comportamiento ORIGINAL, sin cambios.
    // 1 = paleta Venus: degradado de 3 bandas colorLow/colorMid/colorHigh
    // (ver drawRockyPlanet en shaders.h).
    // 2 = bioma Ceres: paleta generica (igual que 0) + "manchas de sal"
    // tipo Occator (parches Voronoi brillantes ocasionales, ver
    // drawRockyPlanet).
    float terrainBiome = 0.0f;

    // 0 = patron de nubes normal (bandas de Coriolis de baja amplitud,
    // ver cloudField en shaders.h) -- Tierra/Marte/etc., sin cambios.
    // >0 = nubes estiradas horizontalmente + distorsion en "V"/"Y" cerca
    // del ecuador, modelando la super-rotacion atmosferica extrema de
    // Venus (la capa de nubes de acido sulfurico da una vuelta al
    // planeta en ~4 dias terrestres, frente a 243 dias del cuerpo
    // solido -- el viento zonal estira cualquier rasgo casi por completo
    // en longitud, y el flujo tipo Hadley hacia el ecuador desde ambos
    // hemisferios produce el patron en "Y" visible en imagenes UV reales
    // -- Akatsuki/Pioneer Venus).
    float cloudBandStrength = 0.0f;
};

// ============================================================
//  Marca de impacto: registrada por MergeBodies (physics.h) en
//  colisiones violentas, para que el shader de planetas rocosos
//  dibuje crateres permanentes, ondas expansivas y zonas de magma
//  (ver drawRockyPlanet en shaders.h). 'localDir' esta en
//  espacio-malla -- la misma convencion que 'surfacePos' en el
//  shader (ver WorldDirToMeshLocal en physics.h) -- asi que el
//  cráter queda fijo sobre la superficie pese a la rotacion del
//  planeta.
// ============================================================
struct ImpactMark {
    Vector3D localDir{0, 1, 0}; // direccion del impacto en espacio-malla (unitario)
    float radius = 0.0f;        // tamano relativo del crater (fraccion angular, 0..1)
    float energy = 0.0f;        // energia especifica del impacto (J/kg)
    float age    = 0.0f;        // segundos transcurridos desde el impacto
};

struct Body {
    // ID estable para referenciar este cuerpo desde fuera de 'bodies' (p.ej.
    // DustParticle::hostBodyId, ver mas abajo) -- los INDICES dentro de
    // 'bodies' NO son estables (se borran elementos mid-vector en
    // camera_input.h y physics.h/MergeBodies), pero este id nunca se
    // reasigna ni se reutiliza durante la ejecucion.
    static inline uint64_t s_nextId = 1;
    uint64_t id = s_nextId++;

    std::string name;

    // Cinemática
    Vector3D pos{}, vel{}, acc{};

    // Posicion al INICIO del sub-paso de fisica actual (antes de
    // LeapfrogStep), usada por ResolveCollisions (physics.h) para una CCD
    // retrospectiva: detecta cruces que YA ocurrieron durante el
    // desplazamiento de este sub-paso (incluido el "tunneling" a traves de
    // un cuerpo entero cuando v*h supera su diametro), algo que una CCD
    // puramente prospectiva (basada en pos/vel ya actualizados) no puede ver.
    Vector3D prevPos{};

    // Propiedades físicas
    double mass     = 0;
    double radius   = 0;
    double luminosity = 0;
    double temperature = 0;

    // Temperatura de equilibrio radiativo (objetivo hacia el que relaja
    // 'temperature' en UpdateThermodynamics, ver physics.h). La calcula
    // UpdateBodiesState a partir de la irradiacion estelar recibida;
    // 'temperature' es el estado termico real (con inercia), que puede
    // dispararse por encima de esto tras un impacto y tarda en disipar
    // ese exceso.
    double equilibriumTemp = 280.0;

    // Efectos visuales y de marea
    float    heatSpike      = 0;
    float    tideStretch    = 0;
    Vector3D tideAxis       = {0, 1, 0};
    float    tideSquash     = 1.0f;
    float    tideElongation = 1.0f;

    // Elongacion/achatamiento "visuales" (ver TidalBodyTransform en
    // renderer.h): derivados del mismo tideStretch pero atenuados segun
    // la rigidez del cuerpo (composicion + tamano, ver ApplyTidesAndRoche
    // en physics.h). tideElongation/tideSquash de arriba son la forma
    // "fisica" real -- usada para distEff y EllipsoidRadiusToward -- y
    // NO cambian con la rigidez, asi que el comportamiento de
    // estres/ruptura/colision es el mismo de siempre.
    float    tideVisualSquash     = 1.0f;
    float    tideVisualElongation = 1.0f;

    // Actividad estelar (0 = tranquila, 1 = extrema)
    float stellarActivity = 0.3f;

    // Estado interno
    double spin         = 0;
    double stellarAge   = 0;
    double fragAge      = 0;
    double intactMass   = 0;
    double tidalDamage  = 0;
    double lastFragTime = 0;
    bool   isDisintegrating = false;
    int    accreteCount = 0;

    // Inmunidad de colision temporal tras spawnear como fragmento/eyeccion
    // (ver MakeFragments/ResolveCollisions en physics.h): cuenta frames
    // (StepPhysics), no segundos -- evita que un fragmento recien creado
    // muy cerca de su cuerpo de origen se funda instantaneamente con el
    // (o lo atraviese sin que la CCD lo detecte) antes de poder alejarse.
    int spawnGraceFrames = 0;

    // Historial de impactos recientes (buffer circular), para el shader
    // de colisiones de planetas rocosos (cráteres permanentes, ondas
    // expansivas, magma -- ver MergeBodies en physics.h).
    static constexpr int MAX_IMPACT_MARKS = 8;
    std::array<ImpactMark, MAX_IMPACT_MARKS> impactMarks{};
    int impactMarkCount = 0;
    int nextImpactSlot  = 0;

    // Clasificación
    BodyMaterial material = MAT_ROCKY;
    bool isStar     = false;
    bool fixed      = false;
    bool isFragment = false;

    // Nucleo diferenciado (ver ApplyTidesAndRoche/shedRate en physics.h):
    // cuando la marea va despojando la envoltura/manto, 'mass' se acerca a
    // 'coreMass' sin saltos de densidad (modelo de volumen compuesto, ver
    // shellBaseDensity). Si mass <= coreMass, la envoltura/manto
    // desaparecio del todo: el cuerpo pasa a 'coreMaterial' (planeta
    // Chthoniano o nucleo metalico expuesto). Dos casos lo usan:
    //  - Gigantes gaseosos/helados: coreMaterial=MAT_ROCKY, ~10% de la masa
    //    (ver SpawnFromCatalog).
    //  - Planetas rocosos/helados lo bastante grandes como para haberse
    //    diferenciado por fusion interna (radio >= DIFFERENTIATION_RADIUS,
    //    ver constants.h): coreMaterial=MAT_METALLIC, ~32.5% de la masa
    //    (fraccion real del nucleo terrestre).
    // 0 para cuerpos que nunca se diferenciaron (asteroides, lunas
    // menores, fragmentos, protocuerpos): se desintegran por completo al
    // cruzar el limite de Roche, sin nucleo remanente.
    double       coreMass     = 0.0;
    BodyMaterial coreMaterial = MAT_ROCKY;

    // Visual base
    Color color = WHITE;
    float atmosphereDensity = 0.0f;
    Color atmosphereColor   = {100, 160, 255, 0};

    // Exponente de Fresnel para el halo/dispersion atmosferica (ver
    // uAtmosphereFalloff en shaders.h): atmosferas finas (Marte) usan un
    // exponente alto -> brillo angosto solo en el limbo; atmosferas densas
    // (Venus) usan uno bajo -> brillo amplio y gradual. Fijado al spawnear
    // (ver SpawnFromCatalog en catalog.h), no varia con la termodinamica.
    float atmosphereFalloff = 3.0f;

    // Inclinacion axial (grados): angulo entre el eje de rotacion propio
    // del cuerpo y la normal de su plano orbital (eje Y del mundo, dado
    // que las orbitas estan en el plano XZ). Es una rotacion FIJA
    // (no varia con el tiempo) alrededor del eje X del mundo, aplicada en
    // TidalBodyTransform (renderer.h) entre el spin diario (eje Y) y el
    // achatamiento por marea. Al ser fija mientras el cuerpo orbita en el
    // plano XZ, el polo inclinado apunta hacia/lejos de la estrella en
    // puntos opuestos de la orbita (estaciones), sin logica adicional.
    // >90 grados = rotacion retrograda vista desde "arriba" (p.ej. Venus,
    // 177.36 grados).
    float axialTilt = 0.0f;

    // Composicion quimica/geologica: fraccion de masa (0..1) de cada
    // elemento/material principal, separada en DOS inventarios -- un
    // cuerpo diferenciado (ver coreMass/coreMaterial arriba) tiene un
    // manto/nucleo SOLIDO de una composicion y, si hasAtmosphere, una
    // envoltura GASEOSA de otra completamente distinta (p.ej. la Tierra:
    // nucleo de Hierro/Niquel + manto de silicatos vs. atmosfera de
    // Nitrogeno/Oxigeno). Mezclarlas en un solo mapa (suma ~1.0 conjunta)
    // impedia calcular la densidad de cada capa por separado. Asignadas en
    // SpawnFromCatalog/AssignComposition (catalog.h) para que la UI/logica
    // externa pueda leerlas (calculos de densidad, terraformacion, etc.);
    // no retroalimentan la simulacion fisica.
    std::unordered_map<std::string, float> solid_composition;
    std::unordered_map<std::string, float> atmospheric_composition;

    bool  hasRings = false;

    // Toggles VISUALES (solo afectan el dibujado, no la simulacion fisica
    // ni la termodinamica -- atmosphereDensity/volatileBudget/etc. siguen
    // su curso normal). Activados desde el panel de seleccion (ui.h) para
    // ver la corteza/superficie sin la capa de atmosfera o de nubes por
    // encima.
    bool  hideAtmosphere = false;
    bool  hideClouds     = false;

    // Inventario de volatiles para transiciones de fase (solo planetas
    // rocosos/helados, ver UpdateThermodynamics en physics.h).
    // iceFraction + vaporFraction + (liquido) = volatileBudget; el
    // liquido alimenta rockyPlanet.waterLevel y el vapor engrosa
    // atmosphereDensity por encima de baseAtmosphereDensity. Un "blow-off"
    // atmosferico (calor extremo) reduce volatileBudget y
    // baseAtmosphereDensity de forma permanente.
    float volatileBudget       = 0.0f;
    float iceFraction          = 0.0f;
    float vaporFraction        = 0.0f;
    float baseAtmosphereDensity = 0.0f;

    // Valores BASE (configurados al crear el cuerpo, ver SpawnFromCatalog
    // en catalog.h) de cobertura de nubes/tinte de oceano/atmosfera, antes
    // de cualquier respuesta al calor. UpdateThermodynamics (physics.h) los
    // usa como punto de partida/retorno para el efecto invernadero
    // (Nivel 1): cloudDensity/colorWater/atmosphereColor se desplazan desde
    // estos valores hacia un aspecto denso/grisaceo-amarillento tipo Venus
    // segun 'vaporFraction/volatileBudget', y regresan aqui si el planeta
    // se enfria.
    float   baseCloudDensity    = 0.0f;
    Vector3 baseColorWater      = {0.02f, 0.12f, 0.35f};
    Color   baseAtmosphereColor = {100, 160, 255, 0};

    // Gigante gaseoso/helado: shader procedural multicapa
    bool isGasGiant = false;
    GasGiantProfile gasGiant;

    // Densidad "base" de la envoltura/manto que rodea a 'coreMass'
    // (kg/m^3), fijada UNA VEZ al crear el cuerpo a partir de su
    // masa/radio/coreMass iniciales (ver SpawnFromCatalog en catalog.h).
    // El modelo de volumen compuesto en ApplyTidesAndRoche (physics.h) la
    // usa para convertir la masa de envoltura/manto RESTANTE en volumen:
    // esa capa no cambia de densidad al perder masa, solo de volumen --
    // por eso el radio se encoge de forma continua y sin saltos al
    // perder gas (gigantes) o manto (rocosos diferenciados).
    double shellBaseDensity = 0.0;

    // Respuesta termica del gigante gaseoso/helado a un exceso de calor
    // (ver UpdateThermodynamics en physics.h): 'turbulenceBoost' (0..1)
    // intensifica temporalmente las bandas/turbulencia/tormentas en el
    // shader (ver UploadBodyUniforms en renderer.h). Relaja de vuelta a
    // 0.0 al enfriarse. El radio visual de los gigantes gaseosos es
    // siempre b.radius (sin multiplicador de expansion) -- un planeta
    // viejo se ve exactamente igual que uno recien spawneado.
    float turbulenceBoost  = 0.0f;

    // Planeta rocoso/helado: shader procedural (terreno+crateres+nubes)
    bool isRockyPlanet = false;
    RockyPlanetProfile rockyPlanet;

    // Texturas (punteros a texturas globales; no gestionadas por Body)
    Texture2D* diffuseTex  = nullptr;
    Texture2D* cloudTex    = nullptr;
    Texture2D* normalTex   = nullptr;
    Texture2D* specularTex = nullptr;
    Texture2D* emissionTex = nullptr;

    // Rotación visual
    float rotationAngle = 0.0f;
    float cloudRotation = 0.0f;

    // Velocidad de rotación actual (grados por "tick" de TIME_STEP/1200,
    // mismo ritmo que antes era una constante de 0.5). ApplyTidesAndRoche
    // la relaja hacia la velocidad angular orbital cuando la marea es
    // fuerte -> bloqueo de marea (rotación síncrona). tidalLock indica
    // que tan bloqueada esta (0 = libre, 1 = totalmente sincrona), solo
    // para mostrar en la UI.
    float spinRateDeg = 0.5f;
    float tidalLock   = 0.0f;

    // Trayectoria (posiciones FISICAS; se convierten a espacio de
    // dibujado en el momento de renderizar, relativas al origen
    // flotante actual -- ver g_renderOrigin en math_utils.h. Cachear
    // ya en espacio de dibujado haria que el trail "saltara" cada vez
    // que el origen flotante se mueve, p.ej. al seguir un cuerpo).
    // Cada punto guarda el tiempo SIMULADO (g_simTime, constants.h) en que
    // se registro -- permite descartar los puntos mas viejos que
    // TRAIL_TIME_SPAN segundos simulados (ver UpdateBodiesState, main.cpp)
    // en vez de un numero fijo de FRAMES, asi el trail se desvanece en
    // pantalla mas rapido a velocidades de simulacion altas y mas lento a
    // velocidades bajas (proporcional al tiempo simulado, no al framerate).
    struct TrailPoint { Vector3D pos; double simTime; };
    std::vector<TrailPoint> trail;
};

// -------- Estructuras auxiliares --------

// Particula de polvo/escombro 3D: sistema visual ligero, separado de
// 'bodies'. No participa en colisiones entre si ni en la gravedad N-cuerpo
// completa -- solo recibe una gravedad restringida (atraccion de los
// cuerpos masivos de 'bodies', ver UpdateDustGravity en physics.h) y una
// colision UNIDIRECCIONAL contra ellos (particula vs. cuerpo, nunca al
// reves). Al ser mucho mas pequena que un Body completo (sin
// GasGiantProfile/RockyPlanetProfile/trail/etc.) decenas de miles de
// particulas cuestan muy poco.
//
// Particle Pool: 'dustField' (main.cpp) es un std::vector<DustParticle> de
// tamano FIJO = MAX_DUST_PARTICLES, preasignado al arrancar el motor. Una
// particula "libre" tiene active=false; instanciar polvo nuevo significa
// encontrar el primer slot con active=false y sobreescribirlo (ver
// SpawnParticle en physics.h) -- nunca se hace push_back/erase sobre este
// vector, evitando reasignaciones de memoria en caliente.
//
// Dos tipos de polvo (mismo struct, distinguidos por 'isRing'):
//  - Polvo de COLISION (isRing=false): tiene vida util -- 'fragAge' crece
//    cada frame y al superar DUST_MAX_LIFE el slot vuelve a active=false
//    (ver UpdateDustLifecycle). Es el unico tipo que se genera hoy
//    (eyecciones de impactos/mareas, ver MakeFragments).
//  - Polvo de ANILLO (isRing=true): vida indefinida -- UpdateDustLifecycle
//    NUNCA lo desactiva por fragAge. Reservado para el sistema de anillos
//    planetarios (proxima implementacion); todavia no se generan
//    particulas con isRing=true.
// Estado fisico del polvo/escombro visual.
// No todo polvo es basura: si esta ligado, formando anillo o acreciendo,
// NO debe morir por un TTL fijo.
enum class DustState {
    Debris = 0,        // eyeccion reciente / nube caotica
    BoundOrbit,        // orbita ligada a un cuerpo masivo
    RingCandidate,     // orbita estable cerca del ecuador del host
    RingParticle,      // material persistente de anillo
    Accreting,         // parte de una nube densa/fria que puede crecer
    ProtoBodySeed,     // agregado listo para promoverse a Body
    Decaying           // basura real: no ligada/no util
};

struct DustParticle {
    Vector3D pos{}, vel{};
    double radius   = 100.0;
    double fragAge  = 0.0;
    float  heatSpike = 0.0f;
    float  seed      = 0.0f;
    Color  color     = WHITE;

    // --- Naturaleza 3D / Particle Pool (ver comentario de arriba) ---
    bool   active = false;   // false = slot libre, reutilizable por el pool
    bool   isRing = false;   // true = polvo de anillo (sin muerte por fragAge)
    DustState state = DustState::Debris;

    // Tiempo acumulado en orbita estable alrededor de hostBodyId.
    // Si supera cierto umbral, el polvo deja de ser basura y pasa a anillo.
    double stableOrbitTime = 0.0;

    // Edad util mientras esta ligado/acreciendo. No mata por si sola.
    double usefulAge = 0.0;

    // Cache ligera para acrecion / diagnostico.
    double densityScore = 0.0;

    // ID del Body duenio de este anillo (ver Body::id), -1 si no aplica
    // (polvo de colision, isRing=false). Permite recalcular cada frame la
    // posicion/radio ACTUALES del planeta para sombra/iluminacion (ver
    // DrawDustField3D, renderer.h) sin depender de un indice de 'bodies'
    // (inestable: se borran elementos mid-vector). Asignado por
    // SpawnPlanetaryRing (physics.h).
    int64_t hostBodyId = -1;

    // Rotacion propia ("tumbling") de la roca low-poly, puramente visual:
    // integrada en UpdateDustGravity (currentRotation += rotationSpeed*h,
    // modulo 2*PI) y usada por DrawDustField3D para orientar cada instancia.
    Vector3 rotationAxis    = {0.0f, 1.0f, 0.0f}; // unitario, asignado al spawn
    float   rotationSpeed   = 0.0f;               // rad/s
    float   currentRotation = 0.0f;               // rad, acumulado

    // Escala visual del mesh low-poly compartido (en unidades de espacio de
    // dibujado, ya multiplicada por RENDER_SCALE al spawnear) -- separada
    // de 'radius' (fisico, en metros) porque DrawMeshInstanced necesita la
    // escala final de cada instancia, no el radio fisico.
    float scale = 1.0f;
};

struct CosmicStar {
    Vector3 position;
    Color   color;
};

// Categoria de catalogo para el menu de la GUI (pestanas "Estrellas",
// "Planetas", "Lunas", "Menores"). Puramente organizativa, no afecta la
// fisica ni el renderizado.
enum class BodyCategory { STAR, PLANET, MOON, MINOR };

struct CatalogItem {
    std::string  name;
    double       mass;
    double       radius;
    Color        color;
    bool         isStar;
    float        atmosphereDensity;
    Color        atmosphereColor;
    BodyMaterial material;

    // Exponente de Fresnel del halo atmosferico (ver atmosphereFalloff en
    // Body). Default 3.0f (NSDMI): las entradas existentes de BuildCatalog
    // que no especifican este campo lo reciben automaticamente.
    float        atmosphereFalloff = 3.0f;

    // Inclinacion axial en grados (ver axialTilt en Body). Default 0.0f
    // (NSDMI): cuerpos sin inclinacion conocida/relevante (luna, asteroides,
    // estrellas) no necesitan especificarlo.
    float        axialTilt = 0.0f;

    // Categoria para las pestanas del menu de catalogo (ver BodyCategory
    // arriba). Default PLANET; BuildCatalog (catalog.h) reasigna STAR/MOON/
    // MINOR para las entradas correspondientes tras construir la lista.
    BodyCategory category = BodyCategory::PLANET;
};
