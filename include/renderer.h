#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "body.h"
#include "constants.h"
#include "math_utils.h"
#include "textures.h"
#include "stellar_evolution.h"

// ============================================================
//  Sistema de renderizado
// ============================================================

// ── Generación de esfera con UVs equirectangulares ──────────
inline Mesh BuildUVSphere(int rings = 32, int slices = 32) {
    int triCount = rings * slices * 2;

    float* vpos = (float*)RL_MALLOC((rings+1)*(slices+1)*3*sizeof(float));
    float* vnrm = (float*)RL_MALLOC((rings+1)*(slices+1)*3*sizeof(float));
    float* vuv  = (float*)RL_MALLOC((rings+1)*(slices+1)*2*sizeof(float));

    for (int r = 0; r <= rings; r++) {
        float v   = (float)r / (float)rings;
        float phi = v * PI;
        for (int s = 0; s <= slices; s++) {
            float u     = (float)s / (float)slices;
            float theta = u * 2.0f * PI;
            int   idx   = r * (slices+1) + s;
            float x =  sinf(phi) * cosf(theta);
            float y =  cosf(phi);
            float z = -sinf(phi) * sinf(theta);
            vpos[idx*3+0] = x; vpos[idx*3+1] = y; vpos[idx*3+2] = z;
            vnrm[idx*3+0] = x; vnrm[idx*3+1] = y; vnrm[idx*3+2] = z;
            vuv [idx*2+0] = u; vuv [idx*2+1] = v;
        }
    }

    Mesh m = {0};
    m.vertexCount   = triCount * 3;
    m.triangleCount = triCount;
    m.vertices  = (float*)RL_MALLOC(m.vertexCount * 3 * sizeof(float));
    m.normals   = (float*)RL_MALLOC(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)RL_MALLOC(m.vertexCount * 2 * sizeof(float));

    int vi = 0;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < slices; s++) {
            int i0 = r*(slices+1)+s, i1=i0+1, i2=i0+(slices+1), i3=i2+1;
            int tris[6] = {i0, i2, i1, i1, i2, i3};
            for (int t = 0; t < 6; t++, vi++) {
                int idx = tris[t];
                m.vertices [vi*3+0] = vpos[idx*3+0];
                m.vertices [vi*3+1] = vpos[idx*3+1];
                m.vertices [vi*3+2] = vpos[idx*3+2];
                m.normals  [vi*3+0] = vnrm[idx*3+0];
                m.normals  [vi*3+1] = vnrm[idx*3+1];
                m.normals  [vi*3+2] = vnrm[idx*3+2];
                m.texcoords[vi*2+0] = vuv [idx*2+0];
                m.texcoords[vi*2+1] = vuv [idx*2+1];
            }
        }
    }
    RL_FREE(vpos); RL_FREE(vnrm); RL_FREE(vuv);
    UploadMesh(&m, false);
    return m;
}

// ── Malla low-poly de "roca espacial" (icosaedro subdividido) ──────────
// Icosaedro de 12 vertices subdividido un nivel (42 vertices unicos, 80
// caras triangulares), con cada vertice desplazado por un jitter aleatorio
// fijo (seed determinista -> misma malla siempre) para que parezca una roca
// irregular. Normal por vertice = direccion radial post-jitter (mismo atajo
// que BuildUVSphere, lineas 35-36: normal=posicion en la esfera unitaria),
// dando sombreado suave en vez de las 8 caras planas del octaedro anterior.
// Una unica instancia de esta malla, dibujada con DrawMeshInstanced
// (DrawDustField3D), representa TODAS las particulas de polvo/escombros del
// campo -- el tamano/posicion/rotacion de cada una viene del atributo de
// instancia 'instanceTransform', no de la geometria. Emision no indexada
// (80*3=240 vertices), misma convencion que BuildUVSphere/octaedro previo.
inline Mesh BuildLowPolyRockMesh(unsigned int seed = 1337u) {
    // Icosaedro estandar (razon aurea), normalizado a radio 1.
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    std::vector<Vector3> verts = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };
    for (Vector3& vtx : verts) vtx = Vector3Normalize(vtx);

    // Tabla estandar de 20 caras del icosaedro (orden antihorario visto
    // desde afuera, igual convencion que el octaedro previo).
    std::vector<std::array<int,3>> faces = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9}, {5,11,4}, {11,10,2}, {10,7,6}, {7,1,8},
        {3,9,4}, {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5}, {2,4,11}, {6,2,10}, {8,6,7}, {9,8,1},
    };

    // Subdivision de 1 nivel (geodesic icosphere): cada arista se divide en
    // su punto medio (normalizado a radio 1), cacheado por par de indices
    // para que aristas compartidas entre caras adyacentes produzcan el
    // MISMO vertice (sin esto, vertices "duplicados" en la misma posicion
    // recibirian jitters distintos -> costuras visibles). 12 + 30 aristas =
    // 42 vertices unicos, 20*4 = 80 caras.
    std::map<std::pair<int,int>, int> midCache;
    auto midpoint = [&](int a, int b) -> int {
        std::pair<int,int> key = (a < b) ? std::make_pair(a,b) : std::make_pair(b,a);
        auto it = midCache.find(key);
        if (it != midCache.end()) return it->second;
        Vector3 m = Vector3Normalize(Vector3Scale(Vector3Add(verts[a], verts[b]), 0.5f));
        int idx = (int)verts.size();
        verts.push_back(m);
        midCache[key] = idx;
        return idx;
    };

    std::vector<std::array<int,3>> subFaces;
    subFaces.reserve(faces.size() * 4);
    for (const auto& f : faces) {
        int a = f[0], b = f[1], c = f[2];
        int ab = midpoint(a,b), bc = midpoint(b,c), ca = midpoint(c,a);
        subFaces.push_back({a, ab, ca});
        subFaces.push_back({b, bc, ab});
        subFaces.push_back({c, ca, bc});
        subFaces.push_back({ab, bc, ca});
    }

    // Jitter determinista por vertice (LCG, mismo patron que FastRand01 en
    // physics.h), independiente para cada uno de los 42 vertices via
    // 'seed ^ (i * 2654435761u)' (constante de dispersion de Knuth, separa
    // los estados iniciales de vertices con indices cercanos).
    //
    // JITTER=0.12: el octaedro original usaba 0.35 sobre 6 vertices
    // (separacion angular ~90 grados entre vecinos). El icosaedro
    // subdividido tiene 42 vertices con separacion angular ~32 grados (la
    // mitad, por la subdivision). Escalando el jitter por la razon de
    // espaciados (~32/90~0.35) se mantiene la misma "rugosidad relativa"
    // percibida con vertices ~3x mas densos: 0.35*0.35~=0.12. Jitter mayor
    // arriesga auto-interseccion (el inradio de cada triangulo a este nivel
    // de subdivision es ~0.25, y 0.12 deja margen >2x).
    constexpr float JITTER = 0.12f;
    std::vector<Vector3> normals(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        unsigned int state = seed ^ (unsigned int)(i * 2654435761u);
        auto rnd01 = [&]() -> float {
            state = state * 1664525u + 1013904223u;
            return (float)(state >> 8) * (1.0f / 16777216.0f); // [0,1)
        };
        Vector3 jitter = { (rnd01()*2.0f-1.0f)*JITTER, (rnd01()*2.0f-1.0f)*JITTER, (rnd01()*2.0f-1.0f)*JITTER };
        verts[i] = Vector3Add(verts[i], jitter);
        normals[i] = Vector3Normalize(verts[i]);
    }

    Mesh m = {0};
    m.triangleCount = (int)subFaces.size();  // 80
    m.vertexCount   = m.triangleCount * 3;   // 240
    m.vertices  = (float*)RL_MALLOC(m.vertexCount * 3 * sizeof(float));
    m.normals   = (float*)RL_MALLOC(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)RL_MALLOC(m.vertexCount * 2 * sizeof(float));

    int vi = 0;
    for (const auto& f : subFaces) {
        Vector3 v0 = verts[f[0]];
        Vector3 v1 = verts[f[1]];
        Vector3 v2 = verts[f[2]];

        Vector3 faceN = Vector3Normalize(
            Vector3CrossProduct(
                Vector3Subtract(v1, v0),
                Vector3Subtract(v2, v0)
            )
        );

        for (int k = 0; k < 3; ++k, ++vi) {
            int idx = f[k];

            m.vertices[vi*3+0] = verts[idx].x;
            m.vertices[vi*3+1] = verts[idx].y;
            m.vertices[vi*3+2] = verts[idx].z;

            m.normals[vi*3+0] = faceN.x;
            m.normals[vi*3+1] = faceN.y;
            m.normals[vi*3+2] = faceN.z;

            m.texcoords[vi*2+0] = 0.0f;
            m.texcoords[vi*2+1] = 0.0f;
        }
    }
    UploadMesh(&m, false);
    return m;
}

// ── Cálculo de color de un body ─────────────────────────────
inline Color ComputeBodyColor(const Body& b, const Vector3& drawPos,
                               const std::vector<Body>& bodies)
{
    Color c = b.color;

    if (b.isStar) {
        float t  = ClampF((float)(b.temperature - 1000.0f) / 15000.0f, 0.0f, 1.0f);
        float r, g, bc;
        if (t < 0.5f) {
            r = 1.0f; g = 0.55f + 0.45f*(t*2.0f); bc = 0.12f + 0.83f*(t*2.0f);
        } else {
            r = 1.0f - 0.25f*(t-0.5f)*2.0f; g = 0.95f + 0.1f*(t-0.5f)*2.0f; bc = 0.95f + 0.05f*(t-0.5f)*2.0f;
        }
        c = { (unsigned char)ClampF(r*255,0,255),
              (unsigned char)ClampF(g*255,0,255),
              (unsigned char)ClampF(bc*255,0,255), 255 };
    }
    else if (b.isFragment) {
        float h = std::max(b.heatSpike, 0.4f);
        c = { (unsigned char)ClampF(b.color.r*(1-h)+255*h, 0,255),
              (unsigned char)ClampF(b.color.g*(1-h)+160*h, 0,255),
              (unsigned char)ClampF(b.color.b*(1-h)+ 20*h, 0,255), 255 };
    }
    else {
        float illum = 0.15f;
        for (const Body& s : bodies)
            if (s.isStar && s.mass > 0.0)
                illum = std::max(illum, ClampF(2000.0f / std::max(Vector3Distance(ToDrawPos(s.pos), drawPos), 1.0f), 0.0f, 1.5f));
        c = { (unsigned char)ClampF(b.color.r*illum, 0,255),
              (unsigned char)ClampF(b.color.g*illum, 0,255),
              (unsigned char)ClampF(b.color.b*illum, 0,255), 255 };
    }

    if (!b.isFragment && b.heatSpike > 0.0f) {
        c = { (unsigned char)ClampF(c.r*(1-b.heatSpike)+255*b.heatSpike, 0,255),
              (unsigned char)ClampF(c.g*(1-b.heatSpike)+200*b.heatSpike, 0,255),
              (unsigned char)ClampF(c.b*(1-b.heatSpike)+ 50*b.heatSpike, 0,255), 255 };
    }
    return c;
}

// ── Uniforms del shader ──────────────────────────────────────
struct ShaderLocs {
    int lightCount, lightPos, lightColor;
    int lightLum[MAX_LIGHTS];
    int lightRadius[MAX_LIGHTS];

    int occluderCount;
    int occluderPos;
    int occluderRadius;

    int renderScale;
    int viewPos, colDiffuse;
    int temp, heatSpike, isStar;
    int ambientStrength, ambientColor;
    int atmosDens, atmosColor, atmosFalloff;
    int spinPhase;
    // Nuevos para estrellas
    int stellarMass;
    int stellarActivity;
    int stellarLuminosity;
    // Fase estelar: uniforms para drawStar() en el shader
    int isGiantPhase;           // 1.0 si SUBGIANT/RED_GIANT/SUPERGIANT
    int isSupernovaPhase;       // 1.0 si SUPERNOVA activa
    int supernovaProgressLoc;   // 0→1 durante SUPERNOVA
    int rotationCriticality;    // 0–1 (fracción velocidad de ruptura), gravity darkening
    // Gigantes gaseosos/helados (shader procedural multicapa)
    int isGasGiant;
    int ggBandCount, ggBandStrength, ggTurbulence, ggJetStream;
    int ggStormFreq, ggStormSize, ggCloudContrast, ggColorVariance, ggSeed;
    int ggBandColors, ggHighCloudColor;
    int ggHasMajorStorm, ggMajorStormLat, ggMajorStormLon, ggMajorStormSize;
    int ggMajorStormColor, ggMajorStormBorder;
    int ggIceGiant;
    // Planetas rocosos/helados (shader procedural drawRockyPlanet)
    int isRockyPlanet;
    int rpWaterLevel, rpOceanIce, rpCraterDensity, rpCloudDensity, rpCloudBandStrength, rpHasCityLights, rpSeed;
    int rpColorLow, rpColorHigh, rpColorMid, rpColorWater, rpCloudColor;
    int rpHasSurfaceTex, rpNormalMap, rpSpecularMap, rpColorMap, rpPolarIceSize, rpSurfaceSpin;
    // Inclinacion axial (ver axialTilt en Body, uAxialTilt y undoAxialTilt
    // en shaders.h): aplica a TODOS los cuerpos (rocosos y gigantes), no
    // solo a los rocosos -- se declara aparte del bloque rpXxx.
    int axialTilt;
    // Sombra de anillos sobre el cuerpo
    int hasRings;
    int ringInnerRadius;
    int ringOuterRadius;
    int ringShadowStrength;
    // Deformacion "papa" (Sistema 5, ver MAX_DEFORM en constants.h y
    // potatoDisp en VERTEX_SHADER_SRC): aplica a TODOS los cuerpos (0 para
    // planetas, >0 para fragmentos/asteroides pequenos) -- igual que
    // axialTilt, se declara aparte del bloque rpXxx.
    int potatoAmp;
    // Relieve real (Sistema 4: cordilleras "ridged" + bump mapping)
    int rpMountainStrength, rpTerrainScale, rpTerrainBiome;
    // Centro del cuerpo en espacio de dibujado (luz direccional, ver
    // uBodyCenter en shaders.h)
    int bodyCenter;
    // Segunda pasada: capa de atmosfera inflada (ver drawAtmosphereShell
    // en shaders.h y DrawBody mas abajo)
    int isAtmosphereShell;
    // Tercera pasada: capa de nubes a radio intermedio (ver
    // drawCloudShell en shaders.h y DrawBody mas abajo)
    int isCloudShell;
    // Marcas de impacto (Sistema 3: crateres, magma, onda expansiva)
    int rpImpactDir, rpImpactRadius, rpImpactEnergy, rpImpactAge, rpImpactCount;
    // Inestabilidad estructural de gigantes/supergigantes (vertex + fragment)
    int instabilityAmp;
    int instabilityTime;
};

inline ShaderLocs GetShaderLocs(Shader& shader) {
    ShaderLocs locs;
    locs.lightCount       = GetShaderLocation(shader, "lightCount");
    locs.lightPos         = GetShaderLocation(shader, "lightPos[0]");
    locs.lightColor       = GetShaderLocation(shader, "lightColor[0]");
    locs.viewPos          = GetShaderLocation(shader, "viewPos");
    locs.colDiffuse       = GetShaderLocation(shader, "colDiffuse");
    locs.temp             = GetShaderLocation(shader, "temp");
    locs.heatSpike        = GetShaderLocation(shader, "heatSpike");
    locs.isStar           = GetShaderLocation(shader, "isStar");
    locs.ambientStrength  = GetShaderLocation(shader, "ambientStrength");
    locs.ambientColor     = GetShaderLocation(shader, "ambientColor");
    locs.atmosDens        = GetShaderLocation(shader, "atmosphereDensity");
    locs.atmosColor       = GetShaderLocation(shader, "atmosphereColor");
    locs.atmosFalloff     = GetShaderLocation(shader, "uAtmosphereFalloff");
    locs.spinPhase        = GetShaderLocation(shader, "spinPhase");
    locs.stellarMass         = GetShaderLocation(shader, "stellarMass");
    locs.stellarActivity     = GetShaderLocation(shader, "stellarActivity");
    locs.stellarLuminosity   = GetShaderLocation(shader, "stellarLuminosity");
    locs.isGiantPhase        = GetShaderLocation(shader, "isGiantPhase");
    locs.isSupernovaPhase    = GetShaderLocation(shader, "isSupernovaPhase");
    locs.supernovaProgressLoc= GetShaderLocation(shader, "supernovaProgress");
    locs.rotationCriticality = GetShaderLocation(shader, "rotationCriticality");

    // Gigantes gaseosos/helados
    locs.isGasGiant        = GetShaderLocation(shader, "isGasGiant");
    locs.ggBandCount       = GetShaderLocation(shader, "ggBandCount");
    locs.ggBandStrength    = GetShaderLocation(shader, "ggBandStrength");
    locs.ggTurbulence      = GetShaderLocation(shader, "ggTurbulence");
    locs.ggJetStream       = GetShaderLocation(shader, "ggJetStream");
    locs.ggStormFreq       = GetShaderLocation(shader, "ggStormFreq");
    locs.ggStormSize       = GetShaderLocation(shader, "ggStormSize");
    locs.ggCloudContrast   = GetShaderLocation(shader, "ggCloudContrast");
    locs.ggColorVariance   = GetShaderLocation(shader, "ggColorVariance");
    locs.ggSeed            = GetShaderLocation(shader, "ggSeed");
    locs.ggBandColors      = GetShaderLocation(shader, "ggBandColors[0]");
    locs.ggHighCloudColor  = GetShaderLocation(shader, "ggHighCloudColor");
    locs.ggHasMajorStorm   = GetShaderLocation(shader, "ggHasMajorStorm");
    locs.ggMajorStormLat   = GetShaderLocation(shader, "ggMajorStormLat");
    locs.ggMajorStormLon   = GetShaderLocation(shader, "ggMajorStormLon");
    locs.ggMajorStormSize  = GetShaderLocation(shader, "ggMajorStormSize");
    locs.ggMajorStormColor = GetShaderLocation(shader, "ggMajorStormColor");
    locs.ggMajorStormBorder= GetShaderLocation(shader, "ggMajorStormBorder");
    locs.ggIceGiant        = GetShaderLocation(shader, "ggIceGiant");

    // Planetas rocosos/helados
    locs.isRockyPlanet    = GetShaderLocation(shader, "isRockyPlanet");
    locs.rpWaterLevel     = GetShaderLocation(shader, "uWaterLevel");
    locs.rpOceanIce       = GetShaderLocation(shader, "uOceanIce");
    locs.rpCraterDensity  = GetShaderLocation(shader, "uCraterDensity");
    locs.rpCloudDensity   = GetShaderLocation(shader, "uCloudDensity");
    locs.rpCloudBandStrength = GetShaderLocation(shader, "uCloudBandStrength");
    locs.rpHasCityLights  = GetShaderLocation(shader, "uHasCityLights");
    locs.rpSeed           = GetShaderLocation(shader, "uSeed");
    locs.rpColorLow       = GetShaderLocation(shader, "uColorLow");
    locs.rpColorHigh      = GetShaderLocation(shader, "uColorHigh");
    locs.rpColorMid       = GetShaderLocation(shader, "uColorMid");
    locs.rpColorWater     = GetShaderLocation(shader, "uColorWater");
    locs.rpCloudColor     = GetShaderLocation(shader, "uCloudColor");
    locs.rpHasSurfaceTex  = GetShaderLocation(shader, "uHasSurfaceTex");
    locs.rpNormalMap      = GetShaderLocation(shader, "uNormalMap");
    locs.rpSpecularMap    = GetShaderLocation(shader, "uSpecularMap");
    locs.rpColorMap       = GetShaderLocation(shader, "uColorMap");
    locs.rpPolarIceSize   = GetShaderLocation(shader, "uPolarIceSize");
    locs.rpSurfaceSpin    = GetShaderLocation(shader, "uSurfaceSpin");

    // Inclinacion axial (todos los cuerpos, ver undoAxialTilt en shaders.h)
    locs.axialTilt        = GetShaderLocation(shader, "uAxialTilt");

    locs.hasRings           = GetShaderLocation(shader, "uHasRings");
    locs.ringInnerRadius    = GetShaderLocation(shader, "uRingInnerRadius");
    locs.ringOuterRadius    = GetShaderLocation(shader, "uRingOuterRadius");
    locs.ringShadowStrength = GetShaderLocation(shader, "uRingShadowStrength");

    // Deformacion "papa" (todos los cuerpos, ver potatoDisp en shaders.h)
    locs.potatoAmp        = GetShaderLocation(shader, "uPotatoAmp");

    // Relieve real (Sistema 4)
    locs.rpMountainStrength = GetShaderLocation(shader, "uMountainStrength");
    locs.rpTerrainScale     = GetShaderLocation(shader, "uTerrainScale");
    locs.rpTerrainBiome     = GetShaderLocation(shader, "uTerrainBiome");

    // Centro del cuerpo (luz direccional, ver uBodyCenter en shaders.h)
    locs.bodyCenter = GetShaderLocation(shader, "uBodyCenter");

    // Segunda pasada: capa de atmosfera inflada
    locs.isAtmosphereShell = GetShaderLocation(shader, "isAtmosphereShell");

    // Tercera pasada: capa de nubes a radio intermedio
    locs.isCloudShell = GetShaderLocation(shader, "isCloudShell");

    // Marcas de impacto (Sistema 3)
    locs.rpImpactDir     = GetShaderLocation(shader, "uImpactDir[0]");
    locs.rpImpactRadius  = GetShaderLocation(shader, "uImpactRadius[0]");
    locs.rpImpactEnergy  = GetShaderLocation(shader, "uImpactEnergy[0]");
    locs.rpImpactAge     = GetShaderLocation(shader, "uImpactAge[0]");
    locs.rpImpactCount   = GetShaderLocation(shader, "uImpactCount");

    // Inestabilidad estructural de gigantes/supergigantes
    locs.instabilityAmp  = GetShaderLocation(shader, "uInstabilityAmp");
    locs.instabilityTime = GetShaderLocation(shader, "uInstabilityTime");

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        locs.lightLum[i]    = GetShaderLocation(shader, TextFormat("lightLum[%d]", i));
        locs.lightRadius[i] = GetShaderLocation(shader, TextFormat("lightRadius[%d]", i));
    }

    locs.occluderCount  = GetShaderLocation(shader, "occluderCount");
    locs.occluderPos    = GetShaderLocation(shader, "occluderPos[0]");
    locs.occluderRadius = GetShaderLocation(shader, "occluderRadius[0]");
    locs.renderScale    = GetShaderLocation(shader, "uRenderScale");

    return locs;
}

// Umbral de Draper (~700K): temperatura a partir de la cual un cuerpo
// empieza a emitir luz visible propia (rojo apagado) por radiacion termica
// pura, sin depender de reflejar luz de otro lado. Mismo umbral usado para
// el auto-brillo de superficie (ver drawRockyPlanet/drawGasGiant en
// shaders.h) y aqui para decidir que cuerpos NO-estrella son lo bastante
// calientes como para sumarse como fuente de luz real para sus vecinos.
constexpr double DRAPER_POINT_K = 700.0;

inline Vector3 KelvinToRGBVec(float kelvin)
{
    kelvin = ClampF(kelvin, 800.0f, 40000.0f);
    float t = kelvin / 100.0f;

    float r, g, b;

    if (kelvin <= 6600.0f) {
        r = 255.0f;
        g = 99.4708025861f * logf(std::max(t, 0.01f)) - 161.1195681661f;
        if (kelvin <= 1900.0f)
            b = 0.0f;
        else
            b = 138.5177312231f * logf(std::max(t - 10.0f, 0.01f)) - 305.0447927307f;
    } else {
        float x = std::max(t - 60.0f, 0.01f);
        r = 329.698727446f * powf(x, -0.1332047592f);
        g = 288.1221695283f * powf(x, -0.0755148492f);
        b = 255.0f;
    }

    return {
        ClampF(r / 255.0f, 0.0f, 1.0f),
        ClampF(g / 255.0f, 0.0f, 1.0f),
        ClampF(b / 255.0f, 0.0f, 1.0f)
    };
}

inline void UploadLightUniforms(Shader& shader, const ShaderLocs& locs,
                                  const std::vector<Body>& bodies, const Camera3D& camera)
{
    struct LightSrc {
        Vector3D pos;
        double lum = 0.0;
        double radius = 0.0;
        double temperature = 5778.0;
        bool fake = false;
    };

    std::vector<LightSrc> stars;
    std::vector<LightSrc> hotBodies;

    for (const Body& b : bodies) {
        if (b.isStar) {
            if (b.mass > 0.0 && b.luminosity > 0.0 && b.radius > 0.0) {
                stars.push_back({
                    b.pos,
                    b.luminosity,
                    b.radius,
                    b.temperature,
                    false
                });
            }
        } else if (b.temperature > DRAPER_POINT_K && b.radius > 0.0) {
            double effLum = 4.0 * PI_D * b.radius * b.radius * SIGMA * std::pow(b.temperature, 4.0);
            hotBodies.push_back({
                b.pos,
                effLum,
                b.radius,
                b.temperature,
                false
            });
        }
    }

    std::sort(stars.begin(), stars.end(),
              [](const LightSrc& a, const LightSrc& b) {
                  return a.lum > b.lum;
              });

    std::sort(hotBodies.begin(), hotBodies.end(),
              [](const LightSrc& a, const LightSrc& b) {
                  return a.lum > b.lum;
              });

    std::vector<LightSrc> lights = stars;

    auto AddFakeLight = [&]() {
        if (!g_fakeLightEnabled || (int)lights.size() >= MAX_LIGHTS) return;

        LightSrc fakeLight{};

        const Vector3 FAKE_LIGHT_DIR = Vector3Normalize({ -0.45f, 0.0f, 0.82f });

        constexpr float FAKE_LIGHT_DIST_DRAW = 10000.0f;
        constexpr double FAKE_LIGHT_REL_IRRADIANCE = 1;

        Vector3 fakeDrawPos = Vector3Scale(FAKE_LIGHT_DIR, FAKE_LIGHT_DIST_DRAW);
        double fakeDistMeters = std::max(1.0, (double)FAKE_LIGHT_DIST_DRAW / RENDER_SCALE);

        fakeLight.pos = ToPhysPos(fakeDrawPos);
        fakeLight.lum = FAKE_LIGHT_REL_IRRADIANCE * 1361.0 * 4.0 * PI_D * fakeDistMeters * fakeDistMeters;

        // Si quieres eclipses más duros/visibles, baja este valor.
        // Solar realista:
        fakeLight.radius = fakeDistMeters * 0.00465;

        fakeLight.temperature = 5778.0;
        fakeLight.fake = false;

        lights.push_back(fakeLight);
    };

    // Si NO hay estrella real, la fake light debe ser lightPos[0].
    // Eso alimenta correctamente todos los paths del shader que usan lightPos[0].
    if (lights.empty()) {
        AddFakeLight();
    }

    // Cuerpos calientes van después de la estrella real o de la fake light principal.
    if ((int)lights.size() < MAX_LIGHTS) {
        for (const LightSrc& h : hotBodies) {
            if ((int)lights.size() >= MAX_LIGHTS) break;
            lights.push_back(h);
        }
    }

    // Si sí hay estrella real, la fake light se añade al final como relleno visual.
    if (!stars.empty()) {
        AddFakeLight();
    }

    Vector3 posArr[MAX_LIGHTS]{};
    Vector3 colArr[MAX_LIGHTS]{};
    float   lumArr[MAX_LIGHTS]{};
    float   radiusArr[MAX_LIGHTS]{};

    int count = std::min((int)lights.size(), MAX_LIGHTS);

    for (int i = 0; i < count; ++i) {
        posArr[i] = ToDrawPos(lights[i].pos);

        colArr[i] = lights[i].fake
            ? Vector3{1.0f, 1.0f, 1.0f}
            : KelvinToRGBVec((float)lights[i].temperature);

        lumArr[i] = (float)std::max(1.0, lights[i].lum);
        radiusArr[i] = (float)std::max(0.0, lights[i].radius * RENDER_SCALE);
    }

    float camPos[3] = {
        camera.position.x,
        camera.position.y,
        camera.position.z
    };

    float ambStr = 0.0f;
    float ambCol[3] = { 1.0f, 1.0f, 1.0f };
    float renderScale = (float)RENDER_SCALE;

    SetShaderValue(shader, locs.lightCount, &count, SHADER_UNIFORM_INT);

    if (count > 0) {
        SetShaderValueV(shader, locs.lightPos,   posArr, SHADER_UNIFORM_VEC3, count);
        SetShaderValueV(shader, locs.lightColor, colArr, SHADER_UNIFORM_VEC3, count);

        // lightRadius es un array GLSL. Usamos la ubicación de lightRadius[0].
        SetShaderValueV(shader, locs.lightRadius[0], radiusArr, SHADER_UNIFORM_FLOAT, count);

        for (int i = 0; i < count; ++i) {
            SetShaderValue(shader, locs.lightLum[i], &lumArr[i], SHADER_UNIFORM_FLOAT);
        }
    }

    SetShaderValue(shader, locs.viewPos,         camPos,       SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locs.ambientStrength, &ambStr,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.ambientColor,    ambCol,       SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locs.renderScale,     &renderScale, SHADER_UNIFORM_FLOAT);

    constexpr int MAX_OCCLUDERS_RENDER = 32;

    Vector3 occPos[MAX_OCCLUDERS_RENDER]{};
    float   occRadius[MAX_OCCLUDERS_RENDER]{};
    int     occCount = 0;

    std::vector<const Body*> occs;
    occs.reserve(bodies.size());

    for (const Body& b : bodies) {
        if (b.mass > 0.0 && b.radius > 0.0) {
            occs.push_back(&b);
        }
    }

    std::sort(occs.begin(), occs.end(),
              [](const Body* a, const Body* b) {
                  return a->radius > b->radius;
              });

    for (const Body* b : occs) {
        if (occCount >= MAX_OCCLUDERS_RENDER) break;

        occPos[occCount] = ToDrawPos(b->pos);
        occRadius[occCount] = (float)(b->radius * RENDER_SCALE);
        occCount++;
    }

    SetShaderValue(shader, locs.occluderCount, &occCount, SHADER_UNIFORM_INT);

    if (occCount > 0) {
        SetShaderValueV(shader, locs.occluderPos, occPos, SHADER_UNIFORM_VEC3, occCount);
        SetShaderValueV(shader, locs.occluderRadius, occRadius, SHADER_UNIFORM_FLOAT, occCount);
    }
}

// ── Upload de uniforms por body ──────────────────────────────
inline void UploadBodyUniforms(Shader& shader, const ShaderLocs& locs, const Body& b) {
    int   isStarVal  = b.isStar ? 1 : 0;
    float tempVal    = (float)b.temperature;
    float spikeVal   = b.heatSpike;
    float spinVal    = (float)b.spin;
    // Toggles visuales (ver hideAtmosphere/hideClouds en body.h): a 0 el
    // shader no dibuja halo/rim/tinte atmosferico ni mezcla de nubes,
    // sin tocar b.atmosphereDensity/rockyPlanet.cloudDensity reales (la
    // termodinamica sigue intacta).
    float atmosDens  = b.hideAtmosphere ? 0.0f : b.atmosphereDensity;
    float atmosCol[3]= { b.atmosphereColor.r/255.0f,
                         b.atmosphereColor.g/255.0f,
                         b.atmosphereColor.b/255.0f };
    float atmosFalloff = b.atmosphereFalloff;

    // Centro del cuerpo en espacio de dibujado: usado por drawRockyPlanet
    // (shaders.h) para calcular la direccion de la luz solar como
    // direccional pura (rayos paralelos), no como 'lightPos - fragPosition'
    // (que diverge punto a punto sobre la superficie, "efecto linterna").
    Vector3 centerPos = ToDrawPos(b.pos);
    SetShaderValue(shader, locs.bodyCenter, &centerPos, SHADER_UNIFORM_VEC3);

    // Inclinacion axial en radianes (ver axialTilt en Body,
    // TidalBodyTransform y undoAxialTilt en shaders.h): deshace en el
    // shader la MISMA rotacion fija (MatrixRotateX) aplicada al modelo,
    // para que terreno/crateres/bandas de nubes queden fijos al eje de
    // spin propio del cuerpo pese a la inclinacion.
    float axialTiltRad = b.axialTilt * DEG2RAD;
    SetShaderValue(shader, locs.axialTilt, &axialTiltRad, SHADER_UNIFORM_FLOAT);

    int hasRingsVal = b.hasRings ? 1 : 0;

    // Valores visuales sugeridos.
    // El anillo dibujado actualmente usa radio exterior ~3.2 * radio visual.
    // Para la sombra usamos una banda: empieza fuera del planeta y termina
    // cerca del radio visual del anillo.
    float ringInnerRadius = b.hasRings ? (float)(b.radius * RENDER_SCALE * 1.35) : 0.0f;
    float ringOuterRadius = b.hasRings ? (float)(b.radius * RENDER_SCALE * 3.20) : 0.0f;

    // 0.0 = no oscurece, 1.0 = anillo totalmente opaco.
    // 0.55-0.75 suele verse bien para anillos de polvo/hielo.
    float ringShadowStrength = b.hasRings ? 0.65f : 0.0f;

    SetShaderValue(shader, locs.hasRings,           &hasRingsVal,          SHADER_UNIFORM_INT);
    SetShaderValue(shader, locs.ringInnerRadius,    &ringInnerRadius,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.ringOuterRadius,    &ringOuterRadius,      SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.ringShadowStrength, &ringShadowStrength,   SHADER_UNIFORM_FLOAT);

    // potatoAmp: 0 para cualquier cuerpo >= DIFFERENTIATION_RADIUS (planetas:
    // sin cambio visual). Crece linealmente hasta MAX_DEFORM por debajo de ese
    // radio -- ver derivaciones de DIFFERENTIATION_RADIUS (constants.h, Ceres/
    // Vesta) y MAX_DEFORM (Vesta/Hyperion, constants.h) arriba.
    // EXCEPCION: estrellas (isStar=true), incluyendo estrellas de neutrones,
    // pulsares y magnetares. Sin importar lo pequenas que sean, la presion de
    // degeneracion/gravedad las mantiene perfectamente esfericas -- una NS de
    // 12km es el objeto mas esferico del universo (desviacion < 1mm del radio).
    // El chequeo de radio sin esta excepcion daba potatoFactor=0.97 al Crab
    // Pulsar (12km << DIFFERENTIATION_RADIUS=400km), deformandolo como un
    // asteroide irregular al maximo.
    float potatoFactor = b.isStar
                       ? 0.0f
                       : ClampF(1.0f - (float)(b.radius / DIFFERENTIATION_RADIUS), 0.0f, 1.0f);
    float potatoAmp    = potatoFactor * MAX_DEFORM;
    SetShaderValue(shader, locs.potatoAmp, &potatoAmp, SHADER_UNIFORM_FLOAT);

    // Por defecto se dibuja el cuerpo solido (corteza); la capa de
    // atmosfera (segunda pasada, malla inflada) activa esto a 1 justo
    // antes de su propio DrawMesh -- ver bloque de atmosfera en DrawBody.
    int atmoShellVal = 0;
    SetShaderValue(shader, locs.isAtmosphereShell, &atmoShellVal, SHADER_UNIFORM_INT);

    // Idem para la capa de nubes (tercera pasada, malla a radio
    // intermedio) -- ver bloque de nubes en DrawBody.
    int cloudShellVal = 0;
    SetShaderValue(shader, locs.isCloudShell, &cloudShellVal, SHADER_UNIFORM_INT);

    SetShaderValue(shader, locs.isStar,    &isStarVal, SHADER_UNIFORM_INT);
    SetShaderValue(shader, locs.temp,      &tempVal,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.heatSpike, &spikeVal,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.spinPhase, &spinVal,   SHADER_UNIFORM_FLOAT);
    // Rotación visual REAL del cuerpo, en radianes.
    // La usan planetas y ahora también estrellas para que el patrón
    // procedural quede pegado al giro físico del Body.
    float surfaceSpinRad = fmodf(b.rotationAngle, 360.0f) * DEG2RAD;
    SetShaderValue(shader, locs.rpSurfaceSpin, &surfaceSpinRad, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.atmosDens, &atmosDens, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.atmosColor, atmosCol,  SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locs.atmosFalloff, &atmosFalloff, SHADER_UNIFORM_FLOAT);

    // Inestabilidad estructural (0 por defecto; calculado abajo para gigantes)
    float instAmp = 0.0f, instTime = 0.0f;

    if (b.isStar) {
        float mass  = (float)(b.mass / M_SUN);
        float act   = b.stellarActivity;
        float lum   = (float)(b.luminosity / L_SUN);
        SetShaderValue(shader, locs.stellarMass,       &mass, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.stellarActivity,   &act,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.stellarLuminosity, &lum,  SHADER_UNIFORM_FLOAT);

        // Fase estelar para drawStar() en el shader.
        // CONTINUO segun cuanto ha crecido el radio respecto a su tamano de
        // secuencia principal (b.radius YA converge gradualmente en
        // ApplyStellarPhaseProperties, ver stellar_evolution.h) -- antes era
        // un booleano atado directo al enum de fase, que cambiaba DE GOLPE
        // en el instante de la transicion aunque el radio/luminosidad
        // tardaran minutos/horas simulados en alcanzar el tamano de gigante;
        // el corona/cromosfera "modo gigante" aparecia antes de que la
        // estrella realmente se hubiera inflado. Ahora sigue al tamano real.
        double mRatioVis = std::max(0.08, b.initialStellarMass / M_SUN);
        double msRadiusVis = R_SUN * std::pow(mRatioVis, 0.8);
        float radiusRatioVis = (float)(b.radius / std::max(1.0, msRadiusVis));
        float giantT = std::clamp((radiusRatioVis - 1.8f) / (6.0f - 1.8f), 0.0f, 1.0f);
        float isGiant = giantT * giantT * (3.0f - 2.0f * giantT);
        float isSupernova = (b.stellarPhase == StellarPhase::SUPERNOVA) ? 1.0f : 0.0f;
        float snProgress  = (float)b.supernovaProgress;
        float rotCrit     = b.criticalRotationFraction;
        SetShaderValue(shader, locs.isGiantPhase,        &isGiant,     SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.isSupernovaPhase,    &isSupernova, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.supernovaProgressLoc,&snProgress,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.rotationCriticality, &rotCrit,     SHADER_UNIFORM_FLOAT);

        // ── Inestabilidad estructural ────────────────────────────────────
        // Amplitud basada en: fase gigante (continuo 0-1), masa, fase evolutiva
        // y pulsacion (proxy de actividad convectiva). Sin efecto en secuencia
        // principal (isGiant=0); crece gradualmente a medida que la estrella
        // se infla hasta alcanzar el maximo en supergigantes con pulsaciones.
        if (isGiant > 0.01f) {
            float mRatio = std::max(0.1f, (float)(b.mass / M_SUN));

            // Base: escala con cuanto se ha expandido la estrella
            instAmp = isGiant * 0.065f;

            // Masas > 8 M_sun tienen conveccion mucho mas energetica
            if (mRatio > 8.0f)
                instAmp *= (1.0f + std::clamp((mRatio - 8.0f) / 22.0f, 0.0f, 2.5f));

            // Supergigantes ya inestables por perdida de masa y mezcla de capas
            if (b.stellarPhase == StellarPhase::SUPERGIANT)
                instAmp *= 2.0f;

            // AGB y pulsos termicos: maxima violencia convectiva antes del final
            if (b.stellarPhase == StellarPhase::AGB ||
                b.stellarPhase == StellarPhase::THERMAL_PULSES)
                instAmp *= 3.5f;

            // Sinergia con pulsaciones: la amplitud de pulsacion ya codifica
            // la actividad convectiva de la fase (Miras ~0.25, Cefeidas ~0.15)
            instAmp *= (1.0f + b.pulsationAmplitude * 5.0f);

            instAmp  = std::clamp(instAmp, 0.0f, 0.38f);

            // Una "vuelta" convectiva ~ 1 ano simulado; visible a partir de x64
            // stellarAge en lugar de stellarPhaseAge: no se resetea al cambiar fase
            instTime = (float)std::fmod(b.stellarAge / 1.0e7, 1000.0);
        }
    }

    // Siempre subir (0 para planetas / estrellas en secuencia principal)
    SetShaderValue(shader, locs.instabilityAmp,  &instAmp,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader, locs.instabilityTime, &instTime, SHADER_UNIFORM_FLOAT);

    int isGasGiantVal = b.isGasGiant ? 1 : 0;
    SetShaderValue(shader, locs.isGasGiant, &isGasGiantVal, SHADER_UNIFORM_INT);

    if (b.isGasGiant) {
        const GasGiantProfile& g = b.gasGiant;
        SetShaderValue(shader,  locs.ggBandCount,     &g.bandCount,          SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggBandStrength,  &g.bandStrength,       SHADER_UNIFORM_FLOAT);

        // Un exceso de calor (impacto reciente, ver UpdateThermodynamics
        // en physics.h) intensifica temporalmente la turbulencia/cizalla/
        // tormentas en vez del antiguo tinte naranja por heatSpike.
        float effTurbulence = ClampF(g.turbulenceStrength + b.turbulenceBoost * 0.6f, 0.0f, 1.5f);
        float effJetStream  = ClampF(g.jetStreamStrength  + b.turbulenceBoost * 0.5f, 0.0f, 1.5f);
        float effStormFreq  = ClampF(g.stormFrequency     + b.turbulenceBoost * 0.4f, 0.0f, 1.0f);
        SetShaderValue(shader,  locs.ggTurbulence,    &effTurbulence, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggJetStream,     &effJetStream,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggStormFreq,     &effStormFreq,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggStormSize,     &g.stormSize,          SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggCloudContrast, &g.cloudContrast,      SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggColorVariance, &g.colorVariance,      SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader,  locs.ggSeed,          &g.seed,               SHADER_UNIFORM_FLOAT);
        SetShaderValueV(shader, locs.ggBandColors,     g.bandColors, SHADER_UNIFORM_VEC3, 5);

        float hcCol[3] = { g.highCloudColor.x, g.highCloudColor.y, g.highCloudColor.z };
        SetShaderValue(shader, locs.ggHighCloudColor, hcCol, SHADER_UNIFORM_VEC3);

        int hasMajor = g.hasMajorStorm ? 1 : 0;
        SetShaderValue(shader, locs.ggHasMajorStorm,  &hasMajor,         SHADER_UNIFORM_INT);
        SetShaderValue(shader, locs.ggMajorStormLat,  &g.majorStormLat,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.ggMajorStormLon,  &g.majorStormLon,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.ggMajorStormSize, &g.majorStormSize, SHADER_UNIFORM_FLOAT);

        float msCol[3] = { g.majorStormColor.x, g.majorStormColor.y, g.majorStormColor.z };
        SetShaderValue(shader, locs.ggMajorStormColor, msCol, SHADER_UNIFORM_VEC3);
        float mbCol[3] = { g.majorStormBorder.x, g.majorStormBorder.y, g.majorStormBorder.z };
        SetShaderValue(shader, locs.ggMajorStormBorder, mbCol, SHADER_UNIFORM_VEC3);

        int iceGiantVal = g.iceGiant ? 1 : 0;
        SetShaderValue(shader, locs.ggIceGiant, &iceGiantVal, SHADER_UNIFORM_INT);
    }

    int isRockyPlanetVal = b.isRockyPlanet ? 1 : 0;
    SetShaderValue(shader, locs.isRockyPlanet, &isRockyPlanetVal, SHADER_UNIFORM_INT);

    if (b.isRockyPlanet) {
        const RockyPlanetProfile& r = b.rockyPlanet;

        SetShaderValue(shader, locs.rpWaterLevel,    &r.waterLevel,    SHADER_UNIFORM_FLOAT);
        float liquid = ClampF(
            b.volatileBudget - b.iceFraction - b.vaporFraction,
            0.0f,
            b.volatileBudget
        );

        float visibleHydro = ClampF(
            liquid + b.iceFraction,
            0.0f,
            1.0f
        );

        float oceanIce = (visibleHydro > 1.0e-5f)
            ? ClampF(b.iceFraction / visibleHydro, 0.0f, 1.0f)
            : 0.0f;

        SetShaderValue(shader, locs.rpOceanIce, &oceanIce, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.rpCraterDensity, &r.craterDensity, SHADER_UNIFORM_FLOAT);

        float cloudDensVal = b.hideClouds ? 0.0f : r.cloudDensity;
        SetShaderValue(shader, locs.rpCloudDensity, &cloudDensVal, SHADER_UNIFORM_FLOAT);

        SetShaderValue(shader, locs.rpCloudBandStrength, &r.cloudBandStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.rpHasCityLights, &r.hasCityLights, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.rpSeed,          &r.seed,          SHADER_UNIFORM_FLOAT);
        // Cuando un planeta con oceanos se congela, los continentes tambien
        // deben cubrirse de nieve/hielo. Expandimos uPolarIceSize (que usa un
        // umbral de latitud sobre abs(pos3D.y)+height) segun cuanto oceano ha
        // congelado: cuando liquid < la mitad del nivel de agua original, los
        // casquetes polares empiezan a bajar hacia el ecuador cubriendo tierra.
        // A liquid=0 (congelo total) el umbral baja a -0.2, cubriendo el ecuador.
        // Solo aplica a planetas con oceanos reales (r.waterLevel > 0.1).
        float effectivePolarIce = r.polarIceSize;
        if (r.waterLevel > 0.1f) {
            float halfOcean  = r.waterLevel * 0.5f;
            float frozenFrac = 1.0f - ClampF(liquid / halfOcean, 0.0f, 1.0f);
            effectivePolarIce += frozenFrac * 2.5f;
        }
        SetShaderValue(shader, locs.rpPolarIceSize, &effectivePolarIce, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.rpMountainStrength, &r.mountainStrength, SHADER_UNIFORM_FLOAT);
        SetShaderValue(shader, locs.rpTerrainScale,     &r.terrainScale,     SHADER_UNIFORM_FLOAT);

        float colLow[3]   = { r.colorLow.x,   r.colorLow.y,   r.colorLow.z   };
        float colHigh[3]  = { r.colorHigh.x,  r.colorHigh.y,  r.colorHigh.z  };
        float colMid[3]   = { r.colorMid.x,   r.colorMid.y,   r.colorMid.z   };
        float colWater[3] = { r.colorWater.x, r.colorWater.y, r.colorWater.z };
        float colCloud[3] = { r.cloudColor.x, r.cloudColor.y, r.cloudColor.z };

        SetShaderValue(shader, locs.rpColorLow,   colLow,   SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, locs.rpColorHigh,  colHigh,  SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, locs.rpColorMid,   colMid,   SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, locs.rpColorWater, colWater, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, locs.rpCloudColor, colCloud, SHADER_UNIFORM_VEC3);

        SetShaderValue(shader, locs.rpTerrainBiome, &r.terrainBiome, SHADER_UNIFORM_FLOAT);

        int hasSurfTexVal = (b.normalTex != nullptr && b.specularTex != nullptr && b.diffuseTex != nullptr) ? 1 : 0;
        SetShaderValue(shader, locs.rpHasSurfaceTex, &hasSurfTexVal, SHADER_UNIFORM_INT);

        if (hasSurfTexVal) {
            int normalUnit = 1, specularUnit = 2, colorUnit = 3;

            rlActiveTextureSlot(normalUnit);
            rlEnableTexture(b.normalTex->id);
            SetShaderValue(shader, locs.rpNormalMap, &normalUnit, SHADER_UNIFORM_INT);

            rlActiveTextureSlot(specularUnit);
            rlEnableTexture(b.specularTex->id);
            SetShaderValue(shader, locs.rpSpecularMap, &specularUnit, SHADER_UNIFORM_INT);

            rlActiveTextureSlot(colorUnit);
            rlEnableTexture(b.diffuseTex->id);
            SetShaderValue(shader, locs.rpColorMap, &colorUnit, SHADER_UNIFORM_INT);

            rlActiveTextureSlot(0);
        }

        Vector3 impDir[Body::MAX_IMPACT_MARKS]{};
        float   impRadius[Body::MAX_IMPACT_MARKS]{};
        float   impEnergy[Body::MAX_IMPACT_MARKS]{};
        float   impAge[Body::MAX_IMPACT_MARKS]{};

        for (int i = 0; i < b.impactMarkCount; ++i) {
            const ImpactMark& m = b.impactMarks[i];
            impDir[i]    = { (float)m.localDir.x, (float)m.localDir.y, (float)m.localDir.z };
            impRadius[i] = m.radius;
            impEnergy[i] = m.energy;
            impAge[i]    = m.age;
        }

        SetShaderValueV(shader, locs.rpImpactDir,    impDir,    SHADER_UNIFORM_VEC3,  Body::MAX_IMPACT_MARKS);
        SetShaderValueV(shader, locs.rpImpactRadius, impRadius, SHADER_UNIFORM_FLOAT, Body::MAX_IMPACT_MARKS);
        SetShaderValueV(shader, locs.rpImpactEnergy, impEnergy, SHADER_UNIFORM_FLOAT, Body::MAX_IMPACT_MARKS);
        SetShaderValueV(shader, locs.rpImpactAge,    impAge,    SHADER_UNIFORM_FLOAT, Body::MAX_IMPACT_MARKS);
        SetShaderValue(shader, locs.rpImpactCount, &b.impactMarkCount, SHADER_UNIFORM_INT);
    }
}

// ── Renderizado del halo/corona exterior ────────────────────
// Se dibuja usando esferas semitransparentes con blending aditivo.
inline void DrawStarHalo(const Vector3& p, float rad, const Color& starColor,
                          float activity, float luminosity, float temp)
{
    // Intensidad del halo según luminosidad y temperatura
    float haloIntensity = std::min(1.0f, 0.25f + std::log(std::max(1.0f, luminosity)) * 0.06f);
    float mDwarf = 1.0f - ClampF((temp - 3400.0f) / (4100.0f - 3400.0f), 0.0f, 1.0f);
    float kDwarf =
        ClampF((temp - 3800.0f) / (4800.0f - 3800.0f), 0.0f, 1.0f) *
        (1.0f - ClampF((temp - 5100.0f) / (5600.0f - 5100.0f), 0.0f, 1.0f));

    haloIntensity *= 1.0f + activity * (mDwarf * 0.9f + kDwarf * 0.35f);
    if (temp > 8000.0f)  haloIntensity *= 1.5f;
    if (temp > 25000.0f) haloIntensity *= 2.0f;
    haloIntensity = std::min(haloIntensity, 1.5f);

    Color hc = { starColor.r, starColor.g, starColor.b, 255 };

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();   // Los halos NO deben tapar planetas/trails detrás de la corona

    // Corona difusa exterior — opacidad baja para no parecer bola sólida
    DrawSphereEx(p, rad * 1.45f, 32, 32,
        Fade(hc, std::min(0.13f, haloIntensity * 0.13f)));

    DrawSphereEx(p, rad * 2.2f, 24, 24,
        Fade(hc, std::min(0.07f, haloIntensity * 0.07f)));

    DrawSphereEx(p, rad * 3.5f, 18, 18,
        Fade(hc, std::min(0.03f, haloIntensity * 0.03f)));

    // Halo extra solo para muy luminosas o extremadamente calientes
    if (luminosity > 100.0f || temp > 20000.0f) {
        DrawSphereEx(p, rad * 6.0f, 14, 14,
            Fade(hc, std::min(0.015f, haloIntensity * 0.015f)));
    }

    // Flush del batch ANTES de re-habilitar depth write — si no, EndBlendMode()
    // hace el flush con GL_DEPTH_MASK=TRUE y las esferas escriben depth de todos modos.
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    EndBlendMode();
}

// Matriz de transformación de un planeta con marea: escala uniforme
// (radio), rotación propia (spin) en espacio local/objeto, y LUEGO un
// achatamiento/elongación alineado con tideAxis -- la dirección REAL
// hacia el cuerpo que provoca la marea (ver ApplyTidesAndRoche en
// physics.h) -- aplicado en espacio mundial, DESPUÉS del spin, para que
// el "bulto" de marea apunte siempre hacia el agresor sin importar cómo
// gire el planeta sobre su eje. matStretch = squash*I + (elong-squash) *
// (tideAxis ⊗ tideAxis) es la matriz simétrica de escala no uniforme a
// lo largo de tideAxis (squash en las direcciones perpendiculares).
// Sin marea (tideVisualElongation==tideVisualSquash==1) matStretch ==
// Identity, asi que esto reproduce exactamente el DrawModelEx anterior.
// Usa tideVisualElongation/tideVisualSquash (no tideElongation/tideSquash):
// estos son la version "atenuada por rigidez" del mismo bulto fisico (ver
// ApplyTidesAndRoche en physics.h) -- la forma de COLISION
// (EllipsoidRadiusToward) sigue el bulto fisico real, pero el DIBUJADO de
// cuerpos pequenos/rigidos se estira menos para el mismo estres.
// Inclinacion axial (ver axialTilt en Body): rotacion FIJA alrededor del
// eje X del mundo, aplicada DESPUES del spin diario (matSpin, eje Y) y
// ANTES del achatamiento por marea (matStretch, que usa tideAxis en
// espacio de mundo -- una esfera girada+inclinada sigue siendo una esfera,
// asi que el estiramiento posterior produce el elipsoide correcto sin
// importar la inclinacion). Como el cuerpo orbita en el plano XZ, esta
// rotacion fija hace que el polo inclinado apunte hacia/lejos de la
// estrella en lados opuestos de la orbita (estaciones), sin logica extra.
// uAxialTilt (shaders.h) deshace esta MISMA rotacion sobre pos3D/N para
// que terreno/crateres/bandas de nubes (definidos en el espacio propio del
// cuerpo) permanezcan pegados a la malla pese a la inclinacion.
// Achatamiento por ROTACION PROPIA (independiente de mareas): cualquier
// cuerpo en rotacion se abulta en el ecuador y se achata en los polos por
// fuerza centrifuga -- la Tierra (f=0.0034), y mucho mas marcado en
// gigantes gaseosos de rotacion rapida (Saturno f=0.098) o protocuerpos/
// fragmentos que tumban rapido tras una colision (ver MakeFragments,
// physics.h). Aproximacion de primer orden para un cuerpo cuasi-fluido:
// q = omega^2*R^3/(G*M) (razon entre aceleracion centrifuga y
// gravitatoria en el ecuador) ~= f (achatamiento), igual orden de magnitud
// que la formula real de Darwin-Radau para un esferoide de Maclaurin.
// Antes esto NO existia: la unica deformacion de forma era el "potato"
// (ruido, solo cuerpos <400km) y el achatamiento por MAREA (gravedad de
// OTRO cuerpo, no la rotacion propia) -- ningun cuerpo se abultaba por
// girar rapido sobre si mismo, sin importar que tan rapido lo hiciera.
inline float RotationalOblateness(const Body& b) {
    if (b.mass <= 0.0 || b.radius <= 0.0) return 0.0f;
    // spinRateDeg esta en grados por "tick" de TIME_STEP/1200 (ver
    // rotationAngle += spinRateDeg*(TIME_STEP/1200) mas abajo) -> omega en
    // rad/s = spinRateDeg * (PI/180) / 1200.
    double omega = (double)b.spinRateDeg * (PI_D / 180.0) / 1200.0;
    double q = (omega * omega * b.radius * b.radius * b.radius) / (G * b.mass);
    return (float)ClampD(q, 0.0, 0.5); // tope de seguridad visual
}

inline Matrix TidalBodyTransform(Vector3 p, float rad, float spinDeg, const Body& b) {
    // Volumen aproximadamente conservado a primer orden: R_ecuador =
    // rad*(1+f/3), R_polo = rad*(1-2f/3) -- el radio "medio" (usado en
    // colisiones/fisica, b.radius) no cambia, solo el aspecto visual.
    float f    = RotationalOblateness(b);
    float rEq  = rad * (1.0f + f / 3.0f);
    float rPol = rad * (1.0f - 2.0f * f / 3.0f);
    Matrix matScale = MatrixScale(rEq, rPol, rEq);
    Matrix matSpin  = MatrixRotateY(spinDeg * DEG2RAD);
    Matrix matTilt  = MatrixRotateX(b.axialTilt * DEG2RAD);

    Vector3 ax = Vector3Normalize({(float)b.tideAxis.x, (float)b.tideAxis.y, (float)b.tideAxis.z});
    float sq = b.tideVisualSquash, el = b.tideVisualElongation, k = el - sq;
    Matrix matStretch = {
        sq + k*ax.x*ax.x, k*ax.x*ax.y,      k*ax.x*ax.z,      0.0f,
        k*ax.x*ax.y,      sq + k*ax.y*ax.y, k*ax.y*ax.z,      0.0f,
        k*ax.x*ax.z,      k*ax.y*ax.z,      sq + k*ax.z*ax.z, 0.0f,
        0.0f,             0.0f,             0.0f,             1.0f
    };

    Matrix matTranslation = MatrixTranslate(p.x, p.y, p.z);
    return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixMultiply(matScale, matSpin), matTilt), matStretch), matTranslation);
}

// ── Dibujado de un body individual ──────────────────────────
inline void DrawBody(Body& b, const Camera3D& camera, const std::vector<Body>& bodies,
                      Model& planetModel, const Texture2D& blankTex,
                      int selectedBodyIdx, int bodyIndex, bool paused,
                      Shader& shader, const ShaderLocs& locs)
{
    // Saltar cuerpos marcados para eliminar (mass=-1.0 es señal interna),
    // cuerpos muertos normales (mass=0 sin isSupernovaRemnant) — los remanentes
    // con mass=0 e isSupernovaRemnant=true sí se renderizan (su shell expansiva).
    if (b.mass < 0.0) return;
    if (b.mass == 0.0) return;

    const Vector3 p  = ToDrawPos(b.pos);
    float rR = (float)(b.radius * RENDER_SCALE);
    float dist3d   = Vector3Distance(camera.position, p);
    float screenR  = (rR / dist3d) * (float)GetScreenHeight() / tanf(camera.fovy * 0.5f * DEG2RAD);
    // Estrellas no se inflan: su radio inflado taparía planetas en órbita cercana
    if (screenR < 3.0f && !b.isStar) rR = dist3d * 3.0f * tanf(camera.fovy * 0.5f * DEG2RAD) / (float)GetScreenHeight();
    rR = std::max(0.0001f, rR);

    const Color dCol = ComputeBodyColor(b, p, bodies);

    if (!paused) {
        // All arithmetic in double: float fmodf loses precision when the raw
        // increment exceeds ~1e7 (ULP there ≈ 1, making mod-360 garbage at
        // x128+ and causing the erratic speed-up/slow-down the user observed).
        double scale = TIME_STEP / 1200.0;
        // Compact remnants (pulsar/magnetar/NS) complete thousands of full
        // rotations even at x1/16.  Sub-x1 sim speeds have no meaningful
        // rotational physics to preserve, so clamp their visual scale floor
        // to x1 so they never appear to slow down at lower sim speeds.
        double rScale = scale;
        if (b.isStar && (b.stellarPhase == StellarPhase::PULSAR   ||
                         b.stellarPhase == StellarPhase::MAGNETAR  ||
                         b.stellarPhase == StellarPhase::NEUTRON_STAR)) {
            rScale = std::max(scale, 1.0);
        }
        double rInc = fmod((double)b.spinRateDeg * rScale, 360.0);
        // Anti-strobe for all compact remnants: fmod(180) folds near-360° increments
        // (which appear as near-zero backwards motion) into [0°,180°); then values
        // below 40° are flipped to [140°,180°) to eliminate near-zero slow zones.
        // Result: always 40°-180°/frame (≥7 RPS) for pulsar, magnetar, and NS.
        if (b.isStar && (b.stellarPhase == StellarPhase::PULSAR   ||
                         b.stellarPhase == StellarPhase::MAGNETAR  ||
                         b.stellarPhase == StellarPhase::NEUTRON_STAR)) {
            rInc = fmod(rInc, 180.0);
            if (rInc < 40.0) rInc = 180.0 - rInc;
        }
        b.rotationAngle = (float)fmod((double)b.rotationAngle + rInc, 360.0);
        double cInc = fmod(0.7 * scale, 360.0);
        b.cloudRotation = (float)fmod((double)b.cloudRotation + cInc, 360.0);
    }

    // --- ESTRELLA: renderizado procedural completo ---
    if (b.isStar) {
        // Para objetos compactos (NS, pulsar, magnetar) NO aplicar el piso
        // de 0.001 draw-units: ese minimo visual existe para que las estrellas
        // lejanas (Sol a distancia Pluton) sigan siendo visibles, pero para un
        // pulsar (rR=0.00012) lo infla 8x respecto al radio fisico real,
        // causando que la esfera visual sea mucho mayor que la hitbox de
        // seleccion (que siempre usa el radio fisico sin piso).
        // SUPERNOVA incluida: la estrella ya está colapsada al tamaño compacto
        // para el 95% de la duración del evento. Sin esto, minRad=0.001f infla
        // la esfera 9× durante la SN y al nacer el pulsar (minRad=0.0f) se ve
        // un encogimiento instantáneo — el "POP" de tamaño reportado.
        float minRad = (b.stellarPhase == StellarPhase::PULSAR   ||
                        b.stellarPhase == StellarPhase::MAGNETAR  ||
                        b.stellarPhase == StellarPhase::NEUTRON_STAR ||
                        b.stellarPhase == StellarPhase::SUPERNOVA)
                       ? 0.0f : 0.001f;
        float rad = std::max(minRad, rR);

        // Subir uniforms específicos de esta estrella
        UploadBodyUniforms(shader, locs, b);

        // Color material = dCol: fallback visible si el shader no carga
        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color   = dCol;
        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = blankTex;
        planetModel.materials[0].maps[MATERIAL_MAP_NORMAL].texture  = blankTex;

        // Usar TidalBodyTransform: aplica deformación por mareas Y achatamiento
        // por rotación (RotationalOblateness), igual que planetas.
        DrawMesh(planetModel.meshes[0], planetModel.materials[0],
                 TidalBodyTransform(p, rad, b.rotationAngle, b));

        // Shell de supernova activa: ahora la dibuja DrawSupernova (raymarcher
        // volumétrico) desde el bucle de render en main.cpp, donde está
        // disponible la cámara. Ya no se dibuja aquí.

        // Halo/corona aditivo — para TODOS los tipos de estrella incluyendo
        // pulsares y magnetares. Los halos son efectos visuales aditivos
        // semi-transparentes: NO afectan el hitbox de seleccion (que usa
        // el radio fisico real). El tamaño visual correcto vs hitbox lo
        // garantiza el fix del std::max(0.001f,rR) en la malla de arriba.
        {
            float lum = (float)(b.luminosity / L_SUN);
            DrawStarHalo(p, rad, dCol, b.stellarActivity, lum, (float)b.temperature);
        }

        goto draw_overlay;
    }

    // --- GIGANTE GASEOSO/HELADO: shader procedural multicapa ---
    if (b.isGasGiant) {
        UploadBodyUniforms(shader, locs, b);

        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color    = WHITE;
        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture  = blankTex;
        planetModel.materials[0].maps[MATERIAL_MAP_NORMAL].texture   = blankTex;
        planetModel.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = blankTex;
        planetModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = blankTex;

        float  rad = std::max(0.001f, rR);
        DrawMesh(planetModel.meshes[0], planetModel.materials[0],
                 TidalBodyTransform(p, rad, b.rotationAngle, b));

        goto draw_overlay;
    }

    // --- PLANETA ROCOSO/HELADO: shader procedural (drawRockyPlanet) ---
    if (b.isRockyPlanet) {
        UploadBodyUniforms(shader, locs, b);

        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color    = WHITE;
        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture  = blankTex;
        // NORMAL (unidad 1), SPECULAR (unidad 2) y ROUGHNESS (unidad 3)
        // se dejan SIN textura (id=0): si DrawMesh() les asignara
        // blankTex pisaria los bindings manuales de uNormalMap/
        // uSpecularMap/uColorMap hechos en UploadBodyUniforms() para
        // la Tierra.
        planetModel.materials[0].maps[MATERIAL_MAP_NORMAL].texture    = Texture2D{0};
        planetModel.materials[0].maps[MATERIAL_MAP_SPECULAR].texture  = Texture2D{0};
        planetModel.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture = Texture2D{0};
        planetModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = blankTex;

        float  rad = std::max(0.001f, rR);
        DrawMesh(planetModel.meshes[0], planetModel.materials[0],
                 TidalBodyTransform(p, rad, b.rotationAngle, b));

        // ── ATMOSFERA: segunda esfera concentrica inflada (~3%) ──────
        // Segunda pasada, MISMA malla a un radio mayor, con blending
        // alfa: drawAtmosphereShell() (shaders.h) calcula su propio
        // color/alfa por Fresnel (grosor optico hacia el limbo) +
        // iluminacion direccional, independiente de la corteza -- la
        // atmosfera ahora tiene volumen real y sobresale del disco en
        // vez de quedar pintada plana sobre la superficie.
        if (!b.hideAtmosphere && b.atmosphereDensity > 0.001f) {
            int shellOn = 1;
            SetShaderValue(shader, locs.isAtmosphereShell, &shellOn, SHADER_UNIFORM_INT);

            // Misma transformacion que la corteza (TidalBodyTransform:
            // escala + spin + ESTIRAMIENTO POR MAREA + traslacion), solo
            // con un radio mayor. Una atmosfera ligada gravitacionalmente
            // a un planeta deformado por mareas se deforma CON el planeta
            // (mismo eje/achatamiento/elongacion, ver tideAxis/
            // tideVisualSquash/tideVisualElongation en body.h) -- de lo
            // contrario la capa inflada se queda esferica mientras la
            // corteza debajo se estira, despegandose visualmente de ella.
            float   radAtmo = rad * 1.03f;
            Matrix  matAtmo = TidalBodyTransform(p, radAtmo, b.rotationAngle, b);

            BeginBlendMode(BLEND_ALPHA);
            rlDisableDepthMask();
            DrawMesh(planetModel.meshes[0], planetModel.materials[0], matAtmo);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            EndBlendMode();

            int shellOff = 0;
            SetShaderValue(shader, locs.isAtmosphereShell, &shellOff, SHADER_UNIFORM_INT);
        }

        // ── NUBES: tercera esfera concentrica a radio intermedio ─────
        // Tercera pasada, MISMA malla a un radio entre la corteza (1.0)
        // y la atmosfera inflada (1.03): drawCloudShell() (shaders.h)
        // dibuja parches de nubes con su propia iluminacion direccional
        // y blending alfa -- ya no es una "calcomania" pintada sobre la
        // corteza, sino una capa con volumen real flotando sobre el
        // relieve, por debajo del borde de la atmosfera.
        if (!b.hideClouds && b.rockyPlanet.cloudDensity > 0.001f) {
            int cloudOn = 1;
            SetShaderValue(shader, locs.isCloudShell, &cloudOn, SHADER_UNIFORM_INT);

            // Igual que la atmosfera (ver bloque anterior): TidalBodyTransform
            // aplica la MISMA deformacion por marea que la corteza, asi la
            // capa de nubes -- arrastrada por la atmosfera del planeta --
            // sigue su forma elipsoidal en vez de quedarse esferica.
            float   radCloud = rad * 1.012f;
            Matrix  matCloud = TidalBodyTransform(p, radCloud, b.rotationAngle, b);

            BeginBlendMode(BLEND_ALPHA);
            rlDisableDepthMask();
            DrawMesh(planetModel.meshes[0], planetModel.materials[0], matCloud);
            rlDrawRenderBatchActive();
            rlEnableDepthMask();
            EndBlendMode();

            int cloudOff = 0;
            SetShaderValue(shader, locs.isCloudShell, &cloudOff, SHADER_UNIFORM_INT);
        }

        goto draw_overlay;
    }

    // --- Planetas con textura ---
    if (b.diffuseTex != nullptr) {
        UploadBodyUniforms(shader, locs, b);

        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color    = WHITE;
        planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture  = *b.diffuseTex;
        planetModel.materials[0].maps[MATERIAL_MAP_NORMAL].texture   = b.normalTex   ? *b.normalTex   : blankTex;
        planetModel.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = b.specularTex ? *b.specularTex : blankTex;
        planetModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = b.emissionTex ? *b.emissionTex : blankTex;

        float rad = std::max(0.001f, rR);
        DrawMesh(planetModel.meshes[0], planetModel.materials[0],
                 TidalBodyTransform(p, rad, b.rotationAngle, b));

        if (b.cloudTex != nullptr && !b.hideClouds) {
            planetModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture  = *b.cloudTex;
            planetModel.materials[0].maps[MATERIAL_MAP_NORMAL].texture   = blankTex;
            planetModel.materials[0].maps[MATERIAL_MAP_SPECULAR].texture = blankTex;
            planetModel.materials[0].maps[MATERIAL_MAP_EMISSION].texture = blankTex;
            DrawMesh(planetModel.meshes[0], planetModel.materials[0],
                     TidalBodyTransform(p, rad * 1.02f, b.cloudRotation, b));
        }
        goto draw_overlay;
    }

    // --- Planetas sin textura con efecto de marea ---
    if (!b.isFragment && std::fabs(b.tideVisualElongation - 1.0f) > 0.01f) {
        float    rad = std::max(0.001f, rR);
        Vector3  ta  = Vector3Normalize({(float)b.tideAxis.x, (float)b.tideAxis.y, (float)b.tideAxis.z});
        float    sq  = b.tideVisualSquash, el = b.tideVisualElongation;
        DrawSphere(p, rad * sq, dCol);
        DrawSphere(Vector3Add(p, Vector3Scale(ta,  rad*(el-sq)*0.5f)), rad*sq*0.9f, dCol);
        DrawSphere(Vector3Add(p, Vector3Scale(ta, -rad*(el-sq)*0.5f)), rad*sq*0.9f, dCol);
        if (b.isDisintegrating)
            DrawSphereWires(p, rad*el*1.05f, 6, 6,
                Fade({255, (unsigned char)(80*(1-b.tidalDamage)), 0, (unsigned char)(180*b.tidalDamage)}, 0.4f*(float)b.tidalDamage));
        goto draw_overlay;
    }

    // --- Esfera simple ---
    DrawSphere(p, std::max(0.001f, rR), dCol);

draw_overlay:
    if (bodyIndex == selectedBodyIdx) {
        // Billboard: DrawCircle3D (rmodels.c) dibuja el circulo en el
        // plano XY (vertices (sin*r, cos*r, 0)) -- su normal por defecto
        // es +Z, NO +Y -- y lo rota 'rotationAngle' grados alrededor de
        // 'rotationAxis'. Sin esto, el aro de seleccion queda SIEMPRE en
        // ese plano fijo del mundo, mostrando el borde de canto desde la
        // mayoria de los angulos de camara en vez de la cara. Para que
        // SIEMPRE muestre la cara a la camara (como un billboard), la
        // normal objetivo es la direccion cuerpo->camara; se calcula el
        // eje/angulo que rota +Z (el normal REAL por defecto) hacia esa
        // normal objetivo.
        Vector3 toCam = Vector3Subtract(camera.position, p);
        Vector3 billboardNormal = (Vector3LengthSqr(toCam) > 1e-8f)
            ? Vector3Normalize(toCam) : Vector3{0, 0, 1};
        Vector3 billboardAxis = Vector3CrossProduct({0, 0, 1}, billboardNormal);
        float   billboardDot  = ClampF(Vector3DotProduct({0, 0, 1}, billboardNormal), -1.0f, 1.0f);
        float   billboardAngle = acosf(billboardDot) * RAD2DEG;
        if (Vector3LengthSqr(billboardAxis) < 1e-8f) billboardAxis = {1, 0, 0}; // normal paralela a +-Z: cualquier eje sirve

        DrawCircle3D(p, rR * 1.4f, billboardAxis, billboardAngle, Fade(WHITE, 0.55f));
    }
    if (b.hasRings && bodyIndex == selectedBodyIdx)
        DrawCircle3D(p, rR * 3.2f, {1,0,0}, 90.0f, Fade(GetColor(0xaaaaaa88), 0.3f));
}

// ── Rotulos de nombre por cuerpo (texto 2D en espacio de pantalla) ──
// Tamano proporcional al mismo "hitbox" de seleccion que usa PickBody
// (camera_input.h): el radio en pixeles que ocuparia el radio FISICO real
// del cuerpo, con un piso de 18px para que objetos lejanos/pequenos sigan
// siendo identificables (ese piso es justamente lo que los hace
// "clicables" tambien). Sin esto, un planeta visto de lejos es un punto
// indistinguible de una luna o un asteroide -- el rotulo (que crece o se
// achica con la MISMA logica que decide que tan facil es seleccionarlo)
// resuelve eso de un vistazo. Se llama DESPUES de EndMode3D() (texto 2D,
// no debe dibujarse dentro de BeginMode3D/EndMode3D). Excluye fragmentos
// de colision (b.isFragment): mostrar el nombre de cada esquirla de un
// cataclismo (potencialmente cientos) saturaria la pantalla sin aportar
// nada -- el objetivo es identificar planetas/lunas/cuerpos del catalogo,
// no escombros.
//
// Corte por cercania: el rotulo se usa para identificar cuerpos PEQUEÑOS
// EN PANTALLA (de lejos, un punto indistinguible) -- de cerca, el cuerpo
// ya se ve perfectamente (superficie, anillos, etc.) y el texto solo
// estorba/tapa. 'screenR' SIN el piso de 18px (a diferencia de
// 'hitboxR') es el radio aparente REAL: por debajo de
// LABEL_HIDE_RADIUS_PX el rotulo esta a full opacidad, y se desvanece
// linealmente hasta desaparecer del todo al llegar a ese radio (en vez de
// un corte seco que "parpadee" al orbitar la camara justo en el limite).
inline void DrawBodyNameLabels(const std::vector<Body>& bodies, const Camera3D& cam)
{
    constexpr float LABEL_HIDE_RADIUS_PX = 60.0f; // radio aparente (px) a partir del cual el rotulo desaparece
    constexpr float LABEL_FADE_START_PX  = 40.0f; // empieza a desvanecerse antes del corte, no de golpe

    Vector3 camFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    int screenW = GetScreenWidth(), screenH = GetScreenHeight();

    for (const Body& b : bodies) {
        if (b.mass <= 0.0 || b.isFragment) continue;

        Vector3 p = ToDrawPos(b.pos);
        if (Vector3DotProduct(camFwd, Vector3Subtract(p, cam.position)) <= 0.0f) continue;

        Vector2 sp = GetWorldToScreen(p, cam);
        if (sp.x < -50 || sp.x > screenW + 50 || sp.y < -50 || sp.y > screenH + 50) continue;

        // Radio proyectado en pixeles usando el radio fisico real
        // (SIN el std::max(0.01,...) que inflaba pulsares/NS x83).
        // Para objetos sub-pixel (pulsares, NS, BH) screenR ≈ 0: se
        // fuerza alpha=1 y se muestra siempre la etiqueta, porque el
        // cuerpo es demasiado pequeño para verse sin ella.
        Vector2 sp2 = GetWorldToScreen(
            Vector3Add(p, {(float)(b.radius * RENDER_SCALE), 0.f, 0.f}), cam);
        float screenR = Vector2Distance(sp, sp2);
        if (screenR >= LABEL_HIDE_RADIUS_PX) continue; // demasiado grande en pantalla

        // Si el cuerpo es sub-pixel (< 3px de radio), forzar alpha=1:
        // el jugador no puede verlo sin la etiqueta.
        float alpha;
        if (screenR < 3.0f) {
            alpha = 1.0f;
        } else {
            alpha = 1.0f - ClampF(
                (screenR - LABEL_FADE_START_PX) / (LABEL_HIDE_RADIUS_PX - LABEL_FADE_START_PX),
                0.0f, 1.0f);
        }

        float hitboxR = std::max(18.0f, screenR);
        int fontSize = (int)ClampF(hitboxR * 0.55f, 12.0f, 42.0f);
        const char* name = b.name.c_str();
        int textW = MeasureText(name, fontSize);

        int tx = (int)sp.x - textW / 2;
        int ty = (int)sp.y + (int)(fontSize * 0.8f) + 6;

        DrawText(name, tx + 1, ty + 1, fontSize, Fade(BLACK, 0.7f * alpha));
        DrawText(name, tx,     ty,     fontSize, Fade(RAYWHITE, alpha));
    }
}

// ── Locs del shader de escombros instanciados (ROCK_INSTANCE_*_SRC) ──
struct RockShaderLocs {
    int lightDir;
    int baseColor;
    int ambientStrength;
    int lightIntensity;
    int viewPos;
    int shadowFactor;
    int translucency;
};

inline RockShaderLocs GetRockShaderLocs(Shader& sh) {
    // SHADER_LOC_MATRIX_MODEL se reutiliza como la ubicacion del ATRIBUTO
    // de instancia 'instanceTransform' -- asi es como DrawMeshInstanced
    // (rmodels.c) localiza el VBO de matrices por-instancia.
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(sh, "instanceTransform");
    RockShaderLocs l;
    l.lightDir         = GetShaderLocation(sh, "uLightDir");
    l.baseColor         = GetShaderLocation(sh, "uBaseColor");
    l.ambientStrength   = GetShaderLocation(sh, "uAmbientStrength");
    l.lightIntensity    = GetShaderLocation(sh, "uLightIntensity");
    l.viewPos           = GetShaderLocation(sh, "uViewPos");
    l.shadowFactor      = GetShaderLocation(sh, "uShadowFactor");
    l.translucency      = GetShaderLocation(sh, "uTranslucency");
    return l;
}

inline float PhysicalLightIntensityRelative(double lumWatts, float distDraw)
{
    double distMeters = std::max(1.0, (double)distDraw / RENDER_SCALE);
    double irradiance = lumWatts / (4.0 * PI_D * distMeters * distMeters);
    double rel = irradiance / 1361.0; // Sol en Tierra ~= 1
    return (float)ClampD(rel, 0.0, 80.0);
}

inline float VisualLightResponseCPU(float physicalAtten)
{
    return ClampF(
        logf(1.0f + physicalAtten * 1.75f) / logf(1.0f + 1.75f),
        0.0f,
        4.0f
    );
}

// Misma formula/constante que lightLumFactor() en shaders.h (duplicada a
// proposito, igual que el resto de utilidades compartidas entre stages de
// shader en este motor): comprime el rango dinamico de luminosidad real
// (Watts) en un factor multiplicativo suave, neutral (factor=1) a
// luminosidad solar.
inline float LightLumFactor(double lumWatts) {
    return (float)ClampD(std::pow(lumWatts / L_SUN, 0.25), 0.15, 3.0);
}

// ── Campo de escombros 3D: rocas low-poly instanciadas ──────
// Sustituye al antiguo campo de billboards 2D: cada particula activa del
// Particle Pool (ver DustParticle::active, body.h) aporta una matriz de
// transformacion (escala 'd.scale' -> rotacion 'currentRotation' alrededor
// de 'rotationAxis' -> traslacion a su posicion). TODAS esas matrices se
// suben a la GPU en un unico DrawMeshInstanced (un solo draw call para
// decenas de miles de rocas, ver BuildLowPolyRockMesh/ROCK_INSTANCE_*_SRC).
//
// Iluminacion: para POLVO DE COLISION (isRing=false, sin host) se usa una
// sola direccion de luz global hacia la estrella mas masiva -- aproximacion
// razonable porque cada nube de escombros ocupa un volumen muy pequeno
// frente a la distancia a la estrella. Para POLVO DE ANILLO (isRing=true,
// con DustParticle::hostBodyId, ver body.h) la direccion de luz se calcula
// relativa a la posicion REAL del planeta duenio (no al foco de camara,
// como hacia la version anterior -- ver g_renderOrigin/ToDrawPos en
// math_utils.h), y cada particula recibe un test de sombra cilindrica
// simple: si esta del lado nocturno de su planeta Y dentro de su radio
// proyectado contra la estrella, queda en un bucket "en sombra" (solo
// ambiente, sin difusa) -- sin esto, el lado del anillo eclipsado por su
// propio planeta brillaba igual que el lado iluminado.
inline void DrawDustField3D(const std::vector<DustParticle>& dust,
                             const std::vector<Body>& bodies,
                             Mesh& rockMesh, Material& rockMaterial,
                             const RockShaderLocs& rLocs,
                             const Camera3D& camera)
{
    Vector3 globalLightDir = {0.0f, 0.0f, 1.0f};
    float   globalLightDist = 0.0f; // unidades de dibujado, ver lightIntensity mas abajo
    const Body* mainStar = nullptr;
    for (const Body& b : bodies)
        if (b.isStar && b.mass > 0.0 && (!mainStar || b.mass > mainStar->mass)) mainStar = &b;
    Vector3D mainStarPosPhys = mainStar ? mainStar->pos : Vector3D{0, 0, 0};
    if (mainStar) {
        Vector3 starDraw = ToDrawPos(mainStar->pos);
        globalLightDist = Vector3Length(starDraw); // distancia foco-de-camara -> estrella
        globalLightDir  = Vector3Normalize(starDraw);
    } else if (g_fakeLightEnabled) {
        // Misma dirección fija que la estrella falsa de UploadLightUniforms().
        // No depende de la cámara.
        const Vector3 FAKE_LIGHT_DIR = Vector3Normalize({ -0.45f, 0.0f, 0.82f });

        globalLightDir  = FAKE_LIGHT_DIR;
        globalLightDist = 10000.0f;
    }
    // Ambiente mínimo solo para evitar negro absoluto en anillos/polvo.
    // La iluminación principal sigue viniendo de la difusa real/falsa.
    const float ambStrength = 0;

    // Lookup id->Body* reconstruido una vez por frame (no por particula):
    // decenas/cientos de 'bodies' contra decenas de miles de particulas de
    // polvo, asi que amortiza de inmediato. Estatico para no realocar el
    // unordered_map cada frame.
    static std::unordered_map<uint64_t, const Body*> hostById;
    hostById.clear();
    for (const Body& b : bodies) hostById[b.id] = &b;

    // Tamano minimo en pantalla: misma formula/umbral que DrawBody (renderer.h)
    // y DrawCursorPreview (ui.h) -- una roca de anillo (100-500m) a
    // RENDER_SCALE=1e-8 mide 1e-6..5e-6 unidades de dibujado, es decir,
    // sub-pixel a cualquier distancia de camara razonable (Saturno mismo
    // mide ~0.58 unidades). Sin esta inflacion el anillo es
    // matematicamente correcto (orbita estable, ver SpawnPlanetaryRing)
    // pero invisible. 'd.scale' (fisico) no se modifica: la inflacion es
    // solo para esta matriz de dibujado, igual que 'rR'/'pR' en
    // DrawBody/DrawCursorPreview no alteran b.radius/catalogEntry.radius.
    const float screenToWorld = tanf(camera.fovy * 0.5f * DEG2RAD) / (float)GetScreenHeight();

    // Color base "gris espacial" para polvo de COLISION (isRing=false):
    // mismo valor que el antiguo color fijo del shader (0.6,0.58,0.55),
    // preservado como aspecto por defecto. El polvo de ANILLO (isRing=true)
    // usa en cambio 'd.color', fijado por SpawnPlanetaryRing segun el
    // material/planeta duenio del anillo (ver main.cpp).
    const Color DEFAULT_DUST_COLOR = {153, 148, 140, 255};

    // Color "caliente": mismo tinte que ComputeBodyColor aplica a cuerpos NO
    // fragmento con heatSpike=1.0 (renderer.h ~214-216) -- reusa la misma
    // paleta de "recien expulsado/incandescente" en todo el motor.
    const Color HOT_DUST_COLOR = {210, 95, 35, 255};

    // HEAT_BUCKET_COUNT=4: heatSpike decae a -0.008/frame (~125 frames,
    // ~2.08s @60fps, de 1.0 a 0 -- ver UpdateDustLifecycle en physics.h).
    // 4 buckets -> ~31 frames (~0.52s) por nivel, suficiente para percibir
    // el enfriamiento como transicion gradual sin parpadeo por-frame.
    constexpr int   HEAT_BUCKET_COUNT = 4;
    constexpr float HEAT_BUCKET_STEP  = 1.0f / HEAT_BUCKET_COUNT; // 0.25

    // Agrupado por (color, calor, host, sombra): un uniform (uBaseColor o
    // uLightDir) es compartido por todo un DrawMeshInstanced, asi que cada
    // combinacion distinta necesita su propio lote/draw call. 'hostKey' es
    // el Body::id del planeta duenio (~0ull = sin host, polvo de colision o
    // anillo huerfano) y 'shadowBucket' cuantiza la fraccion de luz directa
    // que SI llega (penumbra suave, ver mas abajo) en SHADOW_BUCKET_COUNT
    // niveles -- un entero, no un bool, para poder degradar el termino
    // difuso/especular de forma gradual en vez de un corte binario
    // luz/sombra. 'groups' es estatico para conservar la capacidad de los
    // vectores de matrices entre frames (evita reallocs con decenas de
    // miles de particulas).
    struct ColorGroup { Color color; int shadowBucket; uint64_t hostKey; bool isRing; std::vector<Matrix> transforms; };
    static std::vector<ColorGroup> groups;
    for (ColorGroup& g : groups) g.transforms.clear();

    constexpr uint64_t NO_HOST = ~0ull;
    constexpr int SHADOW_BUCKET_COUNT = 4; // niveles de penumbra (0=eclipsado..N=luz plena)

    // Tono de escombro neutro hacia el que se desvanece el color de banda
    // original cuando una particula de anillo se aleja mucho de la
    // distancia a la que nacio (ver DustParticle::spawnRadiusFromHost,
    // body.h) -- sin esto, un anillo realmente disgregado por un
    // sobrevuelo cercano (ver UpdateDustGravity, physics.h) sigue pintado
    // exactamente con el mismo material/banda de siempre, y de lejos
    // parece "el mismo Saturno" aunque las posiciones ya no tengan nada
    // que ver con un disco ordenado.
    constexpr Color DEBRIS_TINT_COLOR = {120, 112, 102, 255};
    constexpr int    DRIFT_BUCKET_COUNT = 4;

    for (const DustParticle& d : dust) {
        if (!d.active) continue;
        Color key = d.isRing ? d.color : DEFAULT_DUST_COLOR;

        if (d.isRing && d.spawnRadiusFromHost > 1.0f) {
            double curR = -1.0;
            if (d.hostBodyId >= 0) {
                auto it = hostById.find((uint64_t)d.hostBodyId);
                if (it != hostById.end()) curR = (d.pos - it->second->pos).length();
            }
            float driftFrac = (curR < 0.0)
                ? 1.0f
                : ClampF((float)(std::fabs(curR - (double)d.spawnRadiusFromHost) / (double)d.spawnRadiusFromHost), 0.0f, 1.0f);
            int driftBucket = (int)std::round(driftFrac * DRIFT_BUCKET_COUNT);
            if (driftBucket > 0) {
                float t = (float)driftBucket / (float)DRIFT_BUCKET_COUNT;
                key = {
                    (unsigned char)ClampF(key.r*(1-t) + DEBRIS_TINT_COLOR.r*t, 0, 255),
                    (unsigned char)ClampF(key.g*(1-t) + DEBRIS_TINT_COLOR.g*t, 0, 255),
                    (unsigned char)ClampF(key.b*(1-t) + DEBRIS_TINT_COLOR.b*t, 0, 255), 255
                };
            }
        }

        // Particulas recien expulsadas (heatSpike>0, ver MakeFragments/
        // UpdateDustLifecycle) se tinen hacia HOT_DUST_COLOR y se enfrian
        // gradualmente a medida que decae heatSpike. Bucketizado (en vez de
        // un tinte continuo) porque DrawMeshInstanced solo admite un
        // uBaseColor por lote -- cada bucket caliente es simplemente otro
        // ColorGroup. bucket==0 (heatSpike<=0, la inmensa mayoria del polvo)
        // reproduce 'key' sin cambios.
        int bucket = (d.heatSpike <= 0.0f) ? 0
                   : std::min(HEAT_BUCKET_COUNT, (int)std::ceil(d.heatSpike / HEAT_BUCKET_STEP));
        Color heatedKey = key;
        if (bucket > 0) {
            float bucketHeat = (float)bucket * HEAT_BUCKET_STEP * 0.45f;
            heatedKey = {
                (unsigned char)ClampF(key.r*(1-bucketHeat) + HOT_DUST_COLOR.r*bucketHeat, 0,255),
                (unsigned char)ClampF(key.g*(1-bucketHeat) + HOT_DUST_COLOR.g*bucketHeat, 0,255),
                (unsigned char)ClampF(key.b*(1-bucketHeat) + HOT_DUST_COLOR.b*bucketHeat, 0,255), 255
            };
        }

        // --- Sombra cilindrica + host, solo polvo de ANILLO con host valido ---
        // Polvo de colision (isRing=false) siempre usa hostKey=NO_HOST y
        // sigue la aproximacion de luz global, sin cambios de comportamiento.
        // 'lit' es la fraccion de luz directa que SI llega: 1.0 a plena luz,
        // 0.0 totalmente eclipsado, con un margen de penumbra suavizado
        // (smoothstep) en el borde del cilindro de sombra del planeta en vez
        // de un corte binario -- evita el filo duro y antialiaseado de la
        // version anterior, donde un lote entero pasaba de luz plena a
        // ambiente puro de golpe.
        double lit = 1.0;
        uint64_t hostKey = NO_HOST;
        if (d.isRing && d.hostBodyId >= 0) {
            auto it = hostById.find((uint64_t)d.hostBodyId);
            if (it != hostById.end()) {
                const Body* host = it->second;
                hostKey = host->id;
                if (mainStar || g_fakeLightEnabled) {
                    // Dirección HACIA la fuente de luz.
                    //
                    // - Con estrella real: dirección desde el planeta host hacia la estrella.
                    // - Con luz falsa: misma dirección fija global usada por UploadLightUniforms().
                    Vector3D toLight = mainStar
                        ? NormalizeSafe(mainStarPosPhys - host->pos)
                        : NormalizeSafe(Vector3D{-0.45, 0.0, 0.82});

                    Vector3D rel = d.pos - host->pos;

                    // Si la particula esta del lado nocturno del planeta host
                    // respecto a la luz, se atenua segun cuanto se adentra en
                    // el cilindro de sombra proyectado (radio del host +/-
                    // margen de penumbra).
                    double proj = rel.dot(toLight);
                    if (proj < 0.0) {
                        Vector3D perp = rel - toLight * proj;
                        double perpLen = perp.length();
                        double margin  = host->radius * 0.18;
                        double inner   = std::max(0.0, host->radius - margin);
                        double outer   = host->radius + margin;
                        double t = ClampD((perpLen - inner) / std::max(1.0, outer - inner), 0.0, 1.0);
                        double smooth = t * t * (3.0 - 2.0 * t); // smoothstep
                        lit = smooth; // 0 en el centro de la sombra, 1 fuera del margen
                    }
                }
            }
        }
        int shadowBucket = (int)std::round(ClampD(lit, 0.0, 1.0) * SHADOW_BUCKET_COUNT);

        ColorGroup* g = nullptr;
        for (ColorGroup& grp : groups) {
            if (grp.shadowBucket == shadowBucket && grp.hostKey == hostKey && grp.isRing == d.isRing
                && grp.color.r == heatedKey.r && grp.color.g == heatedKey.g && grp.color.b == heatedKey.b) { g = &grp; break; }
        }
        if (!g) { groups.push_back({heatedKey, shadowBucket, hostKey, d.isRing, {}}); g = &groups.back(); }

        Vector3 p = ToDrawPos(d.pos);
        float dist3d  = Vector3Distance(camera.position, p);

        // Partículas de cinturón libre (no anillos planetarios): cullear a más de 5000 draw units
        // (~3.3 AU). A mayor distancia, cada partícula inflada a "3px de pantalla" se convierte
        // en cientos de draw units de radio, y 34K partículas forman una esfera sólida que escribe
        // al depth buffer tapando planetas interiores. Dentro de este radio se ven individualmente.
        if (!d.isRing && d.hostBodyId < 0 && dist3d > 5000.0f) continue;

        float screenR = (d.scale / dist3d) / screenToWorld;
        float drawScale = (screenR < 3.0f) ? dist3d * 3.0f * screenToWorld : d.scale;

        // Anillos: orientacion FIJA por particula (no tumbling continuo).
        // Si cada roca de anillo rota libremente CADA FRAME, expone una
        // faceta distinta y aleatoria al unico rayo de luz del lote cuadro
        // a cuadro -> de lejos se ve como un campo de canicas centelleantes
        // en vez de un disco coherente. Pero una orientacion COMPLETAMENTE
        // identica (sin rotacion alguna) para las decenas de miles de
        // instancias del lote tampoco sirve: con el especular/translucidez
        // de mas arriba (shaders.h), todas las instancias comparten
        // exactamente el mismo N, L y (casi) V -> el destello aparece
        // sincronizado en el MISMO punto de cada roca a la vez, como un
        // flash uniforme en vez de un brillo disperso natural. La solucion
        // es una rotacion FIJA (no avanza con el tiempo) pero DISTINTA por
        // particula, derivada de 'd.seed': variedad real de facetas entre
        // instancias, sin parpadeo temporal alguno.
        Matrix m = d.isRing
            ? MatrixMultiply(
                  MatrixMultiply(MatrixScale(drawScale, drawScale, drawScale),
                                  MatrixRotate(d.rotationAxis, d.seed * 2.0f * (float)PI_D)),
                  MatrixTranslate(p.x, p.y, p.z))
            : MatrixMultiply(
                  MatrixMultiply(MatrixScale(drawScale, drawScale, drawScale),
                                  MatrixRotate(d.rotationAxis, d.currentRotation)),
                  MatrixTranslate(p.x, p.y, p.z));
        g->transforms.push_back(m);
    }

    for (ColorGroup& g : groups) {
        if (g.transforms.empty()) continue;

        // lightDir/lightIntensity por GRUPO (no un unico valor global por
        // frame, como antes): cada host necesita su propia direccion/
        // distancia real a la estrella. La atenuacion por sombra ya NO se
        // hace anulando 'ld' (eso descartaba la direccion real incluso para
        // el termino especular/translucido) -- ahora 'uShadowFactor' (ver
        // mas abajo) escala el resultado en el shader, asi que 'ld' siempre
        // es la direccion real hacia la luz.
        Vector3 ld;
        float   lightDistDraw;
        if (g.hostKey != NO_HOST && mainStar) {
            auto it = hostById.find(g.hostKey);
            const Body* host = (it != hostById.end()) ? it->second : nullptr;
            Vector3D ts = host ? NormalizeSafe(mainStarPosPhys - host->pos) : Vector3D{0, 0, 1};
            ld = { (float)ts.x, (float)ts.y, (float)ts.z };
            lightDistDraw = host ? (float)((mainStarPosPhys - host->pos).length() * RENDER_SCALE) : globalLightDist;
        } else {
            ld = globalLightDir;
            lightDistDraw = globalLightDist;
        }
        SetShaderValue(rockMaterial.shader, rLocs.lightDir, &ld, SHADER_UNIFORM_VEC3);

        // Intensidad difusa real: luminosidad de la estrella (Watts) +
        // atenuacion por distancia, misma formula que el resto de cuerpos
        // (lightLumFactor en shaders.h / LightLumFactor aqui arriba), con el
        // atenuado artistico *0.6 de siempre para no saturar 'uBaseColor'.
        // 0 si no hay estrella (sin luz real que aportar, solo ambiente).
        float intensity = 0.0f;

        if (mainStar) {
            float phys = PhysicalLightIntensityRelative(mainStar->luminosity, lightDistDraw);
            intensity = VisualLightResponseCPU(phys) * 0.6f;
        } else if (g_fakeLightEnabled) {
            intensity = 0.6f;
        }
        SetShaderValue(rockMaterial.shader, rLocs.lightIntensity, &intensity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(rockMaterial.shader, rLocs.ambientStrength, &ambStrength, SHADER_UNIFORM_FLOAT);

        // Fraccion de luz directa que llega a este lote (penumbra suave
        // cuantizada, ver shadowBucket arriba) -- escala diffuse/especular
        // en el shader sin tocar 'ld' ni la translucidez.
        float shadowFactor = (float)g.shadowBucket / (float)SHADOW_BUCKET_COUNT;
        SetShaderValue(rockMaterial.shader, rLocs.shadowFactor, &shadowFactor, SHADER_UNIFORM_FLOAT);

        // Translucidez de contraluz: solo polvo de ANILLO (hielo/polvo fino)
        // la exhibe -- el polvo de colision (rocas opacas) se queda en 0,
        // igual que antes de este cambio.
        float translucency = g.isRing ? 0.45f : 0.0f;
        SetShaderValue(rockMaterial.shader, rLocs.translucency, &translucency, SHADER_UNIFORM_FLOAT);

        Vector3 viewPos = camera.position;
        SetShaderValue(rockMaterial.shader, rLocs.viewPos, &viewPos, SHADER_UNIFORM_VEC3);

        Vector3 col = { g.color.r / 255.0f, g.color.g / 255.0f, g.color.b / 255.0f };
        SetShaderValue(rockMaterial.shader, rLocs.baseColor, &col, SHADER_UNIFORM_VEC3);
        // Anillos de planetas muy lejanos: deshabilitar depth write cuando el planeta host
        // es sub-pixel en pantalla (< 50px de radio). Sin esto, las miles de partículas
        // de anillo forman un occluder sólido que tapa planetas aún más lejanos.
        // Cuando el host es grande en pantalla (cerca), depth write ON es correcto (el anillo
        // tapa lo que hay detrás de él).
        bool depthMaskOff = false;
        if (g.isRing && g.hostKey != NO_HOST) {
            auto it2 = hostById.find(g.hostKey);
            if (it2 != hostById.end()) {
                Vector3 hDraw = ToDrawPos(it2->second->pos);
                float hDist = std::max(Vector3Distance(camera.position, hDraw), 0.001f);
                float hScreenR = (float)(it2->second->radius * RENDER_SCALE) / hDist
                                 * (float)GetScreenHeight() / tanf(camera.fovy * 0.5f * DEG2RAD);
                depthMaskOff = (hScreenR < 50.0f);
            }
        }
        if (depthMaskOff) rlDisableDepthMask();
        DrawMeshInstanced(rockMesh, rockMaterial, g.transforms.data(), (int)g.transforms.size());
        if (depthMaskOff) rlEnableDepthMask();
    }
}

// ── Locs del shader de llamaradas ────────────────────────────
struct FlareShaderLocs {
    int realTime, temp, stellarActivity, flareIndex, flareGrow;
    int flareHeightMult, flareMode, flareAsym, flareBurst, flareFade;
    int flareDrainMode, flareDrainStart;
};

// ── Locs del shader volumétrico de supernova (ver SUPERNOVA_*_SRC) ──
struct SupernovaShaderLocs {
    int camPos, center, radius, time, progress, brightness, opacity, seed;
    int colorCore, colorMid, colorEdge;
};

inline SupernovaShaderLocs GetSupernovaShaderLocs(Shader& sh) {
    SupernovaShaderLocs l;
    l.camPos     = GetShaderLocation(sh, "uCamPos");
    l.center     = GetShaderLocation(sh, "uCenter");
    l.radius     = GetShaderLocation(sh, "uRadius");
    l.time       = GetShaderLocation(sh, "uTime");
    l.progress   = GetShaderLocation(sh, "uProgress");
    l.brightness = GetShaderLocation(sh, "uBrightness");
    l.opacity    = GetShaderLocation(sh, "uOpacity");
    l.seed       = GetShaderLocation(sh, "uSeed");
    l.colorCore  = GetShaderLocation(sh, "uColorCore");
    l.colorMid   = GetShaderLocation(sh, "uColorMid");
    l.colorEdge  = GetShaderLocation(sh, "uColorEdge");
    return l;
}

// ── Dibujo volumétrico de la supernova ───────────────────────
// Se llama para estrellas en fase SUPERNOVA. Traza un raymarch esférico
// (shader) sobre un cubo acotante. La paleta de color depende del progenitor
// (temperatura congelada de la supergigante antes de explotar):
//   · frío  (< 5000K, supergigante roja / Tipo II)  → tipo Cangrejo: filamentos
//     rojos/naranjas + capa turquesa (oxígeno) + núcleo blanco-azul.
//   · medio (5000–12000K)                           → naranja/amarillo + rojo.
//   · caliente (> 12000K, progenitor azul)          → púrpura/azul + blanco.
inline void DrawSupernova(const Body& b, const Camera3D& cam, Shader& sh,
                          const SupernovaShaderLocs& l, Model& cubeModel)
{
    if (b.stellarPhase != StellarPhase::SUPERNOVA) return;
    // Emerge cuando la estrella ya está casi completamente compactada (~90%,
    // progress 0.045): primero se ve la implosión, luego revienta la explosión.
    if (b.supernovaProgress <= 0.045) return;

    const Vector3 p = ToDrawPos(b.pos);
    // Fireball base = radio original de la supergigante (guardado en kickVelocity
    // al iniciar la SN, en metros). La bola de fuego arranca a una fracción de
    // ese tamaño y crece con la onda de choque (supernovaRadius).
    double origRadiusM = (b.kickVelocity > 0.0) ? b.kickVelocity : b.radius;
    float  baseR  = (float)(origRadiusM * RENDER_SCALE);
    float  shockR = (float)(b.supernovaRadius * RENDER_SCALE);
    // La explosión emerge PEQUEÑA desde el núcleo de la estrella y crece con la
    // onda de choque (antes arrancaba en baseR*0.5 y aparecía ya enorme, lejos
    // del núcleo). El pequeño baseR*0.08 es la bola de fuego inicial en el centro.
    float  radius = shockR + baseR * 0.08f;
    radius = std::max(radius, baseR * 0.05f);
    if (radius <= 0.0f) return;

    float prog = (float)b.supernovaProgress;
    // Brillo: destello fuerte de la curva de luz + glow base persistente para
    // que el remanente en expansión siga visible tras el pico de luminosidad.
    float lightCurve = (float)SupernovaLightCurve(b.supernovaProgress);
    // Destello suave en el pico + glow base persistente. Multiplicador bajo
    // (2.2) para que el tonemap NO sature a blanco puro al emerger la SN.
    float brightness = 0.6f + 2.2f * lightCurve;

    // Opacidad (presencia): fade-IN al emerger (no aparecer de golpe) y fade-OUT
    // en el último 30% de la fase (no desaparecer de golpe al volverse objeto
    // compacto). Escala col y alpha juntos en el shader (uOpacity).
    auto smooth01 = [](float a, float b2, float x) {
        float t = std::clamp((x - a) / (b2 - a), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    float fadeIn  = smooth01(0.045f, 0.10f, prog);
    float fadeOut = 1.0f - smooth01(0.70f, 1.0f, prog);
    float opacity = fadeIn * fadeOut;

    // Tiempo de animación en tiempo SIMULADO (coherente con el resto de la sim):
    // la turbulencia evoluciona según stellarPhaseAge, no según frames reales.
    float animTime = (float)(b.stellarPhaseAge / 3.0e4);

    float seed = (float)((b.id * 2654435761ull) % 100000ull) * 1e-3f;

    // ── Paleta según progenitor (temperatura previa a la explosión) ──
    float progTemp = (b.flareDeathTemp > 100.0f)
                     ? b.flareDeathTemp : (float)b.temperature;
    Vector3 cCore, cMid, cEdge;
    if (progTemp < 5000.0f) {
        // Supergigante roja → Nebulosa del Cangrejo
        cCore = {0.75f, 0.90f, 1.00f}; // núcleo blanco-azul
        cMid  = {0.20f, 0.85f, 0.70f}; // turquesa (oxígeno)
        cEdge = {1.00f, 0.35f, 0.12f}; // filamentos rojo-naranja
    } else if (progTemp < 12000.0f) {
        // Progenitor templado → cálido
        cCore = {1.00f, 0.98f, 0.85f};
        cMid  = {1.00f, 0.62f, 0.20f};
        cEdge = {0.85f, 0.12f, 0.10f};
    } else {
        // Progenitor azul caliente → púrpura/azul
        cCore = {1.00f, 0.95f, 1.00f};
        cMid  = {0.45f, 0.55f, 1.00f};
        cEdge = {0.70f, 0.15f, 0.85f};
    }

    Vector3 camPos = cam.position;
    SetShaderValue(sh, l.camPos,     &camPos,     SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.center,     &p,          SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.radius,     &radius,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.time,       &animTime,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.progress,   &prog,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.brightness, &brightness, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.opacity,    &opacity,    SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.seed,       &seed,       SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, l.colorCore,  &cCore,      SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.colorMid,   &cMid,       SHADER_UNIFORM_VEC3);
    SetShaderValue(sh, l.colorEdge,  &cEdge,      SHADER_UNIFORM_VEC3);

    // Cubo acotante: escala 1.45× el radio para contener la esfera de marcha
    // (1.4×uRadius, que a su vez abarca los lóbulos deformados hasta r~1.2).
    // Se renderizan las caras TRASERAS (cull frontal) para que el volumen se
    // dibuje también cuando la cámara está DENTRO de la esfera (zoom cercano).
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    rlDisableDepthMask();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    DrawModelEx(cubeModel, p, {0.0f, 1.0f, 0.0f}, 0.0f,
                {radius * 1.45f, radius * 1.45f, radius * 1.45f}, WHITE);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);
    rlEnableDepthMask();
    EndBlendMode();
}

inline FlareShaderLocs GetFlareShaderLocs(Shader& sh) {
    FlareShaderLocs l;
    l.realTime        = GetShaderLocation(sh, "realTime");
    l.temp            = GetShaderLocation(sh, "temp");
    l.stellarActivity = GetShaderLocation(sh, "stellarActivity");
    l.flareIndex      = GetShaderLocation(sh, "flareIndex");
    l.flareGrow       = GetShaderLocation(sh, "flareGrow");
    l.flareHeightMult = GetShaderLocation(sh, "flareHeightMult");
    l.flareMode       = GetShaderLocation(sh, "flareMode");
    l.flareAsym       = GetShaderLocation(sh, "flareAsym");
    l.flareBurst      = GetShaderLocation(sh, "flareBurst");
    l.flareFade       = GetShaderLocation(sh, "flareFade");
    l.flareDrainMode  = GetShaderLocation(sh, "flareDrainMode");
    l.flareDrainStart = GetShaderLocation(sh, "flareDrainStart");
    return l;
}

// ── Malla de arco-cinta (ribbon arch) en el plano XZ ─────────
// archHeight : altura del arco sobre la superficie (en radios)
// phiHalf    : semi-angulo de apertura en la base (radianes)
// ribbonWidth: ancho de la cinta en Y (perpendicular al plano del arco)
// segs       : segmentos a lo largo del arco
// UV.x = 0→1 a lo largo del arco, UV.y = 0→1 a lo ancho de la cinta
inline Mesh GenMeshArchRibbon(float archHeight, float phiHalf, float ribbonWidth, int segs) {
    int nV = (segs + 1) * 2;
    int nT = segs * 2;

    Mesh m = {};
    m.vertexCount   = nV;
    m.triangleCount = nT;
    m.vertices  = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(nV * 2 * sizeof(float));
    m.normals   = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.indices   = (unsigned short*)MemAlloc(nT * 3 * sizeof(unsigned short));

    float rBase = 1.0f; // pegado a la superficie de la estrella

    for (int i = 0; i <= segs; i++) {
        float t = (float)i / (float)segs;

        // IMPORTANTE:
        // Esto corrige el error de "hw no declarado" y además adelgaza el ribbon.
        // No toca phiHalf, así que NO cambia la separación entre los pies del arco.
        float ribbonThin = 0.34f;               // 0.30 = más fino, 0.40 = más ancho
        float centerTaper = sinf(PI * t);       // 0 en pies, 1 en centro
        centerTaper = powf(centerTaper, 0.75f);

        float hw = ribbonWidth * 0.5f * ribbonThin * (0.18f + 0.82f * centerTaper);

        float phi = (-1.0f + 2.0f * t) * phiHalf;
        float st  = sinf(PI * t);
        float r   = rBase + archHeight * st * st;

        float cx  = sinf(phi) * r;
        float cz  = cosf(phi) * r;

        float nx  = sinf(phi);
        float nz  = cosf(phi);

        int vi = i * 2;

        // Borde interior
        m.vertices[vi*3+0] = cx;
        m.vertices[vi*3+1] = -hw;
        m.vertices[vi*3+2] = cz;

        m.texcoords[vi*2+0] = t;
        m.texcoords[vi*2+1] = 0.0f;

        m.normals[vi*3+0] = nx;
        m.normals[vi*3+1] = 0.0f;
        m.normals[vi*3+2] = nz;

        // Borde exterior
        m.vertices[(vi+1)*3+0] = cx;
        m.vertices[(vi+1)*3+1] = hw;
        m.vertices[(vi+1)*3+2] = cz;

        m.texcoords[(vi+1)*2+0] = t;
        m.texcoords[(vi+1)*2+1] = 1.0f;

        m.normals[(vi+1)*3+0] = nx;
        m.normals[(vi+1)*3+1] = 0.0f;
        m.normals[(vi+1)*3+2] = nz;
    }

    for (int i = 0; i < segs; i++) {
        int b  = i * 2;
        int ti = i * 2;

        m.indices[ti*3+0] = (unsigned short)b;
        m.indices[ti*3+1] = (unsigned short)(b + 2);
        m.indices[ti*3+2] = (unsigned short)(b + 1);

        m.indices[(ti+1)*3+0] = (unsigned short)(b + 1);
        m.indices[(ti+1)*3+1] = (unsigned short)(b + 2);
        m.indices[(ti+1)*3+2] = (unsigned short)(b + 3);
    }

    UploadMesh(&m, false);
    return m;
}

// ── Arco TUBO (seccion circular, no plana) ───────────────────
// Sustituye a GenMeshArchRibbon con seccion transversal circular.
// El arco sigue el mismo trazado en el plano XZ, pero cada anillo
// de vertices rodea el eje del arco formando un cilindro curvo.
//
// Parametrizacion del tubo:
//   Centro del anillo:  (sin(φ)·r, 0, cos(φ)·r)     [en el plano XZ]
//   Eje vertical:       Y = (0,1,0)                   [perpendicular al plano]
//   Eje radial:         R = (sin(φ), 0, cos(φ))       [saliente del eje]
//   Vertex del tubo:    centro + tr·(cosα·Y + sinα·R) [circulo en Y×R]
//   Normal:             (sinα·sin(φ), cosα, sinα·cos(φ))
//
// El radio del tubo se afina en los extremos (t=0 y t=1) con
// powf(sin(π·t), 0.5f) para que las llamaradas emerjan suavemente.
inline Mesh GenMeshArchTube(float archHeight, float phiHalf,
                             float tubeRadius, int segs, int sides)
{
    int nV = (segs + 1) * sides;
    int nT = segs * sides * 2;

    Mesh m = {};
    m.vertexCount   = nV;
    m.triangleCount = nT;
    m.vertices  = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(nV * 2 * sizeof(float));
    m.normals   = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.indices   = (unsigned short*)MemAlloc(nT * 3 * sizeof(unsigned short));

    float rBase = 1.0f;

    for (int i = 0; i <= segs; i++) {
        float t  = (float)i / (float)segs;
        float phi = (-1.0f + 2.0f * t) * phiHalf;
        float st  = sinf(PI * t);
        float r   = rBase + archHeight * st * st;

        // Afinar el tubo en los extremos: sin^0.5 da transicion suave
        float tr = tubeRadius * powf(st, 0.5f);

        float sp = sinf(phi), cp = cosf(phi);

        for (int j = 0; j < sides; j++) {
            float alpha = j * 2.0f * PI / sides;
            float cosA = cosf(alpha), sinA = sinf(alpha);

            // Posicion: centro + tr*(cosA * Y + sinA * R)
            // donde Y=(0,1,0) y R=(sin(phi),0,cos(phi))
            float x = sp * r + tr * sinA * sp;
            float y = tr * cosA;
            float z = cp * r + tr * sinA * cp;

            // Normal apuntando hacia fuera del tubo
            float nx = sinA * sp;
            float ny = cosA;
            float nz = sinA * cp;
            float nLen = sqrtf(nx*nx + ny*ny + nz*nz);
            if (nLen > 0.0001f) { nx/=nLen; ny/=nLen; nz/=nLen; }

            int vi = i * sides + j;
            m.vertices [vi*3+0] = x; m.vertices [vi*3+1] = y; m.vertices [vi*3+2] = z;
            m.texcoords[vi*2+0] = t; m.texcoords[vi*2+1] = (float)j / sides;
            m.normals  [vi*3+0] = nx;m.normals  [vi*3+1] = ny;m.normals  [vi*3+2] = nz;
        }
    }

    // Indices: conectar anillos formando quads
    int idx = 0;
    for (int i = 0; i < segs; i++) {
        for (int j = 0; j < sides; j++) {
            int j2 = (j + 1) % sides;
            unsigned short a = (unsigned short)(i * sides + j);
            unsigned short b = (unsigned short)(i * sides + j2);
            unsigned short c = (unsigned short)((i+1) * sides + j);
            unsigned short d = (unsigned short)((i+1) * sides + j2);
            m.indices[idx++]=a; m.indices[idx++]=c; m.indices[idx++]=b;
            m.indices[idx++]=b; m.indices[idx++]=c; m.indices[idx++]=d;
        }
    }

    UploadMesh(&m, false);
    return m;
}

// ── Malla de jet radial: NO ribbon ───────────────────────────
// Eje local +Z = dirección radial hacia fuera de la estrella.
// La base empieza como punto dentro de la superficie para no flotar.
inline Mesh GenMeshRadialJet(float baseR = 1.0f,
                             float length = 0.90f,
                             float maxWidth = 0.072f,
                             int segs = 28,
                             int sides = 10)
{
    int rings = segs + 1;
    int nV = rings * sides;
    int nT = segs * sides * 2;

    Mesh m = {};
    m.vertexCount   = nV;
    m.triangleCount = nT;
    m.vertices  = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(nV * 2 * sizeof(float));
    m.normals   = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.indices   = (unsigned short*)MemAlloc(nT * 3 * sizeof(unsigned short));

    for (int i = 0; i <= segs; ++i) {
        float t = (float)i / (float)segs;

        float z = baseR + length * t;

        // Perfil más delgado que el anterior:
        // - baseFoot baja de 0.16 a 0.045
        // - maxWidth baja de 0.105 a 0.072
        // - taper más fuerte para que el jet no se vea gordito.
        float envelope = sinf(PI * t);
        float taper = powf(1.0f - t, 0.95f);

        float baseFoot = maxWidth * 0.045f;
        float jetCoreWidth = maxWidth * 0.72f;

        float w = baseFoot * (1.0f - t) + jetCoreWidth * envelope * taper;

        for (int j = 0; j < sides; ++j) {
            float a = 2.0f * PI * (float)j / (float)sides;

            float x = cosf(a) * w;
            float y = sinf(a) * w;

            int v = i * sides + j;

            m.vertices[v*3+0] = x;
            m.vertices[v*3+1] = y;
            m.vertices[v*3+2] = z;

            m.texcoords[v*2+0] = t;
            m.texcoords[v*2+1] = (float)j / (float)sides;

            Vector3 n = Vector3Normalize({x, y, 0.20f});
            m.normals[v*3+0] = n.x;
            m.normals[v*3+1] = n.y;
            m.normals[v*3+2] = n.z;
        }
    }

    int idx = 0;
    for (int i = 0; i < segs; ++i) {
        for (int j = 0; j < sides; ++j) {
            int j2 = (j + 1) % sides;

            unsigned short a = (unsigned short)(i * sides + j);
            unsigned short b = (unsigned short)(i * sides + j2);
            unsigned short c = (unsigned short)((i + 1) * sides + j);
            unsigned short d = (unsigned short)((i + 1) * sides + j2);

            m.indices[idx++] = a;
            m.indices[idx++] = c;
            m.indices[idx++] = b;

            m.indices[idx++] = b;
            m.indices[idx++] = c;
            m.indices[idx++] = d;
        }
    }

    UploadMesh(&m, false);
    return m;
}

inline float SmoothStepF(float edge0, float edge1, float x) {
    float denom = edge1 - edge0;
    if (fabsf(denom) < 1e-6f) return (x >= edge1) ? 1.0f : 0.0f;

    float t = ClampF((x - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ── Malla de puff/domo: NO ribbon ────────────────────────────
// Blob unido a la superficie por una base pequeña, no cinta aplastada.
// ── Malla de puff/erupción fallida: penacho lobulado, NO ribbon ─────────
// Eje local +Z = dirección radial.
// La base tiene footprint real sobre la superficie; el shader proyecta
// los vértices para que length(pos) == radio shell real.
inline Mesh GenMeshFlarePuff(float baseR = 1.0f,
                             float height = 0.50f,
                             float maxWidth = 0.24f,
                             int rings = 22,
                             int sides = 24)
{
    int nV = (rings + 1) * sides;
    int nT = rings * sides * 2;

    Mesh m = {};
    m.vertexCount   = nV;
    m.triangleCount = nT;
    m.vertices  = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(nV * 2 * sizeof(float));
    m.normals   = (float*)MemAlloc(nV * 3 * sizeof(float));
    m.indices   = (unsigned short*)MemAlloc(nT * 3 * sizeof(unsigned short));

    for (int i = 0; i <= rings; ++i) {
        float t = (float)i / (float)rings;

        // Mantener t^2: esto fue lo que hizo que la explosión salga desde la base,
        // no como jet.
        float rise = powf(t, 2.0f);

        // Radio real del shell del puff.
        // En t=0 es exactamente baseR, o sea, pegado a la superficie de la estrella.
        float shellR = baseR + height * rise;

        // Forma compacta y gordita, pero no enorme.
        float bottomBlob = 1.0f - SmoothStepF(0.00f, 0.38f, t);
        float midBlob    = sinf(PI * ClampF(t, 0.0f, 1.0f));
        float topFade    = 1.0f - SmoothStepF(0.72f, 1.0f, t);

        float baseWidth =
            maxWidth *
            (0.52f + 0.12f * sinf(PI * ClampF(t / 0.38f, 0.0f, 1.0f))) *
            bottomBlob;

        float bodyOpen = SmoothStepF(0.00f, 0.16f, t);

        float bodyWidth =
            maxWidth *
            bodyOpen *
            (0.76f + 0.24f * midBlob) *
            (0.76f + 0.24f * topFade);

        float w = std::max(baseWidth, bodyWidth);

        // Mantiene la parte baja con volumen para que no vuelva el cuello tipo jet.
        float lowerHold = 1.0f - SmoothStepF(0.22f, 0.44f, t);
        float lowerMinWidth = maxWidth * 0.44f * lowerHold;
        w = std::max(w, lowerMinWidth);

        for (int j = 0; j < sides; ++j) {
            float a = 2.0f * PI * (float)j / (float)sides;

            // CLAVE:
            // La sección transversal es circular. Nada de fanYScale.
            // X e Y usan exactamente el mismo radio.
            float circleX = cosf(a);
            float circleY = sinf(a);

            // Filamentos solo en la parte media/alta, no en la base.
            float filamentStrength = SmoothStepF(0.24f, 0.78f, t);

            float filament =
                1.0f
                + filamentStrength * 0.09f * sinf(a * 3.0f + t * 5.0f + 1.1f)
                + filamentStrength * 0.05f * sinf(a * 7.0f - t * 8.0f + 0.4f);

            float shred =
                1.0f + 0.05f * t * sinf(a * 5.0f + t * 13.0f);

            float ww = w * filament * shred;

            // Menos inclinación y empieza tarde.
            // Importante: lean = 0 cerca de la base para no convertir el círculo en óvalo.
            float leanStart = SmoothStepF(0.36f, 1.0f, t);
            float lean = (t * t) * maxWidth * 0.06f * leanStart;

            float localX = circleX * ww + lean;
            float localY = circleY * ww;

            // ESTA ES LA CORRECCIÓN REAL:
            // Antes hacíamos:
            //     vertex = { localX, localY, shellR };
            //
            // Eso crea un disco plano tangente a la estrella.
            //
            // Ahora hacemos:
            //     dir = normalize({ localX, localY, baseR });
            //     vertex = dir * shellR;
            //
            // Así la base se curva sobre la estrella y deja de verse como
            // una tapa ovalada/plana.
            Vector3 dir = Vector3Normalize({ localX, localY, baseR });
            Vector3 pos = Vector3Scale(dir, shellR);

            int v = i * sides + j;

            m.vertices[v*3+0] = pos.x;
            m.vertices[v*3+1] = pos.y;
            m.vertices[v*3+2] = pos.z;

            m.texcoords[v*2+0] = t;
            m.texcoords[v*2+1] = (float)j / (float)sides;

            // Normal radial del shell curvado.
            m.normals[v*3+0] = dir.x;
            m.normals[v*3+1] = dir.y;
            m.normals[v*3+2] = dir.z;
        }
    }

    int idx = 0;
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sides; ++j) {
            int j2 = (j + 1) % sides;

            unsigned short a = (unsigned short)(i * sides + j);
            unsigned short b = (unsigned short)(i * sides + j2);
            unsigned short c = (unsigned short)((i + 1) * sides + j);
            unsigned short d = (unsigned short)((i + 1) * sides + j2);

            m.indices[idx++] = a;
            m.indices[idx++] = c;
            m.indices[idx++] = b;

            m.indices[idx++] = b;
            m.indices[idx++] = c;
            m.indices[idx++] = d;
        }
    }

    UploadMesh(&m, false);
    return m;
}

// ── Campo magnético y jets de Magnetar ──────────────────────────────────────────
// Visualizacion del campo magnetico dipolar: cientos de filamentos curvos
// (arcos) que emergen desde los polos siguiendo trayectorias dipolares con
// turbulencia anadida, mas jets polares anchos y cortos.
//
// Estructura:
//  • LINEAS DE CAMPO: arcos (archModel) orientados por la ecuacion dipolar
//    r = r_max*sin²(θ), distribuidos en 360° azimutales, con:
//    - Turbulencia temporal (FBM sinusoidal, vibracion, ondulacion)
//    - Variable grosor y longitud segun la latitud de la linea
//    - Colores: azul profundo, cian, violeta, blanco segun distancia al polo
//    - Intensidad decreciente con la distancia
//  • JETS: dos conos anchos y cortos en los polos (mas gordos/cortos que pulsar)
inline void DrawMagnetarEffect(const Body& b, Shader& sh, const FlareShaderLocs& fl,
                                 Model& archModel, Model& jetModel)
{
    if (!b.isStar || b.stellarPhase != StellarPhase::MAGNETAR) return;

    float rR  = (float)(b.radius * RENDER_SCALE);
    if (rR < 1e-7f) rR = 0.0001f;
    Vector3 p   = ToDrawPos(b.pos);
    float   rt  = (float)g_flareClock;
    float   tp  = (float)std::min(b.temperature, 5.0e6);

    // Eje de rotacion del magnetar
    float tiltR = b.axialTilt    * DEG2RAD;
    float rotR  = b.rotationAngle * DEG2RAD;
    Vector3 pole = Vector3Normalize({
        sinf(tiltR)*cosf(rotR), cosf(tiltR), sinf(tiltR)*sinf(rotR)
    });

    // Dos vectores base perpendiculares al polo para el plano azimutal
    Vector3 px = { 1,0,0 };
    if (fabsf(pole.x) > 0.98f) px = { 0,1,0 };
    Vector3 pz = Vector3Normalize(Vector3CrossProduct(pole, px));
    px = Vector3Normalize(Vector3CrossProduct(pz, pole));

    // Hash determinista por semilla (id+i) para variedad sin aleatorio por frame
    auto dh = [](uint64_t id, int i) -> float {
        uint64_t h = id * 2654435761ull ^ (uint64_t)(i * 1664525 + 1013904223);
        h ^= h >> 16; h *= 0x45d9f3bULL; h ^= h >> 16;
        return (float)(h & 0xFFFF) / 65535.0f;
    };

    // Uniforms fijos para todo el efecto (fuera del bucle para evitar
    // 240 llamadas a glUseProgram que saturaban el pipeline y crasheaban)
    float grow=1.f,fade=1.f,mode=0.f,asym=0.3f,burst=0.6f,hmult=1.f;
    float drainM=0.f,drainS=0.f;
    float baseAct = 0.8f + 0.2f*sinf(rt*0.4f);
    float fi0 = 0.f;
    SetShaderValue(sh, fl.realTime,        &rt,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.temp,            &tp,     SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.stellarActivity, &baseAct,SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareGrow,       &grow,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareFade,       &fade,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareMode,       &mode,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareAsym,       &asym,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareBurst,      &burst,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareHeightMult, &hmult,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareDrainMode,  &drainM, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareDrainStart, &drainS, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareIndex,      &fi0,    SHADER_UNIFORM_FLOAT);

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // ────────────────────────────────────────────────────────────
    // LINEAS DE CAMPO MAGNETICO — DOS CORONAS EN CAPAS
    //
    // r(θ) = r₀ · sin²(θ),  θ ∈ [0, π]  (polo N → ecuador → polo S)
    //
    // Corona interna: loops cortos y brillantes, r0 = 2.5–4.5 × rR
    // Corona externa: loops largos y tenues,    r0 = 8–13  × rR
    //
    // CLAVE: r0 depende de la CAPA, no del angulo azimutal.
    // Ambas coronas cubren los 360° completos de forma independiente.
    // ────────────────────────────────────────────────────────────

    // ── CORONA INTERNA (cercana, corta, brillante) ──────────────
    {
        const int N_INNER = 20;
        const int SEGS_I  = 16;

        for (int i = 0; i < N_INNER; i++) {
            float h0 = dh(b.id, i*5+0);
            float h1 = dh(b.id, i*5+1);
            float h2 = dh(b.id, i*5+2);

            // Phi uniformemente distribuido en 360°, sin dependencia de tamano
            float phi = (float)i / N_INNER * 2.0f * PI + h0 * 0.3f;
            phi += (0.06f + h1 * 0.12f) * sinf(rt * (0.2f + h2 * 0.4f) + h0 * 12.0f);

            // r0 pequeno, varia solo por hash (no por angulo)
            float r0 = rR * (2.5f + h0 * 2.0f);   // 2.5–4.5 × rR

            float cp = cosf(phi), sp = sinf(phi);
            Vector3 bRad = Vector3Normalize({
                px.x*cp + pz.x*sp,
                px.y*cp + pz.y*sp,
                px.z*cp + pz.z*sp
            });

            float brightness = 0.65f + 0.35f * h1;
            unsigned char alpha = (unsigned char)(int(200.0f * brightness));
            Color lineColor;
            switch (i % 4) {
            case 0:  lineColor = {  80, 160, 255, alpha }; break; // azul brillante
            case 1:  lineColor = {  40, 220, 255, alpha }; break; // cian brillante
            case 2:  lineColor = { 180,  90, 255, alpha }; break; // violeta
            default: lineColor = { 230, 245, 255, alpha }; break; // blanco-azul
            }

            Vector3 prev  = {};
            bool    hasPrev = false;
            float   prevR   = 0.0f;

            for (int s = 0; s <= SEGS_I; s++) {
                float theta = PI * (float)s / SEGS_I;
                float sinT  = sinf(theta);
                float cosT  = cosf(theta);
                float r     = r0 * sinT * sinT;
                r *= 1.0f + 0.04f * sinf(rt * 0.6f + s * 0.9f + phi * 3.0f);

                Vector3 pos = {
                    p.x + pole.x * r * cosT + bRad.x * r * sinT,
                    p.y + pole.y * r * cosT + bRad.y * r * sinT,
                    p.z + pole.z * r * cosT + bRad.z * r * sinT
                };

                if (hasPrev && r > rR * 0.8f && prevR > rR * 0.8f) {
                    float capsR = rR * (0.018f + 0.065f * sinT * sinT) * brightness;
                    DrawCapsule(prev, pos, capsR, 4, 1, lineColor);
                }
                prev    = pos;
                prevR   = r;
                hasPrev = (r > rR * 0.3f);
            }
            if (i % 8 == 7) rlDrawRenderBatchActive();
        }
    }
    rlDrawRenderBatchActive();

    // ── CORONA EXTERNA (lejana, larga, tenue) ───────────────────
    {
        const int N_OUTER = 20;
        const int SEGS_O  = 24;

        for (int i = 0; i < N_OUTER; i++) {
            // Semillas separadas de la corona interna (offset +100)
            float h0 = dh(b.id, (100 + i)*5+0);
            float h1 = dh(b.id, (100 + i)*5+1);
            float h2 = dh(b.id, (100 + i)*5+2);

            // Phi desplazado media ranura respecto a la corona interna (intercalado)
            float phi = ((float)i + 0.5f) / N_OUTER * 2.0f * PI + h0 * 0.2f;
            phi += (0.08f + h1 * 0.15f) * sinf(rt * (0.15f + h2 * 0.3f) + h0 * 9.0f);

            // r0 grande, varia solo por hash (no por angulo)
            float r0 = rR * (8.0f + h0 * 5.0f);   // 8–13 × rR

            float cp = cosf(phi), sp = sinf(phi);
            Vector3 bRad = Vector3Normalize({
                px.x*cp + pz.x*sp,
                px.y*cp + pz.y*sp,
                px.z*cp + pz.z*sp
            });

            float brightness = 0.25f + 0.25f * h1;
            unsigned char alpha = (unsigned char)(int(80.0f * brightness));
            Color lineColor;
            switch (i % 4) {
            case 0:  lineColor = {  40,  90, 230, alpha }; break; // azul tenue
            case 1:  lineColor = {  15, 140, 230, alpha }; break; // cian tenue
            case 2:  lineColor = { 130,  40, 220, alpha }; break; // violeta tenue
            default: lineColor = { 190, 210, 250, alpha }; break; // blanco-azul tenue
            }

            Vector3 prev  = {};
            bool    hasPrev = false;
            float   prevR   = 0.0f;

            for (int s = 0; s <= SEGS_O; s++) {
                float theta = PI * (float)s / SEGS_O;
                float sinT  = sinf(theta);
                float cosT  = cosf(theta);
                float r     = r0 * sinT * sinT;
                r *= 1.0f + 0.05f * sinf(rt * 0.3f + s * 1.1f + phi * 2.0f);

                Vector3 pos = {
                    p.x + pole.x * r * cosT + bRad.x * r * sinT,
                    p.y + pole.y * r * cosT + bRad.y * r * sinT,
                    p.z + pole.z * r * cosT + bRad.z * r * sinT
                };

                if (hasPrev && r > rR * 0.8f && prevR > rR * 0.8f) {
                    float capsR = rR * (0.005f + 0.018f * sinT * sinT) * brightness;
                    DrawCapsule(prev, pos, capsR, 4, 1, lineColor);
                }
                prev    = pos;
                prevR   = r;
                hasPrev = (r > rR * 0.3f);
            }
            if (i % 8 == 7) rlDrawRenderBatchActive();
        }
    }
    rlDrawRenderBatchActive();

    // ────────────────────────────────────────────────────────────
    // JETS POLARES (mas anchos y cortos que los de un pulsar)
    // JetTRS: embebe Scale+Rotation+Translation en UNA sola matriz.
    // Esto es critico: DrawModelEx aplica el scale en espacio MUNDO
    // (despues de la rotacion), lo que aplasta la seccion circular del
    // jet en direcciones incorrectas. Con la matriz completa en
    // model.transform y DrawModel(scale=1), el scale se aplica primero
    // en espacio LOCAL → seccion circular correcta desde cualquier angulo.
    // ────────────────────────────────────────────────────────────
    auto JetTRS = [rR](Vector3 pos, Vector3 poleAxis, float w, float len) -> Matrix {
        Vector3 up = (fabsf(poleAxis.y) < 0.99f) ? Vector3{0,1,0} : Vector3{1,0,0};
        Vector3 bx = Vector3Normalize(Vector3CrossProduct(up, poleAxis));
        Vector3 by = Vector3Normalize(Vector3CrossProduct(poleAxis, bx));
        // Origen desplazado para que z=1 del mesh quede en la superficie (rR)
        // local z=1 → pos + poleAxis*(rR-len) + poleAxis*len = pos + poleAxis*rR ✓
        float tx = pos.x + poleAxis.x*(rR - len);
        float ty = pos.y + poleAxis.y*(rR - len);
        float tz = pos.z + poleAxis.z*(rR - len);
        return Matrix{
            bx.x*w, by.x*w, poleAxis.x*len, tx,
            bx.y*w, by.y*w, poleAxis.y*len, ty,
            bx.z*w, by.z*w, poleAxis.z*len, tz,
            0.f, 0.f, 0.f, 1.f
        };
    };

    float jetLen = rR * 20.0f;
    float jetW   = rR * 10.0f;
    float jfi0=0.f, jfi1=1.f;
    float jetAct = 1.0f + 0.1f*sinf(rt*0.5f);
    SetShaderValue(sh, fl.stellarActivity, &jetAct, SHADER_UNIFORM_FLOAT);
    float jetHmult = 20.f;
    SetShaderValue(sh, fl.flareHeightMult, &jetHmult, SHADER_UNIFORM_FLOAT);

    SetShaderValue(sh, fl.flareIndex, &jfi0, SHADER_UNIFORM_FLOAT);
    jetModel.transform = JetTRS(p, pole, jetW, jetLen);
    DrawModel(jetModel, {0,0,0}, 1.f, WHITE);

    SetShaderValue(sh, fl.flareIndex, &jfi1, SHADER_UNIFORM_FLOAT);
    jetModel.transform = JetTRS(p, Vector3Negate(pole), jetW, jetLen);
    DrawModel(jetModel, {0,0,0}, 1.f, WHITE);

    jetModel.transform   = MatrixIdentity();
    archModel.transform  = MatrixIdentity();

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}

// ── Jets polares de pulsares ─────────────────────────────────────────────────────
// Dos jets permanentes (norte y sur) alineados con el eje de rotacion real
// del pulsar, definido por b.axialTilt y b.rotationAngle. Si el pulsar se
// tambalea (precesion del eje), el jet se tambalea con el. Los jets usan el
// mismo jetModel (GenMeshRadialJet) y flareShader que las llamaradas estelares
// normales, pero mucho mas largos, mas brillantes, y siempre activos.
inline void DrawPulsarPolarJets(const Body& b, Shader& sh, const FlareShaderLocs& fl,
                                  Model& jetModel)
{
    // Jets polares: SOLO pulsares. Los magnetares tienen campo magnetico
    // brutal pero giran MAS LENTO (periodo ~segundos vs ms en pulsares)
    // y su fenomeno distintivo es el toro/campo magnetico, no los jets.
    if (!b.isStar) return;
    if (b.stellarPhase != StellarPhase::PULSAR) return;

    float rR  = (float)(b.radius * RENDER_SCALE);
    if (rR < 1e-7f) rR = 0.0001f; // minimo visual
    Vector3 p = ToDrawPos(b.pos);
    float rt  = (float)g_flareClock;

    // Temperatura del pulsar (muy alta, da color azul-blanco al shader)
    float tp  = (float)std::min(b.temperature, 2.0e6);

    // Actividad maxima + pulsacion sutil segun spin
    float spinFreq = (b.spinRateDeg > 100.f)
        ? std::min((float)b.spinRateDeg * (float)(PI_D/180.0/1200.0) * 2.0f, 30.0f)
        : 2.0f;
    float pulsation = 1.0f + 0.12f * sinf(rt * spinFreq);
    float act = ClampF(pulsation, 0.8f, 1.2f);

    // ── Eje de rotacion del pulsar ───────────────────────────────
    float tiltR = b.axialTilt    * DEG2RAD;
    float rotR  = b.rotationAngle * DEG2RAD;
    Vector3 poleN = Vector3Normalize({
        sinf(tiltR) * cosf(rotR),
        cosf(tiltR),
        sinf(tiltR) * sinf(rotR)
    });

    // ── Construir transform T×R×S completo (misma razon que JetTRS del magnetar)
    // Scale embebido en la matriz → seccion circular correcta independientemente
    // de la orientacion del polo.
    auto PulsarJetTRS = [rR](Vector3 pos, Vector3 poleAxis, float w, float len) -> Matrix {
        Vector3 up = (fabsf(poleAxis.y) < 0.99f) ? Vector3{0,1,0} : Vector3{1,0,0};
        Vector3 bx = Vector3Normalize(Vector3CrossProduct(up, poleAxis));
        Vector3 by = Vector3Normalize(Vector3CrossProduct(poleAxis, bx));
        float tx = pos.x + poleAxis.x*(rR - len);
        float ty = pos.y + poleAxis.y*(rR - len);
        float tz = pos.z + poleAxis.z*(rR - len);
        return Matrix{
            bx.x*w, by.x*w, poleAxis.x*len, tx,
            bx.y*w, by.y*w, poleAxis.y*len, ty,
            bx.z*w, by.z*w, poleAxis.z*len, tz,
            0.f, 0.f, 0.f, 1.f
        };
    };

    float jetLen   = rR * 50.0f;
    float jetWidth = rR * 6.0f;

    float grow = 1.0f, fade = 1.0f;
    float mode = 1.0f;    // 1 = jet geometry (no arco)
    float hmult = 50.0f;  // estirar el jet a lo largo
    float asym = 0.0f, burst = 0.3f;
    float drainMode = 0.0f, drainStart = 0.0f;
    float fi0 = 0.0f, fi1 = 1.0f; // indices de los dos jets

    SetShaderValue(sh, fl.realTime,        &rt,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.temp,            &tp,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.stellarActivity, &act,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareGrow,       &grow, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareFade,       &fade, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareMode,       &mode, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareHeightMult, &hmult,SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareAsym,       &asym, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareBurst,      &burst,SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareDrainMode,  &drainMode,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.flareDrainStart, &drainStart, SHADER_UNIFORM_FLOAT);

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // ── Jet norte ────────────────────────────────────────────────
    SetShaderValue(sh, fl.flareIndex, &fi0, SHADER_UNIFORM_FLOAT);
    jetModel.transform = PulsarJetTRS(p, poleN, jetWidth, jetLen);
    DrawModel(jetModel, {0,0,0}, 1.0f, WHITE);

    // ── Jet sur ──────────────────────────────────────────────────
    SetShaderValue(sh, fl.flareIndex, &fi1, SHADER_UNIFORM_FLOAT);
    jetModel.transform = PulsarJetTRS(p, Vector3Negate(poleN), jetWidth, jetLen);
    DrawModel(jetModel, {0,0,0}, 1.0f, WHITE);

    // Restaurar transform del modelo
    jetModel.transform = MatrixIdentity();

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}

// ── Llamaradas solares: N arcos-cinta aditivos alrededor de la estrella ──────────
// Cada arco tiene orientacion aleatoria determinista y textura unica (flareIndex).
// El shader anima el fuego independientemente de camara/pausa via realTime.
inline void DrawStarFlares(const Body& b, Shader& sh, const FlareShaderLocs& fl,
                            Model& archModel,
                            Model& jetModel,
                            Model& puffModel,
                            int nFlares)
{
    if (!b.isStar) return;

    // Fases sin fusion activa (enana blanca/negra, remanente compacto) y el
    // evento de supernova: las llamaradas NO existen permanentemente aqui, pero
    // en vez de cortarlas de golpe se deja que las vivas ejecuten su animacion
    // de muerte (reabsorcion) conducida por flareDeathProgress (0→1). Una vez
    // completada la reabsorcion (≈1) ya no se dibuja nada.
    bool noFlarePhase = StarPhaseHasNoFlares(b.stellarPhase);
    if (noFlarePhase && b.flareDeathProgress >= 0.999f) return;

    // Estrella con actividad ~0 y sin reabsorcion en curso: nada que animar.
    bool reabsorbing = b.flareDeathProgress > 0.0f && b.flareDeathProgress < 0.999f;
    if (b.stellarActivity < 0.01f && !reabsorbing) return;

    float rR   = std::max(0.001f, (float)(b.radius * RENDER_SCALE));
    Vector3 p  = ToDrawPos(b.pos);
    float act  = b.stellarActivity;
    // Durante la muerte estelar, usar la temperatura CONGELADA al inicio de la
    // reabsorción: 'tp' alimenta mDwarf/period/color, y el pico térmico de la
    // supernova haría saltar la fase (fmod reloj/period) aunque el reloj esté
    // congelado -- con la temperatura fija, el period y la fase quedan estables.
    float tp   = (b.flareDeathProgress > 0.0f)
                 ? b.flareDeathTemp : (float)b.temperature;
    float rt   = (float)g_flareClock;

    float mDwarf = 1.0f - ClampF((tp - 3400.0f) / (4100.0f - 3400.0f), 0.0f, 1.0f);

    // La clasificacion "M-dwarf" es PURAMENTE de temperatura -- y una gigante
    // roja/supergigante en fase final tiene una fotosfera tan fria (~3000-
    // 4000K) como una enana roja real (ver ApplyStellarPhaseProperties:
    // target de temperatura de RED_GIANT/SUPERGIANT cae bien dentro de este
    // rango). Sin este amortiguador, CUALQUIER gigante/moribunda heredaba el
    // tratamiento de "enana roja hiperactiva" (periodo de llamarada hasta
    // 62% mas corto, hasta el doble de llamaradas simultaneas, mas altura) --
    // apropiado para una enana real, absurdo para una estrella moribunda
    // que deberia tener prominencias grandes y PAUSADAS, no frenéticas.
    bool dyingPhase = b.stellarPhase == StellarPhase::RED_GIANT
                    || b.stellarPhase == StellarPhase::SUPERGIANT
                    || b.stellarPhase == StellarPhase::PLANETARY_NEBULA
                    || b.stellarPhase == StellarPhase::WHITE_DWARF
                    || b.stellarPhase == StellarPhase::SUPERNOVA;
    if (dyingPhase) mDwarf *= 0.30f;

    // NOTA: antes existía aquí un "reordenamiento por inestabilidad" que cada
    // CHAOS_EPOCH_SEC (6s) hacía saltar el seed del patrón entero para gigantes/
    // supergigantes -- pero ese salto discreto recalculaba de golpe el periodo,
    // fase y posición de TODAS las llamaradas a la vez, causando el "corte" que
    // se veía como todas las flares desapareciendo y reiniciándose desde 0.
    // Eliminado: ahora cada llamarada tiene un periodo fijo y evoluciona su
    // propio ciclo (nacer→vivir→reabsorberse→renacer) de forma continua e
    // independiente, escalonada respecto a las demás -- nunca un reset global.

    float kDwarf =
        ClampF((tp - 3800.0f) / (4800.0f - 3800.0f), 0.0f, 1.0f) *
        (1.0f - ClampF((tp - 5100.0f) / (5600.0f - 5100.0f), 0.0f, 1.0f));

    float gDwarf =
        ClampF((tp - 5200.0f) / (5600.0f - 5200.0f), 0.0f, 1.0f) *
        (1.0f - ClampF((tp - 6100.0f) / (6600.0f - 6100.0f), 0.0f, 1.0f));

    float spectralAct = ClampF(
        act * (1.0f + mDwarf * 1.7f + kDwarf * 0.55f - gDwarf * 0.35f),
        0.02f,
        1.0f
    );

    SetShaderValue(sh, fl.realTime,        &rt,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.temp,            &tp,  SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, fl.stellarActivity, &spectralAct, SHADER_UNIFORM_FLOAT);

    BeginBlendMode(BLEND_ADDITIVE);
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    // Seed determinista por estrella, basado en b.id (UNICO y CONSTANTE
    // durante toda la vida del cuerpo, nunca se reusa -- ver body.h).
    // ANTES dependia de b.mass/b.temperature: b.temperature se recalcula
    // CADA FRAME via Stefan-Boltzmann (main.cpp) a partir de radius/
    // luminosity, que a su vez CAMBIAN cada frame mientras una fase
    // estelar converge (lerp exponencial en ApplyStellarPhaseProperties,
    // stellar_evolution.h -- p.ej. mientras una supergigante se
    // hincha/desinfla hacia su target). El cambio fisico real es
    // infimo (fracciones de Kelvin por frame), pero el hash
    // pseudoaleatorio de abajo (sin(x)*constante_enorme - floor(), estilo
    // GLSL) es CAOTICAMENTE sensible a esa entrada -- un delta de medio
    // Kelvin ya basta para que hL1/hL2 (y por tanto el periodo, la fase,
    // la posicion y el tipo de CADA llamarada) salten a valores
    // completamente distintos. Como esto se recalculaba cada frame
    // mientras la estrella evolucionaba, el patron entero de llamaradas
    // se "re-sorteaba" frame tras frame -- de ahi el parpadeo catastrofico
    // reportado durante la evolucion, que cesaba en cuanto el lerp
    // convergia y la temperatura dejaba de cambiar bit a bit. b.id no
    // cambia jamas durante la vida del cuerpo (ni con fusiones -- ver
    // MergeBodies, que conserva el id de majIdx), asi que el patron de
    // llamaradas ahora es perfectamente estable sin importar cuanto
    // fluctuen masa/temperatura/radio mientras tanto.
    float starSeed = (float)((b.id * 2654435761ull) % 1000000ull) * 1e-4f;

    // Reordenamiento DISCRETO del patron para estrellas inestables: cada
    // CHAOS_EPOCH_SEC (tiempo de 'rt'/g_flareClock, que ya tiene un tope
    // de aceleracion x4 incluso a velocidad de simulacion maxima -- ver
    // main.cpp) el patron entero "salta" a una variante nueva, escalado
    // por 'instability' (0 para estrellas estables -> epoch siempre
    // aporta 0, sin cambios). Para estrellas inestables esto se ve como
    // turbulencia real (el patron cambia visiblemente cada pocos
    // segundos) sin reintroducir el parpadeo frame-a-frame original,
    // porque dentro de cada epoch el patron vuelve a ser perfectamente
    // estable.
    // Seed FIJO por estrella: sin saltos discretos. Cada llamarada cicla en su
    // propio periodo continuo -- sin cortes ni reinicios globales sincronizados.
    float effSeed = starSeed;
    // Cantidad de arcos segun actividad: 5 base + hasta 7 extra
    // M: muchas llamaradas. K: intermedio. G/Sol: más estable.
    // Slots visibles potenciales.
    // Una estrella G/Sol estable NO debe quedar condenada a solo 2 anclas eternas:
    // tiene menos actividad fuerte, pero puede tener varios eventos pequeños/sutiles.
    int K = std::min(
        nFlares,
        4 + (int)(spectralAct * (4.0f + mDwarf * 8.0f + kDwarf * 3.0f))
    );
    K = std::max(0, K);

    auto hh = [](float x) -> float { return x - floorf(x); };
    auto ss = [](float a, float b, float x) -> float {
        float t = std::max(0.0f, std::min(1.0f, (x - a) / (b - a)));
        return t * t * (3.0f - 2.0f * t);
    };

    // Anclaje a la estrella: todas las llamaradas rotan con la superficie
    float starRotYaw = b.rotationAngle;
    float giantF     = std::min((float)(b.mass / M_SUN) * 0.12f, 1.6f);

    // Multiplicador global de muerte: al morir la estrella (flareDeathProgress
    // 0→1) TODAS las llamaradas vivas colapsan (lifeScale) y se apagan
    // (flareFade) de forma suave. deathMul=1 mientras la estrella vive → sin
    // efecto; deathMul→0 al completarse la reabsorcion.
    float deathMul = 1.0f - ss(0.0f, 1.0f, b.flareDeathProgress);

    // Reloj de CICLO DE VIDA de las flares. Mientras la estrella muere, se
    // congela en el instante en que empezó la reabsorción (flareDeathClock):
    // así ninguna flare nueva nace a mitad de la muerte y ninguna termina su
    // ciclo por su cuenta -- solo se reabsorben (vía deathMul) las que ya
    // estaban vivas. El shimmer interno del plasma (fl.realTime) SÍ sigue vivo
    // con 'rt' para que los arcos moribundos no se vean congelados.
    float rtLife = (b.flareDeathProgress > 0.0f) ? b.flareDeathClock : rt;

    for (int i = 0; i < K; i++) {
        float fi = (float)i;

        // Hashes de ciclo de vida: constantes por llamarada (cada una tiene su propio periodo)
        float hL1    = hh(sinf(fi * 127.1f + effSeed) * 43758.5f);
        float hL2    = hh(sinf(fi * 311.7f + effSeed * 1.3f) * 23419.7f);
        // ANTES: base 18-52s, hasta 91s para G/Sol (x1.75) -- con el tope
        // de aceleracion x4 de g_flareClock (main.cpp, incluso a velocidad
        // de simulacion maxima), una llamarada de una estrella tranquila
        // tardaba como MINIMO ~20s reales en nacer-vivir-morir, y la
        // probabilidad de presencia (mas abajo) la dejaba ausente buena
        // parte de ese tiempo -- demasiado lento/raro para una escala de
        // tiempo jugable. Base bajada a 5-14s, penalizacion de G/Sol
        // reducida a x1.4 (antes x1.75): sigue siendo notablemente mas
        // calma que una M activa, pero deja de sentirse "congelada".
        float period = 5.0f + hL1 * 9.0f;
        period *= (1.0f - mDwarf * 0.62f);              // enanas rojas: más frecuente
        period *= (1.0f + gDwarf * 0.40f);              // tipo Sol/G: algo mas pausado
        period = std::max(2.0f, period);
        float phase  = fmodf(rtLife / period + hL2, 1.0f);  // fraccion dentro del ciclo actual

        // Cada ciclo de vida es un EVENTO nuevo.
        // Antes: la posicion dependia solo de i + starSeed -> misma llamarada eterna.
        // Ahora: i + cycleId + effSeed -> misma durante el ciclo, distinta al renacer.
        float cycleId = floorf(rtLife / period + hL2);
        float eventSeed = effSeed + fi * 37.173f + cycleId * 91.731f;

        // Probabilidad de que este evento realmente exista. ANTES (base
        // 0.42, piso 0.25) dejaba estrellas tranquilas (G/Sol, spectralAct
        // bajo) ausentes la mayor parte del tiempo ("casi nunca" salen) --
        // base y piso subidos para que una estrella calma siga teniendo
        // llamaradas visibles con regularidad, solo que menos numerosas/
        // intensas que una M activa.
        float eventPresence = ClampF(0.55f + spectralAct * 0.40f + mDwarf * 0.25f + kDwarf * 0.10f - gDwarf * 0.05f, 0.35f, 0.95f);
        float hPresence = hh(sinf(eventSeed * 151.7f + 12.9f) * 76123.45f);
        if (hPresence > eventPresence) continue;

        // Ciclo geometrico: el arco nace pequeno desde la superficie, crece y colapsa
        // lifeScale=0 → arch hidden inside star (depth-tested); lifeScale=1 → max extension
        // Fase 1: plasma recorre la parabola de UV=0 a UV=1 (35% del ciclo)
        // Fase 2: arco completo formado (30% del ciclo)
        // Fase 3: colapso por escala geometrica, arco se encoge hacia la estrella (35%)
        float flareGrow, lifeScale, flareFade;

        // Guardamos si está muriendo para poder cambiar el tipo de muerte después,
        // cuando ya sepamos si el flare es arco regular, jet o puff.
        bool isDying = false;
        float deathT = 0.0f;

        if (phase < 0.35f) {
            // Nacimiento: se revela desde un pie.
            flareGrow = phase / 0.35f;
            lifeScale = 1.0f;
            // ANTES: flareFade llegaba a 1.0 en solo el 10% del periodo, MUCHO
            // mas rapido que flareGrow (35% del periodo) -- como el pie del
            // arco (footAlpha en el shader) ya brilla a maxima intensidad
            // justo en el extremo revelado, el resultado era que la
            // llamarada llegaba a brillo PLENO casi de inmediato, mientras
            // el resto de la geometria seguia creciendo en silencio: se veia
            // como un "pop" a brillo completo en vez de una aparicion
            // gradual. Igualando flareFade a flareGrow, el brillo global
            // crece exactamente al mismo ritmo que la revelacion geometrica.
            flareFade = flareGrow;
        }
        else if (phase < 0.65f) {
            // Vida plena.
            flareGrow = 1.0f;
            lifeScale = 1.0f;
            flareFade = 1.0f;
        }
        else {
            // Muerte base: por defecto, colapsa hacia la estrella como antes.
            isDying = true;
            deathT = (phase - 0.65f) / 0.35f;

            flareGrow = 1.0f;
            lifeScale = 1.0f - ss(0.15f, 1.0f, deathT) * 0.65f;
            flareFade = 1.0f - ss(0.00f, 1.0f, deathT);
        }
        // Reabsorción global al morir la estrella: colapsa y apaga esta flare
        // desde su estado actual, sin importar en qué punto del ciclo esté.
        lifeScale *= deathMul;
        flareFade *= deathMul;
        if (flareGrow < 0.02f) continue;
        if (lifeScale < 0.04f) continue;

        SetShaderValue(sh, fl.flareGrow, &flareGrow, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, fl.flareFade, &flareFade, SHADER_UNIFORM_FLOAT);

        float flareDrainMode = 0.0f;
        float flareDrainStart = 0.0f;

        SetShaderValue(sh, fl.flareDrainMode,  &flareDrainMode,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, fl.flareDrainStart, &flareDrainStart, SHADER_UNIFORM_FLOAT);


        float flareIdx = (float)i;
        SetShaderValue(sh, fl.flareIndex, &flareIdx, SHADER_UNIFORM_FLOAT);

        float h1v = hh(sinf(eventSeed * 127.1f + 11.7f) * 43758.5f);
        float h2v = hh(sinf(eventSeed * 311.7f + 23.3f) * 23419.7f);
        float h3v = hh(sinf(eventSeed * 419.2f + 41.9f) * 31627.2f);
        float h4v = hh(sinf(eventSeed * 733.1f + 59.4f) * 91827.13f);
        float h5v = hh(sinf(eventSeed * 991.7f + 73.8f) * 27183.91f);
        float h6v = hh(sinf(eventSeed * 577.9f + 97.2f) * 51749.37f);

        // Punto de anclaje del EVENTO actual.
        // Permanece fijo durante este ciclo de vida, rota con b.rotationAngle,
        // y cambia cuando la llamarada muere y renace en otro ciclo.
        float lon = (h1v * 360.0f + starRotYaw) * DEG2RAD;
        float lat = (h2v - 0.5f) * 140.0f * DEG2RAD;

        Vector3 radial = {
            cosf(lat) * sinf(lon),
            sinf(lat),
            cosf(lat) * cosf(lon)
        };
        radial = Vector3Normalize(radial);

        Vector3 upRef = (fabsf(radial.y) > 0.96f)
            ? Vector3{1.0f, 0.0f, 0.0f}
            : Vector3{0.0f, 1.0f, 0.0f};

        Vector3 tangentX = Vector3Normalize(Vector3CrossProduct(upRef, radial));
        Vector3 tangentY = Vector3Normalize(Vector3CrossProduct(radial, tangentX));

        float rollRad = h3v * 360.0f * DEG2RAD;
        float cR = cosf(rollRad);
        float sR = sinf(rollRad);

        Vector3 bx = Vector3Add(Vector3Scale(tangentX, cR), Vector3Scale(tangentY, sR));
        Vector3 by = Vector3Add(Vector3Scale(tangentY, cR), Vector3Scale(tangentX, -sR));
        Vector3 bz = radial;

        Matrix rot = {
            bx.x, by.x, bz.x, 0.0f,
            bx.y, by.y, bz.y, 0.0f,
            bx.z, by.z, bz.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        // Modo e intensidad del EVENTO actual.        
        // M Quedan estables durante el ciclo, pero pueden cambiar al renacer.


        float hMode  = h4v;
        float hBurst = h5v;


        // 0 = arco normal
        // 1 = jet / pluma
        // 2 = erupcion fallida / puff
        // Garantiza variedad visible: no todo queda a merced del azar.
        // 0 = arco, 1 = jet, 2 = puff.
        float flareMode = 0.0f; // arco por defecto

        // Distribución por tipo estelar:
        // G/Sol: arcos dominan, pocos jets/puffs.
        // M/K activas: más jets y puffs.
        // Probabilidades reducidas:
        // Queremos que arcos/flares regulares sean claramente dominantes.
        // 0 = arco regular
        // 1 = jet
        // 2 = puff

        float jetChance = ClampF(
            0.035f +
            spectralAct * 0.055f +
            mDwarf * 0.060f +
            kDwarf * 0.025f -
            gDwarf * 0.025f,
            0.015f,
            0.14f
        );

        float puffChance = ClampF(
            0.025f +
            spectralAct * 0.040f +
            mDwarf * 0.050f +
            kDwarf * 0.020f -
            gDwarf * 0.020f,
            0.010f,
            0.11f
        );

        if (hMode < jetChance) {
            flareMode = 1.0f; // jet
        }
        else if (hMode < jetChance + puffChance) {
            flareMode = 2.0f; // puff
        }
        else {
            flareMode = 0.0f; // flare regular / arco
        }

        float flareAsym  = h2v;
        float flareBurst = ClampF(0.35f + hBurst * 0.95f + spectralAct * 0.35f, 0.0f, 1.4f);

        SetShaderValue(sh, fl.flareMode,  &flareMode,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, fl.flareAsym,  &flareAsym,  SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh, fl.flareBurst, &flareBurst, SHADER_UNIFORM_FLOAT);

        // Muerte alternativa SOLO para flares regulares/arcos.
        // Algunos colapsan como antes, otros se drenan siguiendo el arco,
        // como si dejara de salir material y el plasma se reabsorbiera por el otro pie.
        if (flareMode == 0.0f && isDying) {
            float hDeath = hh(sinf(eventSeed * 1201.7f + 8.6f) * 91821.37f);

            // Ajusta esta probabilidad:
            // 0.0 = ninguno se drena
            // 0.5 = mitad colapsa, mitad se drena
            // 1.0 = todos se drenan
            bool reverseDrainDeath = (hDeath < 0.55f);

            if (reverseDrainDeath) {
                float d = ss(0.00f, 1.0f, deathT);

                // IMPORTANTE:
                // No bajamos flareGrow. flareGrow recorta desde el lado equivocado.
                // Para drenar hacia el segundo pie, usamos un modo nuevo del shader:
                // visible solo si uv.x >= flareDrainStart.
                flareGrow = 1.0f;
                flareDrainMode = 1.0f;
                flareDrainStart = d;

                // No colapsa geométricamente hacia la estrella.
                // Mantiene el arco y se va vaciando hacia el pie final.
                // deathMul<1 solo cuando la estrella entera muere (supernova/
                // enana blanca): entonces también colapsa el arco drenante.
                lifeScale = deathMul;

                // ANTES: el fade global se quedaba en 1.0 (brillo pleno) hasta
                // el 82% de deathT y solo se apagaba en el 18% final -- en un
                // periodo corto eso son fracciones de segundo, así que la
                // llamarada parecía desaparecer de golpe en vez de drenarse.
                // Ahora el fade global acompaña TODO el rango de deathT, igual
                // que en la muerte por colapso -- el drenaje geometrico
                // (flareDrainStart, arriba) sigue dando la sensacion de
                // "vaciarse hacia el segundo pie", pero ya no depende de un
                // fade que llega tarde para que se note gradual.
                flareFade = (1.0f - ss(0.00f, 1.0f, deathT)) * deathMul;

                SetShaderValue(sh, fl.flareGrow,       &flareGrow,       SHADER_UNIFORM_FLOAT);
                SetShaderValue(sh, fl.flareFade,       &flareFade,       SHADER_UNIFORM_FLOAT);
                SetShaderValue(sh, fl.flareDrainMode,  &flareDrainMode,  SHADER_UNIFORM_FLOAT);
                SetShaderValue(sh, fl.flareDrainStart, &flareDrainStart, SHADER_UNIFORM_FLOAT);
            }
        }

        // Las puffs no deben revelarse como un jet desde la base.
        // Para puff, la geometría ya nace como explosión completa desde la estrella.
        // Por eso forzamos flareGrow a 1.0 y dejamos que aparezca/desaparezca con flareFade.
        if (flareMode == 2.0f) {
            float puffGrow = 1.0f;
            SetShaderValue(sh, fl.flareGrow, &puffGrow, SHADER_UNIFORM_FLOAT);
        }

        float heightVar = 0.65f + h6v * 0.65f;

        // Nerf real: antes la actividad/M-dwarf/giantF multiplicaban demasiado.
        // Ahora la actividad hace la llamarada más viva, no absurdamente más larga.
        float flareScale =
            0.72f +
            mDwarf * 0.42f +
            kDwarf * 0.18f -
            gDwarf * 0.08f;

        float activityHeight =
            0.82f +
            giantF * 0.28f +
            spectralAct * 0.22f;

        float heightMult =
            activityHeight *
            heightVar *
            lifeScale *
            flareScale;

        // Jets siguen siendo un poco más altos, pero no monstruosos.
        if (flareMode == 1.0f) {
            heightMult *= 1.05f + flareBurst * 0.22f;
        }
        // Puffs deben ser cortos.
        else if (flareMode == 2.0f) {
            // Puff compacta: menos alta y menos enorme.
            // La geometría ya es ancha/redonda, así que no necesitamos tanta escala vertical.
            heightMult *= 0.46f + flareBurst * 0.10f;
        }

        // Tope de altura por modo.
        // flareMode 0 = flare regular / arco
        // flareMode 1 = jet
        // flareMode 2 = puff

        float maxHeight =
            1.05f +
            mDwarf * 0.25f +
            kDwarf * 0.10f +
            giantF * 0.12f;

        if (flareMode == 1.0f) {
            // Jets todavía pueden ser un poco más altos que los arcos.
            maxHeight *= 1.10f;
        }
        else if (flareMode == 2.0f) {
            // Puffs más compactos.
            maxHeight = std::min(maxHeight, 0.82f + flareBurst * 0.10f);
        }

        heightMult = ClampF(heightMult, 0.08f, maxHeight);


        heightMult = ClampF(heightMult, 0.08f, maxHeight);
        SetShaderValue(sh, fl.flareHeightMult, &heightMult, SHADER_UNIFORM_FLOAT);

        Model* flareModel = &archModel;

        if (flareMode == 1.0f) {
            flareModel = &jetModel;
        }
        else if (flareMode == 2.0f) {
            flareModel = &puffModel;
        }

        // Radio real de la superficie en la dirección 'radial', considerando
        // achatamiento por rotación (RotationalOblateness) y elongación por
        // marea (tideVisualElongation/Squash) -- sin esto, las llamaradas
        // siempre nacen de una esfera perfecta aunque la estrella visual
        // sea un elipsoide (TidalBodyTransform).
        float obF      = RotationalOblateness(b);
        float rEqF     = 1.0f + obF / 3.0f;
        float rPolF    = 1.0f - 2.0f * obF / 3.0f;
        float latFrac  = radial.y * radial.y;          // 0=ecuador, 1=polo
        float baseR    = rR * (rEqF * (1.0f - latFrac) + rPolF * latFrac);

        if (b.tideVisualElongation > 1.001f) {
            Vector3 ta = Vector3Normalize({(float)b.tideAxis.x, (float)b.tideAxis.y, (float)b.tideAxis.z});
            float tideProj = fabsf(Vector3DotProduct(radial, ta));
            float tidalScale = b.tideVisualSquash
                + (b.tideVisualElongation - b.tideVisualSquash) * tideProj * tideProj;
            baseR *= tidalScale;
        }

        Vector3 flareScaleVec = { baseR, baseR, baseR };

        flareModel->transform = rot;
        DrawModelEx(*flareModel, p, {0,1,0}, 0.0f, flareScaleVec, WHITE);
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    EndBlendMode();
}
