#pragma once

// ============================================================
//  Shaders GLSL embebidos
// ============================================================

static const char* VERTEX_SHADER_SRC = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
out vec3 fragNormal;
out vec3 fragPosition;
out vec2 fragTexCoord;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform float uSeed;           // semilla procedural (rpSeed, ver renderer.h)
uniform float uPotatoAmp;      // amplitud de deformacion "papa" (0 = sin cambio)
uniform float uInstabilityAmp; // deformacion convectiva de gigantes (0 = ninguna)
uniform float uInstabilityTime;// fase lenta de animacion de celdas convectivas

// Copia de hash/noise/fbm3 (FRAGMENT_SHADER_SRC) -- cada stage de shader
// necesita su propia copia de cualquier funcion que use, GLSL no comparte
// codigo entre stages sin #include.
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash(i),             hash(i+vec3(1,0,0)), f.x),
            mix(hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)), f.x), f.y),
        mix(mix(hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)), f.x),
            mix(hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)), f.x), f.y),
        f.z);
}

float fbm3(vec3 p) {
    float v = noise(p)*0.5 + noise(p*2.03)*0.25 + noise(p*4.11)*0.125;
    return v / 0.875;
}

// POTATO_FREQ=2.0: ~2 ciclos de ruido por radio -> 2-3 "bultos" por
// hemisferio (forma global tipo "papa"), no ruido de superficie de alta
// frecuencia (eso ya lo hace el terreno en el fragment shader).
const float POTATO_FREQ = 2.0;

float potatoDisp(vec3 dir) {
    return (fbm3(dir * POTATO_FREQ + uSeed) * 2.0 - 1.0) * uPotatoAmp;
}

// Deformacion estructural 3D para gigantes/supergigantes: simula celdas
// convectivas masivas, protuberancias y hundimientos del plasma.
// Tres capas de escala espacial + deriva global asimetrica muy lenta.
// Retorna desplazamiento radial normalizado en [-amp, amp].
float stellarInstability(vec3 dir, float amp, float t) {
    // Celdas grandes: 1-2 estructuras convectivas gigantescas por hemisferio
    float cL = fbm3(dir * 1.1  + vec3(t * 0.44, t * 0.32, 1.7)) * 2.0 - 1.0;
    // Celdas medias: hervir del plasma (4-6 celdas visibles)
    float cM = (fbm3(dir * 2.9  + vec3(3.1, t * 0.92, t * 0.68)) * 2.0 - 1.0) * 0.45;
    // Deriva global: asimetria entre hemisferios que migra muy lentamente
    vec3 drift = vec3(sin(t * 0.124 + 0.5) * 0.25,
                      cos(t * 0.108)        * 0.20,
                      sin(t * 0.076 + 1.3)  * 0.18);
    float cD = (fbm3(dir * 0.72 + drift) * 2.0 - 1.0) * 0.55;
    // Micro-turbulencia: pequenas protuberancias de corta vida
    float cF = (fbm3(dir * 6.3  + vec3(t * 1.88, 5.3, t * 1.56)) * 2.0 - 1.0) * 0.18;
    // Suma normalizada (max teorico ~2.18, factor 0.46 ~ 1/2.18)
    return clamp((cL + cM + cD + cF) * 0.46, -1.0, 1.0) * amp;
}

void main() {
    vec3 dir = normalize(vertexPosition);
    vec3 N0  = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));

    if (uPotatoAmp > 0.0001) {
        // Desplaza la posicion radialmente segun ruido 3D de baja
        // frecuencia -- convierte la esfera unitaria (BuildUVSphere) en un
        // blob irregular tipo "papa". Solo cosmetico: b.radius/colisiones
        // no se tocan (este shader corre despues de la fisica).
        vec3 displacedPos = vertexPosition * (1.0 + potatoDisp(dir));

        // Base tangente en espacio objeto (isotropica: cualquier 'upRef'
        // no paralelo a 'dir' sirve).
        vec3 upRef = vec3(0.0, 1.0, 0.0);
        vec3 Tc = cross(upRef, dir);
        if (dot(Tc, Tc) < 1e-4) Tc = cross(vec3(1.0, 0.0, 0.0), dir);
        vec3 T = normalize(Tc);
        vec3 B = cross(dir, T);

        // Mismo metodo que TERRAIN_BUMP_EPS/SCALE (fragment shader):
        // diferencias finitas tangenciales de potatoDisp -> gradiente ->
        // perturbacion de normal. EPS=0.005 (igual orden que terreno, sigue
        // por debajo del semi-periodo 1/POTATO_FREQ/2=0.25). SCALE=8 (igual
        // que terreno) da normales onduladas sin facetado para
        // uPotatoAmp<=MAX_DEFORM=0.20.
        const float POTATO_BUMP_EPS   = 0.005;
        const float POTATO_BUMP_SCALE = 8.0;
        vec3  pT = normalize(dir + T * POTATO_BUMP_EPS);
        vec3  pB = normalize(dir + B * POTATO_BUMP_EPS);
        float dT = potatoDisp(pT) - potatoDisp(dir);
        float dB = potatoDisp(pB) - potatoDisp(dir);
        vec3 Nlocal = normalize(dir - (T * dT + B * dB) * POTATO_BUMP_SCALE);

        fragNormal   = normalize(vec3(matNormal * vec4(Nlocal, 0.0)));
        fragPosition = vec3(matModel * vec4(displacedPos, 1.0));
        gl_Position  = mvp * vec4(displacedPos, 1.0);
    } else if (uInstabilityAmp > 0.0001) {
        // Gigantes/supergigantes: desplazamiento radial por inestabilidad
        // estructural (celdas convectivas masivas, perdida de masa, turbulencia).
        float disp = stellarInstability(dir, uInstabilityAmp, uInstabilityTime);
        vec3  pp   = vertexPosition * (1.0 + disp);

        // Perturbar la normal mediante diferencias finitas tangenciales para
        // que la iluminacion refleje la curvatura real de la superficie deformada
        // (mismo metodo que potatoDisp usa con T/B arriba).
        vec3 upRef = vec3(0.0, 1.0, 0.0);
        vec3 Tc = cross(upRef, dir);
        if (dot(Tc, Tc) < 1e-4) Tc = cross(vec3(1.0, 0.0, 0.0), dir);
        vec3 T = normalize(Tc);
        vec3 B = cross(dir, T);
        const float INST_EPS   = 0.007;
        const float INST_SCALE = 7.5;
        vec3  pT = normalize(dir + T * INST_EPS);
        vec3  pB = normalize(dir + B * INST_EPS);
        float dT = stellarInstability(pT, uInstabilityAmp, uInstabilityTime) - disp;
        float dB = stellarInstability(pB, uInstabilityAmp, uInstabilityTime) - disp;
        vec3 Nlocal = normalize(dir - (T * dT + B * dB) * INST_SCALE);

        fragNormal   = normalize(vec3(matNormal * vec4(Nlocal, 0.0)));
        fragPosition = vec3(matModel * vec4(pp, 1.0));
        gl_Position  = mvp * vec4(pp, 1.0);
    } else {
        // Sin deformacion (planetas, secuencia principal): paso directo.
        fragNormal   = N0;
        fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
        gl_Position  = mvp * vec4(vertexPosition, 1.0);
    }
    fragTexCoord = vertexTexCoord;
}
)GLSL";

static const char* FRAGMENT_SHADER_SRC = R"GLSL(
#version 330
#define MAX_LIGHTS 8
#define MAX_OCCLUDERS 32
in  vec3 fragNormal;
in  vec3 fragPosition;
in  vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;

uniform vec4  colDiffuse;
uniform vec3  viewPos;

uniform int   lightCount;
uniform vec3  lightPos[MAX_LIGHTS];
uniform vec3  lightColor[MAX_LIGHTS];
uniform float lightLum[MAX_LIGHTS];
uniform float lightRadius[MAX_LIGHTS];

uniform int   occluderCount;
uniform vec3  occluderPos[MAX_OCCLUDERS];
uniform float occluderRadius[MAX_OCCLUDERS];

uniform float uRenderScale;

uniform float ambientStrength;
uniform vec3  ambientColor;
uniform float temp;
uniform float heatSpike;
uniform int   isStar;
uniform float spinPhase;
uniform float atmosphereDensity;
uniform vec3  atmosphereColor;

// Exponente de Fresnel del halo/dispersion atmosferica (ver
// opticalDepth = pow(1-viewDot, uAtmosphereFalloff) en
// drawAtmosphereShell/drawRockyPlanet/drawCloudShell/drawGasGiant):
// controla el ANCHO del brillo en el limbo. Atmosferas gruesas (Venus)
// -> exponente bajo (brillo amplio, gradual); atmosferas finas (Marte)
// -> exponente alto (brillo estrecho, solo en angulos rasantes).
uniform float uAtmosphereFalloff;

// Capa de atmosfera inflada (segunda esfera concentrica, ver
// drawAtmosphereShell mas abajo y DrawBody en renderer.h): cuando es 1,
// main() dibuja SOLO esta capa (con blending alfa) en vez del cuerpo
// solido -- la corteza y la atmosfera son DOS pasadas de dibujado
// distintas sobre DOS mallas distintas (radio normal vs. radio inflado).
uniform int isAtmosphereShell;

// Capa de nubes (tercera esfera concentrica, radio intermedio entre la
// corteza y la atmosfera inflada -- ver drawCloudShell mas abajo y
// DrawBody en renderer.h): cuando es 1, main() dibuja SOLO esta capa
// (parches de nubes con blending alfa) en vez del cuerpo solido. Asi las
// nubes dejan de ser una "calcomania" pintada sobre la corteza y pasan a
// ser una capa con volumen propio, visible por encima del relieve.
uniform int isCloudShell;

// ============================================================
//  PLANETA ROCOSO/HELADO PROCEDURAL — perfil de superficie
//  Controla las capas del shader: terreno+oceanos, crateres,
//  nubes con sombra, atmosfera/terminador y luces nocturnas.
// ============================================================
uniform int   isRockyPlanet;
uniform float uWaterLevel;     // 0 = planeta seco (todo "tierra firme")
uniform float uOceanIce;       // 0 = oceano liquido, 1 = oceano congelado
uniform float uCraterDensity;  // 0..1: intensidad de crateres tipo Voronoi
uniform float uCloudDensity;   // 0..1 normalmente: cobertura de la capa de nubes
                                // (>1 fuerza cobertura total, ver lo/hi en
                                // drawRockyPlanet paso 4 y drawCloudShell)
uniform float uCloudBandStrength; // 0 = patron normal; >0 = nubes estiradas en
                                   // longitud + cizalla en "V"/"Y" (super-rotacion
                                   // tipo Venus, ver cloudField)
uniform float uHasCityLights;  // 0/1: luces nocturnas en el lado oscuro
uniform float uSeed;           // variacion procedural (terreno/crateres/ciudades)
uniform vec3  uColorLow;       // color de tierras bajas / cuencas
uniform vec3  uColorHigh;      // color de tierras altas / montanas
uniform vec3  uColorMid;       // 3a banda de elevacion (solo uTerrainBiome>0.5, ver Venus)
uniform vec3  uColorWater;     // color del oceano/liquido
uniform vec3  uCloudColor;     // color de la capa de nubes

// ── Marcas de impacto recientes (Sistema 3, ver ImpactMark en
// body.h y EjectAndMark en physics.h) -- crateres permanentes,
// zonas de magma y onda expansiva en drawRockyPlanet().
#define MAX_IMPACT_MARKS 8
uniform vec3  uImpactDir[MAX_IMPACT_MARKS];    // direccion del impacto, espacio-malla (= surfacePos)
uniform float uImpactRadius[MAX_IMPACT_MARKS]; // tamano angular del crater
uniform float uImpactEnergy[MAX_IMPACT_MARKS]; // energia especifica del impacto (J/kg)
uniform float uImpactAge[MAX_IMPACT_MARKS];    // tiempo transcurrido desde el impacto (s)
uniform int   uImpactCount;

// Texturas reales de relieve/biomas (solo Tierra; uHasSurfaceTex==0
// para el resto de mundos, que usan exclusivamente colores procedurales)
uniform int       uHasSurfaceTex;
uniform sampler2D uNormalMap;
uniform sampler2D uSpecularMap;
uniform sampler2D uColorMap;

// Tamano de los casquetes polares: 0.0 = sin hielo (Luna, Ceres,
// asteroides), 1.0 = casquetes grandes (Tierra), valores intermedios
// (p.ej. 0.6 en Marte) = casquetes pequenos solo en los polos.
uniform float uPolarIceSize;

// Relieve real (ver terrainHeight): uMountainStrength controla cuanto
// sobresalen las cordilleras de ruido "ridged" sobre el terreno base
// (0 = sin cordilleras); uTerrainScale multiplica la frecuencia de TODO
// el campo de altura (terreno base + cordilleras), para que fragmentos
// distintos luzcan escalas de relieve distintas.
uniform float uMountainStrength;
uniform float uTerrainScale;

// Solo afecta la PALETA de color en drawRockyPlanet -- terrainHeight
// (forma del relieve) es la MISMA para todos los rocosos, incluido
// Venus (ver terrainHeight, mas abajo).
// 0 = paleta generica (Marte/Luna/Mercurio/asteroides/Planeta Rocoso):
// degradado de color colorLow/colorHigh por elevacion -- comportamiento
// ORIGINAL, sin cambios.
// 1 = paleta Venus: degradado de 3 bandas colorLow/colorMid/colorHigh.
// 2 = bioma Ceres: paleta generica (igual que 0); anade "manchas de sal"
// tipo Occator (ver drawRockyPlanet).
uniform float uTerrainBiome;

// Centro del planeta en espacio de dibujado (ToDrawPos(b.pos), ver
// UploadBodyUniforms en renderer.h). La luz estelar real llega en rayos
// PARALELOS a un cuerpo del tamano de un planeta (su radio es
// insignificante frente a la distancia a la estrella) -- usar
// 'lightPos[i] - fragPosition' (que varia punto a punto sobre la
// superficie) producia un terminador curvo/desplazado y un atenuado
// que crecia hacia el punto mas cercano a la luz ("efecto linterna").
// 'lightPos[i] - uBodyCenter' es CONSTANTE sobre toda la esfera: luz
// direccional pura, terminador recto.
uniform vec3 uBodyCenter;

// Angulo actual de rotacion del modelo alrededor del eje Y (radianes,
// equivalente a b.rotationAngle en grados convertido y reducido a
// [0, 2*PI)). Permite "despegar" pos3D (normal en espacio de mundo,
// invariante ante la rotacion de una esfera) y recuperar la
// coordenada solidaria con la malla -> el terreno/UVs rotan con el
// planeta.
uniform float uSurfaceSpin;

// Inclinacion axial (radianes, ver axialTilt en Body y TidalBodyTransform
// en renderer.h): rotacion FIJA alrededor del eje X de mundo que
// TidalBodyTransform aplica AL MODELO (entre el spin diario y el
// achatamiento por marea). N/fragNormal llegan aqui ya en espacio de
// mundo (post-tilt); undoAxialTilt() deshace esa MISMA rotacion para
// recuperar la orientacion "pre-tilt" del cuerpo (su propio eje de spin
// alineado con Y de mundo) -- el marco en el que terreno/crateres/bandas
// de nubes estan definidos (pos3D, surfacePos, windPos). Sin esto, un
// cuerpo inclinado (p.ej. Urano a 97.77 grados) mostraria su terreno/
// bandas "pegados" al marco de mundo en vez de a su propio eje, y al
// orbitar el patron entero giraria con la inclinacion en vez de
// permanecer fijo respecto al cuerpo.
uniform float uAxialTilt;

// Sombra de anillos del cuerpo actual.
// El anillo se modela como una banda plana centrada en uBodyCenter,
// alineada con el ecuador visual del cuerpo.
uniform int   uHasRings;
uniform float uRingInnerRadius;
uniform float uRingOuterRadius;
uniform float uRingShadowStrength;

// Parametros estelares extendidos
uniform float stellarMass;
uniform float stellarActivity;
uniform float stellarLuminosity;
// Fase del ciclo de vida estelar (ver StellarPhase en body.h)
uniform float isGiantPhase;        // 1.0 si SUBGIANT/RED_GIANT/SUPERGIANT
uniform float isSupernovaPhase;    // 1.0 durante SUPERNOVA activa
uniform float supernovaProgress;   // 0→1 durante SUPERNOVA
uniform float rotationCriticality; // 0-1: fracción de velocidad de ruptura rotacional
// Inestabilidad estructural: celdas convectivas en gigantes/supergigantes
uniform float uInstabilityAmp;     // 0=sin efecto, >0=deformacion activa
uniform float uInstabilityTime;    // fase lenta de animacion de celdas convectivas

// ============================================================
//  GIGANTE GASEOSO PROCEDURAL — perfil atmosferico
// ============================================================
uniform int   isGasGiant;
uniform float ggBandCount;
uniform float ggBandStrength;
uniform float ggTurbulence;
uniform float ggJetStream;
uniform float ggStormFreq;
uniform float ggStormSize;
uniform float ggCloudContrast;
uniform float ggColorVariance;
uniform float ggSeed;
uniform vec3  ggBandColors[5];
uniform vec3  ggHighCloudColor;
uniform int   ggHasMajorStorm;
uniform float ggMajorStormLat;
uniform float ggMajorStormLon;
uniform float ggMajorStormSize;
uniform vec3  ggMajorStormColor;
uniform vec3  ggMajorStormBorder;
uniform int   ggIceGiant;

// ============================================================
//  INCLINACION AXIAL: deshacer la rotacion de TidalBodyTransform
// ============================================================
// Inversa de MatrixRotateX(uAxialTilt) (ver renderer.h): para una
// rotacion alrededor de X, la inversa es la rotacion por el angulo
// opuesto (R_x(theta)^-1 = R_x(-theta) = R_x(theta)^T). Aplicada a un
// vector en espacio de mundo (post-tilt), recupera su orientacion
// "pre-tilt" -- el marco propio del cuerpo, donde su eje de spin
// coincide con Y de mundo.
vec3 undoAxialTilt(vec3 v) {
    float c = cos(uAxialTilt), s = sin(uAxialTilt);
    return vec3(v.x, v.y*c + v.z*s, -v.y*s + v.z*c);
}

// Lleva un vector tangente del marco de N (mundo, post-tilt/post-spin) al
// marco de surfacePos, aplicando la MISMA secuencia de rotaciones
// (undoAxialTilt + contra-spin alrededor de Y, con css=cos/sss=sin de
// uSurfaceSpin) que transforma N -> pos3D -> surfacePos (ver
// drawRockyPlanet). Ambos pasos son rotaciones (transformaciones
// ortogonales): preservan productos internos, asi que si v es tangente a
// N (v.N=0), el resultado es tangente a surfacePos. Se usa para el
// bump-mapping del terreno: T,B (tangentes a N) deben convertirse a este
// marco antes de perturbar surfacePos, o el patron de sombreado queda
// rotado respecto al patron de color (que usa surfacePos directamente).
vec3 rotateToSurfaceFrame(vec3 v, float css, float sss) {
    vec3 v2 = undoAxialTilt(v);
    return vec3(v2.x*css - v2.z*sss, v2.y, v2.x*sss + v2.z*css);
}

// ============================================================
//  NOISE
// ============================================================
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(hash(i),             hash(i+vec3(1,0,0)), f.x),
            mix(hash(i+vec3(0,1,0)), hash(i+vec3(1,1,0)), f.x), f.y),
        mix(mix(hash(i+vec3(0,0,1)), hash(i+vec3(1,0,1)), f.x),
            mix(hash(i+vec3(0,1,1)), hash(i+vec3(1,1,1)), f.x), f.y),
        f.z);
}

float fbm5(vec3 p) {
    float v = noise(p)*0.5 + noise(p*2.03)*0.25 + noise(p*4.11)*0.125
            + noise(p*8.17)*0.0625 + noise(p*16.3)*0.03125;
    return v / 0.96875;   // normalizado a 0-1
}

float fbm3(vec3 p) {
    float v = noise(p)*0.5 + noise(p*2.03)*0.25 + noise(p*4.11)*0.125;
    return v / 0.875;
}

// Ruido "ridged": pliega noise() alrededor de su punto medio y lo eleva
// al cuadrado -- produce valles anchos y suaves con picos estrechos y
// afilados (perfil tipico de una cordillera), a diferencia de las colinas
// redondeadas de fbm5/fbm3.
float ridgeNoise(vec3 p) {
    float n = 1.0 - abs(noise(p) * 2.0 - 1.0);
    return n * n;
}

// Solo 3 octavas (persistencia estricta 1/2^i: 0.5, 0.25, 0.125) -- las
// 2 octavas de alta frecuencia que tenia la version de 5 octavas son las
// que, al pasar por ridgeNoise() (pliegue + cuadrado, que AUMENTA el
// contraste de cada octava), producian el "efecto pasa/esponja": picos y
// valles afilados repetidos a escala de pixel por toda la superficie. Con
// 3 octavas las cordilleras grandes (frecuencia base) se conservan pero
// sin el ruido fino superpuesto.
//
// Solo la octava DOMINANTE (frecuencia base, peso 0.5) pasa por
// ridgeNoise() -- las otras 2 (pesos 0.25/0.125) usan noise() normal
// (colinas redondeadas). Con las 3 octavas plegadas por igual, CUALQUIER
// parte de la cordillera (no solo las crestas principales) seguia
// mostrando picos afilados a la escala de cada octava, dando un aspecto
// uniformemente "afilado"/sintetico en vez de crestas grandes con laderas
// y valles suaves entre ellas (lo real: el detalle fino de una montana es
// erosion/rugosidad suave, no mas picos).
float ridgedFbm5(vec3 p) {
    float v = ridgeNoise(p)*0.5 + noise(p*2.03)*0.25 + noise(p*4.11)*0.125;
    return v / 0.875;
}

// ============================================================
//  GRANULACION (sin loops — usando noise en varias fases)
//  Simula bordes de celda oscuros, interiores brillantes.
// ============================================================
float granulation(vec3 p) {
    // 3 muestras desplazadas: la distancia minima entre ellas identifica los bordes
    // de celda con mas precision que comparar solo dos
    float a = fbm3(p);
    float b = fbm3(p + vec3(1.73, 2.31, 0.97));
    float c = fbm3(p + vec3(0.53, 1.17, 1.92));
    float e0 = abs(a - b);
    float e1 = abs(b - c);
    float e2 = abs(a - c);
    float edge = min(e0, min(e1, e2));
    // Thresholding mas agresivo: bordes mas finos y celdas mas definidas
    return 1.0 - smoothstep(0.0, 0.20, edge * 4.5);
}

// ============================================================
//  VORONOI 3D (F1) — para crateres de impacto
//  Distancia al punto-semilla mas cercano entre la celda de p
//  y sus 26 vecinas. 100% cartesiano: sin atan/asin, por lo que
//  no introduce costuras ni pinchamiento polar.
// ============================================================
float voronoi3(vec3 p) {
    vec3 ip = floor(p);
    vec3 fp = fract(p);
    float minD = 8.0;
    for (int z = -1; z <= 1; z++)
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec3 cell = vec3(float(x), float(y), float(z));
        vec3 seed = vec3(hash(ip + cell),
                         hash(ip + cell + vec3(19.1, 0.0, 0.0)),
                         hash(ip + cell + vec3(0.0, 27.7, 0.0)));
        vec3 diff = cell + seed - fp;
        minD = min(minD, dot(diff, diff));
    }
    return sqrt(minD);
}

// Perfil radial de un crater de impacto: depresion parabolica en el
// centro (bowl) + borde levantado justo en el radio del crater (rim).
// d = distancia al centro de la celda Voronoi, radius = radio del crater.
float craterProfile(float d, float radius) {
    float bowl = 1.0 - smoothstep(0.0, radius, d);
    bowl = bowl * bowl;                                        // perfil parabolico
    float rim  = exp(-pow((d - radius) / (radius * 0.35), 2.0)); // anillo gaussiano en d=radius
    return rim * 0.55 - bowl * 0.85;
}

// Suma de crateres a 3 escalas (grandes/medianos/pequenos), modulada
// por uCraterDensity. A densidad 0 el terreno queda perfectamente liso.
float craterField(vec3 p, float density) {
    if (density <= 0.001) return 0.0;
    float c  = craterProfile(voronoi3(p * 2.2  + 11.0), 0.42) * 1.00;
    c       += craterProfile(voronoi3(p * 5.5  + 37.0), 0.30) * 0.55;
    c       += craterProfile(voronoi3(p * 13.0 + 73.0), 0.22) * 0.30;
    return c * density;
}

// ============================================================
//  CRATERES DE IMPACTO PERMANENTES (Sistema 3)
//  A diferencia de craterField() (Voronoi distribuido por todo el
//  planeta), estos crateres se anclan a las direcciones EXACTAS
//  registradas en ImpactMark (ver EjectAndMark en physics.h):
//  quedan fijos sobre la malla en el punto exacto del impacto, con
//  tamano segun la fraccion de masa expulsada. surfacePos y
//  uImpactDir[i] son ambos vectores unitarios en espacio-malla, asi
//  que la distancia euclidea entre ellos sirve directamente como
//  "d" para craterProfile().
// ============================================================
float impactCraterField(vec3 surfacePos) {
    float total = 0.0;
    for (int i = 0; i < uImpactCount && i < MAX_IMPACT_MARKS; i++) {
        float d = length(surfacePos - uImpactDir[i]);
        total  += craterProfile(d, uImpactRadius[i]);
    }
    return total;
}

// ============================================================
//  CAMPO DE RELIEVE COMPLETO (Sistema 4)
//  Combina terreno base + cordilleras (ridged FBM) + crateres
//  Voronoi + cicatrices de impacto permanentes en un solo campo
//  de altura escalar. Factorizado en una funcion para poder
//  muestrearlo en puntos vecinos y derivar la normal por
//  diferencias finitas (bump mapping real, ver drawRockyPlanet).
// ============================================================
float terrainHeight(vec3 sp) {
    vec3 sp2 = sp * uTerrainScale;

    // 1. FORMA BASE (baja frecuencia): continentes/cuencas. Define
    // grandes llanuras lisas separadas por regiones "altas" -- sin esto,
    // las cordilleras quedarian repartidas uniformemente por todo el
    // planeta ("efecto esponja").
    float baseShape = fbm3(sp2 * 1.5 + uSeed);

    // 2. MASCARA DE MONTANAS: smoothstep ancho (0.35..0.75) que barre por
    // completo el ruido escarpado en las zonas bajas de baseShape -- esas
    // zonas quedan en mask=0 (llanuras/cuencas perfectamente lisas, como
    // los mares lunares o las planicies de Marte) y solo las regiones
    // realmente altas (cordilleras) acumulan relieve adicional.
    float mountainMask = smoothstep(0.35, 0.75, baseShape);

    // 4. MICRORELIEVE: aspereza fina de alta frecuencia. Atenuada por la
    // MISMA mountainMask que las cordilleras -- en las llanuras (mask=0)
    // queda en cero (superficie lisa); solo se acumula junto al relieve
    // montanoso, donde es coherente con el resto del paisaje.
    float roughness = (fbm5(sp2 * 8.0 + uSeed * 1.7) - 0.5) * 0.15;

    // 3. CORDILLERAS / CRESTAS: ridged FBM (picos afilados, valles
    // anchos), con su propio offset de semilla (uSeed*5.3) para que su
    // patron no coincida con el de baseShape. Mismo campo de relieve para
    // TODOS los rocosos (incluido Venus, ver MakeVenusProfile): el
    // "bioma Venus" de ridge noise de alta frecuencia cubriendo todo el
    // planeta se probo y se descarto por verse "podrido"/sin relacion con
    // la mascara de continentes. uTerrainBiome ahora solo afecta la
    // PALETA de color (ver drawRockyPlanet), no la generacion de relieve.
    float mountains = (uMountainStrength > 0.001)
        ? ridgedFbm5(sp2 * 3.0 + uSeed * 5.3) * uMountainStrength
        : 0.0;
    float h = baseShape * 0.5 + (mountains + roughness) * mountainMask;

    // 5. CRATERES: su contribucion al CAMPO DE ALTURA (-> bump mapping,
    // ver drawRockyPlanet) se reduce de 0.5/0.6 a 0.18/0.20. El TINTE de
    // color de los crateres (lineas mas abajo en drawRockyPlanet, via su
    // propia evaluacion de craterField/impactCraterField) NO cambia --
    // siguen viendose igual de marcados. Lo que cambia es solo el RELIEVE
    // 3D que generan.
    //
    // Justificacion (pendiente del borde del crater, donde craterProfile
    // pasa de bowl a rim en un ancho radius*0.35): para el octavo
    // dominante (radius=0.42, density=0.85 en la Luna), ese borde cambia
    // craterProfile en ~0.55*(1-exp(-1)) =~ 0.35 sobre un ancho de
    // 0.42*0.35 =~ 0.147 -> pendiente "cruda" =~ 0.35/0.147 * 0.85 =~ 2.0.
    // Con el peso ANTERIOR (0.5) y bumpScale=1.2, tan(pendiente) =~
    // 2.0*0.5*1.2 =~ 1.2 -> ~50 grados (paredes casi verticales, el
    // "efecto pasa/esponja"). Con el peso NUEVO (0.18): tan(pendiente) =~
    // 2.0*0.18*1.2 =~ 0.43 -> ~23 grados, comparable a la pendiente real
    // de un borde de crater lunar fresco (10-25 grados).
    // Las llanuras/cuencas lisas (mountainMask~0, "mares" lunares o
    // planicies marcianas) muestran MENOS crateres que las tierras altas
    // (mountainMask~1): un mare real esta resurfaceado por lava antigua,
    // que borra la mayoria de los crateres viejos. Antes la densidad de
    // crateres era uniforme en TODO el planeta sin importar si la zona es
    // "tierra alta" o "cuenca" -- una de las razones por las que el
    // terreno se veia parejo/sintetico en vez de geologicamente coherente
    // (sin regiones con caracter propio).
    float craterDensityHere = uCraterDensity * mix(0.25, 1.0, mountainMask);
    h += craterField(sp + uSeed * 3.7, craterDensityHere) * 0.18;
    h += impactCraterField(sp) * 0.20;
    return h;
}

// ============================================================
//  CAMPO DE NUBES: DUAL-PHASE FLOW MAP CROSSFADE
//  El flujo zonal (bandas de viento tipo Coriolis) necesita una
//  direccion/velocidad CONTINUA por latitud (sin saltos de signo entre
//  bandas, que dejan costuras visibles). Pero rotar la malla por un
//  angulo que depende de la latitud DEL PROPIO FRAGMENTO es una
//  rotacion diferencial: cada paralelo gira a su propio ritmo, y el
//  desfase entre paralelos crece SIN LIMITE con el tiempo (cizalladura
//  infinita).
//
//  Solucion (tecnica estandar de flow-maps de agua/nubes): el tiempo
//  usado para la rotacion no crece sin limite, se reinicia cada CYCLE
//  segundos (mod(windTime, CYCLE)) -- lo que acota la cizalladura a un
//  maximo de CYCLE*0.25 radianes entre bandas. El "salto" del reinicio
//  se oculta mezclando con una SEGUNDA capa desfasada CYCLE/2: cada
//  capa llega a peso 0 (invisible) justo cuando se reinicia, momento en
//  que la otra esta en su pico de opacidad.
// ============================================================

// Una capa del flujo de nubes: rota pos3D segun 'viento' (intensidad/
// direccion de la banda de Coriolis en esta latitud, continua) y 't'
// (tiempo de esta capa, acotado a [0,CYCLE)), aplica el warp por
// vortices y devuelve el ruido base de nubes en esa posicion.
float cloudLayerNoise(vec3 pos3D, float viento, float t, float windTime) {
    // Velocidad de rotacion zonal reducida (0.25 -> 0.12): con CYCLE=4.0
    // (ver cloudField) el desfase maximo entre el inicio y el fin de un
    // ciclo es ahora t*0.12*viento <= 4.0*0.12 =~ 0.48 rad (~27 grados),
    // frente a los ~2.0 rad (~115 grados) de antes -- la nube se
    // desvanece (crossfade) antes de estirarse lo bastante para perder
    // su forma fractal.
    float cloudAngle = uSurfaceSpin + t * 0.12 * viento;
    float cw = cos(cloudAngle), sw = sin(cloudAngle);
    vec3  p  = vec3(pos3D.x*cw - pos3D.z*sw, pos3D.y, pos3D.x*sw + pos3D.z*cw);

    // Vortices/ciclones: ruido de dominio de BAJA FRECUENCIA (0.8, frente
    // al 2.5 anterior) que distorsiona la posicion de muestreo. Una
    // frecuencia mucho menor que la del ruido base (3.0) produce remolinos
    // a gran escala (curvatura visible de los cumulos) en vez de moteado
    // fino casi indistinguible del ruido base.
    vec3 warp = vec3(
        fbm5(p * 0.8 + vec3(7.1, 2.3, 0.0) + windTime * 0.03),
        fbm5(p * 0.8 + vec3(0.0, 9.4, 3.1) + windTime * 0.03),
        fbm5(p * 0.8 + vec3(5.5, 0.0, 8.8) + windTime * 0.03)
    ) - 0.5;
    p += warp * 0.7;

    return fbm5(p * 3.0 + vec3(5.2, 1.3, 9.7) + uSeed);
}

// Mezcla (crossfade triangular) las dos capas desfasadas. 'pos3D' es la
// posicion SIN rotar (normal de esfera, o desplazada hacia el Sol para
// la sombra -- ver paso 4 de drawRockyPlanet y drawCloudShell, que
// llaman a esta misma funcion para que ambas capas queden alineadas).
float cloudField(vec3 pos3D, float windTime) {
    vec3 p = pos3D;

    // ── Super-rotacion tipo Venus (uCloudBandStrength > 0) ──
    // La capa de nubes de acido sulfurico de Venus da una vuelta al
    // planeta en ~4 dias terrestres frente a 243 dias del cuerpo solido:
    // el viento zonal estira cualquier rasgo casi por completo en la
    // direccion de longitud. Comprimir la componente horizontal (x,z) del
    // vector de muestreo reduce la frecuencia efectiva del ruido en esa
    // direccion -> rasgos alargados en longitud (bandas horizontales).
    // Ademas, la circulacion tipo Hadley arrastra esas bandas hacia el
    // ecuador desde ambos hemisferios, formando el patron en "Y"/"V" visto
    // en UV (Akatsuki, Pioneer Venus): se aproxima rotando la longitud un
    // angulo proporcional a latitud^2 (simetrico, nulo en el ecuador,
    // maximo en los polos). Con uCloudBandStrength == 0 (resto de
    // planetas) p == pos3D: sin cambios.
    if (uCloudBandStrength > 0.0) {
        float lat = asin(clamp(p.y, -1.0, 1.0));
        float stretch = 1.0 + uCloudBandStrength * 5.0;
        p.x /= stretch;
        p.z /= stretch;
        float vShear = lat * lat * uCloudBandStrength * 2.5;
        float cs = cos(vShear), sn = sin(vShear);
        p = vec3(p.x * cs - p.z * sn, p.y, p.x * sn + p.z * cs);
    }

    // 'latitud' en [-PI/2, PI/2]. cos(latitud*3.5) da el patron de 3
    // bandas con direccion alternante (ecuador+, latitudes medias-,
    // polos+) de forma CONTINUA -- sin el salto de signo de sign(),
    // que era lo que generaba la costura entre bandas.
    float latitud = asin(clamp(p.y, -1.0, 1.0));
    float viento  = cos(latitud * 3.5);

    // CYCLE acortado de 8.0 a 4.0: acota aun mas la rotacion diferencial
    // (ver cloudLayerNoise) para que cada capa se desvanezca via
    // crossfade antes de estirarse visiblemente.
    const float CYCLE = 4.0;
    float t1 = mod(windTime, CYCLE);
    float t2 = mod(windTime + CYCLE * 0.5, CYCLE);

    float noise1 = cloudLayerNoise(p, viento, t1, windTime);
    float noise2 = cloudLayerNoise(p, viento, t2, windTime);

    // Pesos triangulares: cada capa se desvanece a 0 justo antes de
    // reiniciarse (t=0 o t=CYCLE), momento en que la otra capa esta en
    // su pico (t=CYCLE/2) -- el reinicio queda invisible.
    float w1 = 1.0 - abs(t1 / (CYCLE * 0.5) - 1.0);
    float w2 = 1.0 - abs(t2 / (CYCLE * 0.5) - 1.0);
    return (noise1 * w1 + noise2 * w2) / (w1 + w2);
}

// ============================================================
//  COLOR DE CUERPO NEGRO (Aproximacion Mitchell Charity, log natural)
// ============================================================
vec3 blackBodyColor(float tK) {
    tK = clamp(tK, 800.0, 40000.0);
    float t = tK * 0.01;          // T/100
    float r, g, b;

    if (tK <= 6600.0) {
        r = 1.0;
        g = clamp((99.4708 * log(max(t, 0.01)) - 161.1196) / 255.0, 0.0, 1.0);
        b = (tK <= 1900.0) ? 0.0
            : clamp((138.5177 * log(max(t - 10.0, 0.01)) - 305.0448) / 255.0, 0.0, 1.0);
    } else {
        float x = max(t - 60.0, 0.01);
        r = clamp(329.6987 * pow(x, -0.1332) / 255.0, 0.0, 1.0);
        g = clamp(288.1222 * pow(x, -0.0755) / 255.0, 0.0, 1.0);
        b = 1.0;
    }
    return vec3(r, g, b);
}

// ============================================================
//  FACTOR DE LUMINOSIDAD REAL
//  lightLum[i] llega en Watts reales (luminosidad de una estrella, ver
//  catalog.h, o la luminosidad efectiva Stefan-Boltzmann de un cuerpo lo
//  bastante caliente, ver UploadLightUniforms en renderer.h). Antes la
//  atenuacion por distancia ignoraba por completo cuanta luz emite la
//  fuente (dos estrellas de brillo muy distinto iluminaban igual a la
//  misma distancia) -- potencia 1/4 (mismo exponente que T~L^0.25 de
//  Stefan-Boltzmann) comprime el rango dinamico enorme entre fuentes
//  tenues y brillantes en un factor suave. A luminosidad solar (L_SUN) da
//  factor=1.0 -- el brillo ya calibrado para el Sol/Tierra no cambia.
// ============================================================
float lightLumFactor(float lum) {
    const float L_SUN_REF = 3.828e26; // ver constants.h::L_SUN
    return clamp(pow(lum / L_SUN_REF, 0.25), 0.15, 3.0);
}

float physicalLightAttenuation(float lumWatts, float distDraw)
{
    // distDraw está en unidades de render.
    // uRenderScale convierte unidades de render -> metros:
    // distMeters = distDraw / RENDER_SCALE.
    float distMeters = max(distDraw / max(uRenderScale, 1e-20), 1.0);

    // Irradiancia física: W/m²
    float irradiance = lumWatts / (4.0 * 3.14159265359 * distMeters * distMeters);

    // Normalización visual: 1361 W/m² = constante solar en la Tierra.
    // Resultado: Sol a 1 UA ~= 1.0
    float relativeToEarthSun = irradiance / 1361.0;

    // Se deja margen HDR; el tonemapping posterior ya comprime.
    return clamp(relativeToEarthSun, 0.0, 80.0);
}

// Convierte irradiancia física relativa en una respuesta visual comprimida.
// Mantiene Sol a 1 UA cerca de 1.0, pero evita que planetas muy cercanos
// a una estrella se quemen como blanco puro por recibir valores enormes.
float visualLightResponse(float physicalAtten)
{
    return clamp(log(1.0 + physicalAtten * 1.75) / log(1.0 + 1.75), 0.0, 4.0);
}

// Terminador suave para capas atmosféricas/nubes.
// A diferencia de max(dot(N,L),0), permite una transición crepuscular,
// especialmente en atmósferas densas.
float softTerminator(vec3 N, vec3 L, float atmosDensity)
{
    float d = dot(N, L);
    float a = mix(-0.03, -0.22, clamp(atmosDensity, 0.0, 1.0));
    float b = mix( 0.08,  0.28, clamp(atmosDensity, 0.0, 1.0));
    return smoothstep(a, b, d);
}

float circleOverlapArea(float R, float r, float d)
{
    // Área de intersección entre dos discos de radios R y r separados d.
    // Devuelve área absoluta, no normalizada.

    R = max(R, 1e-7);
    r = max(r, 1e-7);
    d = max(d, 0.0);

    if (d >= R + r) return 0.0;

    if (d <= abs(R - r))
    {
        float m = min(R, r);
        return 3.14159265359 * m * m;
    }

    float R2 = R * R;
    float r2 = r * r;
    float d2 = d * d;

    float a = acos(clamp((d2 + R2 - r2) / max(2.0 * d * R, 1e-7), -1.0, 1.0));
    float b = acos(clamp((d2 + r2 - R2) / max(2.0 * d * r, 1e-7), -1.0, 1.0));

    float c = 0.5 * sqrt(max(
        (-d + R + r) *
        ( d + R - r) *
        ( d - R + r) *
        ( d + R + r), 0.0));

    return R2 * a + r2 * b - c;
}

float lightVisibilityFromPoint(vec3 pointPos, int lightIndex)
{
    vec3 toLight = lightPos[lightIndex] - pointPos;
    float lightDist = length(toLight);

    if (lightDist <= 1e-5) return 1.0;

    vec3 lightDir = toLight / lightDist;

    // Radio angular aparente de la fuente luminosa.
    float sourceAngularRadius = atan(lightRadius[lightIndex] / max(lightDist, 1e-5));

    // Para luces puntuales/falsas o cuerpos calientes diminutos:
    // valor pequeño pero no cero para evitar divisiones degeneradas.
    sourceAngularRadius = max(sourceAngularRadius, 1e-5);

    float visibility = 1.0;

    for (int j = 0; j < occluderCount && j < MAX_OCCLUDERS; j++)
    {
        vec3 toOcc = occluderPos[j] - pointPos;
        float occDist = length(toOcc);
        float occR = occluderRadius[j];

        if (occR <= 0.0 || occDist <= 1e-5) continue;

        // Evitar que el propio cuerpo receptor se eclipse a sí mismo.
        // uBodyCenter es el centro del cuerpo que se está dibujando.
        if (length(occluderPos[j] - uBodyCenter) < max(occR * 0.01, 1e-6))
            continue;

        // Evitar que la propia fuente luminosa cuente como bloqueador.
        if (length(occluderPos[j] - lightPos[lightIndex]) < max(occR * 0.01, 1e-6))
            continue;

        // El bloqueador debe estar entre el punto sombreado y la luz.
        float along = dot(toOcc, lightDir);
        if (along <= 0.0 || along >= lightDist) continue;

        vec3 closest = lightDir * along;
        float perpDist = length(toOcc - closest);

        float blockerAngularRadius = asin(clamp(occR / max(occDist, 1e-5), 0.0, 0.999));
        float separationAngular    = atan(perpDist / max(along, 1e-5));

        // Fracción del disco luminoso tapada por el bloqueador.
        float overlap = circleOverlapArea(sourceAngularRadius, blockerAngularRadius, separationAngular);
        float sourceArea = 3.14159265359 * sourceAngularRadius * sourceAngularRadius;

        float blocked = clamp(overlap / max(sourceArea, 1e-7), 0.0, 1.0);

        // Múltiples cuerpos pueden tapar parte de la fuente.
        visibility *= (1.0 - blocked);

        if (visibility <= 0.001)
            return 0.0;
    }

    return clamp(visibility, 0.0, 1.0);
}

float ringShadowTransmission(vec3 pointPos, int lightIndex)
{
    if (uHasRings == 0) return 1.0;
    if (uRingOuterRadius <= uRingInnerRadius) return 1.0;
    if (uRingShadowStrength <= 0.001) return 1.0;

    vec3 toLight = lightPos[lightIndex] - pointPos;
    float lightDist = length(toLight);
    if (lightDist <= 1e-5) return 1.0;

    vec3 L = toLight / lightDist;

    // El plano del anillo se alinea con el ecuador visual del cuerpo.
    // TidalBodyTransform aplica MatrixRotateX(axialTilt), así que el eje Y
    // local del planeta en mundo queda aproximadamente como:
    // normal = R_x(uAxialTilt) * vec3(0,1,0) = vec3(0, cos, sin).
    vec3 ringNormal = normalize(vec3(0.0, cos(uAxialTilt), sin(uAxialTilt)));

    float denom = dot(L, ringNormal);

    // Si el rayo es casi paralelo al plano del anillo, no calculamos cruce
    // para evitar parpadeos numéricos.
    if (abs(denom) < 1e-5) return 1.0;

    // Intersección del rayo pointPos -> luz con el plano del anillo.
    float t = dot(uBodyCenter - pointPos, ringNormal) / denom;

    // El anillo debe estar entre el punto sombreado y la luz.
    if (t <= 0.0 || t >= lightDist) return 1.0;

    vec3 hitPos = pointPos + L * t;
    float ringR = length(hitPos - uBodyCenter);

    // Bordes suaves para que la sombra no sea una línea dura perfecta.
    float bandWidth = max(uRingOuterRadius - uRingInnerRadius, 1e-5);
    float feather = max(bandWidth * 0.08, uRingOuterRadius * 0.015);

    float innerMask = smoothstep(uRingInnerRadius, uRingInnerRadius + feather, ringR);
    float outerMask = 1.0 - smoothstep(uRingOuterRadius - feather, uRingOuterRadius, ringR);
    float ringMask = clamp(innerMask * outerMask, 0.0, 1.0);

    // Transmisión final: 1.0 sin sombra, menor cuando cruza el anillo.
    return mix(1.0, 1.0 - uRingShadowStrength, ringMask);
}

// ============================================================
//  UTILIDADES DE COLOR
// ============================================================
vec3 boostSat(vec3 c, float s) {
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return clamp(mix(vec3(lum), c, s), 0.0, 2.0);
}

float invSmooth(float hi, float lo, float x) {
    return 1.0 - smoothstep(lo, hi, x);
}

// ============================================================
//  COLOR ESTELAR ARTISTICO (Space Engine / Universe Sandbox)
//  blackBodyColor() es fisicamente correcto pero casi blanco
//  para tipos G/F/A (spread R-B < 10%). Aqui se aplica una curva
//  de potencia a los canales no dominantes para dar identidad
//  visual fuerte por tipo espectral, manteniendo la coherencia
//  termica (frio=rojo, caliente=azul).
// ============================================================
vec3 artisticStarColor(float tK) {
    vec3 c = blackBodyColor(tK);
    float peak = max(c.r, max(c.g, c.b));
    c /= max(peak, 0.001);
    if (tK <= 6600.0) {
        // G/K/M: rojo domina, se comprimen verde y azul
        float f = 1.0 - clamp((tK - 2000.0) / 4600.0, 0.0, 1.0);
        c.b = pow(max(c.b, 0.0), mix(1.8, 3.8, f));
        c.g = pow(max(c.g, 0.0), mix(1.2, 1.9, f));
    } else {
        // A/B/O: azul domina, se comprimen rojo y verde
        float f = clamp((tK - 6600.0) / 33400.0, 0.0, 1.0);
        c.r = pow(max(c.r, 0.0), mix(1.3, 2.2, f));
        c.g = pow(max(c.g, 0.0), mix(1.1, 1.5, f));
    }
    return clamp(c, 0.0, 1.0);
}

// ============================================================
//  PERFIL ESTELAR POR TIPO ESPECTRAL
//  M/K/G/F/A/B/O aproximado por temperatura efectiva.
// ============================================================
struct StarProfile {
    float mDwarf;
    float kDwarf;
    float gDwarf;
    float hotStar;
    float giantLike;

    float activity;
    float flareBias;
    float flareSize;
    float spotAmount;
    float granScale;
    float granContrast;
    float cellSharpness;
    float filamentStrength;
};

StarProfile getStarProfile(float tK, float mSolar, float baseActivity) {
    StarProfile p;

    p.mDwarf   = 1.0 - smoothstep(3400.0, 4100.0, tK);
    p.kDwarf   = smoothstep(3800.0, 4800.0, tK) * (1.0 - smoothstep(5100.0, 5600.0, tK));
    p.gDwarf   = smoothstep(5200.0, 5600.0, tK) * (1.0 - smoothstep(6100.0, 6600.0, tK));
    p.hotStar  = smoothstep(7500.0, 12000.0, tK);
    p.giantLike = smoothstep(3.0, 8.0, mSolar) * (1.0 - smoothstep(7000.0, 9000.0, tK));

    // Enanas rojas: actividad visual alta aunque stellarActivity base sea moderado.
    // G/K: más tranquilas. Hot stars: más lisas, más viento/radiación que manchas.
    float coolBoost = p.mDwarf * 1.55 + p.kDwarf * 0.55;
    float stableDamp = p.gDwarf * 0.45;
    p.activity = clamp(baseActivity * (1.0 + coolBoost - stableDamp), 0.0, 1.0);

    p.flareBias = clamp(0.10 + p.mDwarf * 0.85 + p.kDwarf * 0.25 - p.gDwarf * 0.25, 0.02, 1.0);
    p.flareSize = clamp(0.55 + p.mDwarf * 1.35 + p.kDwarf * 0.35 - p.gDwarf * 0.20, 0.35, 2.2);

    p.spotAmount = clamp(0.12 + p.activity * 0.85 + p.mDwarf * 0.65 - p.hotStar * 0.70, 0.0, 1.4);

    // Visual: M = celdas pequeñas/contrastadas; Sol/K = granulado medio; gigantes = pocas celdas grandes.
    p.granScale = mix(7.5, 3.2, p.giantLike);
    p.granScale = mix(p.granScale, 10.5, p.mDwarf);
    p.granScale = mix(p.granScale, 5.5, p.kDwarf);
    p.granScale = mix(p.granScale, 6.8, p.gDwarf);
    p.granScale = mix(p.granScale, 13.0, p.hotStar);

    p.granContrast = clamp(0.35 + p.mDwarf * 0.75 + p.kDwarf * 0.35 + p.gDwarf * 0.22 - p.hotStar * 0.18 + p.giantLike * 0.55, 0.20, 1.25);
    p.cellSharpness = clamp(0.45 + p.mDwarf * 0.40 + p.giantLike * 0.35 - p.hotStar * 0.22, 0.25, 1.0);
    p.filamentStrength = clamp(p.activity * (0.35 + p.mDwarf * 1.15 + p.kDwarf * 0.55), 0.0, 1.4);

    return p;
}

// Voronoi celular 3D para granulación estelar real: centros calientes,
// bordes oscuros intergranulares.
float starCellField(vec3 p, out float edge, out float cellRnd) {
    vec3 ip = floor(p);
    vec3 fp = fract(p);
    float d1 = 9.0;
    float d2 = 9.0;
    float rnd = 0.0;

    for (int z = -1; z <= 1; z++)
    for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
        vec3 c = vec3(float(x), float(y), float(z));
        vec3 r = vec3(
            hash(ip + c + vec3(13.1, 7.7, 2.4)),
            hash(ip + c + vec3(5.3, 17.2, 9.1)),
            hash(ip + c + vec3(11.9, 3.4, 21.6))
        );
        vec3 q = c + r - fp;
        float d = dot(q, q);
        if (d < d1) {
            d2 = d1;
            d1 = d;
            rnd = hash(ip + c + 31.7);
        } else if (d < d2) {
            d2 = d;
        }
    }

    float f1 = sqrt(d1);
    float f2 = sqrt(d2);
    edge = clamp((f2 - f1) * 2.8, 0.0, 1.0);
    cellRnd = rnd;
    return f1;
}

float magneticArcMask(vec3 n, float t, float seed, float strength) {
    float lon = atan(n.z, n.x);
    float lat = asin(clamp(n.y, -1.0, 1.0));

    float bands = sin(lon * 7.0 + sin(lat * 9.0 + seed) * 1.7 + t * 1.8);
    float ropes = sin(lon * 13.0 - lat * 11.0 + t * 2.4 + seed * 3.1);
    float braided = abs(bands * 0.65 + ropes * 0.35);

    float activeLat = invSmooth(1.25, 0.15, abs(lat)); // más en latitudes medias/bajas
    return smoothstep(0.78, 0.98, braided) * activeLat * strength;
}

// ============================================================
//  ESTRELLA PROCEDURAL (Remastered — plasma, prominencias, flares)
// ============================================================
void drawStar() {
    vec3  N  = normalize(fragNormal);
    vec3  V  = normalize(viewPos - fragPosition);
    float mu = clamp(dot(N, V), 0.0, 1.0);
    float oneMu = 1.0 - mu;

    // Tiempo + perfil espectral
    float ts = spinPhase * 0.0003;

    StarProfile sp = getStarProfile(temp, stellarMass, stellarActivity);

    // Rotación REAL de la superficie.
    // IMPORTANTE:
    // - uSurfaceSpin viene de b.rotationAngle desde renderer.h.
    // - Esto hace que granulación, manchas y red magnética giren con el cuerpo.
    // - spinPhase/ts queda solo para animar hervor/plasma, NO para girar toda la textura.
    float rA  = uSurfaceSpin;
    float crA = cos(rA), srA = sin(rA);
    vec3 rN = vec3(N.x*crA - N.z*srA, N.y, N.x*srA + N.z*crA);

    // Mismo marco para manchas/faculas/red magnética.
    // Antes sN usaba otra velocidad derivada de rA; eso hacía que capas visuales
    // se deslizasen entre sí y no coincidieran con las llamaradas.
    float sA  = uSurfaceSpin;
    float crS = cos(sA), srS = sin(sA);
    vec3 sN = vec3(N.x*crS - N.z*srS, N.y, N.x*srS + N.z*crS);

    vec3 baseColor = boostSat(artisticStarColor(temp), mix(1.35, 2.25, sp.mDwarf + sp.kDwarf));

    float cellScale = sp.granScale;
    float granContrast = sp.granContrast;

    // ─── GRANULACIÓN CELULAR REAL: celdas convectivas + carriles oscuros ───
    float edgeA, edgeB, rndA, rndB;

    vec3 flow = vec3(
        fbm3(rN * 2.0 + vec3(ts * 0.22, 3.1, 1.7)),
        fbm3(rN * 2.0 + vec3(2.4, ts * 0.18, 5.3)),
        fbm3(rN * 2.0 + vec3(6.7, 1.2, ts * 0.20))
    ) - 0.5;

    vec3 cellPosA = rN * cellScale + flow * mix(0.45, 1.15, sp.activity) + vec3(ts * 0.35, ts * 0.18, 0.0);
    vec3 cellPosB = rN * (cellScale * 2.15) - flow * 0.6 + vec3(0.0, ts * 0.46, ts * 0.21);

    float fA = starCellField(cellPosA, edgeA, rndA);
    float fB = starCellField(cellPosB, edgeB, rndB);

    float laneA = 1.0 - smoothstep(0.08, mix(0.38, 0.18, sp.cellSharpness), edgeA);
    float laneB = 1.0 - smoothstep(0.06, 0.22, edgeB);

    float cellCore = invSmooth(0.72, 0.12, fA) * (0.82 + rndA * 0.28);
    float smallBoil = invSmooth(0.65, 0.18, fB) * 0.35;

    float boiling = clamp(cellCore + smallBoil - laneA * 0.75 - laneB * 0.25, 0.0, 1.25);

    vec3 coolLane = artisticStarColor(temp * mix(0.72, 0.82, sp.hotStar)) * mix(0.16, 0.34, sp.hotStar);
    vec3 hotCell  = artisticStarColor(min(40000.0, temp * mix(1.06, 1.18, sp.mDwarf + sp.kDwarf))) * mix(1.25, 2.15, granContrast);
    vec3 midCell  = artisticStarColor(temp) * mix(0.65, 1.05, boiling);

    vec3 granSurface = mix(coolLane, hotCell, boiling);
    granSurface = mix(granSurface, midCell, 0.22);

    // Red magnética: más visible en M/K activas, casi apagada en G tranquilas.
    float mag = magneticArcMask(sN, ts, stellarMass * 17.13 + temp * 0.001, sp.filamentStrength);
    vec3 magCol = artisticStarColor(min(40000.0, temp * 1.35)) * (1.2 + sp.mDwarf * 1.6);
    granSurface += magCol * mag;

    // Microturbulencia no uniforme, no “ruido de TV”.
    float micro = fbm5(rN * (cellScale * 7.0) + vec3(ts * 2.2, ts * 1.4, ts * 0.9));
    granSurface *= 1.0 + (micro - 0.5) * mix(0.08, 0.28, sp.activity + sp.mDwarf);

    vec3 surfColor = mix(baseColor, granSurface, granContrast);

    // ─── OSCURECIMIENTO AL LIMBO ────────────────────────────────
    float ldA = 0.52, ldB = 0.22;
    if      (temp < 4000.0)                     { ldA = 0.68; ldB = 0.27; }
    else if (temp >= 8000.0  && temp < 20000.0) { ldA = 0.35; ldB = 0.12; }
    else if (temp >= 20000.0)                   { ldA = 0.20; ldB = 0.06; }
    float ld = max(0.08, 1.0 - ldA*oneMu - ldB*oneMu*oneMu);
    surfColor *= ld;

    // ─── MANCHAS ESTELARES: M rojas con manchas grandes; G/K más sobrias ───
    if (sp.spotAmount > 0.02 && temp < 17000.0) {
        float latZone = invSmooth(0.98, 0.12, abs(sN.y));
        float spotLarge = fbm5(sN * mix(2.0, 3.4, sp.mDwarf) + vec3(0.0, ts * 0.055, 0.0));
        float spotSmall = fbm3(sN * mix(5.5, 9.0, sp.mDwarf) + vec3(ts * 0.16, 0.0, ts * 0.09));

        float largeMask = smoothstep(0.72 - sp.spotAmount * 0.26, 0.86, spotLarge) * latZone;
        float smallMask = smoothstep(0.66 - sp.spotAmount * 0.18, 0.82, spotSmall) * latZone * 0.65;

        float spotF = clamp((largeMask + smallMask) * sp.spotAmount, 0.0, 0.95);
        vec3 spotCol = artisticStarColor(temp * mix(0.58, 0.78, sp.hotStar)) * mix(0.18, 0.42, sp.hotStar);
        surfColor = mix(surfColor, spotCol, spotF);
    }

    // ─── FACULAS / REGIONES ACTIVAS ───
    if (sp.activity > 0.035 && temp < 18000.0) {
        float activeNet = magneticArcMask(sN, ts * 1.7, temp * 0.004 + 8.1, sp.activity);
        float facN = fbm5(sN * 11.0 + vec3(ts * 1.8, ts * 0.9, 0.0));
        float fac = activeNet * smoothstep(0.42, 0.82, facN);

        vec3 facCol = artisticStarColor(min(40000.0, temp * mix(1.18, 1.55, sp.mDwarf))) * mix(1.4, 4.2, sp.mDwarf + sp.kDwarf);
        surfColor += facCol * fac * mix(0.25, 1.15, sp.activity);
    }

    // ─── FILAMENTOS / CUERDAS MAGNÉTICAS SOBRE LA FOTOSFERA ───
    if (sp.filamentStrength > 0.02 && temp < 22000.0) {
        float ropes = magneticArcMask(rN, ts * 2.4, 19.7 + stellarMass, sp.filamentStrength);
        float ropeNoise = fbm5(rN * 18.0 + vec3(ts * 3.2, ts * 1.5, 0.0));
        vec3 ropeCol = artisticStarColor(min(40000.0, temp * 1.42)) * (0.7 + sp.mDwarf * 1.4);
        surfColor += ropeCol * ropes * smoothstep(0.48, 0.88, ropeNoise);
    }

    // ─── CROMOSFERA ─────────────────────────────────────────────
    float chromoFade  = invSmooth(0.28, 0.0, mu);
    float chromoNoise = fbm3(rN * 7.5 + vec3(ts*1.9, ts*0.8, 0.0));
    vec3  chromoCol   = blackBodyColor(clamp(temp * 1.15, 2000.0, 40000.0)) * 1.4;
    surfColor = mix(surfColor, chromoCol, chromoFade * 0.45 * (0.4 + chromoNoise * 1.2));

    // ─── ESPICULAS ───────────────────────────────────────────────
    if (mu < 0.26 && temp < 15000.0) {
        float spicN    = fbm5(rN * 13.0 + vec3(ts*3.5, ts*1.8, 0.0));
        float spicMask = invSmooth(0.26, 0.0, mu);
        surfColor += baseColor * smoothstep(0.56, 0.74, spicN) * spicMask * 1.1;
    }

    // ─── CORONA ─────────────────────────────────────────────────
    float coronaVar = 1.0 + fbm3(vec3(rN.x*2.1, ts*0.13, rN.z*2.1)) * 0.55;
    float rim       = (pow(oneMu, 5.5) * 3.5 + pow(oneMu, 2.8) * 0.28) * coronaVar;
    float cScale    = clamp(0.55 + log(max(1.0, stellarLuminosity)) * 0.12, 0.30, 5.0);
    if (temp > 8000.0)  cScale *= 1.6;
    if (temp > 25000.0) cScale *= 2.5;
    surfColor += blackBodyColor(min(40000.0, temp * 1.4)) * cScale * rim;

    // ─── PROMINENCIAS Y ERUPCIONES: M = frecuentes/grandes; G/K = raras ───
    // Suprimidas durante la SUPERNOVA: la estrella está implosionando a objeto
    // compacto, no hay cromosfera que sostenga prominencias. Además 'ts' depende
    // del spin (tiempo de simulación) y a velocidad baja se congelaría, dejando
    // prominencias estáticas sobre la superficie en colapso.
    if (sp.activity > 0.015 && temp < 33000.0 && isSupernovaPhase < 0.5) {
        float limbZone = invSmooth(0.38, 0.0, mu);

        float pA = atan(sN.z, sN.x);
        float cycle = floor(ts * mix(0.35, 1.35, sp.flareBias));
        float phase = fract(ts * mix(0.35, 1.35, sp.flareBias));

        float seed = hash(vec3(cycle, stellarMass * 9.1, temp * 0.003));
        float activeWindow = smoothstep(0.00, 0.12, phase) * (1.0 - smoothstep(0.45 + 0.30 * sp.mDwarf, 0.95, phase));

        float flareLongitude = sin(pA * mix(4.0, 9.0, sp.mDwarf) + seed * 6.2831);
        float flareLat = fbm3(vec3(sN.y * 5.0, seed * 3.0, ts * 0.25));

        float promMask = smoothstep(0.70 - sp.flareBias * 0.22, 0.92, flareLongitude * 0.5 + 0.5);
        promMask *= smoothstep(0.45, 0.85, flareLat);
        promMask *= limbZone * activeWindow * sp.activity;

        vec3 promCold = artisticStarColor(clamp(temp * 0.78, 2000.0, 9000.0));
        vec3 promHot  = artisticStarColor(min(40000.0, temp * mix(1.45, 2.35, sp.mDwarf)));
        vec3 promCol  = mix(promCold, promHot * 3.2, phase);

        surfColor += promCol * promMask * (2.0 + sp.flareSize * 3.2);

        // Destellos en disco, no solo limbo.
        float diskSpark = magneticArcMask(sN, ts * 4.1, seed * 31.0, sp.flareBias);
        float sparkPulse = exp(-pow((phase - 0.18) / 0.10, 2.0));
        vec3 sparkCol = artisticStarColor(min(40000.0, temp * 2.4)) * (1.5 + sp.flareSize * 2.5);
        surfColor += sparkCol * diskSpark * sparkPulse * sp.activity;
    }

    // Efectos por tipo estelar
    if (stellarMass > 4.0 && temp < 6000.0) {
        float turb = (fbm5(rN * 1.4 + vec3(ts*0.5, ts*0.28, 0.0)) - 0.5) * 0.55;
        surfColor *= (1.0 + turb);
    }
    if (temp > 25000.0) {
        float wind = max(0.0, fbm3(rN * 3.5 + vec3(ts*3.2, ts*2.1, ts*1.5)) - 0.38) * 0.9;
        surfColor += baseColor * wind;
    }

    float ft = spinPhase * 0.0001;
    surfColor *= clamp(1.0 + sin(ft*7.31+N.x*3.14)*0.007
                           + stellarActivity*0.014*sin(ft*29.7+N.y*2.7), 0.93, 1.10);

    if (heatSpike > 0.0)
        surfColor = mix(surfColor, vec3(1.5, 1.2, 0.5), heatSpike * 0.65);

    // ─── FASES DE CICLO DE VIDA ESTELAR ─────────────────────────
    // Gigantes (subgigante/gigante roja/supergigante): células de convección
    // mayores, corona más expandida y luminosa, cromosfera más prominente.
    // isGiantPhase ahora es CONTINUO (ver UploadBodyUniforms, renderer.h):
    // sigue el radio real de la estrella en vez de saltar de golpe en el
    // instante del cambio de fase, asi el boost de corona/cromosfera crece
    // a la par que la estrella se infla de verdad.
    if (isGiantPhase > 0.01) {
        surfColor = mix(surfColor, blackBodyColor(max(temp - 500.0, 2000.0)), isGiantPhase * 0.12);
    }

    // ─── INESTABILIDAD ESTRUCTURAL: CELDAS CONVECTIVAS MASIVAS ───────────
    // Las zonas de ascenso (upwellings) son mas calientes y brillantes;
    // las de descenso (downdrafts) mas frias y tenues. Las celdas se animan
    // con el mismo uInstabilityTime que el vertex shader, para que la
    // variacion de color este alineada visualmente con las deformaciones 3D.
    if (uInstabilityAmp > 0.01) {
        float ti = uInstabilityTime;
        // Celda de conveccion a misma escala que el vertex shader (cL)
        float upCell = fbm3(rN * 1.05 + vec3(ti * 0.44, ti * 0.32, 1.7)) * 2.0 - 1.0;
        // Modulacion de detalle para romper uniformidad dentro de cada celda
        float detail = fbm3(rN * 3.1  + vec3(1.8, ti * 0.84, ti * 0.60)) * 2.0 - 1.0;
        float convMask = clamp(upCell * 0.72 + detail * 0.28, -1.0, 1.0);
        // Upwelling (+): brillo y temperatura; downdraft (-): dimmer y mas rojo
        float brightMod = convMask * uInstabilityAmp * 3.0;
        surfColor *= (1.0 + clamp(brightMod, -0.25, 0.42));
        // Tinte rojizo/frio en los descensos convectivos
        float coolBias = clamp(-convMask * uInstabilityAmp * 2.3, 0.0, 0.34);
        surfColor.r *= (1.0 + coolBias * 0.12);
        surfColor.b *= (1.0 - coolBias * 0.22);
    }

    // Gravity darkening: polos calientes, ecuador frío (rotación rápida)
    // Von Zeipel (1924): T ∝ g_eff^0.25, donde g_eff = g - Ω²r en ecuador
    if (rotationCriticality > 0.05) {
        // Ambos polos son fríos por igual (g_eff sólo depende de la
        // distancia al eje de rotación, no del hemisferio) -- usar N.y con
        // signo oscurecía por completo el polo sur a rotación alta.
        float absLat = abs(N.y);  // 1=polo, 0=ecuador, simétrico
        float tFactor = 1.0 + rotationCriticality * 0.5 * absLat
                      - rotationCriticality * 0.5 * (1.0 - absLat);
        surfColor = surfColor * tFactor;
    }

    // Supernova activa: flash azul-blanco que sigue la curva de luz real.
    // snPeak sube 0→1 en el primer 8% del progreso (coincide con el pico de
    // SupernovaLightCurve); snDecay cae exponencialmente después del pico.
    // El snIntensity anterior (progress*2, solo en primera mitad) era demasiado
    // sutil y llegaba tarde: a progress=0.08 (maximo brillo) solo mezclaba 13%.
    if (isSupernovaPhase > 0.5) {
        float snPeak   = clamp(supernovaProgress / 0.08, 0.0, 1.0);
        float snDecay  = exp(-5.0 * max(0.0, supernovaProgress - 0.08));
        float snFactor = snPeak * snDecay;
        vec3  snColor  = vec3(4.0, 4.2, 6.5); // azul-blanco intenso
        surfColor      = mix(surfColor, snColor, snFactor * 0.95);
    }

    // ─── TONEMAPPING FILMIC EN LUMINANCIA + GAMMA ───────────────
    // 1-exp aplicado a la luminancia (no por canal): preserva el matiz/
    // saturacion del color de entrada. El tonemapping por canal hacia
    // converger todo a blanco al comprimir valores HDR brillantes.
    surfColor  = max(surfColor, vec3(0.0));
    float exposure = mix(1.35, 1.75, sp.mDwarf + sp.kDwarf);
    exposure = mix(exposure, 1.20, sp.gDwarf);
    exposure = mix(exposure, 1.05, sp.hotStar);

    surfColor *= exposure;
    float lum  = dot(surfColor, vec3(0.2126, 0.7152, 0.0722));
    float lumT = 1.0 - exp(-lum);
    surfColor *= (lumT / max(lum, 0.0001));
    surfColor  = pow(surfColor, vec3(1.0 / 2.2));
    finalColor = vec4(clamp(surfColor, 0.0, 1.0), 1.0);
}

// ============================================================
//  PALETA DE GIGANTE GASEOSO
//  Interpola entre los 5 colores de banda (de frio/oscuro a
//  calido/claro) segun t en 0..1.
// ============================================================
vec3 ggPalette(float t) {
    t = clamp(t, 0.0, 1.0) * 4.0;
    int   i0 = int(floor(t));
    float f  = fract(t);
    vec3 c0, c1;
    if      (i0 <= 0) { c0 = ggBandColors[0]; c1 = ggBandColors[1]; }
    else if (i0 == 1) { c0 = ggBandColors[1]; c1 = ggBandColors[2]; }
    else if (i0 == 2) { c0 = ggBandColors[2]; c1 = ggBandColors[3]; }
    else              { c0 = ggBandColors[3]; c1 = ggBandColors[4]; }
    return mix(c0, c1, f);
}

// ============================================================
//  GIGANTE GASEOSO PROCEDURAL (hiperrealista — bandas + vortices polares)
//
//  4 principios fisicos/matematicos clave:
//
//   1. RUIDO 100% EN CARTESIANAS 3D: nunca se reconstruye lat/lon
//      (atan/asin) para alimentar fbm/turbulencia. Toda la macro y
//      microestructura se muestrea sobre "windPos", que es fragNormal
//      rotado alrededor del eje de spin (rotacion vectorial pura).
//      En los polos (N.x=N.z=0) la rotacion es un no-op y windPos
//      sigue siendo (0,+-1,0): un punto regular en R3 -> SIN
//      pinchamiento polar ni costura en +-PI.
//
//   2. MASCARA POLAR (polarMask = smoothstep(0.6,0.95,|windPos.y|)):
//      en el ecuador, bandas horizontales rigidas (bandRigidity);
//      hacia los polos las bandas se desvanecen POR COMPLETO y se
//      reemplazan por vortices/ciclones (ruido 3D + espiral vectorial,
//      ver seccion 2b).
//
//   3. INESTABILIDAD DE KELVIN-HELMHOLTZ: el interior de cada banda
//      permanece recto (bandRigidity la mantiene casi plana); solo
//      sus BORDES (limite entre jet streams vecinos que viajan a
//      distinta velocidad) reciben ruido de alta frecuencia (khNoise),
//      generando los remolinos/ondulaciones de cizalla tipicos de
//      Jupiter sin "derretir" toda la banda.
//
//   4. TINTE POLAR: la paleta base se oscurece y vira hacia un tono
//      frio azulado/grisaceo (polarHaze) segun polarMask, simulando
//      los aerosoles de la alta atmosfera en los polos.
//
//  Despues de la macroestructura: nubes finas + nubes altas, un
//  sistema celular de tormentas menores y una tormenta mayor opcional
//  (tipo Gran Mancha Roja/Oscura), ambas atenuadas hacia los polos por
//  (1 - polarMask) ya que alli el rol de "tormenta" lo cumplen los
//  vortices polares.
//
//  Iluminacion: terminador dia/noche suave, brillo de nubes altas
//  cerca del terminador y dispersion atmosferica en el limbo
//  (reutilizando atmosphereColor/atmosphereDensity).
// ============================================================
void drawGasGiant() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);

    // ── TIEMPOS DESACOPLADOS Y CONTINUOS ────────────────────────
    // windAngle = rotacion atmosferica global (eje de spin).
    // noiseAngle = deriva lenta del ruido. Ambos se usan EXCLUSIVAMENTE
    // dentro de cos()/sin() (rotaciones vectoriales u offsets
    // circulares): junto con el periodo de wrap de spinPhase (ver
    // main.cpp, b.spin = fmod(..., 3926990.8125)), windAngle salta
    // exactamente -2*PI*20 y noiseAngle exactamente -2*PI al
    // reiniciarse -> cos/sin son perfectamente continuos, CERO "clip"
    // visible en la rotacion ni en la animacion del ruido.
    float windTime   = spinPhase * 0.0008;
    float windAngle  = windTime * 0.04;
    float noiseAngle = mod(spinPhase * 0.0000016, 6.2831853);

    // ── 1. POSICION 3D ROTADA (sin lat/lon, sin singularidad polar) ──
    // Rotar el vector normal alrededor del eje Y reproduce el "viento
    // global" sin pasar nunca por atan()/asin(). Es una rotacion
    // vectorial pura: en los polos no hace nada, asi que no introduce
    // ni pinchamiento ni costura.
    // Nlocal = undoAxialTilt(N): el "viento" (bandas/Coriolis) gira
    // alrededor del eje de spin PROPIO del cuerpo, no del eje Y de
    // mundo -- en Urano (axialTilt=97.77) las bandas quedan casi
    // perpendiculares a la orbita, con los polos apuntando hacia/lejos
    // del Sol segun la fase orbital (ver comentario de uAxialTilt).
    vec3  Nlocal = undoAxialTilt(N);
    float cw = cos(windAngle), sw = sin(windAngle);
    vec3  windPos = vec3(Nlocal.x * cw - Nlocal.z * sw, Nlocal.y, Nlocal.x * sw + Nlocal.z * cw);

    // ── 2. MASCARA POLAR ────────────────────────────────────────
    // 0 en el ecuador (régimen de bandas), 1 en los polos (régimen de
    // vortices/ciclones).
    float polarMask = smoothstep(0.6, 0.95, abs(windPos.y));

    // ── 3. TURBULENCIA + INESTABILIDAD DE KELVIN-HELMHOLTZ ─────
    // Deriva de baja frecuencia: el interior de cada banda apenas se
    // mueve (bandRigidity pequeno), por lo que las bandas permanecen
    // rectas y definidas en vez de "derretidas".
    vec3  driftA    = vec3(cos(noiseAngle), sin(noiseAngle), ggSeed*1.7) * 0.5;
    float macroWarp = fbm3(windPos * 1.6 + vec3(ggSeed*3.1, 0.0, 0.0) + driftA) - 0.5;

    float bandRigidity  = 0.10;
    float bandCoordsRaw = windPos.y * ggBandCount + macroWarp * bandRigidity * ggTurbulence;

    // Distancia al borde de banda mas cercano: 0 = borde, 0.5 = centro.
    float edgeFrac = fract(bandCoordsRaw);
    float edgeDist = min(edgeFrac, 1.0 - edgeFrac);
    float edgeMask = 1.0 - smoothstep(0.0, 0.20, edgeDist);

    // Ruido de alta frecuencia SOLO cerca de los bordes: simula los
    // remolinos de cizalla entre corrientes en chorro vecinas que se
    // mueven a distinta velocidad (Kelvin-Helmholtz).
    vec3  driftKH = vec3(sin(noiseAngle*3.0 - ggSeed), cos(noiseAngle*3.0 - ggSeed), -1.4) * 0.7;
    float khNoise = fbm5(windPos * (7.0 + ggBandCount * 0.6) + driftKH) - 0.5;
    float bandCoords = bandCoordsRaw + khNoise * edgeMask * 1.8 * ggTurbulence;

    // ── 4. MACROESTRUCTURA: bandas de ancho variable ───────────
    // bandCoords es continuo; cada banda entera tiene un valor de paleta
    // estable (hash), y fract() se mezcla con un smoothstep para que la
    // transicion entre bandas sea siempre difusa pero definida.
    float bandIdx  = floor(bandCoords);
    float bandFrac = smoothstep(0.30, 0.70, fract(bandCoords));

    float hA = hash(vec3(bandIdx,       ggSeed*3.7, 11.0));
    float hB = hash(vec3(bandIdx + 1.0, ggSeed*3.7, 11.0));
    float tA = clamp(0.5 + (hA - 0.5) * ggBandStrength, 0.0, 1.0);
    float tB = clamp(0.5 + (hB - 0.5) * ggBandStrength, 0.0, 1.0);
    float paletteT = mix(tA, tB, bandFrac);

    // colorNoise (3D, windPos): la identidad de color de cada banda
    // nunca se estira ni se desincroniza con el tiempo.
    vec3  driftC     = vec3(cos(noiseAngle + ggSeed*2.0), 0.0, sin(noiseAngle + ggSeed*2.0)) * 0.8;
    float colorNoise = fbm3(windPos * vec3(1.2, ggBandCount * 0.5, 1.2) + driftC + ggSeed*4.0);
    paletteT = clamp(paletteT + (colorNoise - 0.5) * ggColorVariance, 0.0, 1.0);

    // ── 3b. GIGANTES DE HIELO: bandas ecuatoriales muy sutiles ──
    // Urano/Neptuno son casi azules solidos: se comprime paletteT hacia
    // el centro de la paleta para que el contraste de banda sea apenas
    // perceptible, conservando solo ligeras variaciones de tono.
    if (ggIceGiant == 1) {
        paletteT = mix(0.5, paletteT, 0.35);
    }

    // ── 2b. VORTICES POLARES (reemplazan las bandas en los polos) ──
    // Espiral generada rotando windPos.xz por un angulo proporcional a
    // su distancia al eje (sin atan): en el propio polo (radio=0) la
    // rotacion es un no-op, asi que la construccion sigue sin tener
    // singularidad. El resultado son remolinos/ciclones circulares en
    // vez de bandas horizontales.
    float poleRadius  = length(windPos.xz);
    float spiralAngle = poleRadius * 8.0 + noiseAngle * 2.0;
    float scs = cos(spiralAngle), ssn = sin(spiralAngle);
    vec3  spiralPos = vec3(windPos.x * scs - windPos.z * ssn,
                            windPos.y * 1.5,
                            windPos.x * ssn + windPos.z * scs);
    vec3  driftPolar   = vec3(sin(noiseAngle*1.5 + ggSeed*7.0), cos(noiseAngle*1.5 - ggSeed*7.0), ggSeed) * 0.6;
    float cycloneNoise = fbm5(spiralPos * (3.0 + ggBandCount * 0.35) + driftPolar);
    float cycloneT     = clamp(0.5 + (cycloneNoise - 0.5) * (0.6 + ggBandStrength * 0.4), 0.0, 1.0);

    paletteT = mix(paletteT, cycloneT, polarMask);
    vec3 atmosCol = ggPalette(paletteT);

    // ── 5. MICROESTRUCTURA: nubes finas + nubes altas ──────────
    // windPos (3D) directo: sin costuras ni cizalla; la "vida" de las
    // nubes proviene exclusivamente del offset circular driftD.
    vec3  driftD  = vec3(cos(noiseAngle*3.0 + ggSeed), sin(noiseAngle*3.0 + ggSeed), cos(noiseAngle*3.0 - ggSeed)) * 1.2;
    vec3  finePos = windPos * (10.0 + ggBandCount) + driftD;
    float fineNoise = fbm5(finePos + ggSeed*9.1);

    float highCloudMask;
    if (ggIceGiant == 1) {
        // Variacion tonal fina MUY sutil (Urano/Neptuno son casi azul
        // solido) — nada de "manchas sucias" de bajo contraste.
        atmosCol *= (1.0 + (fineNoise - 0.5) * 0.15);

        // Cirros de hielo de metano: umbral ESTRICTO sobre ruido de alta
        // frecuencia -> solo los picos mas altos generan estrias blancas
        // afiladas y esporadicas, sumadas como brillo puro (no mezcladas).
        float methaneNoise  = fbm5(finePos * 1.6 + vec3(7.1, 2.3, 9.7));
        float methaneClouds = smoothstep(0.80, 0.94, methaneNoise);
        atmosCol += vec3(1.0) * methaneClouds * (0.7 + ggCloudContrast * 0.6);
        highCloudMask = methaneClouds;
    } else {
        atmosCol *= (1.0 + (fineNoise - 0.5) * 0.5 * ggCloudContrast);
        highCloudMask = smoothstep(0.60, 0.88, fbm5(finePos*0.55 + vec3(11.3, 4.7, 8.2)));
        atmosCol = mix(atmosCol, ggHighCloudColor, highCloudMask * 0.30 * (0.4 + ggCloudContrast));
    }

    // ── 4b. TINTE POLAR (aerosoles de la alta atmosfera) ───────
    // Hacia los polos la paleta base se oscurece y vira a un tono frio
    // azulado/grisaceo, simulando neblina de hidrocarburos de alta
    // altitud (igual que en Jupiter/Saturno reales).
    vec3 polarHaze = vec3(0.55, 0.63, 0.74);
    atmosCol = mix(atmosCol, atmosCol * polarHaze * 0.78, polarMask * 0.65);

    // ── 6. TORMENTAS MENORES (sistema celular) ─────────────────
    // Atenuadas hacia los polos por (1-polarMask): alli el patron de
    // vortices polares (2b) ya cumple ese papel.
    if (ggStormFreq > 0.001) {
        float stormLon = atan(windPos.z, windPos.x);
        vec2  stormUV  = vec2(stormLon / 6.2831853, windPos.y * ggBandCount) * 4.0;
        vec2  gid      = floor(stormUV);
        vec3  stormCol = vec3(0.0);
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                vec2  cell = gid + vec2(float(ox), float(oy));
                float pres = hash(vec3(cell, ggSeed*17.3));
                if (pres < ggStormFreq) {
                    vec2  jit    = vec2(hash(vec3(cell,1.1)), hash(vec3(cell,2.2)));
                    vec2  center = cell + 0.2 + jit*0.6;
                    vec2  d      = stormUV - center;
                    float size   = (0.30 + hash(vec3(cell,3.3)) * 0.35) * ggStormSize;
                    float dist   = length(d) / max(0.05, size);
                    float mask   = smoothstep(1.0, 0.15, dist);
                    if (mask > 0.0) {
                        float swirlSign = (hash(vec3(cell,4.4)) > 0.5) ? 1.0 : -1.0;
                        float swirlAng  = mod(atan(d.y, d.x) + dist*5.0 + windTime*3.0*swirlSign, 6.2831853);
                        float swirlN    = fbm3(vec3(cos(swirlAng), sin(swirlAng), dist*2.0) + cell.x*0.31 + cell.y*0.77);
                        float bright    = (hash(vec3(cell,5.5)) > 0.5) ? 1.0 : -1.0;
                        stormCol += atmosCol * bright * (0.18 + swirlN*0.14) * mask;
                    }
                }
            }
        }
        atmosCol += stormCol * (1.0 - polarMask);
    }

    // ── 7. TORMENTA MAYOR (Gran Mancha Roja / Oscura) ──────────
    // Posicion fija respecto a windPos (sin cizalla): la tormenta no
    // se deforma con el tiempo, solo su remolino interno evoluciona.
    // Atenuada hacia los polos por (1-polarMask) por consistencia con
    // las tormentas menores.
    if (ggHasMajorStorm == 1) {
        float lon  = atan(windPos.z, windPos.x);
        float dLon = atan(sin(lon - ggMajorStormLon), cos(lon - ggMajorStormLon));
        float dLat = windPos.y - ggMajorStormLat;
        vec2  sd    = vec2(dLon * 1.6, dLat * 3.2) / max(0.02, ggMajorStormSize);
        float sdist = length(sd);

        if (ggIceGiant == 1) {
            // Gran Mancha Oscura: ovalo RELLENO (sdist ya esta achatado en
            // el eje Y por el escalado *3.2 de arriba) que OSCURECE el
            // azul base hacia el azul marino/negro profundo. Sin anillo
            // ni emision de luz blanca.
            float darkCore = (1.0 - smoothstep(0.7, 1.05, sdist)) * (1.0 - polarMask);
            if (darkCore > 0.001) {
                float swirlAng = mod(atan(sd.y, sd.x) + sdist*4.0 + windTime*4.0, 6.2831853);
                float swirlN   = fbm3(vec3(cos(swirlAng), sin(swirlAng), sdist*2.5) + ggSeed*2.0);
                vec3  darkCol  = ggMajorStormColor * mix(0.45, 0.85, swirlN);
                atmosCol = mix(atmosCol, darkCol, darkCore);
            }

            // Nubes de metano condensadas SOLO en el borde sur (sd.y < 0)
            // de la mancha: una franja fina justo fuera del ovalo oscuro.
            float southBand   = smoothstep(0.85, 1.0, sdist) * (1.0 - smoothstep(1.0, 1.25, sdist));
            float southSide   = smoothstep(0.0, 0.5, -sd.y / max(0.001, sdist));
            float methaneEdge = smoothstep(0.45, 0.85, fbm5(windPos*16.0 + vec3(3.1, 7.7, sdist*5.0)));
            float edgeClouds  = southBand * southSide * methaneEdge * (1.0 - polarMask);
            atmosCol += ggMajorStormBorder * edgeClouds * 0.8;
        } else {
            float coreRaw = smoothstep(1.0, 0.55, sdist);
            float ringRaw = smoothstep(1.35, 1.0, sdist) - coreRaw;
            float core = coreRaw * (1.0 - polarMask);
            float ring = ringRaw * (1.0 - polarMask);
            if (core > 0.001 || ring > 0.001) {
                float swirlAng = mod(atan(sd.y, sd.x) + sdist*4.0 + windTime*4.0, 6.2831853);
                float swirlN   = fbm3(vec3(cos(swirlAng), sin(swirlAng), sdist*2.5) + ggSeed*2.0);
                vec3  stormCol = mix(ggMajorStormColor*0.80, ggMajorStormColor*1.25, swirlN);
                stormCol *= mix(1.0, 0.85, core);   // sombra suave interna
                atmosCol = mix(atmosCol, stormCol, core);
                atmosCol = mix(atmosCol, ggMajorStormBorder, ring * 0.7);
            }
        }
    }

    atmosCol = clamp(atmosCol, 0.0, 1.6);

    // ── 7. ILUMINACION: volumen atmosferico + terminador ESTRICTO ──
    vec3 mainL = (lightCount > 0) ? normalize(lightPos[0] - fragPosition) : vec3(0.0);

    // Mascara de luz estricta: exactamente 0.0 en el lado nocturno.
    // CUALQUIER brillo de borde/nubes altas/atmosfera se multiplica
    // por esta mascara (o por ndl en el bucle de luces), de modo que
    // el lado opuesto al sol queda en negro total — sin "canicas
    // magicas" brillando en el limbo nocturno.
    float litMask = 0.0;
    if (lightCount > 0) {
        litMask = softTerminator(N, mainL, atmosphereDensity)
        * lightVisibilityFromPoint(fragPosition, 0)
        * ringShadowTransmission(fragPosition, 0);
    }

    // Limb darkening: el espesor atmosferico oscurece el borde visible
    // (aproximacion tipo Rayleigh) en el lado iluminado, dando
    // sensacion de volumen/esfera.
    float limbDark = pow(max(dot(N, V), 0.0), 0.6);
    vec3  shadedCol = atmosCol * mix(0.25, 1.0, limbDark);

    // Piso ambiental tenue, uniforme (no depende del angulo de vista ni del
    // terminador) -- ahora puede ser EXACTAMENTE 0 (ver g_fakeLightEnabled/
    // UploadLightUniforms en renderer.h): sin el piso fijo de 0.05 de
    // antes, desactivar la luz "falsa" deja el lado nocturno realmente
    // negro cuando no hay ninguna luz real cerca.
    vec3 rgb = atmosCol * ambientColor * ambientStrength;

    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        vec3  L     = normalize(lightPos[i] - fragPosition);
        float ndl   = mix(max(dot(N, L), 0.0), softTerminator(N, L, atmosphereDensity), 0.35);
        float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - fragPosition)));
        float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);
        rgb += shadedCol * lightColor[i] * ndl * atten * vis;

        // Brillo de nubes altas cerca del limbo: solo en el lado
        // diurno (gateado por ndl) -> 0.0 en el lado nocturno.
        rgb += ggHighCloudColor * highCloudMask * limbDark * ndl * atten * vis * 0.5;
    }

    // Dispersion atmosferica en el limbo: solo en el lado iluminado
    // (gateada por litMask) -> 0.0 en el lado nocturno.
    if (atmosphereDensity > 0.001) {
        float limb = pow(1.0 - max(dot(N, V), 0.0), uAtmosphereFalloff) * atmosphereDensity;
        rgb += atmosphereColor * limb * litMask;
    }

    // ── AUTO-BRILLO POR CALOR (cuerpo negro) ─────────────────────
    // Un gigante gaseoso lo bastante caliente (hot Jupiter en orbita
    // rasante, o post-impacto) emite luz visible propia por radiacion
    // termica, INDEPENDIENTE de cualquier luz externa -- sin esto, su lado
    // nocturno quedaria negro pese a estar al rojo vivo. Mismo umbral
    // (DRAPER_POINT_K=700, ver renderer.h) que decide si este cuerpo cuenta
    // ademas como fuente de luz real para sus vecinos.
    if (temp > 700.0) {
        float glow = clamp((temp - 700.0) / 3500.0, 0.0, 1.0);
        rgb += blackBodyColor(temp) * glow * glow * 0.9;
    }

    rgb = pow(max(rgb / (rgb + vec3(1.0)), vec3(0.0)), vec3(1.0 / 2.2));
    finalColor = vec4(clamp(rgb, 0.0, 1.0), colDiffuse.a);
}

// ============================================================
//  PLANETA ROCOSO PROCEDURAL — terreno+oceanos, crateres,
//  nubes con sombra proyectada, casquetes polares, atmosfera/
//  terminador cinematica y luces nocturnas. Funcion unificada y
//  parametrica: Tierra, Marte, Luna, Ceres y asteroides
//  procedurales se generan variando uWaterLevel / uCraterDensity /
//  uCloudDensity / uHasCityLights / uSeed / paleta de colores
//  (uColor*). Para la Tierra (uHasSurfaceTex==1) el relieve y los
//  biomas usan las texturas reales de normal/especular en lugar
//  de la paleta procedural.
//
//  Mismo patron "todo en cartesianas 3D" que drawGasGiant():
//  pos3D = undoAxialTilt(normalize(fragNormal)) alimenta directamente
//  fbm/voronoi, y la rotacion de nubes es una rotacion vectorial
//  alrededor del eje Y (igual que windPos). En los polos
//  (pos3D.x=pos3D.z=0) la rotacion es un no-op -> sin pinchamiento ni
//  costura en +-PI ni en los polos.
// ============================================================
void drawRockyPlanet() {
    vec3 N     = normalize(fragNormal);
    vec3 V     = normalize(viewPos - fragPosition);
    vec3 pos3D = undoAxialTilt(N);   // normal/posicion en espacio de MUNDO,
                      // con la inclinacion axial DESHECHA (ver uAxialTilt):
                      // para una esfera es invariante ante su propia
                      // rotacion (cada (posicion,normal) del mundo siempre
                      // esta ocupado por algun vertice) -> por si solo NO
                      // sirve para "pegar" el terreno a la malla, pero SI
                      // fija el terreno/crateres/bandas al eje de spin
                      // PROPIO del cuerpo en vez del eje Y de mundo.

    // surfacePos = pos3D contrarrotado por uSurfaceSpin (el mismo
    // angulo, alrededor del eje Y, que usa DrawModelEx para orientar
    // el modelo). Recupera la coordenada solidaria con la malla, de
    // forma que el terreno/UVs roten visualmente con el planeta.
    // Misma convencion de rotacion vectorial en Y que cloudPos/windPos
    // (sin pinchamiento ni costura en los polos).
    float css = cos(uSurfaceSpin), sss = sin(uSurfaceSpin);
    vec3  surfacePos = vec3(pos3D.x*css - pos3D.z*sss, pos3D.y, pos3D.x*sss + pos3D.z*css);

    vec3 Nf    = N;   // normal final para iluminacion (puede perturbarse
                      // con el mapa de normales real de la Tierra)

    // Luz direccional pura (rayos paralelos): se calcula respecto al
    // CENTRO del planeta (uBodyCenter), no a fragPosition. Con
    // fragPosition, cada punto de la superficie "ve" la luz en una
    // direccion ligeramente distinta (los rayos divergen desde la
    // estrella como el haz de una linterna cercana) y el termino de
    // atenuacion crece hacia el punto mas cercano a la luz -> terminador
    // curvo/desplazado y un "hotspot" de brillo tipo foco de escritorio.
    // Usando uBodyCenter, lightDir y atten son CONSTANTES sobre toda la
    // esfera: el Sol ilumina el planeta entero como una fuente lejana
    // real, con un terminador recto.
    vec3 mainL = (lightCount > 0) ? normalize(lightPos[0] - uBodyCenter) : vec3(0.0);

    // ── TIEMPO: las nubes co-rotan con la superficie + FLUJO ZONAL ──
    // (circulacion atmosferica tipo Coriolis con bandas de latitud
    // alternantes, ver cloudField()/cloudLayerNoise() mas arriba).
    float windTime = spinPhase * 0.0008;

    // ── 1. TERRENO: terrainHeight() = FBM base + cordilleras "ridged"
    // (uMountainStrength/uTerrainScale) + crateres + cicatrices de
    // impacto, todo en surfacePos (rota con el planeta, sin costuras).
    float height = terrainHeight(surfacePos);

    // 'craters'/'impactCraters' se reutilizan abajo para tenir bordes/
    // fondos (regolito expuesto vs sombra del crater).
    float craters       = craterField(surfacePos + uSeed * 3.7, uCraterDensity);
    float impactCraters = impactCraterField(surfacePos);

    // ── TSUNAMI: misma onda expansiva del impacto (aro luminoso del
    // paso 9c, mas abajo), pero evaluada AQUI para que tambien empuje
    // el agua -- no solo brille sobre el resultado ya iluminado.
    // shockwaveMask en [0,1]: maximo en el frente de la onda activa,
    // decae con la edad del impacto (fade) y con la distancia al
    // frente (ringMask), igual que el aro visual.
    float shockwaveMask = 0.0;
    for (int i = 0; i < uImpactCount && i < MAX_IMPACT_MARKS; i++) {
        if (uImpactEnergy[i] > 0.001) {
            const float waveDuration = 2500.0;
            if (uImpactAge[i] < waveDuration) {
                float d     = length(surfacePos - uImpactDir[i]);
                float front = (uImpactAge[i] / waveDuration) * 2.4;
                float fade  = 1.0 - (uImpactAge[i] / waveDuration);
                float ringMask = exp(-pow((d - front) / 0.12, 2.0));
                shockwaveMask = max(shockwaveMask, ringMask * fade);
            }
        }
    }
    // El aro base (ringMask*fade) rara vez supera ~0.3 (es delgado);
    // x10 + clamp satura su nucleo a maxima intensidad (oleaje que
    // aplana la superficie del agua, ver mas abajo) mientras decae
    // suavemente en sus bordes, como el frente de un tsunami real.
    float fuerzaTsunami = clamp(shockwaveMask * 10.0, 0.0, 1.0);

    // Base tangente (T,B): comun a ambas ramas (Tierra y resto de mundos)
    // para el bump-mapping del relieve real (mapa de normales/oleaje en
    // la Tierra, terrainHeight en el resto -- ver mas abajo).
    vec3 upRef = vec3(0.0, 1.0, 0.0);
    vec3 Tc    = cross(upRef, N);
    if (dot(Tc, Tc) < 1e-4) Tc = cross(vec3(1.0, 0.0, 0.0), N);
    vec3 T = normalize(Tc);
    vec3 B = cross(N, T);

    // Ts,Bs: T,B llevados al marco de surfacePos (ver rotateToSurfaceFrame
    // mas arriba) -- direcciones a usar para MUESTREAR terrainHeight() en
    // el bump-mapping (mas abajo), de forma que el gradiente medido
    // corresponda realmente a las direcciones T,B sobre la esfera (y por
    // tanto al patron de color, que usa surfacePos directamente).
    vec3 Ts = rotateToSurfaceFrame(T, css, sss);
    vec3 Bs = rotateToSurfaceFrame(B, css, sss);

    bool  isWater = false;
    vec3  baseColor;
    float roughness, specStrength;

    // [Tierra] mascara real de oceano (canal R de uSpecularMap, ver rama
    // uHasSurfaceTex mas abajo). 0.0 en el resto de mundos (sin textura
    // real ni uHasCityLights) -- usada en el paso 7 para anclar las luces
    // nocturnas al mapa real, independiente de uWaterLevel/evaporacion.
    float specMask = 0.0;

    // Oclusion ambiental "geologica": atenua la luz ambiental en valles/
    // grietas (height bajo) respecto a picos (height alto), para que el
    // relieve siga siendo visible incluso fuera de la luz directa. 1.0 =
    // sin atenuar (Tierra: el relieve real lo dan las texturas, no
    // 'height'); las ramas procedurales la ajustan segun su 'height'.
    float aoFactor = 1.0;

    if (uHasSurfaceTex > 0) {
        // ════════ TIERRA: relieve + biomas con texturas reales ════════
        // UV esferica a partir de surfacePos: longitud = atan(z,x) de
        // la coordenada solidaria con la malla -> rota con el planeta.
        // La latitud (asin(y)) es identica en pos3D y surfacePos (la
        // rotacion es alrededor de Y), asi que da igual cual se use.
        vec2 uv = vec2(atan(surfacePos.z, surfacePos.x) / 6.2831853 + 0.5,
                       asin(clamp(surfacePos.y, -1.0, 1.0)) / 3.14159265 + 0.5);
        // La textura tiene el origen V arriba y U creciente hacia el
        // este; atan()/asin() producen el mapeo en espejo en AMBOS
        // ejes -> se invierten ambas coordenadas para que los
        // continentes queden orientados y del lado correcto.
        uv.y = 1.0 - uv.y;
        uv.x = 1.0 - uv.x;

        // textureLod fuerza LOD=0: evita el "seam" vertical que
        // produciria texture() al elegir mip segun dFdx/dFdy, que se
        // disparan en la discontinuidad de atan() en la antimeridiana.
        specMask = textureLod(uSpecularMap, uv, 0.0).r;
        vec3  nmSample = textureLod(uNormalMap, uv, 0.0).rgb * 2.0 - 1.0;
        vec3  albedo   = textureLod(uColorMap, uv, 0.0).rgb;

        // ── DANO MATEMATICO (Sistema 3): crateres de impacto reales ──
        // 'impactCraters' (= impactCraterField(surfacePos), calculado una
        // sola vez en el paso 1) es un delta de ALTURA con signo: negativo
        // en el fondo de la cuenca (hasta -0.85), positivo en el borde
        // levantado (hasta +0.55), ~0 lejos de cualquier impacto -- ver
        // craterProfile(). 'craterMask' normaliza su magnitud a [0,1] para
        // usarla como peso de mezcla (cicatriz de albedo / normal).
        float craterDepth = impactCraters;
        float craterMask  = clamp(abs(craterDepth), 0.0, 1.0);

        // ── DESACOPLE DEL AGUA RESPECTO AL MAPA ESPECULAR ESTATICO ──
        // 'depthTone' aproxima la profundidad real a partir del canal azul
        // del albedo (tambien usado mas abajo para 'waterCol'): costas poco
        // profundas (depthTone bajo) cerca del umbral, oceano profundo
        // (depthTone alto) muy por debajo.
        //
        // 'baseTerrain' es una elevacion CONTINUA (mismo rango [0,1] que
        // 'height' en la rama procedural) derivada del mapa especular real
        // + la profundidad: tierra=0.85 (firme, por encima de cualquier
        // uWaterLevel alcanzable), costa=0.35, oceano profundo=0.05. A
        // uWaterLevel==0.45 (valor inicial de la Tierra, ver
        // rocky_planets.h/UpdateThermodynamics) esto reproduce el mapa
        // real: toda 'tierra' queda por encima del umbral y todo 'oceano'
        // por debajo, con margen (0.03) para una transicion smoothstep.
        //
        // 'damagedTerrain' suma el delta de altura del crater SIN atenuar
        // (misma escala que craterProfile): un impacto que hunda la
        // 'tierra' (0.85) por debajo de uWaterLevel la inunda; si
        // uWaterLevel baja (oceanos evaporados, ver UpdateThermodynamics),
        // la 'costa' (0.35) emerge primero, y el oceano profundo (0.05)
        // solo si la sequia es extrema.
        float depthTone      = clamp((albedo.b - albedo.r) * 1.6, 0.0, 1.0);
        float baseTerrain    = mix(0.85, mix(0.35, 0.05, depthTone), specMask);
        float damagedTerrain = baseTerrain + craterDepth;

        // Transicion suave (banda de 0.06, mismo orden de magnitud que el
        // smoothstep(0.45,0.55,...) estatico que reemplaza) alrededor del
        // nivel de agua dinamico.
        float waterMix = 1.0 - smoothstep(uWaterLevel - 0.03, uWaterLevel + 0.03, damagedTerrain);
        isWater = waterMix > 0.5;

        // Relieve SUTIL: el mapa de normales solo oscurece un poco las
        // laderas pronunciadas (componente Z alejada de "recto hacia
        // arriba"), sin reemplazar el albedo real por tonos planos de
        // roca (eso es lo que volvia el terreno "putrido"/pantanoso).
        float slopeShade = clamp((1.0 - nmSample.z) * 1.5, 0.0, 1.0);
        vec3  landCol = albedo * mix(1.0, 0.80, slopeShade);

        // ── OCEANO: oleaje animado + variacion de profundidad/brillo ──
        // uColorWater plano + normal de esfera sin perturbar = aspecto
        // "plastico". Se anima un oleaje (fbm5 desplazado por windTime,
        // misma base temporal que la deriva de nubes -> continuo en los
        // wraps de spinPhase) que perturba color, normal y especularidad;
        // la profundidad se aproxima con el propio albedo real (oceano
        // profundo = azul mas oscuro/saturado que la plataforma costera).
        vec3  oceanPos  = surfacePos * 55.0 + vec3(windTime * 1.3, 0.0, windTime * 0.9);
        float waveBig   = fbm5(oceanPos);
        float waveFine  = fbm5(surfacePos * 220.0 - vec3(windTime * 2.6, 0.0, windTime * 1.7));

        vec3  deepWater    = uColorWater * 0.55;
        vec3  shallowWater = uColorWater * 1.6 + vec3(0.02, 0.05, 0.06);
        vec3  waterCol     = mix(shallowWater, deepWater, depthTone);
        waterCol *= mix(0.85, 1.15, waveBig);
        waterCol += vec3(0.05, 0.07, 0.08) * smoothstep(0.62, 0.88, waveFine);
        waterCol += vec3(0.12, 0.16, 0.22) * pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);

        // Hielo oceánico sobre texturas reales.
        float oceanIce = clamp(uOceanIce, 0.0, 1.0);

        float earthCrackD = voronoi3(surfacePos * 24.0 + uSeed * 4.7);
        float earthCracks = 1.0 - smoothstep(0.035, 0.085, earthCrackD);

        float earthIceNoise = fbm5(surfacePos * 18.0 + uSeed * 2.1);
        vec3 earthIceCol = mix(vec3(0.55, 0.72, 0.86), vec3(0.92, 0.97, 1.0), earthIceNoise);
        earthIceCol = mix(earthIceCol, vec3(0.13, 0.23, 0.32), earthCracks * 0.65);

        waterCol = mix(waterCol, earthIceCol, oceanIce);

        baseColor = mix(landCol, waterCol, waterMix);
        roughness = mix(0.92, mix(0.02, 0.18, waveFine), waterMix);
        roughness = mix(roughness, 0.82, waterMix * oceanIce);

        specStrength = specMask * mix(1.0, 2.2, waveFine);
        specStrength = mix(specStrength, 0.025, waterMix * oceanIce);

        // Relieve: perturbar la normal con el mapa de normales real
        // (de [0,1] a [-1,1]), para que las montanas proyecten sombreado
        // con el sol. Sobre agua se usa en cambio un bump de oleaje
        // (derivadas finitas del ruido oceanico, animado).
        vec3 mapN = normalize(T * nmSample.x + B * nmSample.y + N * max(nmSample.z, 0.2));
        float wD  = 0.04;
        float wdx = fbm5(oceanPos + vec3(wD,0.0,0.0)) - fbm5(oceanPos - vec3(wD,0.0,0.0));
        float wdz = fbm5(oceanPos + vec3(0.0,0.0,wD)) - fbm5(oceanPos - vec3(0.0,0.0,wD));
        vec3 waveN = normalize(N + T * wdx * 1.5 + B * wdz * 1.5);
        vec3 landNf = normalize(mix(N, mapN, 0.7));

        vec3 waterNfLiquid = normalize(mix(N, waveN, 0.6));

        // Normal de hielo oceánico: quieta, quebrada, con grietas.
        const float EARTH_ICE_BUMP_EPS = 0.006;
        vec3 eIT = normalize(surfacePos + Ts * EARTH_ICE_BUMP_EPS);
        vec3 eIB = normalize(surfacePos + Bs * EARTH_ICE_BUMP_EPS);

        float earthIceH = fbm5(surfacePos * 20.0 + uSeed * 3.3) * 0.030
                        - earthCracks * 0.040;

        float earthIceHT = fbm5(eIT * 20.0 + uSeed * 3.3) * 0.030
                        - (1.0 - smoothstep(0.035, 0.085, voronoi3(eIT * 24.0 + uSeed * 4.7))) * 0.040;

        float earthIceHB = fbm5(eIB * 20.0 + uSeed * 3.3) * 0.030
                        - (1.0 - smoothstep(0.035, 0.085, voronoi3(eIB * 24.0 + uSeed * 4.7))) * 0.040;

        vec3 waterNfIce = normalize(N - (T * (earthIceHT - earthIceH) + B * (earthIceHB - earthIceH)) * 8.0);

        vec3 waterNf = normalize(mix(waterNfLiquid, waterNfIce, oceanIce));
        Nf = normalize(mix(landNf, waterNf, waterMix));

        // Pendiente del crater matematico (diferencias finitas de
        // impactCraterField, mismo peso 0.20 que en terrainHeight(),
        // linea ~359, para que la magnitud sea consistente con el resto
        // del relieve -- el gradiente CRUDO de craterProfile, hasta ~1.4
        // entre fondo y borde, produciria normales casi verticales).
        // 'craterMask' limita la mezcla a las celdas afectadas por algun
        // crater, dejando intacto el resto del relieve real (mapN/waveN).
        const float CRATER_BUMP_EPS    = 0.015;
        const float CRATER_BUMP_WEIGHT = 0.20;
        vec3  pT  = normalize(surfacePos + T * CRATER_BUMP_EPS);
        vec3  pB  = normalize(surfacePos + B * CRATER_BUMP_EPS);
        float dCT = (impactCraterField(pT) - craterDepth) * CRATER_BUMP_WEIGHT;
        float dCB = (impactCraterField(pB) - craterDepth) * CRATER_BUMP_WEIGHT;
        vec3  craterN = normalize(N - (T * dCT + B * dCB) / CRATER_BUMP_EPS);
        Nf = normalize(mix(Nf, craterN, craterMask));

        // TSUNAMI sobre oceano real: el frente de onda aplana la
        // superficie del agua hacia su normal de reposo (N, "recto
        // hacia arriba" localmente en una esfera) y la cubre de espuma
        // -- limitado a celdas de agua (waterMix) para no afectar tierra.
        if (fuerzaTsunami > 0.001) {
            float tsunamiHere = fuerzaTsunami * waterMix * (1.0 - oceanIce);
            Nf = normalize(mix(Nf, N, tsunamiHere));
            baseColor = mix(baseColor, vec3(0.85, 0.88, 0.92), tsunamiHere * 0.6);
        }
    } else {
        // ════════ RESTO DE MUNDOS: terreno/oceano procedural ════════
        // uWaterLevel == 0.0 -> planeta seco, todo el cuerpo es "tierra firme"
        //
        // TSUNAMI: el frente de la onda expansiva empuja agua hacia
        // tierra (inundacion costera transitoria), elevando el nivel
        // de agua efectivo en 'fuerzaTsunami * TSUNAMI_WAVE_HEIGHT'.
        // 0.06 esta en el mismo orden de magnitud que el resto de
        // perturbaciones del campo de altura (craterField/
        // impactCraterField pesan 0.18/0.20 en terrainHeight, ver
        // arriba), asi que la inundacion queda contenida a la franja
        // costera en vez de cubrir continentes enteros.
        const float TSUNAMI_WAVE_HEIGHT = 0.06;
        isWater = (uWaterLevel > 0.0) && (height < uWaterLevel + fuerzaTsunami * TSUNAMI_WAVE_HEIGHT);

        if (isWater) {
            float oceanIce = clamp(uOceanIce, 0.0, 1.0);

            vec3 liquidCol = uColorWater;

            // Placas y variacion del hielo oceánico.
            float iceNoise   = fbm5(surfacePos * 18.0 + uSeed * 2.1);
            float plateNoise = fbm3(surfacePos * 5.0  + uSeed * 6.4);

            // Grietas finas tipo banquisa usando Voronoi.
            float crackD = voronoi3(surfacePos * 22.0 + uSeed * 4.7);
            float cracks = 1.0 - smoothstep(0.035, 0.085, crackD);

            vec3 iceDeep = vec3(0.42, 0.62, 0.78);
            vec3 iceBase = vec3(0.72, 0.84, 0.92);
            vec3 iceHigh = vec3(0.92, 0.97, 1.00);

            vec3 iceCol = mix(iceDeep, iceBase, plateNoise);
            iceCol = mix(iceCol, iceHigh, smoothstep(0.55, 0.90, iceNoise));
            iceCol = mix(iceCol, vec3(0.12, 0.22, 0.30), cracks * 0.65);

            baseColor = mix(liquidCol, iceCol, oceanIce);

            // Agua liquida = lisa; hielo = mate/rugoso.
            roughness    = mix(0.05, 0.82, oceanIce);
            specStrength = mix(0.01, 0.025, oceanIce);

            // Bump de hielo: placas elevadas + grietas hundidas.
            const float ICE_BUMP_EPS = 0.006;
            vec3 iT = normalize(surfacePos + Ts * ICE_BUMP_EPS);
            vec3 iB = normalize(surfacePos + Bs * ICE_BUMP_EPS);

            float iceH  = fbm5(surfacePos * 20.0 + uSeed * 3.3) * 0.035
                        - cracks * 0.045;

            float iceHT = fbm5(iT * 20.0 + uSeed * 3.3) * 0.035
                        - (1.0 - smoothstep(0.035, 0.085, voronoi3(iT * 22.0 + uSeed * 4.7))) * 0.045;

            float iceHB = fbm5(iB * 20.0 + uSeed * 3.3) * 0.035
                        - (1.0 - smoothstep(0.035, 0.085, voronoi3(iB * 22.0 + uSeed * 4.7))) * 0.045;

            vec3 iceN = normalize(N - (T * (iceHT - iceH) + B * (iceHB - iceH)) * 8.0);
            Nf = normalize(mix(Nf, iceN, oceanIce));

            // Tsunami: si hay hielo, afecta menos al oleaje visual.
            if (fuerzaTsunami > 0.001) {
                float tsunamiOnLiquid = fuerzaTsunami * (1.0 - oceanIce);
                Nf = normalize(mix(Nf, N, tsunamiOnLiquid));
                baseColor = mix(baseColor, vec3(0.85, 0.88, 0.92), tsunamiOnLiquid * 0.6);
            }
        } else {
            float t = clamp((height - uWaterLevel) / max(1e-3, 1.0 - uWaterLevel), 0.0, 1.0);

            // ── PALETA POR ELEVACION (uTerrainBiome) ────────────
            // 0 = generico: degradado simple colorLow->colorHigh,
            // comportamiento ORIGINAL sin cambios.
            // 1 = Venus ("infierno basaltico"): degradado de 3
            // bandas por elevacion. Las transiciones smoothstep
            // (ancho 0.3, centradas en t=0.3 y t=0.7) reproducen las
            // bandas pedidas (0.0-0.3 colorLow, 0.3-0.7 colorMid,
            // 0.7-1.0 colorHigh) con alto contraste pero sin aliasing.
            // 2 = Ceres: igual que el bioma generico (0), mas "manchas
            // de sal" (ver mas abajo).
            if (uTerrainBiome > 0.5 && uTerrainBiome < 1.5) {
                vec3 lowMid = mix(uColorLow, uColorMid, smoothstep(0.15, 0.45, t));
                baseColor   = mix(lowMid, uColorHigh, smoothstep(0.55, 0.85, t));
            } else {
                baseColor = mix(uColorLow, uColorHigh, t);
            }

            // ── MANCHAS DE SAL (Ceres, uTerrainBiome==2) ────────────
            // Deposito brillante tipo Occator/Cerealia Facula: rejilla
            // Voronoi de baja frecuencia (3 celdas por unidad de
            // surfacePos); hash(celda) decide si esa celda tiene mancha
            // (umbral 0.85 -> ~15% de las celdas, "ocasionales"). Dentro
            // de una celda activa, un disco de radio 0.10 alrededor del
            // punto Voronoi (1-smoothstep) se mezcla a blanco azulado
            // brillante (sal de carbonato/sulfato).
            if (uTerrainBiome > 1.5) {
                vec3  saltP    = surfacePos * 3.0 + uSeed * 13.7;
                float saltCell = hash(floor(saltP) + 5.0);
                float saltDist = voronoi3(saltP);
                float saltSpot = step(0.85, saltCell) * (1.0 - smoothstep(0.0, 0.10, saltDist));
                baseColor = mix(baseColor, vec3(0.92, 0.94, 0.97), saltSpot);
            }

            // Manchas continentales de baja frecuencia (tipo basalto):
            // rompen el tono base uniforme ("bola de madera") en mundos
            // secos como Marte. MISMA formula que baseShape/mountainMask en
            // terrainHeight() (sp2=surfacePos*uTerrainScale, fbm3(sp2*1.5+
            // uSeed)) -- antes este oscurecimiento usaba un ruido
            // INDEPENDIENTE (fbm5 a otra frecuencia/semilla), asi que las
            // zonas oscuras no coincidian con las llanuras lisas (menos
            // crateres/montanas, ver terrainHeight): color y relieve se
            // veian como dos capas sin relacion entre si en vez de regiones
            // geologicas coherentes (oscuras Y lisas a la vez, como un mare
            // lunar real).
            float regionShape = fbm3(surfacePos * uTerrainScale * 1.5 + uSeed);
            float lowlandTint = 1.0 - smoothstep(0.35, 0.75, regionShape);
            baseColor = mix(baseColor, baseColor * vec3(0.55, 0.53, 0.58), lowlandTint);

            // Bordes de crater: regolito expuesto mas claro; fondos, mas oscuros
            baseColor = mix(baseColor, baseColor * 1.40 + 0.05, clamp( craters, 0.0, 1.0));
            baseColor = mix(baseColor, baseColor * 0.55,        clamp(-craters, 0.0, 1.0));
            roughness    = 1.0;    // tierra firme: 100% rugosa (ver specStrength abajo)

            // ── OCLUSION AMBIENTAL FALSA (Fake AO / cavity, Sistema 4c) ──
            // Sin esto, bajo luz unicamente ambiental (sin sol directo) el
            // relieve del bump-mapping desaparece: el termino ambiental
            // (paso 5, rgb = baseColor*ambientColor*ambientStrength*
            // aoFactor) no depende de Nf, solo de baseColor*aoFactor. 't'
            // (altura normalizada [0,1], ya calculada arriba) sirve de
            // proxy de "cavidad vs cresta": oscurecer los valles (t bajo)
            // y aclarar las crestas (t alto) deja el relieve "pintado" en
            // el albedo, visible incluso en oscuridad total -- igual que
            // un mapa de oclusion ambiental horneado en una textura real.
            aoFactor = mix(0.7, 1.2, t);

            // Roca/regolito 100% MATE (iluminacion puramente Lambertiana):
            // un specStrength residual (incluso bajo, 0.01-0.04) seguia
            // produciendo un brillo "plastico/lodo mojado" sobre crateres
            // y montanas, opacando el relieve real (bump-mapping).
            // specStrength=0.0 anula el termino especular del todo. Esto
            // NO produce pow(0,0) indefinido: con roughness=1.0 (linea
            // de arriba), el exponente especular es
            // mix(8.0,256.0,1.0-roughness) = mix(8,256,0) = 8 (no 0), y
            // pow(dot(Nf,H),8) esta bien definido incluso si dot(Nf,H)=0
            // (pow(0,8)=0). Unica roca que brilla: magma (paso 2d/8b,
            // specStrength->0.6 mas abajo) o hielo polar (paso 3,
            // specStrength->0.05).
            specStrength = 0.0;

            // ── BUMP MAPPING (Sistema 4b): normal de un campo de
            // alturas por diferencias finitas, mismo principio que
            // craterN para la Tierra (mas abajo): para una superficie
            // z=h(x,y), la normal es normalize(-dh/dx, -dh/dy, 1) en
            // espacio tangente -- aqui T,B son los ejes x/y y N el eje
            // z (normal sin perturbar).
            //
            // TERRAIN_BUMP_EPS=0.005 rad: por debajo de medio periodo
            // del termino 'mountains' (ridgedFbm5(sp2*3*uTerrainScale
            // +..), 3 octavas, multiplicador maximo 4.11) para el
            // uTerrainScale mas alto entre los rocosos (Mercurio, 1.8)
            // -> frecuencia 3*1.8*4.11~22.2 -> periodo ~1/22.2=0.045
            // rad -> medio periodo 0.0225 > EPS=0.005, asi la
            // diferencia finita resuelve la escala de las cordilleras
            // sin aliasing.
            //
            // TERRAIN_BUMP_SCALE=8.0 (reducido de 20.0, mismo metodo
            // gradiente_crudo * EPS * SCALE = tan(pendiente), octavo
            // DOMINANTE de cada bioma, peso 0.5/0.875=0.571 tras
            // normalizar). terrainHeight() usa la MISMA formula de
            // 'mountains' para todos los rocosos (ver comentario en
            // terrainHeight) -- solo cambian mountainStrength y
            // terrainScale por perfil:
            //   - Generico (mountainStrength~0.40, terrainScale~1.0,
            //     freq 3*1.0=3): amplitud 0.571*0.40=0.228,
            //     semi-periodo (1/3)/2=0.167 -> gradiente~1.37 ->
            //     tan(pendiente)=1.37*0.005*8~0.055 -> ~3 grados
            //     (relieve sutil; con SCALE=20 llegaba a ~8 grados,
            //     demasiado "arrugado" bajo luz directa).
            //   - Venus (mountainStrength=0.70, terrainScale=0.9,
            //     freq 3*0.9=2.7): amplitud 0.571*0.70=0.40,
            //     semi-periodo (1/2.7)/2~0.185 -> gradiente~2.16 ->
            //     tan(pendiente)=2.16*0.005*8~0.087 -> ~5 grados
            //     (relieve mas marcado que el generico por su
            //     mountainStrength mayor, pero a frecuencia MAS BAJA
            //     -> crestas anchas en vez de una red de surcos
            //     estrechos ("gusanos")).
            //
            // bpT/bpB se muestrean a lo largo de Ts/Bs (no T/B): Ts,Bs
            // son T,B llevados al marco de surfacePos (ver
            // rotateToSurfaceFrame), asi el gradiente medido corresponde
            // a las direcciones T,B reales sobre la esfera -- sin esto
            // el patron de sombreado quedaba rotado respecto al patron
            // de color (que usa surfacePos directamente), sensacion de
            // "relieve desfasado del color".
            const float TERRAIN_BUMP_EPS   = 0.005;
            const float TERRAIN_BUMP_SCALE = 8.0;
            vec3  bpT = normalize(surfacePos + Ts * TERRAIN_BUMP_EPS);
            vec3  bpB = normalize(surfacePos + Bs * TERRAIN_BUMP_EPS);
            float dHT = terrainHeight(bpT) - height;
            float dHB = terrainHeight(bpB) - height;
            Nf = normalize(N - (T * dHT + B * dHB) * TERRAIN_BUMP_SCALE);
        }
    }

    // ── 2c. TINTE DE LAS CICATRICES DE IMPACTO (Sistema 3) ──────
    // UNICO paso de tinte de crater, igual para todos los rocosos
    // (incluida la Tierra): borde (regolito expuesto) mas claro
    // (*1.5+0.05), fondo de la cuenca mas oscuro (*0.40). Se aplica
    // sobre CUALQUIER baseColor (textura real u oceano incluidos), asi
    // un impacto sobre el mar tambien deja marca.
    //
    // (La Tierra tenia ANTES un segundo tinte previo hacia 'burntRock'
    // antes de este paso, que en una cuenca profunda (craterMask~0.85)
    // dejaba landCol~burntRock=(0.15,0.12,0.10) y luego este paso lo
    // oscurecia *0.49 -> ~(0.07,0.06,0.05), casi negro. Eliminado: ahora
    // la Tierra recibe el mismo unico tinte que el resto (cuenca ~0.49
    // del color real, no de un negro forzado).)
    if (impactCraters != 0.0) {
        baseColor = mix(baseColor, baseColor * 1.5 + 0.05, clamp( impactCraters, 0.0, 1.0));
        baseColor = mix(baseColor, baseColor * 0.40,       clamp(-impactCraters, 0.0, 1.0));
    }

    // ── 2d. CATACLISMO GLOBAL: BOLA DE LAVA ─────────────────────
    // 'temp' (= b.temperature) ahora persiste durante meses/anios tras
    // un impacto masivo (ver UpdateThermodynamics, inercia termica). Si
    // el planeta ENTERO supera el punto de fusion de la roca, el
    // baseColor (oceanos, hielo, terreno, texturas reales) transiciona a
    // magma emisivo -- independiente de las cicatrices de impacto
    // locales (paso 2b/9b), que siguen funcionando para impactos
    // menores que no calientan el planeta entero.
    //
    // 'globalMagmaIntensity' es CUANTO se calienta (0..1 segun 'temp',
    // umbral 1500K). El color se mantiene en la gama roja/naranja del
    // magma (900K-2200K) incluso en intensidad maxima -- el blanco casi
    // puro queda reservado para el nucleo de un impacto local extremo
    // (paso 9b), no para el planeta entero.
    //
    // 'globalMagmaReveal' es DONDE ya llego el calor: una onda expansiva
    // centrada en cada ImpactMark (uImpactDir/uImpactAge) recorre la
    // superficie y va revelando el magma global detras de su frente --
    // "abriendo el camino al infierno" desde el epicentro en vez de
    // encender TODA la esfera de golpe.
    float globalMagmaIntensity = clamp((temp - 1500.0) / 2500.0, 0.0, 1.0);
    float globalMagmaReveal    = 0.0;
    if (globalMagmaIntensity > 0.001) {
        if (uImpactCount == 0) {
            // Sin marcas de impacto activas (p.ej. mundo de lava ya
            // establecido al cargar la escena): no hay onda que
            // propagar, revelar todo de inmediato.
            globalMagmaReveal = 1.0;
        } else {
            // d (distancia entre direcciones unitarias) va de 0 (mismo
            // punto) a 2 (antipoda) -> el frente debe alcanzar ~2.18
            // para cubrir el planeta entero.
            const float GLOBAL_WAVE_SPEED = 2.18 / 60000.0; // segundos simulados para cubrir todo el planeta
            for (int i = 0; i < uImpactCount && i < MAX_IMPACT_MARKS; i++) {
                float waveFront = uImpactAge[i] * GLOBAL_WAVE_SPEED;
                float d         = length(surfacePos - uImpactDir[i]);
                globalMagmaReveal = max(globalMagmaReveal, 1.0 - smoothstep(waveFront - 0.18, waveFront + 0.18, d));
            }
        }
    }
    float globalMagma = globalMagmaIntensity * globalMagmaReveal;
    vec3  globalMagmaColor = vec3(0.0);
    if (globalMagma > 0.001) {
        globalMagmaColor = blackBodyColor(mix(900.0, 2200.0, globalMagma));
        baseColor    = mix(baseColor, globalMagmaColor, globalMagma);
        roughness    = mix(roughness, 0.25, globalMagma);
        specStrength = mix(specStrength, 0.6, globalMagma);
    }

    // ── 3. CASQUETES POLARES (controlados por uPolarIceSize) ────
    // uPolarIceSize escala el TAMANO del casquete corriendo el umbral
    // del smoothstep: 0.0 -> umbral 1.20, fuera de rango (la altura
    // normalizada + abs(latitud) nunca llega a 1.2 -> sin hielo, p.ej.
    // Luna/Ceres/asteroides). 1.0 -> umbral 0.80 (casquetes moderados
    // en la Tierra). 0.6 -> umbral ~0.96 (casquetes pequenos, solo en
    // los polos, p.ej. Marte).
    float iceThreshold = mix(1.20, 0.80, uPolarIceSize);
    float polarIce = (uPolarIceSize > 0.001)
        ? smoothstep(iceThreshold, iceThreshold + 0.16, abs(pos3D.y) + height * 0.12) * (1.0 - globalMagma)
        : 0.0;
    if (polarIce > 0.001) {
        float snowNoise = fbm5(surfacePos * 35.0 + uSeed * 8.1);
        float glacierFlow = ridgedFbm5(surfacePos * 9.0 + uSeed * 11.3);

        vec3 snowCol    = vec3(0.94, 0.96, 0.98);
        vec3 blueGlace  = vec3(0.68, 0.82, 0.92);
        vec3 dirtySnow  = vec3(0.72, 0.74, 0.72);

        vec3 polarCol = mix(blueGlace, snowCol, snowNoise);
        polarCol = mix(polarCol, dirtySnow, smoothstep(0.55, 0.88, glacierFlow) * 0.25);

        baseColor    = mix(baseColor, polarCol, polarIce);
        roughness    = mix(roughness, 0.92, polarIce);
        specStrength = mix(specStrength, 0.025, polarIce);
        aoFactor     = mix(aoFactor, aoFactor * mix(0.92, 1.08, snowNoise), polarIce);
        // Bump glaciar suave para casquetes polares.
        const float POLAR_BUMP_EPS = 0.006;
        vec3 pIT = normalize(surfacePos + Ts * POLAR_BUMP_EPS);
        vec3 pIB = normalize(surfacePos + Bs * POLAR_BUMP_EPS);

        float polarH  = fbm5(surfacePos * 28.0 + uSeed * 9.1) * 0.020
                    + ridgedFbm5(surfacePos * 7.0 + uSeed * 12.4) * 0.018;

        float polarHT = fbm5(pIT * 28.0 + uSeed * 9.1) * 0.020
                    + ridgedFbm5(pIT * 7.0 + uSeed * 12.4) * 0.018;

        float polarHB = fbm5(pIB * 28.0 + uSeed * 9.1) * 0.020
                    + ridgedFbm5(pIB * 7.0 + uSeed * 12.4) * 0.018;

        vec3 polarN = normalize(N - (T * (polarHT - polarH) + B * (polarHB - polarH)) * 5.0);
        Nf = normalize(mix(Nf, polarN, polarIce));
    }

    // ── 4. SOMBRA DE NUBES SOBRE EL TERRENO ─────────────────────
    // La capa de nubes VISIBLE ya no se pinta sobre esta malla (era una
    // "calcomania" plana sobre la corteza, sin volumen) -- ahora es una
    // capa propia a media altura entre la corteza y la atmosfera (ver
    // drawCloudShell, segunda/tercera pasada en DrawBody, renderer.h).
    // La SOMBRA que esa capa proyecta sobre el terreno SI es un efecto de
    // la corteza: se muestrea el mismo campo de nubes desplazado hacia
    // mainL (vector hacia el Sol) y oscurece el terreno debajo.
    if (uCloudDensity > 0.0) {
        float lo = mix(0.78, 0.30, uCloudDensity);
        float hi = mix(0.96, 0.55, uCloudDensity);

        // Mismo campo (dual-phase crossfade) que la capa visible de
        // nubes en drawCloudShell, desplazado hacia el Sol -- la sombra
        // queda alineada con la nube que la proyecta. pos3D ya esta en
        // el marco "pre-tilt" del cuerpo (undoAxialTilt), asi que mainL
        // (direccion al Sol, espacio de mundo) debe pasar por la MISMA
        // transformacion antes de combinarse con pos3D -- de lo
        // contrario la sombra se desalinearia de la nube en cuerpos
        // inclinados.
        vec3  mainLLocal  = undoAxialTilt(mainL);
        vec3  shadowDir   = normalize(pos3D + mainLLocal * 0.06);
        float shadowNoise = cloudField(shadowDir, windTime);
        float cloudShadow = smoothstep(lo, hi, shadowNoise);
        baseColor *= mix(1.0, 0.5, cloudShadow);
    }

    // ── 5. ILUMINACION: ambiente + difusa + especular ───────────
    // Se usa Nf (normal perturbada por el mapa de relieve en la Tierra,
    // o igual a N en el resto de mundos) para que las montanas reales
    // proyecten sombreado con el sol.
    float litMask = max(dot(Nf, mainL), 0.0);
    // Sin piso fijo (antes max(...,0.05)): con g_fakeLightEnabled
    // desactivado (ambientStrength=0, ver renderer.h) el lado nocturno de
    // un planeta sin estrella cercana queda realmente negro.
    vec3  rgb = baseColor * ambientColor * ambientStrength * aoFactor;

    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        // L y atten respecto a uBodyCenter (igual que mainL, ver arriba):
        // luz direccional constante sobre toda la esfera, sin "linterna".
        // atten ahora pondera por lightLumFactor (luminosidad REAL de la
        // fuente, ver mas arriba) en vez de solo distancia.
        vec3  L     = normalize(lightPos[i] - uBodyCenter);
        float ndl   = max(dot(Nf, L), 0.0);
        float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - uBodyCenter)));
        float spec  = pow(max(dot(Nf, normalize(L + V)), 0.0), mix(8.0, 256.0, 1.0 - roughness)) * specStrength;
        float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);
        rgb += baseColor * lightColor[i] * ndl * atten * vis;
        rgb += lightColor[i] * spec * ndl * atten * vis;
    }

    // ── 6. ATMOSFERA: PROFUNDIDAD OPTICA (dispersion/extincion) ──
    // El HALO geometrico (limbo que sobresale del disco) sigue siendo
    // la SEGUNDA esfera concentrica ~3% mas grande (drawAtmosphereShell,
    // mas abajo) -- eso no cambia. Aqui, sobre la propia corteza, se
    // aplica la aproximacion de dispersion simple (single-scattering,
    // Beer-Lambert): cuanto mas rasante es el angulo de vista (Nf casi
    // perpendicular a V), mayor el camino optico a traves de la
    // atmosfera, y la superficie se funde con el color atmosferico
    // (extincion de la luz reflejada + luz solar dispersada hacia la
    // camara). opticalDepth = (1-viewDot)^uAtmosphereFalloff: el
    // exponente (por planeta) fija el ANCHO de esta franja -- atmosferas
    // finas (Marte, falloff alto) solo afectan los pixeles mas rasantes
    // del limbo; atmosferas densas (Venus, falloff bajo) afectan gran
    // parte del disco visible.
    if (atmosphereDensity > 0.001) {
        float viewDot      = max(dot(Nf, V), 0.0);
        float opticalDepth = pow(1.0 - viewDot, uAtmosphereFalloff);
        float totalAtmosLight = 0.0;
        for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
            vec3 L = normalize(lightPos[i] - uBodyCenter);
            float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - uBodyCenter)));
            float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);

            totalAtmosLight += max(dot(Nf, L), 0.0) * atten * vis;
        }
        totalAtmosLight = clamp(totalAtmosLight, 0.0, 1.0);
        vec3  atmosTint    = atmosphereColor * mix(ambientStrength, 1.0, totalAtmosLight);
        rgb = mix(rgb, atmosTint, opticalDepth * atmosphereDensity);
    }

    // ── 7. LUCES NOCTURNAS (efecto Tierra) ──────────────────────
    // cityNoise/cityMask son 100% procedurales (ruido fbm5), sin relacion
    // con ninguna textura -- solo el gate decide DONDE pueden aparecer.
    // !isWater (damagedTerrain vs uWaterLevel, banda smoothstep de 0.06)
    // es preciso tierra adentro, pero esa banda es menos fiable en costas
    // (specMask real en transicion ~0.5): si ahi isWater cae en "false" y
    // el ruido de ciudad supera su umbral, aparecen luces sobre el oceano.
    // specMask < 0.1 exige ADEMAS "tierra firme" segun el mapa especular
    // REAL (estatico, independiente de uWaterLevel/evaporacion) -- ancla
    // las luces a la geografia real sin alterar isWater/waterMix (la
    // evaporacion de oceanos sigue intacta). En el resto de mundos
    // (uHasCityLights==0) specMask=0.0 por defecto, sin efecto.
    if (uHasCityLights > 0.5 && !isWater && specMask < 0.1) {
        float night = 1.0 - smoothstep(-0.15, 0.05, dot(N, mainL));
        if (night > 0.001) {
            float cityNoise = fbm5(surfacePos * 90.0 + uSeed * 5.0 + 12.3);
            float cityMask  = smoothstep(0.80, 0.94, cityNoise);
            rgb += vec3(1.0, 0.82, 0.45) * cityMask * night * 1.6;
        }
    }

    // ── 8a. BRILLO EMISIVO DEL CATACLISMO GLOBAL (paso 2d) ──────
    // Una bola de lava emite luz propia: visible incluso en el lado
    // nocturno, igual que el magma local de las cicatrices de impacto.
    if (globalMagma > 0.001) {
        rgb += globalMagmaColor * globalMagma * globalMagma * 1.8;
    }

    // ── 8b. ZONAS DE MAGMA LOCAL + ONDA EXPANSIVA (Sistema 3) ───
    // El brillo de magma NO depende de 'temp' (= b.temperature): esa
    // variable relaja hacia el equilibrio radiativo en la MISMA llamada
    // a StepPhysics que registra el impacto (Sistema 2, enfriamiento
    // Stefan-Boltzmann), asi que cualquier pico termico desaparece antes
    // de que se pinte el frame. En su lugar, cada cicatriz de impacto
    // (paso 2b) lleva su propia energia/edad (uImpactEnergy/uImpactAge):
    // el magma decae con su propia exponencial, visible un tiempo
    // proporcional a la violencia del impacto, sin importar que tan
    // rapido se enfrie el planeta en conjunto.
    for (int i = 0; i < uImpactCount && i < MAX_IMPACT_MARKS; i++) {
        float energyHeat = clamp((uImpactEnergy[i] - 1.0e6) / (5.0e7 - 1.0e6), 0.0, 1.0);
        if (energyHeat <= 0.001) continue;

        float coolingTime = mix(3000.0, 60000.0, energyHeat);
        float magmaHeat   = energyHeat * exp(-uImpactAge[i] / coolingTime);
        if (magmaHeat <= 0.001) continue;

        float d    = length(surfacePos - uImpactDir[i]);
        float zone = 1.0 - smoothstep(0.0, uImpactRadius[i] * 1.4, d);
        float glow = zone * magmaHeat;
        if (glow > 0.001) {
            vec3 magmaColor = blackBodyColor(mix(900.0, 4000.0, magmaHeat));
            rgb = mix(rgb, magmaColor, glow * 0.9);
            rgb += magmaColor * glow * glow * 1.5;
        }
    }

    // Onda expansiva de impacto: efecto pasajero que se propaga desde el
    // epicentro y se desvanece (no afecta el relieve permanente -- ver
    // impactCraterField/terrainHeight para la marca permanente, que cubre
    // tanto impactos como cráteres inversos por desprendimiento de marea).
    // Solo los impactos con energia cinetica externa (uImpactEnergy>0)
    // emiten esta onda de choque por COMPRESION (aro luminoso). Los
    // desprendimientos por marea (uImpactEnergy==0) ya no generan ningun
    // efecto pasajero adicional -- solo queda el crater permanente.
    for (int i = 0; i < uImpactCount && i < MAX_IMPACT_MARKS; i++) {
        if (uImpactEnergy[i] > 0.001) {
            const float waveDuration = 2500.0;
            if (uImpactAge[i] < waveDuration) {
                float d     = length(surfacePos - uImpactDir[i]);
                float front = (uImpactAge[i] / waveDuration) * 2.4;
                float fade  = 1.0 - (uImpactAge[i] / waveDuration);
                float ringMask = exp(-pow((d - front) / 0.12, 2.0));
                rgb += vec3(1.0, 0.95, 0.85) * ringMask * fade * fade * 2.0;
            }
        }
    }

    // ── AUTO-BRILLO POR CALOR GLOBAL (cuerpo negro) ──────────────
    // El magma de arriba (paso 8b) es LOCAL a las marcas de impacto y NO
    // depende de 'temp' (la temperatura global del cuerpo, ver comentario
    // en ese bloque). Este termino es el complementario: si el planeta
    // ENTERO esta lo bastante caliente (orbita rasante, post-impacto a
    // escala global, etc.) emite luz visible propia por radiacion termica,
    // visible incluso en su lado nocturno sin ninguna luz externa. Mismo
    // umbral (700K, Draper) que decide si este cuerpo ademas cuenta como
    // fuente de luz real para sus vecinos (ver renderer.h).
    if (temp > 700.0) {
        float glow = clamp((temp - 700.0) / 3500.0, 0.0, 1.0);
        rgb += blackBodyColor(temp) * glow * glow * 0.9;
    }

    rgb = pow(max(rgb / (rgb + vec3(1.0)), vec3(0.0)), vec3(1.0 / 2.2));
    finalColor = vec4(clamp(rgb, 0.0, 1.0), colDiffuse.a);
}

// ============================================================
//  CAPA DE ATMOSFERA INFLADA (segunda esfera concentrica)
//  Segunda pasada de dibujado sobre una malla esferica ~3% mas
//  grande que la corteza (ver DrawBody en renderer.h), con
//  blending alfa. Reemplaza el halo "pintado sobre la corteza"
//  (plano, sin volumen) por una capa con geometria propia: el
//  limbo sobresale realmente del disco del planeta.
//
//  Puramente difusa/dispersion por diseno: a diferencia de
//  drawRockyPlanet, esta funcion NO calcula ningun termino
//  especular (no hay specStrength/spec aqui). Los brillos
//  puntuales del Sol (reflejo en oceano/magma) solo deben verse
//  en la corteza, nunca "flotando" en el gas.
// ============================================================
void drawAtmosphereShell() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);

    // Efecto Fresnel / oclusion de borde (grosor optico): mirando al
    // centro del disco, N es casi paralela a V -> camino optico minimo a
    // traves de la capa -> casi transparente (se ve la corteza debajo).
    // Hacia el limbo, N es casi perpendicular a V -> camino optico
    // maximo -> capa densa, arco brillante caracteristico de una
    // atmosfera real vista de canto. uAtmosphereFalloff alto (>=4.0,
    // ver catalog.h/rocky_planets.h) concentra esta curva cerca del
    // limbo (rimFactor~1) y la anula en el resto del disco -- de lo
    // contrario el halo se mezcla con el relieve/bump-mapping de la
    // corteza y lava su contraste.
    float rimFactor = 1.0 - max(dot(N, V), 0.0);
    float alpha     = clamp(pow(rimFactor, uAtmosphereFalloff) * atmosphereDensity, 0.0, 1.0);

    // Desvanecido hacia el vacio (silueta exterior): con la formula de
    // arriba, alpha sigue cerca de su maximo (atmosphereDensity) hasta el
    // ULTIMO fragmento rasterizado de la malla 1.03R (rimFactor~1, vista
    // de canto), y al terminar la malla cae a "nada" (sin fragmento) en
    // un solo pixel -- el borde poligonal duro contra el espacio negro.
    // edgeFade rampa alpha a 0 en el ultimo 15% del rango de rimFactor
    // (0.85-1.0), difuminando esa transicion en varios pixeles en vez de
    // cortarla de golpe. Banda mas ancha que el 8% original (0.92-1.0):
    // con la banda estrecha, el alfa seguia alto en una franja amplia
    // cerca del limbo, y el sombreado de alto contraste de la corteza a
    // ese angulo rasante (bump-mapping del relieve, ver drawRockyPlanet)
    // se filtraba a traves de esa franja semitransparente -- "relieve
    // saliendo de la atmosfera".
    float edgeFade = 1.0 - smoothstep(0.85, 1.0, rimFactor);
    alpha *= edgeFade;

    // Iluminacion direccional (mismo criterio que drawRockyPlanet: luz
    // respecto al centro del planeta, rayos paralelos -- ver
    // uBodyCenter). El lado diurno muestra el color/brillo completo de
    // la atmosfera; el lado nocturno se atenua a un resplandor tenue
    // (dispersion residual/crepusculo) en vez de desaparecer de golpe
    // -- terminador difuminado, no un corte duro.
    float totalLight = 0.0;

    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        vec3 L = normalize(lightPos[i] - uBodyCenter);
        float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - uBodyCenter)));
        float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);

        totalLight += softTerminator(N, L, atmosphereDensity) * atten * vis;
    }

    totalLight = clamp(totalLight, 0.0, 1.0);
    vec3 rgb = atmosphereColor * (ambientStrength + totalLight);

    finalColor = vec4(rgb, alpha);
}

// ============================================================
//  CAPA DE NUBES (tercera esfera concentrica, radio intermedio)
//  Tercera pasada de dibujado sobre una malla esferica entre la
//  corteza (radio normal) y la atmosfera inflada (~1.03x, ver
//  drawAtmosphereShell) -- ver DrawBody en renderer.h. Sustituye la
//  antigua "calcomania" de nubes pintada sobre la corteza: ahora las
//  nubes tienen geometria propia y flotan realmente sobre el relieve.
//  La SOMBRA que proyectan sobre el terreno sigue calculandose en
//  drawRockyPlanet (paso 4), con el mismo campo de ruido.
// ============================================================
void drawCloudShell() {
    vec3 N     = normalize(fragNormal);
    vec3 V     = normalize(viewPos - fragPosition);
    vec3 pos3D = undoAxialTilt(N);

    // Mismo campo de nubes (dual-phase flow map crossfade, ver
    // cloudField()/cloudLayerNoise() mas arriba) que la sombra calculada
    // en el paso 4 de drawRockyPlanet -- ambas capas quedan alineadas
    // (pos3D usa la MISMA undoAxialTilt() en ambas funciones).
    // Esta malla se dibuja SIN el contra-spin de surfacePos (ver
    // TidalBodyTransform/capa de nubes en renderer.h: solo escala +
    // tilt + traslacion), asi que pos3D ya es la coordenada equivalente
    // a la que usa drawRockyPlanet.
    float windTime  = spinPhase * 0.0008;
    float cloudNoise = cloudField(pos3D, windTime);
    float lo = mix(0.78, 0.30, uCloudDensity);
    float hi = mix(0.96, 0.55, uCloudDensity);
    float cloudCoverage = smoothstep(lo, hi, cloudNoise);

    if (cloudCoverage <= 0.001) {
        finalColor = vec4(0.0);
        return;
    }

    // Iluminacion: mismo criterio direccional que el resto del planeta
    // (luz respecto a uBodyCenter, rayos paralelos -- ver
    // drawRockyPlanet/drawAtmosphereShell). Lado diurno: nube iluminada
    // por el sol; lado nocturno: solo la luz ambiental tenue.

    float totalLight = 0.0;
    float totalScatter = 0.0;

    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        vec3 L = normalize(lightPos[i] - uBodyCenter);
        float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - uBodyCenter)));
        float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);

        // Para nubes no usamos un corte duro tipo Lambert puro.
        // Una atmósfera/nube gruesa dispersa luz alrededor del terminador.
        float daySoft = softTerminator(N, L, atmosphereDensity);
        float ndl = max(dot(N, L), 0.0);

        totalLight   += ndl * atten * vis;
        totalScatter += daySoft * atten * vis;
    }

    totalLight = clamp(totalLight, 0.0, 1.0);
    totalScatter = clamp(totalScatter, 0.0, 1.0);

    // Nubes finas: más dependientes de luz directa.
    // Nubes gruesas tipo Venus: más dominadas por dispersión múltiple.
    float thickCloud = smoothstep(0.8, 1.2, uCloudDensity);

    vec3 directCloud  = uCloudColor * totalLight;
    vec3 scatterCloud = mix(uCloudColor, atmosphereColor, 0.35) * totalScatter * mix(0.35, 0.95, thickCloud);
    vec3 ambientCloud = uCloudColor * ambientColor * ambientStrength;

    vec3 cloudLit = ambientCloud + directCloud + scatterCloud;

    // Misma profundidad optica (single-scattering) que el paso 6 de
    // drawRockyPlanet: cerca del limbo las nubes tambien se funden con
    // el halo atmosferico (mismas uAtmosphereFalloff/atmosphereColor/
    // atmosphereDensity por cuerpo).
    if (atmosphereDensity > 0.001) {
        float viewDot      = max(dot(N, V), 0.0);
        float opticalDepth = pow(1.0 - viewDot, uAtmosphereFalloff);
        vec3  atmosTint    = atmosphereColor * mix(ambientStrength, 1.0, totalLight);
        cloudLit = mix(cloudLit, atmosTint, opticalDepth * atmosphereDensity);
    }

    // Cobertura total (uCloudDensity>=1.0, hoy solo Venus -- ver
    // MakeVenusProfile, cloudDensity=2.5): alfa=1.0 (100% opaco) en vez
    // de cloudCoverage*0.85. Real: la capa de acido sulfurico de Venus
    // tiene ~20km de espesor y es opticamente gruesa -- la corteza NUNCA
    // es visible desde el espacio. Con alfa<1, el relieve de alto
    // contraste de la corteza (bump-mapping del "infierno basaltico") se
    // filtraba a traves de la capa ("relieve visible a traves del gas").
    float cloudAlpha = (uCloudDensity >= 1.0) ? 0.96 : cloudCoverage * 0.85;
    finalColor = vec4(cloudLit, cloudAlpha);
}

// ============================================================
//  MAIN
// ============================================================
void main() {
    if (isAtmosphereShell == 1) {
        drawAtmosphereShell();
        return;
    }
    if (isCloudShell == 1) {
        drawCloudShell();
        return;
    }
    if (isStar == 1) {
        drawStar();
        return;
    }
    if (isGasGiant == 1) {
        drawGasGiant();
        return;
    }
    if (isRockyPlanet == 1) {
        drawRockyPlanet();
        return;
    }

    // ── Planetas ─────────────────────────────────────────────
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    vec4 texSample = texture(texture0, fragTexCoord);
    vec3 baseColor = texSample.rgb * colDiffuse.rgb;
    // Sin piso fijo (ver drawRockyPlanet): con la luz "falsa" desactivada
    // y sin estrella/cuerpo caliente cerca, este cuerpo queda negro real.
    vec3 rgb = baseColor * ambientColor * ambientStrength;

    for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
        vec3  L    = normalize(lightPos[i] - fragPosition);
        float ndl  = smoothstep(
            mix(-0.04, -0.20, atmosphereDensity),
            mix( 0.04,  0.10, atmosphereDensity),
            dot(N, L)) * max(dot(N, L), 0.0);

        float spec  = pow(max(dot(N, normalize(L + V)), 0.0), 32.0) * 0.08;
        float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - fragPosition)));
        float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);
        rgb += (baseColor * lightColor[i] * ndl + lightColor[i] * spec) * atten * vis;
    }

    if (atmosphereDensity > 0.001) {
        float limb = pow(1.0 - max(dot(N, V), 0.0), uAtmosphereFalloff) * atmosphereDensity;
        float totalLight = 0.0;
        for (int i = 0; i < lightCount && i < MAX_LIGHTS; i++) {
            vec3 L = normalize(lightPos[i] - fragPosition);
            float atten = visualLightResponse(physicalLightAttenuation(lightLum[i], length(lightPos[i] - fragPosition)));
            float vis = lightVisibilityFromPoint(fragPosition, i) * ringShadowTransmission(fragPosition, i);

            totalLight += max(dot(N, L), 0.0) * atten * vis;
        }
        totalLight = clamp(totalLight, 0.0, 1.0);
        rgb += atmosphereColor * limb * (ambientStrength + totalLight * 0.8);
    }

    // Auto-brillo por calor (cuerpo negro) -- mismo termino/umbral que
    // drawRockyPlanet/drawGasGiant, usando blackBodyColor() en vez del
    // degradado naranja->blanco fijo de antes (mas preciso y consistente
    // con el resto del motor).
    if (temp > 700.0) {
        float glow = clamp((temp - 700.0) / 3500.0, 0.0, 1.0);
        rgb += blackBodyColor(temp) * glow * glow * 0.9;
    }

    if (heatSpike > 0.0)
        rgb = mix(rgb, mix(vec3(0.85, 0.15, 0.02), vec3(1.0, 0.90, 0.35), heatSpike), heatSpike * 0.65);

    rgb = pow(max(rgb / (rgb + vec3(1.0)), vec3(0.0)), vec3(1.0 / 2.2));
    finalColor = vec4(clamp(rgb, 0.0, 1.0), colDiffuse.a);
}
)GLSL";

// ============================================================
//  Shader de PROMINENCIAS ESTELARES (volumétrico, raymarching)
//  Renderiza arcos de plasma que sobresalen de la superficie.
// ============================================================
// ── Shader de llamaradas solares (ribbon arch mesh, basado en tutorial Blender) ──────
static const char* FLARE_VERT_SRC = R"GLSL(
#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;

out vec2 fragUV;

uniform mat4 mvp;

// Multiplica SOLO la altura radial del arco, no su punto de anclaje.
uniform float flareHeightMult;
uniform float flareMode;   // 0=arco, 1=jet/pluma, 2=erupcion fallida/puff
uniform float flareAsym;   // asimetria lateral/altura
uniform float flareBurst;  // intensidad de explosion/caos

const float BASE_R = 1.0;

// Proyecta una geometría local con eje radial +Z sobre una esfera
// de radio shellR. Esto garantiza length(pos) == shellR aunque
// haya anchura lateral en X/Y.
vec3 projectRadialShell(vec3 p, float shellR) {
    float lateral2 = dot(p.xy, p.xy);
    float z = sqrt(max(shellR * shellR - lateral2, 0.000001));
    return vec3(p.x, p.y, z);
}

void main() {
    fragUV = vertexTexCoord;
    float t = vertexTexCoord.x;

    // Modo 0: arco clásico/ribbon.
    if (flareMode < 0.5) {
        vec2 xz = vertexPosition.xz;
        float radial = length(xz);
        float phi = atan(xz.x, xz.y);
        vec2 dir = vec2(sin(phi), cos(phi));
        float extra = max(radial - BASE_R, 0.0);

        float y = vertexPosition.y;

        // Radio esférico base del arco.
        float shellR = BASE_R + extra * flareHeightMult;

        // ANCLAJE DE PIES:
        // En los extremos del arco, hundimos un poco la geometría dentro de la estrella.
        // Esto evita que el flare parezca flotar sobre la superficie por la curvatura,
        // el bloom o el blending aditivo.
        // Pie de salida: pequeño y limpio.
        float footA = 1.0 - smoothstep(0.00, 0.105, t);

        // Pie de entrada/caída: MÁS ancho.
        // Este es el que faltaba visualmente: si es igual de pequeño que footA,
        // el arco parece quedarse alto antes de tocar la estrella.
        float footB = 1.0 - smoothstep(0.00, 0.175, 1.0 - t);

        // Hundimiento asimétrico:
        // - salida: apenas se mete, porque ya se ve bien.
        // - entrada: se mete más para que parezca que realmente entra en la fotosfera.
        const float FOOT_SINK_OUT = 0.018;
        const float FOOT_SINK_IN  = 0.034;

        shellR -= max(FOOT_SINK_OUT * footA, FOOT_SINK_IN * footB);

        // Seguridad: no dejar que el ancho lateral supere el radio corregido.
        shellR = max(shellR, BASE_R * 0.96);

        // Corrige el radio XZ para que sqrt(xz^2 + y^2) == shellR.
        float correctedRadial = sqrt(max(shellR * shellR - y * y, 0.000001));

        float side = vertexTexCoord.y - 0.5;
        float twist = sin(t * 11.0 + flareAsym * 31.0) * side * 0.035 * flareBurst;
        float cs = cos(twist);
        float sn = sin(twist);

        vec3 pos = vec3(dir.x * correctedRadial, y, dir.y * correctedRadial);

        // Twist alrededor del eje radial/estrella. Mantiene distancia.
        pos.xz = vec2(pos.x * cs - pos.z * sn, pos.x * sn + pos.z * cs);

        gl_Position = mvp * vec4(pos, 1.0);
        return;
    }

    // Modo 1 y 2: geometría real no-ribbon.
    vec3 pos = vertexPosition;

    // En estas mallas, pos.z representa el radio-shell base antes de corregir
    // por anchura lateral. shellR será el radio real final.
    float extra = max(pos.z - BASE_R, 0.0);
    float shellR = BASE_R;

    if (flareMode < 1.5) {
        // Jet: alarga radialmente.
        shellR = BASE_R + extra * flareHeightMult;

        // Turbulencia lateral, pero la proyección posterior mantiene
        // la base pegada al radio real de la estrella.
        float wob = sin(t * 21.0 + flareAsym * 17.0) * 0.06 * flareBurst;
        pos.x += pos.x * wob;
        pos.y += pos.y * wob;
    }
    else {
        // Puff / erupción fallida:
        // grande, abierta y filamentosa; NO una bola/hongo.
        shellR = BASE_R + extra * flareHeightMult;

        float fan = smoothstep(0.05, 0.75, t);

        // Aplastar el volumen para que lea como abanico de plasma.
        pos.y *= mix(0.72, 0.48, fan);

        // Ligero estiramiento lateral, no expansión radial tipo explosión.
        pos.x *= 1.0 + fan * flareBurst * 0.16;

        // Pequeño desgarro lateral animado por forma, no por tiempo:
        // estable durante el evento, sin parpadeo.
        pos.x += sin(t * 17.0 + flareAsym * 9.0) * 0.025 * flareBurst * fan;
        pos.y += sin(t * 11.0 + flareAsym * 13.0) * 0.012 * flareBurst * fan;
    }

    pos = projectRadialShell(pos, shellR);

    gl_Position = mvp * vec4(pos, 1.0);
}

)GLSL";

static const char* FLARE_FRAG_SRC = R"GLSL(
#version 330
in  vec2 fragUV;
out vec4 finalColor;

uniform float realTime;
uniform float temp;
uniform float stellarActivity;
uniform float flareIndex;
uniform float flareGrow;    // 0=nada visible, 1=arco completo (revelado progresivo)

uniform float flareFade;

uniform float flareMode;
uniform float flareAsym;
uniform float flareBurst;

uniform float flareDrainMode;
uniform float flareDrainStart;

float invSmooth(float hi, float lo, float x) {
    return 1.0 - smoothstep(lo, hi, x);
}

// ── Hash / valor aleatorio 2D ─────────────────────────────────
float fh(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5); }
float n2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    return mix(mix(fh(i), fh(i+vec2(1,0)), f.x),
               mix(fh(i+vec2(0,1)), fh(i+vec2(1,1)), f.x), f.y);
}
// FBM a diferentes densidades (tutorial: Musgrave + Noise como capas separadas)
float fbm3(vec2 p){float v=0.,a=.5;for(int i=0;i<3;i++){v+=a*n2(p);p*=2.1;a*=.5;}return v;}
float fbm4(vec2 p){float v=0.,a=.5;for(int i=0;i<4;i++){v+=a*n2(p);p*=2.1;a*=.5;}return v;}
float fbm5(vec2 p){float v=0.,a=.5;for(int i=0;i<5;i++){v+=a*n2(p);p*=2.1;a*=.5;}return v;}

// Tutorial: color ramp negro → rojo oscuro → naranja → amarillo brillante
// Estrellas calientes desplazan hacia azul/violeta (B/O); frias hacia rojo/naranja (M/K)
vec3 flareCol(float t, float tK) {
    float tF = clamp((tK - 3000.0) / 27000.0, 0.0, 1.0);
    vec3 c1 = mix(vec3(0.80, 0.10, 0.01), vec3(0.12, 0.08, 0.82), tF * 0.6);
    vec3 c2 = mix(vec3(1.00, 0.42, 0.02), vec3(0.42, 0.30, 1.00), tF * 0.5);
    vec3 c3 = mix(vec3(1.00, 0.88, 0.35), vec3(0.85, 0.85, 1.00), tF * 0.4);
    t = clamp(t, 0.0, 1.0);
    if (t < 0.35) return mix(vec3(0.0), c1, t / 0.35);
    if (t < 0.65) return mix(c1, c2, (t - 0.35) / 0.30);
    return             mix(c2, c3, (t - 0.65) / 0.35);
}

void main() {
    vec2  uv  = fragUV;   // uv.x = a lo largo del arco (0→1), uv.y = ancho de cinta (0→1)

    float mDwarf = 1.0 - smoothstep(3400.0, 4100.0, temp);
    float kDwarf = smoothstep(3800.0, 4800.0, temp) * (1.0 - smoothstep(5100.0, 5600.0, temp));
    float gDwarf = smoothstep(5200.0, 5600.0, temp) * (1.0 - smoothstep(6100.0, 6600.0, temp));

    float spectralAct = clamp(stellarActivity * (1.0 + mDwarf * 1.7 + kDwarf * 0.55 - gDwarf * 0.35), 0.02, 1.0);
    float act = spectralAct;

    // M/K frías: llamaradas más vivas y más “gruesas”; G: más tranquilas.
    float spd = mix(0.45, 2.2, clamp((temp - 2800.0) / 24000.0, 0.0, 1.0));
    spd *= mix(1.0, 1.65, mDwarf);
    spd *= mix(1.0, 0.78, gDwarf);

    // Tutorial: variacion por posicion de objeto (empty node) → seed por llamarada
    float seed = flareIndex * 1.618 + 0.31;

    // Animacion 5-7x mas rapida que antes para que el fuego se vea vivo
    float W    = realTime * 0.22 * spd + seed * 2.7;
    float flow = realTime * 0.28 * spd + seed * 1.4;

    vec2 p = vec2(uv.x * 2.8 + flow, uv.y * 1.3 + W);

    // Tutorial: Musgrave (grueso) + Noise (fino) con domain warping
    float coarse = fbm3(p * 1.8 + vec2(seed * 2.3, seed * 1.1));
    float mid    = fbm4(p * 3.8 + vec2(coarse * 1.8, seed * 0.9));
    float fine   = fbm5(p * 2.3 + vec2(mid * 1.3, coarse * 0.7));

    // Erosion de alta frecuencia: rompe el silhouette rectangular en bordes llameantes
    float erosion = fbm3(p * 5.8 + vec2(seed * 3.1, W * 1.5)) * 0.38;
    float fireVal = clamp((fine - erosion * 0.55) * 1.5, 0.0, 1.0);

    // Mascaras duras: flareMode llega como 0, 1 o 2 desde C++.
    // Esto evita que los modos se mezclen visualmente y parezcan todos iguales.
    float archMask = 1.0 - step(0.5, abs(flareMode - 0.0));
    float jetMask  = 1.0 - step(0.5, abs(flareMode - 1.0));
    float puffMask = 1.0 - step(0.5, abs(flareMode - 2.0));

    // Perfil visual por modo.
    // Arco: sube y baja.
    // Jet: crece hacia la punta.
    // Puff: máximo cerca de la explosión, no en el centro del arco.
    float archT = sin(3.14159265 * uv.x);
    float archHeightF = archT * archT;

    float burstCenterForHeight = mix(0.30, 0.62, flareAsym);
    float jetHeightF  = pow(clamp(uv.x, 0.0, 1.0), 0.85);
    float puffHeightF = exp(-pow((uv.x - burstCenterForHeight) / 0.22, 2.0));

    float heightF =
        archHeightF * archMask +
        jetHeightF  * jetMask +
        puffHeightF * puffMask;

    // Footpoints: manchas calientes donde nace/termina la llamarada.
    // En jets solo domina el pie izquierdo; en arcos se ven ambos pies.
    // Pie de salida: compacto.
    float footA = exp(-pow(uv.x / 0.055, 2.0));

    // Pie de entrada: MÁS ancho y más visible.
    // Si este pie es igual al de salida, el lado que cae/entra se pierde
    // con el fade del borde y parece flotar.
    float footB = exp(-pow((1.0 - uv.x) / 0.095, 2.0));

    // Para arcos regulares queremos ambos pies,
    // pero el de entrada necesita refuerzo visual.
    float archFootMask = footA + footB * 1.55;

    float footMask = mix(archFootMask, footA, jetMask);
    footMask = mix(footMask, footA * 1.25, puffMask);

    // Explosion interna para erupciones fallidas.
    float burstCenter = mix(0.30, 0.62, flareAsym);
    float burstBlob = exp(-pow((uv.x - burstCenter) / mix(0.12, 0.24, flareBurst), 2.0));
    burstBlob *= smoothstep(0.02, 0.35, uv.y) * smoothstep(0.02, 0.35, 1.0 - uv.y);

    // Color: frio/H-alpha en pies, caliente y brillante en la cima
    vec3 col = mix(flareCol(fireVal * 0.65, temp * 0.74),
                   flareCol(fireVal * 1.08, temp),
                   heightF);

    // Emision HDR para que el bloom la capture bien
    float flareScale = 1.0 + mDwarf * 1.6 + kDwarf * 0.45 - gDwarf * 0.25;
    col *= (2.4 + act * 3.2 * flareScale) * (0.32 + heightF * (0.85 + mDwarf * 0.55));

    // Bordes mas estrechos para arcos mas finos e irregulares
    float eL  = smoothstep(0.0, 0.075, uv.x);
    float eR  = smoothstep(0.0, 0.075, 1.0 - uv.x);
    float eY0 = smoothstep(0.0, 0.10, uv.y);
    float eY1 = smoothstep(0.0, 0.12, 1.0 - uv.y);
    float ribbonEdgeMask = eL * eR * eY0 * eY1;

    // En mallas tubulares/blob, UV.y es ángulo alrededor del volumen,
    // no “ancho de cinta”; no queremos borde rectangular de ribbon.
    float volumeEdgeMask = eL * eR;

    float edgeMask =
        ribbonEdgeMask * archMask +
        volumeEdgeMask * jetMask +
        volumeEdgeMask * puffMask;

    // Umbral mas alto + erosion para bordes irregulares/flameantes
    float noiseAlpha = smoothstep(0.28, 0.68, fine - erosion);

    // Recorte del arco.
    // Modo normal:
    //   visible desde uv.x=0 hasta uv.x=flareGrow.
    // Modo drenaje:
    //   visible desde uv.x=flareDrainStart hasta uv.x=1.
    // Esto hace que el plasma se reabsorba por el segundo pie, no por el pie de origen.
    float drainMode = step(0.5, flareDrainMode);

    if (drainMode > 0.5) {
        if (uv.x < flareDrainStart) discard;
    }
    else {
        if (uv.x > flareGrow) discard;
    }

    // Borde activo.
    // En modo normal, el borde activo está en flareGrow.
    // En modo drenaje, el borde activo está en flareDrainStart.
    float tipFade = 1.0;

    if (drainMode > 0.5) {
        tipFade = smoothstep(flareDrainStart, min(1.0, flareDrainStart + 0.12), uv.x);
    }
    else {
        tipFade = (flareGrow >= 1.0)
                ? 1.0
                : invSmooth(flareGrow, max(0.0, flareGrow - 0.12), uv.x);
    }

    float widthBoost = 1.0 + mDwarf * 0.65 + kDwarf * 0.20;
    // Jets: se erosionan mas hacia la punta, como material expulsado/deshilachado.
    float jetTipFade = mix(1.0, 1.0 - smoothstep(0.72, 1.0, uv.x) * 0.45, jetMask);

    // Failed eruption: no forma arco completo; se corta despues de la burbuja.
    float puffCut = mix(
        1.0,
        1.0 - smoothstep(burstCenter + 0.10, burstCenter + 0.22, uv.x),
        puffMask
    );

    if (puffMask > 0.5 && uv.x > burstCenter + 0.30) discard;

    // Romper zonas internas para que no sea una cinta constante.
    float holesFreqY = mix(7.0, 2.5, max(jetMask, puffMask));
    float holes = fbm4(vec2(uv.x * 9.0 + seed, uv.y * holesFreqY + realTime * 0.35));
    float holeMask = mix(1.0, smoothstep(0.18, 0.72, holes), 0.28 + flareBurst * 0.22);

    // La base debe brillar aunque el ruido principal sea bajo.
    float footAlpha = footMask * (0.45 + act * 0.75) * (0.65 + flareBurst * 0.45);

    // La explosion fallida tiene cuerpo propio.
    float burstAlpha = burstBlob * puffMask * (0.18 + flareBurst * 0.22);

    // Los arcos regulares necesitan cuerpo visible propio.
    // Antes dependían demasiado de noiseAlpha*act, y en estrellas tipo Sol
    // quedaban casi invisibles comparados con jets/puffs.
    float archCenterMask =
        smoothstep(0.04, 0.22, uv.y) *
        smoothstep(0.04, 0.22, 1.0 - uv.y);

    float archBodyAlpha =
        archMask *
        archHeightF *
        archCenterMask *
        (0.18 + act * 0.35 + gDwarf * 0.28);

    // Boost SOLO para arcos. Jets/puffs ya tenían bastante presencia.
    float archVisibilityBoost = 1.0 + archMask * (0.70 + gDwarf * 1.05);

    float alpha =
        (noiseAlpha * edgeMask * act * tipFade * widthBoost * jetTipFade * puffCut * holeMask * archVisibilityBoost)
        + footAlpha
        + burstAlpha
        + archBodyAlpha;

    // Brillo extra en la base del arco (plasma mas denso junto a la cromosfera)
    // Manchas calientes en los pies: H-alpha/amarillo/blanco segun temperatura.
    vec3 footCol = flareCol(0.92, temp * mix(0.85, 1.25, act)) * (1.5 + flareBurst * 1.4);

    // Brillo base de ambos pies.
    col += footCol * footMask * (1.2 + act * 2.0);

    // Refuerzo extra SOLO para el pie de entrada del arco regular.
    // Esto crea la sensación de que el plasma está tocando/entrando en la estrella.
    col += footCol * footB * archMask * (0.9 + act * 1.4);

    // Cuerpo del arco clásico: visible en G/Sol sin convertirlo en foco nuclear.
    vec3 archCoreCol = flareCol(0.95, temp) * (1.0 + gDwarf * 1.35);
    col += archCoreCol * archBodyAlpha;

    // Nucleo explosivo para erupciones que no completan loop.
    vec3 burstCol = flareCol(0.85, temp * 1.10) * (0.65 + flareBurst * 0.75);
    col += burstCol * burstBlob * puffMask;

    // Base densa cromosferica.
    float baseGlow = footMask * 0.85;
    col += col * baseGlow;

    alpha *= flareFade;
        col   *= mix(0.35, 1.0, flareFade);

    if (alpha < 0.010) discard;
    finalColor = vec4(col, alpha);
}
)GLSL";

// ============================================================
//  Shader de SUPERNOVA — raymarcher volumétrico
//  Adaptado del shader de nube volumétrica de Godot (box raymarch con
//  volume texture) a un raymarch ESFÉRICO con ruido FBM PROCEDURAL, para
//  no depender de texturas 3D (que raylib no maneja bien). Se dibuja sobre
//  un cubo que acota la esfera de la explosión; el fragment traza un rayo
//  desde la cámara, interseca la esfera analíticamente y acumula emisión
//  volumétrica (filamentos por domain-warp FBM) con una paleta de color que
//  depende del progenitor. Ver DrawSupernova (renderer.h).
// ============================================================
static const char* SUPERNOVA_VERT_SRC = R"GLSL(
#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 vWorld;
void main() {
    vWorld      = (matModel * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

static const char* SUPERNOVA_FRAG_SRC = R"GLSL(
#version 330
in  vec3 vWorld;
out vec4 finalColor;

uniform vec3  uCamPos;      // cámara en espacio de dibujo
uniform vec3  uCenter;      // centro de la explosión
uniform float uRadius;      // radio de la esfera acotante (draw units)
uniform float uTime;        // reloj de animación (tiempo simulado escalado)
uniform float uProgress;    // 0→1 avance de la supernova
uniform float uBrightness;  // brillo global (curva de luz + glow base)
uniform float uOpacity;     // presencia/desvanecimiento (0=invisible, 1=pleno)
uniform float uSeed;        // variación por estrella
uniform vec3  uColorCore;   // núcleo caliente
uniform vec3  uColorMid;    // capa media
uniform vec3  uColorEdge;   // filamentos externos fríos

float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float vnoise(vec3 p){
    vec3 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    float n000=h3(i), n100=h3(i+vec3(1,0,0));
    float n010=h3(i+vec3(0,1,0)), n110=h3(i+vec3(1,1,0));
    float n001=h3(i+vec3(0,0,1)), n101=h3(i+vec3(1,0,1));
    float n011=h3(i+vec3(0,1,1)), n111=h3(i+vec3(1,1,1));
    return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),
               mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);
}
float fbm(vec3 p){
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++){ v += a*vnoise(p); p = p*2.02 + vec3(1.7, 9.2, 3.3); a *= 0.5; }
    return v;
}
// FBM ligero (3 octavas) para deformaciones direccionales baratas.
float fbm3(vec3 p){
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++){ v += a*vnoise(p); p = p*2.03 + vec3(1.7); a *= 0.5; }
    return v;
}

// Intersección rayo-esfera centrada en origen. x=entrada, y=salida (y<x => miss)
vec2 hitSphere(vec3 ro, vec3 rd, float rad){
    float b = dot(ro, rd);
    float c = dot(ro, ro) - rad*rad;
    float d = b*b - c;
    if (d < 0.0) return vec2(1.0, -1.0);
    float s = sqrt(d);
    return vec2(-b - s, -b + s);
}

// Densidad volumétrica en coord local q (esfera unitaria), r=|q|
float density(vec3 q, float r){
    vec3 dir = q / max(r, 1e-4);

    // ── Romper FUERTE la simetría esférica ──
    // El radio del cascarón se DESPLAZA según la dirección con ruido a 3 escalas:
    // la superficie deja de ser esfera y se vuelve muy grumosa/lobulada, con
    // protuberancias que sobresalen (como los remanentes reales, nada regular).
    float lump  = fbm3(dir*1.5  + vec3(uSeed*3.0))        - 0.5;   // lóbulos grandes
    float lump2 = fbm3(dir*3.3  + vec3(uSeed*1.7 + 11.0)) - 0.5;   // grumos medios
    float lump3 = fbm3(dir*6.5  + vec3(uSeed*0.9 + 23.0)) - 0.5;   // detalle fino
    float shellC = mix(0.05, 0.66, smoothstep(0.0, 1.0, uProgress));
    shellC += lump*0.60 + lump2*0.32 + lump3*0.16;                 // deformación fuerte

    // Grosor del cascarón muy variable por dirección (zonas gruesas y huecas).
    float sigma = mix(0.50, 0.18, smoothstep(0.0, 1.0, uProgress));
    sigma *= 0.40 + fbm3(dir*2.5 + vec3(uSeed))*1.4;
    float shell = exp(-((r - shellC)*(r - shellC)) / (2.0*sigma*sigma));

    // Rayos/ejecta radiales MUY marcados (chorros que apuntan hacia afuera).
    float rays = fbm3(dir*8.0 + vec3(uSeed*2.3));
    rays = pow(clamp(rays, 0.0, 1.0), 4.0) * 3.0;

    // Filamentos finos con domain warp FUERTE (aspecto fibroso/turbulento).
    vec3 base = q*2.2 + vec3(uSeed);
    vec3 flow = vec3(0.0, uTime*0.06, uTime*0.03);
    vec3 warp = vec3(fbm3(base + flow),
                     fbm3(base + vec3(5.2,1.3,8.7) - flow),
                     fbm3(base + vec3(2.9,7.1,4.4)));
    float fil = fbm(q*6.5 + warp*3.6 + vec3(uSeed*1.7) + flow*0.5);
    fil = pow(clamp(fil, 0.0, 1.0), 1.8);

    // ── Relleno interior turbulento ──
    // Sin esto el interior (r < shellC) queda casi vacío/liso y el núcleo se ve
    // como una bola redonda. Añade nubes/wisps turbulentos hacia el centro,
    // deformados por su propio ruido, para que el interior tenga estructura
    // irregular (no una esfera).
    float interiorMask = smoothstep(shellC + sigma*1.6, 0.0, r);   // fuerte hacia el centro
    float interiorFil  = fbm(q*4.2 + warp*2.4 + vec3(uSeed*4.3 + 17.0));
    interiorFil = pow(clamp(interiorFil, 0.0, 1.0), 1.6);
    float interior = interiorMask * interiorFil * 0.75;

    // Filamentos base + ejecta radial acentuada + relleno interior.
    float d = shell * (fil + fil*rays*0.9) + interior;
    d *= smoothstep(1.28, 0.55, r); // deja que los lóbulos sobresalgan mucho más
    return d;
}

vec3 palette(float t){
    t = clamp(t, 0.0, 1.0);
    vec3 c = mix(uColorEdge, uColorMid, smoothstep(0.0, 0.55, t));
    c      = mix(c, uColorCore, smoothstep(0.55, 1.0, t));
    return c;
}

void main(){
    vec3 ro = uCamPos - uCenter;            // origen relativo al centro
    vec3 rd = normalize(vWorld - uCamPos);
    // Esfera de marcha 1.4× mayor que uRadius: los lóbulos deformados llegan
    // hasta r~1.2 (normalizado), así que la esfera acotante debe ir más allá de
    // r=1 o los cortaría planos (volviendo a verse esférico en el borde).
    vec2 tt = hitSphere(ro, rd, uRadius * 1.4);
    if (tt.y < tt.x) discard;

    float t0 = max(tt.x, 0.0);
    float t1 = tt.y;
    const int STEPS = 56;
    float dt = (t1 - t0) / float(STEPS);
    // Jitter por-fragmento del primer paso: dispersa las posiciones de muestreo
    // para que detalles finos (anillo/frente de choque delgados, más finos que
    // dt) se rendericen suaves en vez de entrecortados, sin subir STEPS.
    float jitter = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float t  = t0 + dt*jitter;

    vec3  col = vec3(0.0);
    float transmittance = 1.0;

    // Progreso LOCAL de la explosión: 0 en la emergencia (uProgress 0.045) → 1 al
    // final. Se usa para que el frente de choque y el anillo arranquen DESDE EL
    // CENTRO (núcleo de la estrella) en vez de aparecer ya afuera -- con uProgress
    // directo, pow(0.045,exp) ya daba un radio grande al emerger.
    float blastProg = clamp((uProgress - 0.045) / 0.955, 0.0, 1.0);

    // Frente de choque: cascarón MUY delgado y brillante que corre hacia afuera
    // (blast wave de la implosión nuclear). Nace en el centro y revienta afuera.
    float shockFront = mix(0.03, 1.05, pow(blastProg, 0.5));
    float shockInt   = (1.0 - smoothstep(0.0, 0.55, uProgress)); // fuerte al inicio

    // ── Anillo de onda expansiva 3D (blast ring) ──
    // Disco delgado toroidal que se expande en un plano, perpendicular a un eje
    // determinista por estrella (uSeed). Es el "anillo de choque" característico
    // (tipo SN 1987A / anillo sci-fi) que revienta hacia afuera y se disipa.
    // Arranca desde el centro (ringR~0) y se expande rápido.
    vec3 ringAxis = normalize(vec3(sin(uSeed*12.9 + 0.3),
                                   cos(uSeed*7.3),
                                   sin(uSeed*4.1 + 1.0)));
    float ringR   = mix(0.02, 1.28, pow(blastProg, 0.4));      // nace en el núcleo, expande rápido
    float ringInt = (1.0 - smoothstep(0.05, 0.80, uProgress));  // fuerte al inicio, se disipa

    for (int i = 0; i < STEPS; i++){
        vec3 pos = ro + rd*t;
        vec3 q   = pos / uRadius;
        float r  = length(q);
        float dens = density(q, r);

        // Contribución del frente de choque (rim delgado brillante).
        float shockGlow = exp(-((r - shockFront)*(r - shockFront)) / (2.0*0.055*0.055));
        dens += shockGlow * shockInt * 0.9;

        // Contribución del anillo (toro delgado en el plano perpendicular al eje).
        float hAx     = dot(q, ringAxis);                 // distancia fuera del plano
        float rInPl   = length(q - ringAxis*hAx);         // radio dentro del plano
        float ringRad = exp(-((rInPl - ringR)*(rInPl - ringR)) / (2.0*0.03*0.03)); // más delgado (radial)
        float ringDsk = exp(-(hAx*hAx) / (2.0*0.045*0.045)); // disco más delgado (fuera del plano)
        // Irregularidad azimutal del anillo (grumoso, no un toro perfecto).
        vec3  dir     = q / max(r, 1e-4);
        float ringMod = 0.5 + fbm3(dir*5.0 + vec3(uSeed*3.7))*1.1;
        float ring    = ringRad * ringDsk * ringMod;
        dens += ring * ringInt * 1.2;

        if (dens > 0.002){
            // Radio TÉRMICO deformado: sin esto el núcleo caliente (color core,
            // azul) es función pura de r → una esfera perfecta. Se perturba con
            // ruido posicional + direccional para romper esa esfericidad y darle
            // grumos/lóbulos irregulares al interior.
            float coreWarp = (fbm(q*3.0 + vec3(uSeed*2.0) + vec3(0.0, uTime*0.04, 0.0)) - 0.5) * 0.60
                           + (fbm3(dir*2.2 + vec3(uSeed*5.1)) - 0.5) * 0.45;
            float rHeat = clamp(r + coreWarp, 0.0, 2.0);
            float heat = clamp(1.0 - rHeat*1.15, 0.0, 1.0);   // núcleo caliente (deformado)
            float ct   = heat*0.65 + dens*0.35;
            vec3 emit  = palette(ct);
            // Destello de núcleo al inicio (implosión → bola de fuego).
            emit += uColorCore * heat*heat * (1.0 - uProgress) * 1.8;
            // El frente de choque emite un tono cálido (no blanco puro, para no
            // saturar) de intensidad moderada.
            emit += mix(uColorMid, uColorCore, 0.55) * shockGlow * shockInt * 2.0;
            // El anillo emite un tono cálido (mezcla núcleo/medio), brillo moderado.
            emit += mix(uColorMid, uColorCore, 0.55) * ring * ringInt * 3.5;
            // Profundidad óptica NORMALIZADA por el radio: sin dividir por uRadius,
            // 'dt' (en unidades de dibujo absolutas) hace que la acumulación
            // dependa del tamaño absoluto de la explosión (de miles a cientos de
            // miles de unidades) y sature -> bola sólida. Normalizada, la
            // translucidez volumétrica es igual a cualquier escala.
            // uOpacity escala la profundidad óptica: col y alpha (1-transmittance)
            // se desvanecen JUNTOS, evitando que quede una esfera oscura al bajar
            // solo el brillo (blending premultiplicado).
            float dd = dens * (dt / uRadius) * 9.0 * uOpacity;
            col += emit * dd * transmittance;
            transmittance *= exp(-dd * 1.5);
            if (transmittance < 0.01) break;
        }
        t += dt;
    }

    col *= uBrightness;
    col  = col / (1.0 + col);                 // tonemap suave (evita saturación)
    float alpha = clamp(1.0 - transmittance, 0.0, 1.0);
    if (alpha < 0.003) discard;
    finalColor = vec4(col, alpha);            // premultiplicado (BLEND_ALPHA_PREMULTIPLY)
}
)GLSL";

// ============================================================
//  Shader de ESCOMBROS 3D instanciados (campo de polvo/anillos)
//  Una unica malla low-poly (octaedro irregular, BuildLowPolyRockMesh
//  en renderer.h) se dibuja decenas de miles de veces con
//  DrawMeshInstanced -- la posicion/escala/rotacion de cada roca llega
//  por el atributo de instancia 'instanceTransform' (un VBO de matrices
//  4x4, una por particula), nunca por uniforms. Iluminacion difusa simple
//  (Lambert + ambiente), con uLightDir/uLightIntensity/uAmbientStrength
//  subidos POR GRUPO (no un unico valor global, ver DrawDustField3D en
//  renderer.h): cada anillo usa la direccion/intensidad real hacia la
//  estrella relativa a SU planeta (con sombra propia), y el polvo de
//  colision usa una aproximacion global -- suficiente para que cada roca
//  se lea como un solido 3D sin necesitar el sistema de luces completo
//  del shader principal.
// ============================================================
static const char* ROCK_INSTANCE_VERT_SRC = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in mat4 instanceTransform;
out vec3 fragNormal;
out vec3 fragPos;
uniform mat4 mvp;
void main() {
    fragNormal  = normalize(mat3(instanceTransform) * vertexNormal);
    fragPos     = (instanceTransform * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);
}
)GLSL";

static const char* ROCK_INSTANCE_FRAG_SRC = R"GLSL(
#version 330
in  vec3 fragNormal;
in  vec3 fragPos;
out vec4 finalColor;
uniform vec3  uLightDir;  // direccion (normalizada) HACIA la estrella, relativa al host (ver DrawDustField3D)
uniform vec3  uBaseColor; // color base del lote (material del anillo/polvo, ver DrawDustField3D)
uniform vec3  uViewPos;   // posicion de camara en espacio de dibujado (ver DrawDustField3D)
// Ambiente ligado a g_fakeLightEnabled (renderer.h): 0.15 si la luz "falsa"
// esta activa, 0.0 si no -- desactivada, sin estrella real cerca, el polvo
// queda negro real igual que cualquier otro cuerpo del motor.
uniform float uAmbientStrength;
// Intensidad difusa real: incluye la luminosidad de la estrella y la
// atenuacion por distancia (misma formula 2000*lightLumFactor/dist que el
// resto de cuerpos, calculada en C++ en DrawDustField3D ya que este shader
// no recibe lightLum/posiciones por-particula) y el atenuado artistico
// 0.6 de siempre (evita el aspecto "plastico brillante" de un difuso
// pleno).
uniform float uLightIntensity;
// Fraccion de luz directa que SI llega (1 = totalmente iluminado, 0 =
// eclipsado por el propio planeta host) -- ver sombra cilindrica suave en
// DrawDustField3D. Sustituye al viejo apagado binario (uLightDir=0): ahora
// el termino difuso/especular se atenua de forma continua, con un borde de
// penumbra en vez de un corte duro entre luz y sombra.
uniform float uShadowFactor;
// Intensidad de translucidez/retro-dispersion: las particulas de anillo
// (hielo/polvo fino) dejan pasar algo de luz cuando estan a contraluz,
// generando el clasico brillo de borde de los anillos vistos a contraluz.
// 0 para polvo de colision (rocas opacas, sin este efecto).
uniform float uTranslucency;
void main() {
    // Las rocas no llevan textura: a esta escala (instanciado masivo) no
    // vale la pena depender del uniform colDiffuse del material -- el
    // color base llega como uniform por lote, fijado por DrawDustField3D
    // segun el planeta duenio del anillo (o un gris por defecto para
    // polvo de colision).
    vec3 baseColor = uBaseColor;
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(uViewPos - fragPos);
    vec3 L = uLightDir;

    float lit = clamp(uShadowFactor, 0.0, 1.0);

    float diffuse = max(dot(N, L), 0.0) * lit;

    // Especular suave (Blinn-Phong de bajo brillo): destellos de hielo/roca
    // pulida en el anillo, mas notorios cerca del angulo de reflexion
    // especular que de la normal directa al sol.
    vec3 H = normalize(L + V);
    float specAngle = max(dot(N, H), 0.0);
    float specular  = pow(specAngle, 28.0) * lit * 0.35;

    // Translucidez a contraluz: cuando la cara visible mira casi opuesta a
    // la luz (dot(N,L) muy negativo) pero la camara la ve de frente,
    // particulas finas de hielo/polvo dejan pasar luz como si brillaran
    // desde dentro -- efecto clasico de los anillos fotografiados a
    // contraluz. Se atenua igual por 'lit' para que el lado realmente
    // eclipsado por el planeta no brille de la nada.
    float backLight   = max(dot(-N, L), 0.0);
    float rim          = pow(1.0 - max(dot(N, V), 0.0), 2.0);
    float translucent = backLight * rim * uTranslucency * lit;

    float light = diffuse + specular + translucent;
    finalColor = vec4(light * uLightIntensity * baseColor + uAmbientStrength * baseColor, 1.0);
}
)GLSL";
