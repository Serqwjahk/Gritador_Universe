#pragma once
#include "raylib.h"

// ============================================================
//  Gestión de texturas globales
// ============================================================

// Texturas actualmente usadas en render:
//   - Tierra: daymap (diffuse), normal map, specular map, nightmap (emission), clouds
//   - Gritador: textura propia (renderizado legacy por textura, sin shader procedural)
//   - Skybox: cargada directamente en main.cpp (no pasa por este store)
//
// Texturas eliminadas (2k_jupiter/mercury/moon/mars/neptune/sun .jpg):
//   - Todos los planetas rocosos y gigantes usan shaders procedurales.
//   - El shader drawRockyPlanet solo activa texturas si los TRES mapas
//     (diffuse + normal + specular) son no-nulos — sin normal/specular propios,
//     la textura difusa nunca se renderiza y se convierte en basura residual.
struct GlobalTextures {
    Texture2D* earthBase  = nullptr;
    Texture2D* earthNorm  = nullptr;
    Texture2D* earthSpec  = nullptr;
    Texture2D* earthNight = nullptr;
    Texture2D* earthCloud = nullptr;
    Texture2D* gritador   = nullptr;
    Texture2D* blank      = nullptr;
};

struct TextureStore {
    Texture2D earthBase, earthNorm, earthSpec, earthNight, earthCloud;
    Texture2D gritador;
    Texture2D blank;

    void Load() {
        earthBase  = LoadTexture("2k_earth_daymap.png");
        earthNorm  = LoadTexture("2k_earth_normal_map.png");
        earthSpec  = LoadTexture("2k_earth_specular_map.png");
        earthNight = LoadTexture("2k_earth_nightmap.png");
        earthCloud = LoadTexture("2k_earth_clouds.png");
        gritador   = LoadTexture("gritador.png");

        Image imgBlank = GenImageColor(2, 2, WHITE);
        blank = LoadTextureFromImage(imgBlank);
        UnloadImage(imgBlank);
    }

    void Unload() {
        UnloadTexture(earthBase);  UnloadTexture(earthNorm);
        UnloadTexture(earthSpec);  UnloadTexture(earthNight);
        UnloadTexture(earthCloud); UnloadTexture(gritador);
        UnloadTexture(blank);
    }

    GlobalTextures MakeRefs() {
        return {
            &earthBase, &earthNorm, &earthSpec, &earthNight, &earthCloud,
            &gritador, &blank
        };
    }
};
