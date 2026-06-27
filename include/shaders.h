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
uniform float uSeed;       // semilla procedural (rpSeed, ver renderer.h)
uniform float uPotatoAmp;  // amplitud de deformacion "papa" (0 = sin cambio)

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
    } else {
        // uPotatoAmp==0 (planetas): identico al shader original.
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
//  ESTRELLA PROCEDURAL (Remastered — plasma, prominencias, flares)
// ============================================================
void drawStar() {
    vec3  N  = normalize(fragNormal);
    vec3  V  = normalize(viewPos - fragPosition);
    float mu = clamp(dot(N, V), 0.0, 1.0);
    float oneMu = 1.0 - mu;

    // Tiempo y rotacion diferencial
    float ts       = spinPhase * 0.0003;
    float rotSpeed = clamp(2.0 / max(0.1, stellarMass), 0.15, 10.0);
    float rA  = ts * rotSpeed;
    float crA = cos(rA), srA = sin(rA);
    vec3 rN = vec3(N.x*crA - N.z*srA, N.y, N.x*srA + N.z*crA);
    float sA  = rA * 0.6;
    float crS = cos(sA), srS = sin(sA);
    vec3 sN = vec3(N.x*crS - N.z*srS, N.y, N.x*srS + N.z*crS);

    vec3 baseColor = boostSat(artisticStarColor(temp), 1.75);

    // granContrast: cuanto del patron de granulacion se mezcla (0=uniforme, 1=maximo)
    float cellScale = 3.5, granContrast = 1.0;
    if      (stellarMass > 8.0 && temp < 6000.0)  { cellScale = 1.3; granContrast = 1.0; }
    else if (temp < 4000.0)                         { cellScale = 4.5; granContrast = 0.90; }
    else if (temp > 8000.0  && temp < 25000.0)      { cellScale = 7.0; granContrast = 0.28; }
    else if (temp >= 25000.0)                       { cellScale = 4.0; granContrast = 0.07; }

    // ─── SUPERGRANULACION: celdas grandes, lentas ──────────────
    vec3 sgPos  = rN * (cellScale * 0.28) + vec3(ts*0.11, ts*0.06, ts*0.04);
    float sgWx  = fbm3(sgPos * 0.7 + vec3(3.1, 7.4, ts*0.2)) - 0.5;
    float sgWz  = fbm3(sgPos * 0.7 + vec3(6.8, 1.9, ts*0.15)) - 0.5;
    float superGran = granulation(sgPos + vec3(sgWx, 0.0, sgWz) * 0.6);
    float sgSharp   = smoothstep(0.15, 0.85, superGran);

    // ─── GRANULACION MEDIA: celdas convectivas superficiales ───
    vec3 baseGPos = rN * cellScale + vec3(ts*0.9, ts*0.55, ts*0.35);
    float wx = fbm3(baseGPos * 0.65 + vec3(1.7, 9.2, ts*0.4)) - 0.5;
    float wz = fbm3(baseGPos * 0.65 + vec3(8.3, 2.8, ts*0.3)) - 0.5;
    float gran     = granulation(baseGPos + vec3(wx, 0.0, wz) * 0.55);
    float granSharp = smoothstep(0.18, 0.82, gran);

    // ─── TURBULENCIA INTERGRANULAR: detalle fino en los bordes ─
    vec3 igPos   = rN * (cellScale * 5.5) + vec3(ts*2.6, ts*1.4, ts*0.85);
    float igTurb = (fbm5(igPos) - 0.5) * 0.13;

    // ─── COLOR DE SUPERFICIE CON ALTO CONTRASTE ────────────────
    // Carriles intergranulares: plasma frio, casi negro
    vec3 laneCol = artisticStarColor(temp * 0.83) * 0.25;
    // Interiores de celda: plasma caliente, brillante (HDR deliberado)
    vec3 cellCol = mix(baseColor, boostSat(artisticStarColor(min(40000.0, temp * 1.09)), 1.75), 0.28) * 1.70;
    // Red magnetica en bordes de supergranulas: ligeramente mas caliente
    vec3 netCol  = artisticStarColor(min(40000.0, temp * 1.14)) * 0.85;

    // Composicion: granulacion media modulada por supergranulacion
    vec3 granSurface = mix(laneCol, cellCol, granSharp);
    granSurface *= mix(0.72, 1.02, sgSharp);
    granSurface += netCol * (1.0 - sgSharp) * stellarActivity * 0.30;
    granSurface += baseColor * igTurb;               // turbulencia fina

    // Blend entre base uniforme y patron de granulacion segun tipo estelar
    vec3 surfColor = mix(baseColor, granSurface, granContrast);

    // ─── OSCURECIMIENTO AL LIMBO ────────────────────────────────
    float ldA = 0.52, ldB = 0.22;
    if      (temp < 4000.0)                     { ldA = 0.68; ldB = 0.27; }
    else if (temp >= 8000.0  && temp < 20000.0) { ldA = 0.35; ldB = 0.12; }
    else if (temp >= 20000.0)                   { ldA = 0.20; ldB = 0.06; }
    float ld = max(0.08, 1.0 - ldA*oneMu - ldB*oneMu*oneMu);
    surfColor *= ld;

    // ─── MANCHAS ESTELARES ──────────────────────────────────────
    if (stellarActivity > 0.04 && temp < 16000.0) {
        float latZone = 1.0 - smoothstep(0.25, 0.95, abs(sN.y));
        float sn      = fbm5(sN * 2.6 + vec3(0.0, ts*0.08, 0.0));
        float thr     = 0.68 - stellarActivity * 0.28;
        float bigSpot = smoothstep(thr, thr + 0.13, sn) * latZone;
        float sn2     = fbm3(sN * 5.8 + vec3(ts*0.22, 0.0, ts*0.14));
        float smlSpot = smoothstep(0.62, 0.74, sn2) * latZone * 0.55;
        float spotF   = clamp(bigSpot + smlSpot, 0.0, 0.90) * stellarActivity;
        if (spotF > 0.01) {
            vec3 spotCol = blackBodyColor(temp * (0.70 + 0.12*(1.0-stellarActivity))) * 0.35;
            surfColor = mix(surfColor, spotCol, spotF);
        }
    }

    // ─── FACULAS Y PUNTOS BRILLANTES MAGNETICOS ─────────────────
    // (valores > 1.0 intencionales: simula hotspots visibles como blanco)
    if (stellarActivity > 0.05 && temp < 15000.0) {
        float fn  = fbm3(sN * 4.2 + vec3(ts*1.3, ts*0.45, 0.0));
        float fac = max(0.0, fn - 0.50) * stellarActivity * 2.2;
        float fn2 = fbm5(sN * 9.8 + vec3(ts*2.4, ts*1.1, ts*0.6));
        float bp  = pow(max(0.0, fn2 - 0.71), 1.4) * stellarActivity * 7.0;
        vec3 facCol = blackBodyColor(min(40000.0, temp * 1.22)) * 2.8;
        vec3 bpCol  = blackBodyColor(min(40000.0, temp * 1.38)) * 4.8;
        surfColor  += facCol * fac + bpCol * bp;
    }

    // ─── FIBRILLAS DE PLASMA EN TODO EL DISCO ───────────────────
    if (stellarActivity > 0.05 && temp < 20000.0) {
        vec3  fibPos  = rN * (cellScale * 8.8) + vec3(ts*3.1, ts*1.8, ts*0.9);
        float fibN    = fbm5(fibPos);
        float fibMask = max(0.0, fibN - 0.53);
        float latAct  = smoothstep(0.55, 0.15, abs(rN.y)) * stellarActivity;
        surfColor    += baseColor * fibMask * latAct * 1.2;
    }

    // ─── CROMOSFERA ─────────────────────────────────────────────
    float chromoFade  = smoothstep(0.28, 0.0, mu);
    float chromoNoise = fbm3(rN * 7.5 + vec3(ts*1.9, ts*0.8, 0.0));
    vec3  chromoCol   = blackBodyColor(clamp(temp * 1.15, 2000.0, 40000.0)) * 1.4;
    surfColor = mix(surfColor, chromoCol, chromoFade * 0.45 * (0.4 + chromoNoise * 1.2));

    // ─── ESPICULAS ───────────────────────────────────────────────
    if (mu < 0.26 && temp < 15000.0) {
        float spicN    = fbm5(rN * 13.0 + vec3(ts*3.5, ts*1.8, 0.0));
        float spicMask = smoothstep(0.26, 0.0, mu);
        surfColor += baseColor * smoothstep(0.56, 0.74, spicN) * spicMask * 1.1;
    }

    // ─── CORONA ─────────────────────────────────────────────────
    float coronaVar = 1.0 + fbm3(vec3(rN.x*2.1, ts*0.13, rN.z*2.1)) * 0.55;
    float rim       = (pow(oneMu, 5.5) * 3.5 + pow(oneMu, 2.8) * 0.28) * coronaVar;
    float cScale    = clamp(0.55 + log(max(1.0, stellarLuminosity)) * 0.12, 0.30, 5.0);
    if (temp > 8000.0)  cScale *= 1.6;
    if (temp > 25000.0) cScale *= 2.5;
    surfColor += blackBodyColor(min(40000.0, temp * 1.4)) * cScale * rim;

    // ─── PROMINENCIAS Y ERUPCIONES (shader) ─────────────────────
    if (stellarActivity > 0.02 && temp < 33000.0) {
        float limbZone = smoothstep(0.33, 0.0, mu);
        float pA  = atan(sN.z, sN.x);
        float pn1 = fbm3(vec3(pA*3.1 + ts*0.42, sN.y*5.2, ts*0.21));
        float pn2 = fbm3(vec3(pA*6.3 + ts*0.68, sN.y*8.8, ts*0.50) + vec3(1.9,0.7,0.4));
        float prom = smoothstep(0.63, 0.83, pn1) * smoothstep(0.59, 0.79, pn2) * limbZone
                     * stellarActivity * 4.5;
        surfColor += mix(blackBodyColor(clamp(temp*0.80,2000.0,8000.0)),
                         blackBodyColor(min(40000.0,temp*1.9))*2.8, pn2) * prom;

        float flareN   = fbm5(sN * 2.9 + vec3(ts*1.55, ts*0.72, 0.0));
        float flareThr = 0.79 - stellarActivity * 0.19;
        float flare    = smoothstep(flareThr, flareThr+0.09, flareN)
                         * (limbZone*0.65 + smoothstep(0.90,0.70,mu)*0.35)
                         * stellarActivity * 4.2;
        vec3 flareTint = vec3(1.5, 0.50, 0.04) * (1.0 - clamp(temp/11000.0, 0.0, 1.0));
        surfColor += (blackBodyColor(min(40000.0,temp*2.3))*3.8 + flareTint) * flare;
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

    // ─── TONEMAPPING FILMIC EN LUMINANCIA + GAMMA ───────────────
    // 1-exp aplicado a la luminancia (no por canal): preserva el matiz/
    // saturacion del color de entrada. El tonemapping por canal hacia
    // converger todo a blanco al comprimir valores HDR brillantes.
    surfColor  = max(surfColor, vec3(0.0));
    surfColor *= 1.45;
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

        baseColor    = mix(landCol, waterCol, waterMix);
        roughness    = mix(0.92, mix(0.02, 0.18, waveFine), waterMix);  // tierra rugosa <-> oceano con brillo "centelleante"

        // Mascara especular REAL: specMask (canal R de uSpecularMap) ya
        // es ~0 sobre tierra/nubes y ~1 sobre oceano. Multiplicar
        // directamente por specMask (en vez de partir de un piso 0.05)
        // anula el brillo especular en tierra -- sin esto, el "punto de
        // brillo" del Sol aparecia tambien sobre los continentes,
        // dandoles un aspecto de "bola de billar de plastico". Solo el
        // oceano refleja el punto brillante del Sol.
        specStrength = specMask * mix(1.0, 2.2, waveFine);

        // Relieve: perturbar la normal con el mapa de normales real
        // (de [0,1] a [-1,1]), para que las montanas proyecten sombreado
        // con el sol. Sobre agua se usa en cambio un bump de oleaje
        // (derivadas finitas del ruido oceanico, animado).
        vec3 mapN = normalize(T * nmSample.x + B * nmSample.y + N * max(nmSample.z, 0.2));
        float wD  = 0.04;
        float wdx = fbm5(oceanPos + vec3(wD,0.0,0.0)) - fbm5(oceanPos - vec3(wD,0.0,0.0));
        float wdz = fbm5(oceanPos + vec3(0.0,0.0,wD)) - fbm5(oceanPos - vec3(0.0,0.0,wD));
        vec3 waveN = normalize(N + T * wdx * 1.5 + B * wdz * 1.5);

        vec3 landNf  = normalize(mix(N, mapN,  0.7));
        vec3 waterNf = normalize(mix(N, waveN, 0.6));
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
            float tsunamiHere = fuerzaTsunami * waterMix;
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
            baseColor    = uColorWater;
            roughness    = 0.05;   // agua liquida: baja rugosidad...
            specStrength = 0.01;   // ...pero SIN brillo especular (ver nota mas abajo)

            // Oleaje violento del tsunami: la superficie se aplana
            // hacia su normal de reposo (N) y se cubre de espuma.
            if (fuerzaTsunami > 0.001) {
                Nf = normalize(mix(Nf, N, fuerzaTsunami));
                baseColor = mix(baseColor, vec3(0.85, 0.88, 0.92), fuerzaTsunami * 0.6);
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
        baseColor    = mix(baseColor, vec3(1.0), polarIce);
        roughness    = mix(roughness, 0.85, polarIce); // hielo mate
        specStrength = mix(specStrength, 0.05, polarIce);
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
in vec2 texcoord0;
out vec2 fragUV;
uniform mat4 mvp;
void main() {
    fragUV      = texcoord0;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
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
    float act = clamp(stellarActivity, 0.1, 1.0);

    // Tutorial: velocidad de animacion proporcional a temperatura
    float spd  = mix(0.4, 2.5, clamp((temp - 3000.0) / 27000.0, 0.0, 1.0));
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

    // Factor de altura en el arco: 0 en los pies, 1 en la cima
    float archT   = sin(3.14159265 * uv.x);
    float heightF = archT * archT;

    // Color: frio/H-alpha en pies, caliente y brillante en la cima
    vec3 col = mix(flareCol(fireVal * 0.65, temp * 0.74),
                   flareCol(fireVal * 1.08, temp),
                   heightF);

    // Emision HDR para que el bloom la capture bien
    col *= (2.8 + act * 2.2) * (0.38 + heightF * 0.85);

    // Bordes mas estrechos para arcos mas finos e irregulares
    float eL  = smoothstep(0.0, 0.13, uv.x);
    float eR  = smoothstep(0.0, 0.13, 1.0 - uv.x);
    float eY0 = smoothstep(0.0, 0.10, uv.y);
    float eY1 = smoothstep(0.0, 0.12, 1.0 - uv.y);
    float edgeMask = eL * eR * eY0 * eY1;

    // Umbral mas alto + erosion para bordes irregulares/flameantes
    float noiseAlpha = smoothstep(0.28, 0.68, fine - erosion);

    // Revelado progresivo: plasma avanzando desde el pie izquierdo (UV.x=0) al pie derecho
    // UV.x > flareGrow = region aun no formada, se descarta
    if (uv.x > flareGrow) discard;
    // Punta del arco: borde frontal suavizado para simular plasma activo avanzando
    float tipFade = (flareGrow >= 1.0)
                    ? 1.0
                    : smoothstep(flareGrow, max(0.0, flareGrow - 0.12), uv.x);
    float alpha = noiseAlpha * edgeMask * act * tipFade;

    // Brillo extra en la base del arco (plasma mas denso junto a la cromosfera)
    float baseGlow = smoothstep(0.5, 0.0, archT) * 0.80;
    col += col * baseGlow;

    if (alpha < 0.006) discard;
    finalColor = vec4(col, alpha);
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
uniform mat4 mvp;
void main() {
    fragNormal  = normalize(mat3(instanceTransform) * vertexNormal);
    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);
}
)GLSL";

static const char* ROCK_INSTANCE_FRAG_SRC = R"GLSL(
#version 330
in  vec3 fragNormal;
out vec4 finalColor;
uniform vec3  uLightDir;  // direccion (normalizada) HACIA la estrella, relativa al host (ver DrawDustField3D)
uniform vec3  uBaseColor; // color base del lote (material del anillo/polvo, ver DrawDustField3D)
// Ambiente ligado a g_fakeLightEnabled (renderer.h): 0.15 si la luz "falsa"
// esta activa, 0.0 si no -- desactivada, sin estrella real cerca, el polvo
// queda negro real igual que cualquier otro cuerpo del motor.
uniform float uAmbientStrength;
// Intensidad difusa real: incluye la luminosidad de la estrella y la
// atenuacion por distancia (misma formula 2000*lightLumFactor/dist que el
// resto de cuerpos, calculada en C++ en DrawDustField3D ya que este shader
// no recibe lightLum/posiciones por-particula) y el atenuado artistico
// 0.6 de siempre (evita el aspecto "plastico brillante" de un difuso
// pleno). 0 en el bucket de sombra (ver uLightDir=(0,0,0) ahi).
uniform float uLightIntensity;
void main() {
    // Las rocas no llevan textura: a esta escala (instanciado masivo) no
    // vale la pena depender del uniform colDiffuse del material -- el
    // color base llega como uniform por lote, fijado por DrawDustField3D
    // segun el planeta duenio del anillo (o un gris por defecto para
    // polvo de colision).
    vec3 baseColor = uBaseColor;
    float diffuse  = max(dot(fragNormal, uLightDir), 0.0);
    finalColor = vec4((diffuse * uLightIntensity + uAmbientStrength) * baseColor, 1.0);
}
)GLSL";
