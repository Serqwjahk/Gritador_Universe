#pragma once
#include "raylib.h"

// ============================================================
//  Gestión de texturas globales
// ============================================================

// Contenedor de todas las texturas del juego.
// Se carga una vez en main() y se pasa por referencia.
struct GlobalTextures {
    Texture2D* earthBase  = nullptr;
    Texture2D* earthNorm  = nullptr;
    Texture2D* earthSpec  = nullptr;
    Texture2D* earthNight = nullptr;
    Texture2D* earthCloud = nullptr;
    Texture2D* jupiter    = nullptr;
    Texture2D* mercury    = nullptr;
    Texture2D* luna       = nullptr;
    Texture2D* marte      = nullptr;
    Texture2D* neptuno    = nullptr;
    Texture2D* sol        = nullptr;
    Texture2D* gritador   = nullptr;
    Texture2D* blank      = nullptr;   // Textura vacía (2x2 BLANK)
};

// Carga todas las texturas desde disco y las asigna al struct.
// Las texturas de los objetos se guardan como miembros en las instancias de TextureStore.
struct TextureStore {
    Texture2D earthBase, earthNorm, earthSpec, earthNight, earthCloud;
    Texture2D jupiter, mercury;
    Texture2D luna, marte, neptuno, sol;
    Texture2D gritador;
    Texture2D blank;

    void Load() {
        earthBase  = LoadTexture("2k_earth_daymap.jpg");
        earthNorm  = LoadTexture("2k_earth_normal_map.png");
        earthSpec  = LoadTexture("2k_earth_specular_map.png");
        earthNight = LoadTexture("2k_earth_nightmap.jpg");
        earthCloud = LoadTexture("2k_earth_clouds.png");
        jupiter    = LoadTexture("2k_jupiter.jpg");
        mercury    = LoadTexture("2k_mercury.jpg");
        luna       = LoadTexture("2k_moon.jpg");
        marte      = LoadTexture("2k_mars.jpg");
        neptuno    = LoadTexture("2k_neptune.jpg");
        sol        = LoadTexture("2k_sun.jpg");
        gritador   = LoadTexture("gritador.jpg");

        Image imgBlank = GenImageColor(2, 2, WHITE);  // WHITE, no BLANK (BLANK=transparente = negro con shader fallback)
        blank = LoadTextureFromImage(imgBlank);
        UnloadImage(imgBlank);
    }

    void Unload() {
        UnloadTexture(earthBase);  UnloadTexture(earthNorm);
        UnloadTexture(earthSpec);  UnloadTexture(earthNight);
        UnloadTexture(earthCloud); UnloadTexture(jupiter);
        UnloadTexture(mercury);    UnloadTexture(luna);
        UnloadTexture(marte);      UnloadTexture(neptuno);
        UnloadTexture(sol);        UnloadTexture(gritador);
        UnloadTexture(blank);
    }

    // Construye un GlobalTextures con punteros a los miembros de este store
    GlobalTextures MakeRefs() {
        return {
            &earthBase, &earthNorm, &earthSpec, &earthNight, &earthCloud,
            &jupiter, &mercury, &luna, &marte, &neptuno, &sol, &gritador, &blank
        };
    }
};
