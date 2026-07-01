#pragma once
// ============================================================
//  Agujero Negro — Post-proceso de distorsión de UV
//
//  Técnica: render-to-texture de toda la escena, luego un
//  fullscreen pass que desplaza las UV del framebuffer segun
//  la distancia al centro del agujero negro en pantalla:
//
//    dir  = UV - bhCenter          (vector hacia el BH)
//    dist = length(dir)
//    dir *= pow(dist, -strength)   (mas fuerte cerca del centro)
//    dir *= pow(1-dist, atten)     (se atenúa en el borde)
//    COLOR = texture(scene, UV - dir)
//
//  Adaptado del shader de Godot (canvas_item) proporcionado por
//  el usuario. La esfera negra en si la dibuja el renderer 3D
//  normal (body.temperature=0, body.luminosity=0 = negro total).
//
//  Skybox cubemap (skybox_nebula_dark, proyecto Blackhole de
//  Ross Ning, MIT) como fondo para toda la simulacion.
// ============================================================
#include "raylib.h"
#include "rlgl.h"
#include "math_utils.h"
#include "body.h"
#include "constants.h"
#include <cmath>
#include <cstring>
#include <vector>

// ── Vertex shader compartido ──────────────────────────────────
static const char* BH_VERT_SRC = R"GLSL(
#version 330 core
layout(location=0) in vec3 vertexPosition;
layout(location=1) in vec2 vertexTexCoord;
out vec2 fragTexCoord;
uniform mat4 mvp;
void main(){
    fragTexCoord = vertexTexCoord;
    gl_Position  = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

// ── Shader de distorsión gravitacional multi-fuente ───────────
// Soporta hasta MAX_DIST_SOURCES fuentes simultáneas (BHs + pulsares).
// Cada fuente tiene posición, radios y tipo (BH o NS):
//   • BH (srcIsBlackHole[i]=1): dentro del horizonte → disco negro opaco.
//   • NS/Pulsar (srcIsBlackHole[i]=0): dentro del radio de la estrella
//     → passthrough directo de sceneRT (la estrella se ve, sin negro).
// Los desplazamientos de TODAS las fuentes se ACUMULAN: si hay 3 BHs y
// 2 pulsares, los 5 contribuyen simultáneamente al mismo pixel.
//
// Convención Y: fragTexCoord viene de DrawTextureRec({0,0,sw,-sh}).
// fragTexCoord.y=0 = BORDE INFERIOR pantalla. bhScreenPx.y = sh - raylib_y.
static const char* BH_DISTORT_FRAG = R"GLSL(
#version 330 core
in vec2 fragTexCoord;
out vec4 fragColor;

uniform sampler2D texture0;     // escena, via DrawTextureRec
uniform vec2  screenSize;
uniform int   debugMode;

// Hasta 8 fuentes de distorsion simultaneas (BHs + pulsares)
const int MAX_SRC = 8;
uniform vec2  srcPos[MAX_SRC];        // centros en pixeles (y=0 abajo)
uniform float srcHorizon[MAX_SRC];    // radio del horizonte/superficie (px)
uniform float srcPeak[MAX_SRC];       // radio de max distorsion (px)
uniform float srcInfluence[MAX_SRC];  // radio donde distorsion = 0 (px)
uniform float srcStrength[MAX_SRC];   // desplazamiento maximo (px)
uniform float srcIsBlackHole[MAX_SRC];// 1=BH(disco negro), 0=NS(passthrough)
uniform int   srcCount;               // numero de fuentes activas

float lensProfileSrc(float d, float hz, float pk, float inf){
    if(d < hz || d > inf) return 0.0;
    if(d < pk){
        float t = (d - hz) / max(pk - hz, 1.0);
        return smoothstep(0.0, 1.0, t);
    } else {
        float t = (d - pk) / max(inf - pk, 1.0);
        return 1.0 - smoothstep(0.0, 1.0, t);
    }
}

void main(){
    vec2 pixPos = fragTexCoord * screenSize;

    // ── MODOS DEBUG (trabaja sobre la primera fuente para diagnóstico) ─
    if(debugMode > 0 && srcCount > 0){
        vec2 dir0 = pixPos - srcPos[0];
        float d0  = length(dir0);
        if(debugMode == 1){
            if(d0 < srcHorizon[0])   { fragColor=vec4(0,0,0,1); return; }
            if(d0 < srcPeak[0])      { fragColor=vec4(1,0,0,1); return; }
            if(d0 < srcInfluence[0]) { fragColor=vec4(0,1,0,1); return; }
            fragColor=vec4(0,0,1,1); return;
        }
        if(debugMode == 2){
            fragColor = texture(texture0, fragTexCoord);
            float rw = max(2.0, screenSize.x*0.002);
            if(abs(d0-srcHorizon[0])<rw)   fragColor=vec4(1,0,0,1);
            if(abs(d0-srcPeak[0])<rw)      fragColor=vec4(0,1,0,1);
            if(abs(d0-srcInfluence[0])<rw) fragColor=vec4(0,0,1,1);
            return;
        }
        if(debugMode == 3){
            float n = clamp(d0/max(srcInfluence[0],1.0), 0.0, 1.0);
            fragColor = vec4(n, n*0.5, 1.0-n, 1.0);
            if(d0 < 4.0) fragColor = vec4(1,1,1,1);
            return;
        }
    }

    // ── MODO NORMAL: acumular distorsion de todas las fuentes ────
    vec2 totalDisp = vec2(0.0);

    for(int i = 0; i < srcCount; i++){
        vec2  dir  = pixPos - srcPos[i];
        float dist = length(dir);
        vec2  uDir = dir / max(dist, 0.5);

        if(dist < srcHorizon[i]){
            if(srcIsBlackHole[i] > 0.5){
                // Dentro del horizonte de eventos: negro absoluto.
                // Domina sobre cualquier otra fuente.
                fragColor = vec4(0.0, 0.0, 0.0, 1.0);
                return;
            }
            // Dentro de la superficie de una NS/pulsar: la estrella
            // se ve directamente en sceneRT — sin distorsion, sin negro.
            continue;
        }

        if(dist > srcInfluence[i]) continue;

        float profile = lensProfileSrc(dist,
                            srcHorizon[i], srcPeak[i], srcInfluence[i]);
        vec2  disp = uDir * profile * srcStrength[i];

        // Anti-horizonte solo para BH (evita samplear el disco negro)
        if(srcIsBlackHole[i] > 0.5){
            vec2  sc   = pixPos + disp - srcPos[i];
            float sd   = length(sc);
            float guard = srcHorizon[i] + 2.0;
            if(sd < guard) disp = srcPos[i] + normalize(sc)*guard - pixPos;
        }

        totalDisp += disp;
    }

    vec2 samplePx  = pixPos + totalDisp;
    vec2 sampleUV  = clamp(samplePx / screenSize, 0.001, 0.999);
    fragColor = vec4(texture(texture0, sampleUV).rgb, 1.0);
}
)GLSL";

// ── Skybox cubemap (sin distorsion) ──────────────────────────
static const char* SKY_FRAG_SRC = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec2  resolution;
uniform samplerCube galaxy;  // unit 1
uniform vec3 camForward;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float fovScale;
void main(){
    vec2 uv = gl_FragCoord.xy/resolution - vec2(0.5);
    uv.x   *= resolution.x/resolution.y;
    vec3 dir = normalize(camForward + uv.x*fovScale*camRight + uv.y*fovScale*camUp);
    fragColor = vec4(texture(galaxy, dir).rgb, 1.0);
}
)GLSL";

// ── Passthrough (cuando no hay BH) ───────────────────────────
static const char* BH_PASS_FRAG = R"GLSL(
#version 330 core
in vec2 fragTexCoord;
out vec4 fragColor;
uniform sampler2D texture0;
void main(){ fragColor = texture(texture0, fragTexCoord); }
)GLSL";

// ─────────────────────────────────────────────────────────────
struct BlackholeRenderer {
    // Cubemap para el skybox
    unsigned int cubemapId = 0;

    // Shaders
    Shader skyShader     = {};
    Shader distortShader = {};
    Shader passShader    = {};  // blit sin distorsion

    // Render texture de la escena completa
    RenderTexture2D sceneRT = {};

    bool ready = false;
    int  sw = 0, sh = 0;

    // Parametros configurables (en pixeles)
    // factor: lensWidthPx = horizonPx * factor, clampado a [min,max]
    float debugMode      = 0.0f;  // 0=normal,1=zonas,2=anillos,3=gradiente

    // Parametros de la lente. Antes habia minimos artificiales
    // (minHorizonPx=8, minLensWidth=32) que congelaban el tamano
    // visual del BH en pixeles fijos independientemente de la distancia
    // de la camara. Resultado: el BH parecia CRECER al alejarse porque
    // todo lo demas se achicaba por perspectiva pero el efecto se
    // mantenia en un tamano fijo. Ahora se usa la proyeccion 3D real
    // (GetWorldToScreen) sin pisos artificiales, igual que cualquier
    // otro cuerpo de la escena.
    //
    // La proporcionalidad con la masa es automatica: body.radius del
    // BH ES su radio de Schwarzschild (Rs ∝ masa), asi que un BH
    // mas masivo proyecta mas grande a la misma distancia.
    float lensFactor     = 2.5f;   // zona de lente = horizonte × factor
    float minLensWidth   = 0.0f;   // sin minimo artificial (antes 32px)
    float maxLensWidth   = 500.0f; // maximo amplio para supergigantes cercanos
    float peakFactor     = 0.15f;  // pico muy cerca del borde del horizonte
    float lensStrengthF  = 1.0f;   // fuerza del desplazamiento (antes 0.55)
    float minHorizonPx   = 0.0f;   // sin minimo artificial (antes 8px)

    // Locs del skybox
    int skyLoc_galaxy   = -1;
    int skyLoc_forward  = -1;
    int skyLoc_right    = -1;
    int skyLoc_up       = -1;
    int skyLoc_fov      = -1;
    int skyLoc_res      = -1;
    // Locs del distortion shader multi-fuente
    int dLoc_srcPos       = -1;
    int dLoc_srcHorizon   = -1;
    int dLoc_srcPeak      = -1;
    int dLoc_srcInfluence = -1;
    int dLoc_srcStrength  = -1;
    int dLoc_srcIsBH      = -1;
    int dLoc_srcCount     = -1;
    int dLoc_screenSize   = -1;
    int dLoc_debugMode    = -1;

    // ── Inicialización ───────────────────────────────────────
    void Init(int screenW, int screenH) {
        sw = screenW; sh = screenH;

        // Cargar cubemap skybox
        const char* faces[6] = {
            "skybox_nebula_dark/right.png",  "skybox_nebula_dark/left.png",
            "skybox_nebula_dark/top.png",    "skybox_nebula_dark/bottom.png",
            "skybox_nebula_dark/front.png",  "skybox_nebula_dark/back.png"
        };
        Image imgs[6];
        int faceW = 512; bool ok = true;
        for(int i = 0; i < 6 && ok; i++){
            imgs[i] = LoadImage(faces[i]);
            if(!imgs[i].data){ ok = false; break; }
            ImageFormat(&imgs[i], PIXELFORMAT_UNCOMPRESSED_R8G8B8);
            if(i == 0) faceW = imgs[i].width;
        }
        if(ok){
            int fb = faceW*faceW*3;
            auto* buf = (unsigned char*)RL_MALLOC(6*fb);
            for(int i = 0; i < 6; i++){ memcpy(buf+i*fb, imgs[i].data, fb); UnloadImage(imgs[i]); }
            cubemapId = rlLoadTextureCubemap(buf, faceW, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8, 1);
            RL_FREE(buf);
        } else {
            for(int i = 0; i < 6; i++) if(imgs[i].data) UnloadImage(imgs[i]);
            unsigned char b[6*3] = {};
            cubemapId = rlLoadTextureCubemap(b, 1, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8, 1);
            TraceLog(LOG_WARNING, "BH: skybox_nebula_dark no encontrado");
        }

        skyShader     = LoadShaderFromMemory(BH_VERT_SRC, SKY_FRAG_SRC);
        distortShader = LoadShaderFromMemory(BH_VERT_SRC, BH_DISTORT_FRAG);
        passShader    = LoadShaderFromMemory(BH_VERT_SRC, BH_PASS_FRAG);

        skyLoc_galaxy   = GetShaderLocation(skyShader, "galaxy");
        skyLoc_forward  = GetShaderLocation(skyShader, "camForward");
        skyLoc_right    = GetShaderLocation(skyShader, "camRight");
        skyLoc_up       = GetShaderLocation(skyShader, "camUp");
        skyLoc_fov      = GetShaderLocation(skyShader, "fovScale");
        skyLoc_res      = GetShaderLocation(skyShader,     "resolution");
        dLoc_srcPos     = GetShaderLocation(distortShader, "srcPos");
        dLoc_srcHorizon = GetShaderLocation(distortShader, "srcHorizon");
        dLoc_srcPeak    = GetShaderLocation(distortShader, "srcPeak");
        dLoc_srcInfluence=GetShaderLocation(distortShader, "srcInfluence");
        dLoc_srcStrength= GetShaderLocation(distortShader, "srcStrength");
        dLoc_srcIsBH    = GetShaderLocation(distortShader, "srcIsBlackHole");
        dLoc_srcCount   = GetShaderLocation(distortShader, "srcCount");
        dLoc_screenSize = GetShaderLocation(distortShader, "screenSize");
        dLoc_debugMode  = GetShaderLocation(distortShader, "debugMode");

        // Fijar galaxy en unit 1 para sky shader
        int u1 = 1;
        SetShaderValue(skyShader, skyLoc_galaxy, &u1, SHADER_UNIFORM_INT);

        sceneRT = LoadRenderTexture(sw, sh);
        ready   = true;
    }

    // ── Empieza captura de la escena ─────────────────────────
    // Llama ANTES del skybox y BeginMode3D en el loop principal.
    void BeginSceneCapture(){
        if(!ready) return;
        BeginTextureMode(sceneRT);
        ClearBackground(BLACK);
    }

    // ── Termina captura ──────────────────────────────────────
    void EndSceneCapture(){
        if(!ready) return;
        EndTextureMode();
    }

    // ── Skybox cubemap ───────────────────────────────────────
    void DrawCubemapSkybox(const Camera3D& cam) const {
        if(!ready) return;
        float res[2] = {(float)sw, (float)sh};
        Vector3 fwd   = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
        Vector3 up    = Vector3CrossProduct(right, fwd);
        float fovS    = tanf(cam.fovy * DEG2RAD * 0.5f);

        rlActiveTextureSlot(1); rlEnableTextureCubemap(cubemapId);
        rlActiveTextureSlot(0);

        BeginShaderMode(skyShader);
            SetShaderValue(skyShader, skyLoc_forward, &fwd,   SHADER_UNIFORM_VEC3);
            SetShaderValue(skyShader, skyLoc_right,   &right, SHADER_UNIFORM_VEC3);
            SetShaderValue(skyShader, skyLoc_up,      &up,    SHADER_UNIFORM_VEC3);
            SetShaderValue(skyShader, skyLoc_fov,     &fovS,  SHADER_UNIFORM_FLOAT);
            SetShaderValue(skyShader, skyLoc_res,     res,    SHADER_UNIFORM_VEC2);
            DrawRectangle(0, 0, sw, sh, WHITE);
        EndShaderMode();

        rlActiveTextureSlot(1); rlDisableTextureCubemap();
        rlActiveTextureSlot(0);
    }

    // ── Vuelca la sceneRT con distorsión de todos los objetos compactos ─
    // Recorre TODOS los BHs y pulsares de la escena, los proyecta a
    // pantalla y sube los arrays al shader. El shader acumula la
    // distorsion de TODAS las fuentes simultaneamente.
    // • BH (BLACK_HOLE): horizonte negro + parametros completos
    // • Pulsar (PULSAR): sin disco negro + fuerza reducida (~35%)
    // • Magnetar: pendiente de futuro
    void BlitWithBHDistortion(const std::vector<Body>& bodies,
                               const Camera3D& cam)
    {
        if(!ready) return;

        static const int MAX_SRC = 8;

        // Arrays de datos para el shader (float para compatibilidad uniforme)
        float posArr[MAX_SRC * 2];      // vec2 array empaquetado
        float horizArr[MAX_SRC];
        float peakArr [MAX_SRC];
        float infArr  [MAX_SRC];
        float strArr  [MAX_SRC];
        float isBHArr [MAX_SRC];        // 1.0=BH, 0.0=NS
        int   count = 0;

        // Helper: proyectar radio del cuerpo en pixeles (perpendicular a camara)
        auto projectRadius = [&](Vector3 drawPos, float drawRadius) -> float {
            Vector3 toCam = Vector3Subtract(cam.position, drawPos);
            float   d3D   = Vector3Length(toCam);
            Vector3 fwd   = (d3D > 1e-7f) ? Vector3Scale(toCam, 1.f/d3D)
                                           : Vector3{0.f,0.f,-1.f};
            Vector3 up    = cam.up;
            if(std::fabs(Vector3DotProduct(fwd, up)) > 0.999f) up = {1.f,0.f,0.f};
            Vector3 perp  = Vector3Normalize(Vector3CrossProduct(fwd, up));
            Vector3 edge  = Vector3Add(drawPos, Vector3Scale(perp, drawRadius));
            Vector2 cSrc  = GetWorldToScreen(drawPos, cam);
            Vector2 cEdge = GetWorldToScreen(edge,    cam);
            return Vector2Distance(cEdge, cSrc);
        };

        for(const auto& b : bodies){
            if(count >= MAX_SRC) break;
            if(b.mass <= 0.0) continue;

            bool isBHBody = (b.stellarPhase == StellarPhase::BLACK_HOLE);
            bool isPSR    = (b.stellarPhase == StellarPhase::PULSAR  ||
                             b.stellarPhase == StellarPhase::MAGNETAR);
            if(!isBHBody && !isPSR) continue;

            Vector3 dPos    = ToDrawPos(b.pos);
            float   drawR   = (float)(b.radius * RENDER_SCALE);
            float   projPx  = projectRadius(dPos, drawR);
            Vector2 scrPos  = GetWorldToScreen(dPos, cam);

            // Parametros diferenciados BH vs pulsar
            float effLensFactor  = isBHBody ? lensFactor        : 1.8f;
            float effPeakFactor  = isBHBody ? peakFactor        : 0.18f;
            float effStrF        = isBHBody ? lensStrengthF     : lensStrengthF * 0.35f;
            float effMinLens     = isBHBody ? minLensWidth      : 8.0f;
            float effMaxLens     = isBHBody ? maxLensWidth      : maxLensWidth * 0.6f;
            float horizFloor     = isBHBody ? minHorizonPx      : 0.0f;

            float horizPx  = std::max(projPx, horizFloor);
            float lensW    = std::clamp(horizPx * effLensFactor, effMinLens, effMaxLens);
            float peakP    = horizPx + lensW * effPeakFactor;
            float infP     = horizPx + lensW;
            float strP     = lensW * effStrF;

            // Posicion en coordenadas del shader (y=0 abajo)
            posArr[count*2 + 0] = scrPos.x;
            posArr[count*2 + 1] = (float)sh - scrPos.y;
            horizArr[count]     = horizPx;
            peakArr [count]     = peakP;
            infArr  [count]     = infP;
            strArr  [count]     = strP;
            isBHArr [count]     = isBHBody ? 1.0f : 0.0f;
            count++;
        }

        // Sin objetos: blit simple sin shader
        if(count == 0){
            DrawTextureRec(sceneRT.texture,
                {0.f, 0.f, (float)sw, -(float)sh},
                {0.f, 0.f}, WHITE);
            return;
        }

        float screenSzArr[2] = { (float)sw, (float)sh };
        int   dbgMode        = (int)debugMode;

        BeginShaderMode(distortShader);
            SetShaderValueV(distortShader, dLoc_srcPos,      posArr,   SHADER_UNIFORM_VEC2,  count);
            SetShaderValueV(distortShader, dLoc_srcHorizon,  horizArr, SHADER_UNIFORM_FLOAT, count);
            SetShaderValueV(distortShader, dLoc_srcPeak,     peakArr,  SHADER_UNIFORM_FLOAT, count);
            SetShaderValueV(distortShader, dLoc_srcInfluence,infArr,   SHADER_UNIFORM_FLOAT, count);
            SetShaderValueV(distortShader, dLoc_srcStrength, strArr,   SHADER_UNIFORM_FLOAT, count);
            SetShaderValueV(distortShader, dLoc_srcIsBH,     isBHArr,  SHADER_UNIFORM_FLOAT, count);
            SetShaderValue (distortShader, dLoc_srcCount,    &count,   SHADER_UNIFORM_INT);
            SetShaderValue (distortShader, dLoc_screenSize,  screenSzArr, SHADER_UNIFORM_VEC2);
            SetShaderValue (distortShader, dLoc_debugMode,   &dbgMode, SHADER_UNIFORM_INT);
            DrawTextureRec(sceneRT.texture,
                {0.f, 0.f, (float)sw, -(float)sh},
                {0.f, 0.f}, WHITE);
        EndShaderMode();
    }

    void Unload(){
        if(!ready) return;
        UnloadShader(skyShader);
        UnloadShader(distortShader);
        UnloadShader(passShader);
        UnloadRenderTexture(sceneRT);
        rlUnloadTexture(cubemapId);
        ready = false;
    }
};

inline BlackholeRenderer& GetBlackholeRenderer(){
    static BlackholeRenderer inst;
    return inst;
}
