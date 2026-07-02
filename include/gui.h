#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "imgui.h"
#include "raylib.h"
#include "constants.h"
#include "body.h"
#include "stellar_evolution.h"
#include "camera_input.h"
#include "preview_renderer.h"

// ============================================================
//  GUI estilo "Universe Sandbox" (Dear ImGui + rlImGui)
// ============================================================
// IMPORTANTE -- glyphs soportados: rlImGuiSetup(true) (main.cpp) carga la
// fuente por defecto de Dear ImGui SIN glyph ranges extra, que solo cubre
// Basic Latin + Latin-1 Supplement (U+0020-U+00FF). Eso incluye TODOS los
// acentos/eñes del español (á é í ó ú ñ ¿ ¡, todos <= U+00FF) y simbolos
// comunes (± × ° , todos <= U+00FF), pero NO simbolos astronomicos como
// ☉ (U+2609, sol), ⊕ (U+2295, tierra) ni emojis (⚡ U+26A1 etc.) -- esos
// se renderizan como glyph faltante/caja vacia. NUNCA usar esos simbolos
// en texto de ImGui (Text/Button/Selectable/...); usar abreviaturas ASCII
// en su lugar ("Rsol", "Msol", "Lsol", sin simbolo decorativo en botones).
// Comentarios de C++ no tienen esta restriccion (no pasan por ImGui).

// Aplica el tema oscuro/translucido global pedido por el usuario:
// fondos de ventana casi negros con transparencia, esquinas
// redondeadas, y botones grises-azulados sutiles con hover claro.
inline void ApplyDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    style.WindowRounding    = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]        = ImVec4(0.05f, 0.05f, 0.05f, 0.85f);
    colors[ImGuiCol_ChildBg]         = ImVec4(0.05f, 0.05f, 0.05f, 0.00f);
    colors[ImGuiCol_PopupBg]         = ImVec4(0.07f, 0.07f, 0.09f, 0.92f);
    colors[ImGuiCol_Border]          = ImVec4(0.30f, 0.32f, 0.40f, 0.50f);

    colors[ImGuiCol_FrameBg]         = ImVec4(0.15f, 0.16f, 0.20f, 0.75f);
    colors[ImGuiCol_FrameBgHovered]  = ImVec4(0.24f, 0.27f, 0.38f, 0.85f);
    colors[ImGuiCol_FrameBgActive]   = ImVec4(0.27f, 0.38f, 0.62f, 0.90f);

    colors[ImGuiCol_TitleBg]         = ImVec4(0.06f, 0.06f, 0.08f, 0.90f);
    colors[ImGuiCol_TitleBgActive]   = ImVec4(0.10f, 0.11f, 0.16f, 0.95f);

    colors[ImGuiCol_Button]          = ImVec4(0.18f, 0.19f, 0.24f, 0.80f);
    colors[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.45f, 0.75f, 0.85f);
    colors[ImGuiCol_ButtonActive]    = ImVec4(0.26f, 0.38f, 0.62f, 0.95f);

    colors[ImGuiCol_Header]          = ImVec4(0.18f, 0.19f, 0.24f, 0.80f);
    colors[ImGuiCol_HeaderHovered]   = ImVec4(0.30f, 0.45f, 0.75f, 0.85f);
    colors[ImGuiCol_HeaderActive]    = ImVec4(0.26f, 0.38f, 0.62f, 0.95f);

    colors[ImGuiCol_Tab]             = ImVec4(0.13f, 0.14f, 0.18f, 0.80f);
    colors[ImGuiCol_TabHovered]      = ImVec4(0.30f, 0.45f, 0.75f, 0.85f);
    colors[ImGuiCol_TabActive]       = ImVec4(0.24f, 0.34f, 0.55f, 0.95f);

    colors[ImGuiCol_SliderGrab]       = ImVec4(0.40f, 0.55f, 0.85f, 0.90f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.65f, 0.95f, 1.00f);
    colors[ImGuiCol_CheckMark]        = ImVec4(0.50f, 0.65f, 0.95f, 1.00f);
}

// ── Estado persistente de la GUI ────────────────────────────
struct GuiState {
    bool showCatalogMenu = false;
    bool showMainMenu    = false;

    // ── Buscador de objetos en escena (DrawObjectSearchPanel) ──
    bool showObjectSearch = false;
    char searchBuf[64]    = "";

    // Pestana activa del menu de catalogo: 0=Todos, 1=Estrellas, 2=Planetas,
    // 3=Lunas, 4=Menores (ver BodyCategory en body.h y DrawCatalogMenu).
    int  catalogCategory = 0;

    // ── Inspector de propiedades (DrawObjectInspector) ──
    // ID ESTABLE (Body::id, nunca se reusa) del cuerpo seleccionado la
    // ultima vez que se dibujo el inspector: al cambiar, se resincroniza
    // 'nameBuf' con b.name (para no pisar lo que el usuario esta
    // escribiendo mientras edita). ANTES se comparaba por INDICE
    // (selectedIdx) en vez de id -- si se borraba un cuerpo y se creaba
    // otro, el nuevo podia heredar el MISMO indice que el anterior (el
    // vector se reacomoda), asi que la comparacion "no cambio" daba
    // verdadero y el campo de nombre se quedaba mostrando el nombre del
    // cuerpo BORRADO en vez del nuevo.
    uint64_t lastSelectedId = 0;
    char nameBuf[64]        = "";

    // Unidad de masa/radio mostrada/editada -- ver massUnitNames/
    // radiusUnitNames en DrawObjectInspector. Se RE-CALCULA
    // automaticamente (la mas adecuada a la escala del cuerpo, ver
    // AutoMassUnit/AutoRadiusUnit) cada vez que cambia el cuerpo
    // seleccionado (mismo trigger que 'lastSelectedId'), pero el jugador
    // puede cambiarla manualmente en cualquier momento para EL CUERPO
    // ACTUAL via el combo correspondiente.
    int  massUnit   = 0;
    int  radiusUnit = 0;

    // Senial hacia main.cpp (que SI tiene acceso a Database/tex, ver
    // LoadRealisticSolarSystem en solar_system_template.h) de que el
    // usuario confirmo cargar la plantilla de Sistema Solar Realista --
    // gui.h se mantiene deliberadamente sin esa dependencia pesada
    // (catalog.h/physics.h), igual que el resto de este archivo.
    bool requestLoadRealisticSystem = false;
};

// Flags comunes para los paneles "flotantes" sin marco/titulo del HUD.
static constexpr ImGuiWindowFlags HUD_FLAGS =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing;

// Texto "xN" de la velocidad de simulacion actual a partir de TIME_STEP.
// Misma escala que ui.h: TIME_STEP=1200.0 (1 dia/seg de sim) equivale a "x1".
inline const char* TimeScaleLabel() {
    double rt = TIME_STEP / 1200.0;
    return rt>=32768?"x32768":rt>=16384?"x16384":rt>=8192?"x8192":rt>=4096?"x4096":
           rt>=2048?"x2048":rt>=1024?"x1024":rt>=512?"x512":rt>=256?"x256":
           rt>=128?"x128":rt>=64?"x64":rt>=32?"x32":rt>=16?"x16":rt>=8?"x8":
           rt>=4?"x4":rt>=2?"x2":rt>=1?"x1":rt>=0.5?"x1/2":rt>=0.25?"x1/4":
           rt>=0.125?"x1/8":rt>=0.0625?"x1/16":rt>=0.03125?"x1/32":
           rt>=0.015625?"x1/64":rt>=0.0078125?"x1/128":rt>=0.00390625?"x1/256":
           rt>=0.001953125?"x1/512":rt>=0.000976563?"x1/1024":
           rt>=0.000488281?"x1/2048":rt>=0.000244141?"x1/4096":
           rt>=0.000122070?"x1/8192":"x1/16384";
}

// ── HUD de controles de tiempo (esquina inferior izquierda) ─
inline void DrawTimeControlsHUD(GuiState& gui, bool& paused) {
    (void)gui;
    const float pad = 12.0f;
    ImVec2 winSize(296.0f, 56.0f);
    ImGui::SetNextWindowPos(ImVec2(pad, (float)GetScreenHeight() - winSize.y - pad));
    ImGui::SetNextWindowSize(winSize);
    ImGui::Begin("##TimeControlsHUD", nullptr, HUD_FLAGS);

    if (ImGui::Button(paused ? "Reanudar" : "Pausar", ImVec2(80, 32)))
        paused = !paused;

    ImGui::SameLine();
    if (ImGui::Button("<<", ImVec2(36, 32)))
        TIME_STEP = std::max(TIME_STEP * 0.5, 1200.0 / 16384.0);

    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2(36, 32)))
        TIME_STEP = std::min(TIME_STEP * 2.0, 1200.0 * 32768.0);

    ImGui::SameLine();
    if (ImGui::Button(TimeScaleLabel(), ImVec2(86, 32)))
        TIME_STEP = 1200.0;

    ImGui::End();
}

// ── Overlay de estado (sobre el HUD de tiempo, esquina inferior
//    izquierda): contadores de cuerpos/polvo y atajos contextuales que
//    antes vivian en DrawUIPanel (ui.h), retirado en favor de esta GUI. ──
inline void DrawStatusOverlay(const std::vector<Body>& bodies, int dustCount, const InputState& input) {
    const float pad = 12.0f;
    ImGui::SetNextWindowPos(ImVec2(pad, (float)GetScreenHeight() - 56.0f - 2.0f * pad),
                             ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::Begin("##StatusOverlay", nullptr, HUD_FLAGS | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("Cuerpos: %d / %d", (int)bodies.size(), MAX_BODIES);
    ImGui::Text("Polvo: %d / %d", dustCount, MAX_DUST_PARTICLES);

    if (input.selectedBodyIdx >= 0 && input.selectedBodyIdx < (int)bodies.size()) {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "[SUPR] Borrar seleccionado");
        if (input.followSelected)
            ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.0f, 1.0f), ">> Siguiendo: %s",
                                bodies[(size_t)input.selectedBodyIdx].name.c_str());
        else
            ImGui::TextDisabled("[F] o doble-click para seguir");
    }

    ImGui::End();
}

// ── Boton "+" central para abrir/cerrar el menu de catalogo ─
inline void DrawAddButton(GuiState& gui) {
    const float pad = 12.0f;
    const float size = 56.0f;
    ImVec2 winSize(size + 16.0f, size + 16.0f);
    ImGui::SetNextWindowPos(ImVec2(((float)GetScreenWidth() - winSize.x) * 0.5f,
                                    (float)GetScreenHeight() - winSize.y - pad));
    ImGui::SetNextWindowSize(winSize);
    ImGui::Begin("##AddButton", nullptr, HUD_FLAGS);

    bool wasOpen = gui.showCatalogMenu;
    if (wasOpen)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
    if (ImGui::Button("+", ImVec2(size, size)))
        gui.showCatalogMenu = !gui.showCatalogMenu;
    if (wasOpen)
        ImGui::PopStyleColor();

    ImGui::End();
}

// Devuelve true si 'cat' pertenece a la pestana 'tabIdx' (0=Todos cubre
// todas las categorias; 1..4 mapean 1:1 con BodyCategory STAR/PLANET/MOON/MINOR).
inline bool CategoryMatchesTab(BodyCategory cat, int tabIdx) {
    if (tabIdx == 0) return true;
    return (int)cat == (tabIdx - 1);
}

// ── Menu de catalogo: pestanas de categoria + grid + modos de colocacion ─
inline void DrawCatalogMenu(GuiState& gui, InputState& input,
                             const std::vector<CatalogItem>& Database, int& catIdx)
{
    if (!gui.showCatalogMenu) return;

    const float pad     = 12.0f;
    const float winW    = (float)GetScreenWidth() - 2.0f * pad;
    // Altura adaptada a las celdas mas grandes (110px): 3 filas + pestanas/separadores
    const float winH    = std::min(420.0f, (float)GetScreenHeight() * 0.44f);
    const float bottomY = (float)GetScreenHeight() - 56.0f - 2.0f * pad - winH;

    ImGui::SetNextWindowPos(ImVec2(pad, bottomY));
    ImGui::SetNextWindowSize(ImVec2(winW, winH));
    ImGui::Begin("Catalogo de Objetos", &gui.showCatalogMenu,
                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // ── Pestanas de categoria ──
    static const char* tabNames[] = {"Todos", "Estrellas", "Planetas", "Lunas", "Menores"};
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine();
        bool active = (gui.catalogCategory == i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        if (ImGui::Button(tabNames[i])) gui.catalogCategory = i;
        if (active) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    const float rightW = 220.0f;

    // ── Grid central de objetos del catalogo ──
    ImGui::BeginChild("##CatalogGrid", ImVec2(-rightW, 0), false);
    {
        const float cellSize = 110.0f;
        ImGuiStyle& style = ImGui::GetStyle();
        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

        for (size_t i = 0; i < Database.size(); ++i) {
            const CatalogItem& item = Database[i];
            if (!CategoryMatchesTab(item.category, gui.catalogCategory)) continue;

            ImGui::PushID((int)i);
            ImVec2 size(cellSize, cellSize);
            bool clicked = ImGui::InvisibleButton("cell", size);
            bool hovered = ImGui::IsItemHovered();
            bool selected = ((int)i == catIdx);

            ImVec2 rmin = ImGui::GetItemRectMin();
            ImVec2 rmax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImU32 bg = selected ? IM_COL32(80, 110, 180, 220)
                     : hovered  ? IM_COL32(55, 65, 95, 200)
                                : IM_COL32(28, 28, 38, 160);
            dl->AddRectFilled(rmin, rmax, bg, 4.0f);

            // Miniatura 3D cacheada: reemplaza la bolita de color
            {
                unsigned int tid = GetCatalogCache().GetTextureId((int)i);
                if (tid) {
                    // Área disponible para el icono: toda la celda menos el label (22px)
                    float iconAreaH = cellSize - 22.0f;
                    // Cuadrado que ocupa el área del icono con un margen de 2px
                    float thumbSize = iconAreaH - 4.0f;
                    float thumbX = (rmin.x + rmax.x) * 0.5f - thumbSize * 0.5f;
                    float thumbY = rmin.y + 2.0f;
                    // UV invertida verticalmente (quirk de raylib render textures)
                    dl->AddImage((ImTextureID)(uintptr_t)tid,
                                 ImVec2(thumbX, thumbY),
                                 ImVec2(thumbX + thumbSize, thumbY + thumbSize),
                                 ImVec2(0, 1), ImVec2(1, 0));
                } else {
                    // Fallback mientras no hay miniatura lista
                    ImVec2 center((rmin.x + rmax.x) * 0.5f, rmin.y + (cellSize - 22.0f) * 0.5f);
                    dl->AddCircleFilled(center, 16.0f,
                                        IM_COL32(item.color.r, item.color.g, item.color.b, 255));
                }
            }

            ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str());
            float tx = (rmin.x + rmax.x) * 0.5f - textSize.x * 0.5f;
            float ty = rmax.y - textSize.y - 4.0f;
            dl->AddText(ImVec2(tx, ty), IM_COL32(230, 230, 230, 255), item.name.c_str());

            // Elegir un objeto del catalogo activa por defecto el modo
            // "Inmovil" SOLO si no se esta ya en un modo de colocacion
            // (input.mode == MODE_SELECT, p.ej. recien abierto el catalogo):
            // sin la condicion, cambiar de objeto tras elegir manualmente
            // "Orbita"/"Lanzar" reescribia ese modo a "Inmovil" cada vez.
            if (clicked) {
                catIdx = (int)i;
                if (input.mode == MODE_SELECT) input.mode = MODE_STATIC;
            }
            ImGui::PopID();

            float lastX2 = ImGui::GetItemRectMax().x;
            float nextX2 = lastX2 + style.ItemSpacing.x + cellSize;
            if (nextX2 < windowVisibleX2) ImGui::SameLine();
        }
    }
    ImGui::EndChild();

    // ── Panel lateral: modos de colocacion ──
    ImGui::SameLine();
    ImGui::BeginChild("##PlacementModes", ImVec2(rightW - 8.0f, 0), true);
    {
        ImGui::TextUnformatted("Modo de colocacion");
        ImGui::Separator();

        auto modeButton = [&](const char* label, SpawnMode m) {
            bool active = (input.mode == m);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button(label, ImVec2(-1, 32))) input.mode = m;
            if (active) ImGui::PopStyleColor();
        };
        modeButton("Inmovil", MODE_STATIC);
        modeButton("Orbita",  MODE_ORBIT);
        modeButton("Lanzar",  MODE_LAUNCH);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Checkbox("Orbita retrograda", &input.orbitRetrograde);
        ImGui::SliderFloat("Excentricidad", &input.orbitEccentricity, 0.0f, 0.95f);
    }
    ImGui::EndChild();

    ImGui::End();
}

// Lista ordenada (descendente por fraccion de masa) de un inventario de
// composicion quimica/geologica (solid_composition/atmospheric_composition,
// ver body.h y composition.h). "Agua_Hielo" -> "Agua Hielo" para legibilidad.
inline void DrawCompositionList(const std::unordered_map<std::string, float>& comp) {
    if (comp.empty()) {
        ImGui::TextDisabled("Sin datos");
        return;
    }
    std::vector<std::pair<std::string, float>> items(comp.begin(), comp.end());
    std::sort(items.begin(), items.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (auto& item : items) {
        std::string label = item.first;
        std::replace(label.begin(), label.end(), '_', ' ');
        ImGui::Text("%-22s %5.2f%%", label.c_str(), item.second * 100.0f);
    }
}

// Periodo orbital (Kepler III) usando el cuerpo mas masivo del sistema
// como proxy de "cuerpo central": T = 2*pi*sqrt(r^3 / (G*M_central)), con
// r = |b.pos - central.pos|. Si 'b' ES el cuerpo mas masivo (no hay un
// "central" distinto), devuelve "N/A".
inline std::string OrbitalPeriod(const Body& b, const std::vector<Body>& bodies) {
    const Body* central = nullptr;
    for (const auto& other : bodies) {
        if (other.mass <= 0.0) continue;
        if (!central || other.mass > central->mass) central = &other;
    }
    if (!central || central == &b) return "N/A";

    double r = (b.pos - central->pos).length();
    if (r <= 0.0) return "N/A";

    double T = 2.0 * PI_D * std::sqrt((r * r * r) / (G * central->mass));
    double days = T / 86400.0;

    char buf[64];
    if (days >= 365.25)
        std::snprintf(buf, sizeof(buf), "%.2f anios", days / 365.25);
    else
        std::snprintf(buf, sizeof(buf), "%.2f dias", days);
    return std::string(buf);
}

// Periodo de ROTACION (no orbital), derivado del spin ACTUAL del cuerpo
// (b.spinRateDeg) -- no de un valor fijo de catalogo, asi que si el
// bloqueo de marea lo cambia en vivo (ver physics.h, ApplyTidesAndRoche)
// este numero se actualiza con el. spinRateDeg esta en GRADOS POR 1200s
// DE TIEMPO SIMULADO (ver renderer.h: rotationAngle += spinRateDeg *
// (TIME_STEP/1200.0)), asi que periodoSeg = 360 / (spinRateDeg/1200) =
// 432000 / spinRateDeg. Signo negativo = rotacion retrograda (Venus,
// Urano, Pluton...) -- se preserva en el resultado, no se oculta.
inline std::string RotationPeriod(float spinRateDeg) {
    if (std::fabs(spinRateDeg) < 1e-6f) return "Sin rotacion";

    double periodDays = (432000.0 / std::fabs((double)spinRateDeg)) / 86400.0;
    double periodSec  = periodDays * 86400.0;
    const char* retro = (spinRateDeg < 0.0f) ? " (retrograda)" : "";

    char buf[64];
    if (periodSec < 0.001) {
        // Pulsar de ms (periodo < 1ms)
        std::snprintf(buf, sizeof(buf), "%.3f ms%s", periodSec * 1000.0, retro);
    } else if (periodSec < 1.0) {
        // Pulsar normal (periodo < 1s, p.ej. Cangrejo: 0.033s)
        std::snprintf(buf, sizeof(buf), "%.4f s%s", periodSec, retro);
    } else if (periodSec < 60.0) {
        // Rotacion rapida en segundos
        std::snprintf(buf, sizeof(buf), "%.2f s%s", periodSec, retro);
    } else if (periodDays < 1.0) {
        std::snprintf(buf, sizeof(buf), "%.2f horas%s", periodDays * 24.0, retro);
    } else if (periodDays < 365.25) {
        std::snprintf(buf, sizeof(buf), "%.2f dias%s", periodDays, retro);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f anios%s", periodDays / 365.25, retro);
    }
    return std::string(buf);
}

// ── Unidades de MASA/RADIO para el inspector de propiedades ──────────────
// Cada parametro tiene su PROPIA lista de unidades (antes una unica lista
// global de masa -- kg/Masas Terrestres/Masas Lunares -- se aplicaba sin
// distincion a CUALQUIER cuerpo, obligando a leer numeros gigantes para
// una supergigante en "Masas Lunares" o numeros minusculos para un
// asteroide en "Masas Terrestres"). AutoMassUnit/AutoRadiusUnit eligen la
// unidad mas legible (valor mostrado típicamente entre 1 y tres digitos)
// segun la magnitud real del cuerpo; se re-evaluan al cambiar de cuerpo
// seleccionado (ver GuiState::massUnit/radiusUnit), pero el jugador puede
// pisarlas a mano en cualquier momento via el combo correspondiente.
static const char*  massUnitNames[] = {
    "kg", "Masas Terrestres", "Masas Jovianas", "Masas Solares", "Masas Galacticas"
};
static const double massUnitDiv[] = { 1.0, M_EARTH, M_JUPITER, M_SUN, M_MILKYWAY };
constexpr int MASS_UNIT_COUNT = 5;

inline int AutoMassUnit(double massKg) {
    if (massKg < M_EARTH)    return 0;
    if (massKg < M_JUPITER)  return 1;
    if (massKg < M_SUN)      return 2;
    if (massKg < M_MILKYWAY) return 3;
    return 4;
}

static const char*  radiusUnitNames[] = {
    "km", "Radios Terrestres", "Radios Jovianos", "Radios Solares", "UA", "Radios Galacticos"
};
static const double radiusUnitDiv[] = { 1000.0, R_EARTH, R_JUPITER, R_SUN, AU_IN_M, R_MILKYWAY };
constexpr int RADIUS_UNIT_COUNT = 6;

inline int AutoRadiusUnit(double radiusM) {
    if (radiusM < R_EARTH)    return 0;
    if (radiusM < R_JUPITER)  return 1;
    if (radiusM < R_SUN)      return 2;
    if (radiusM < AU_IN_M)    return 3;
    if (radiusM < R_MILKYWAY) return 4;
    return 5;
}

// ── Panel de Propiedades del Objeto (inspector, anclado a la derecha) ─
inline void DrawObjectInspector(GuiState& gui, Body& b,
                                 const std::vector<Body>& bodies)
{
    const float winW = 350.0f;
    const float pad  = 12.0f;
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() - winW - pad, pad), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, (float)GetScreenHeight() - 2.0f * pad), ImGuiCond_Always);
    ImGui::Begin("Propiedades del Objeto", nullptr, ImGuiWindowFlags_NoCollapse);

    // Resincroniza el buffer del nombre Y las unidades de masa/radio solo
    // al cambiar de cuerpo seleccionado (para no pisar lo que el usuario
    // esta escribiendo, ni una unidad que haya elegido a mano para ESTE
    // cuerpo mientras lo sigue inspeccionando). Comparado por b.id
    // (estable), no por indice -- ver comentario en GuiState::lastSelectedId.
    if (gui.lastSelectedId != b.id) {
        std::strncpy(gui.nameBuf, b.name.c_str(), sizeof(gui.nameBuf) - 1);
        gui.nameBuf[sizeof(gui.nameBuf) - 1] = '\0';
        gui.massUnit        = AutoMassUnit(b.mass);
        gui.radiusUnit      = AutoRadiusUnit(b.radius);
        gui.lastSelectedId  = b.id;
    }

    // ── Cabecera: preview isometrico 3D + nombre editable ──
    {
        float pw = ImGui::GetContentRegionAvail().x;
        // Altura proporcional al ancho del panel (relacion 200:256 del RT)
        float ph = pw * (240.0f / 320.0f);  // relacion 4:3 del RT 320×240
        GetPreviewRenderer().ShowInspector(pw, ph);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", gui.nameBuf, sizeof(gui.nameBuf)))
            b.name = gui.nameBuf;
    }

    ImGui::Separator();

    // Flags pre-calculados para tabs condicionales (declarados aqui para
    // que esten disponibles en toda la estructura del TabBar de abajo)
    bool hasAtmo      = b.atmosphereDensity > 0.001f;
    bool hasClouds    = (b.isRockyPlanet && b.rockyPlanet.cloudDensity > 0.001f) || b.cloudTex != nullptr;
    bool hasSolidComp = !b.solid_composition.empty();
    bool hasAtmoComp  = !b.atmospheric_composition.empty();
    bool hasHydro     = b.isRockyPlanet && b.volatileBudget > 0.0f;
    // Objeto estelar (estrella viva o remanente compacto): para estos, "Material"
    // (rocoso/gaseoso/…) no aplica y su composición NO es atmosférica sino estelar.
    bool isStellarObject = b.isStar || b.isSupernovaRemnant
        || b.stellarPhase == StellarPhase::NEUTRON_STAR
        || b.stellarPhase == StellarPhase::BLACK_HOLE
        || b.stellarPhase == StellarPhase::PULSAR
        || b.stellarPhase == StellarPhase::MAGNETAR;

    if (ImGui::BeginTabBar("##inspTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {

    // ── General ──────────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("General")) {
        // ── Edad del objeto (global, aplica a todo) ──
        {
            // Para estrellas y remanentes compactos se usa stellarAge (edad cosmica,
            // inicializada en catalog.h a partir de la masa).
            // Para planetas/rocas/etc. se usa bodyAge, que en catalog.h se inicializara
            // con la edad real del objeto (datos por sesion); si bodyAge == 0 el objeto
            // acaba de nacer en la simulacion (colision, eyeccion, spawn manual).
            bool useStellarAge = b.isStar || b.isSupernovaRemnant
                || b.stellarPhase == StellarPhase::NEUTRON_STAR
                || b.stellarPhase == StellarPhase::BLACK_HOLE
                || b.stellarPhase == StellarPhase::PULSAR
                || b.stellarPhase == StellarPhase::MAGNETAR;
            double ageS = useStellarAge ? b.stellarAge : b.bodyAge;

            auto FormatAge = [](char* buf, int sz, double s) {
                double yr = s / 3.15576e7;
                if      (s   < 60.0)    snprintf(buf, sz, "%.1f s",   s);
                else if (s   < 3600.0)  snprintf(buf, sz, "%.1f min", s / 60.0);
                else if (s   < 86400.0) snprintf(buf, sz, "%.1f h",   s / 3600.0);
                else if (s   < 3.15576e7) snprintf(buf, sz, "%.1f dias", s / 86400.0);
                else if (yr  < 1000.0)  snprintf(buf, sz, "%.2f anos", yr);
                else if (yr  < 1.0e6)   snprintf(buf, sz, "%.3f Kyr",  yr / 1000.0);
                else if (yr  < 1.0e9)   snprintf(buf, sz, "%.3f Myr",  yr / 1.0e6);
                else                    snprintf(buf, sz, "%.3f Gyr",  yr / 1.0e9);
            };

            char ageBuf[64];
            FormatAge(ageBuf, sizeof(ageBuf), ageS);
            ImGui::Text("Edad: %s", ageBuf);
        }
        ImGui::Separator();

        const char* matStr = b.material == MAT_ICY      ? "Helado"
                            : b.material == MAT_GASEOUS  ? "Gaseoso"
                            : b.material == MAT_METALLIC ? "Metalico" : "Rocoso";
        // "Material" es una clasificación planetaria: no se muestra para estrellas
        // ni remanentes compactos (un agujero negro no es "Gaseoso").
        if (!isStellarObject)
            ImGui::Text("Material: %s", matStr);
        ImGui::Text("Temperatura: %.0f K", b.temperature);
        ImGui::Text("Marea: %.2f   Dano: %.0f%%", b.tideStretch, (float)b.tidalDamage * 100.0f);
        if (b.tidalLock > 0.02f)
            ImGui::Text("Bloqueo de marea: %.0f%%", b.tidalLock * 100.0f);
        // IMPORTANTE: el chequeo de estrella va PRIMERO. Antes era un
        // else-if detras de 'accreteCount > 0', asi que cualquier
        // estrella que alguna vez acrecionara un cuerpo (cometa,
        // asteroide, fragmento -- algo casi garantizado con el tiempo en
        // una simulacion activa) perdia el panel de ciclo de vida PARA
        // SIEMPRE, reemplazado por la linea de acrecion -- la fase
        // mostrada quedaba congelada en lo ultimo visto (a menudo
        // "Secuencia Principal") sin importar que tan lejos evolucionara
        // realmente la estrella despues.
        if (b.isStar || b.isSupernovaRemnant
            || b.stellarPhase == StellarPhase::NEUTRON_STAR
            || b.stellarPhase == StellarPhase::BLACK_HOLE
            || b.stellarPhase == StellarPhase::PULSAR
            || b.stellarPhase == StellarPhase::MAGNETAR) {
            // ── Inspector de ciclo de vida estelar ──
            static const char* phaseNames[] = {
                "Protoestrella", "Secuencia Principal", "Subgigante",
                "Gigante Roja", "Supergigante", "Nebulosa Planetaria",
                "Supernova", "Enana Blanca", "Remanente de SN",
                "Enana Azul (hipotetica)",
                // indices 10-14: fases masa intermedia + hipotetica
                "Flash de Helio",
                "Rama Horizontal",
                "Rama Gigante Asintotica (AGB)",
                "Pulsos Termicos (AGB tardia)",
                "Enana Negra (hipotetica)",
                // indices 15-16: remanentes compactos post-SN
                "Estrella de Neutrones",
                "Agujero Negro",
                // indices 17-18: subtipos de estrella de neutrones activos
                "Pulsar",
                "Magnetar"
            };
            // Orden de EVOLUCION/visualizacion para el ComboBox -- distinto
            // del orden numerico del enum (fases nuevas se agregaron al
            // final para no renumerar valores existentes; aqui se ponen en
            // orden evolutivo correcto). phaseNames[] sigue indexado por
            // valor de enum.
            static const StellarPhase phaseDisplayOrder[] = {
                // Ruta comun
                StellarPhase::PROTOSTAR,    StellarPhase::MAIN_SEQUENCE,
                StellarPhase::SUBGIANT,
                // Ruta enana roja (hipotetica)
                StellarPhase::BLUE_DWARF,
                // Ruta masa intermedia (Sol)
                StellarPhase::RED_GIANT,
                StellarPhase::HELIUM_FLASH, StellarPhase::HORIZONTAL_BRANCH,
                StellarPhase::AGB,          StellarPhase::THERMAL_PULSES,
                StellarPhase::PLANETARY_NEBULA,
                StellarPhase::WHITE_DWARF,  StellarPhase::BLACK_DWARF,
                // Ruta masiva: TERMINA en supergigante. Los objetos compactos
                // (estrella de neutrones/pulsar/magnetar/agujero negro) NO son
                // fases seleccionables ni forzables -- solo se alcanzan detonando
                // la supernova (botón), que decide el tipo según la masa.
                StellarPhase::SUPERGIANT,
            };
            int phaseIdx = std::clamp((int)b.stellarPhase, 0, 18);
            // Durante la supernova (evento, no fase) mostrar el objeto compacto destino
            auto currentPhaseName = [&]() -> const char* {
                if (b.stellarPhase == StellarPhase::SUPERNOVA) {
                    double mR = std::max(0.08, b.initialStellarMass / M_SUN);
                    return (b.compactRemnantType == 2 || mR >= 20.0)
                        ? "Agujero Negro (en formacion)" : "Pulsar (en formacion)";
                }
                return phaseNames[phaseIdx];
            };
            ImGui::Text("Fase: %s", currentPhaseName());

            // Inspector estelar
            if (b.isStar) {
                bool isCompactRemnant = (b.stellarPhase == StellarPhase::NEUTRON_STAR
                    || b.stellarPhase == StellarPhase::PULSAR
                    || b.stellarPhase == StellarPhase::MAGNETAR
                    || b.stellarPhase == StellarPhase::BLACK_HOLE);

                double mR  = std::max(0.08, b.initialStellarMass / M_SUN);
                double tMS = 1.0e10 * 3.156e7 * std::pow(mR, -2.5);

                if (!isCompactRemnant) {
                    // Clase espectral aproximada (no aplica a remanentes compactos)
                    const char* specClass = "?";
                    double T = b.temperature;
                    if      (T < 3700)  specClass = "M";
                    else if (T < 5200)  specClass = "K";
                    else if (T < 6000)  specClass = "G";
                    else if (T < 7500)  specClass = "F";
                    else if (T < 10000) specClass = "A";
                    else if (T < 30000) specClass = "B";
                    else                specClass = "O";
                    ImGui::Text("Clase: %s  |  Vida MS: %.1f Gyr", specClass, tMS / 3.156e16);
                    ImGui::Separator();

                    // Edad absoluta (stellarAge) al inicio de cada fase
                    auto phaseEntryAge = [tMS](StellarPhase p) -> double {
                        switch (p) {
                        case StellarPhase::PROTOSTAR:          return 0.0;
                        case StellarPhase::MAIN_SEQUENCE:      return tMS * 0.005;
                        case StellarPhase::SUBGIANT:           return tMS * 0.90;
                        case StellarPhase::RED_GIANT:
                        case StellarPhase::SUPERGIANT:         return tMS * 0.95;
                        case StellarPhase::HELIUM_FLASH:       return tMS * 1.10;
                        case StellarPhase::HORIZONTAL_BRANCH:  return tMS * 1.101;
                        case StellarPhase::AGB:                return tMS * 1.111;
                        case StellarPhase::THERMAL_PULSES:     return tMS * 1.151;
                        case StellarPhase::PLANETARY_NEBULA:   return tMS * 1.171;
                        case StellarPhase::SUPERNOVA:          return tMS * 1.00;
                        case StellarPhase::WHITE_DWARF:        return tMS * 1.191;
                        case StellarPhase::BLACK_DWARF:        return tMS * 2.5;
                        case StellarPhase::SUPERNOVA_REMNANT:  return tMS * 1.05;
                        case StellarPhase::NEUTRON_STAR:
                        case StellarPhase::PULSAR:
                        case StellarPhase::MAGNETAR:
                        case StellarPhase::BLACK_HOLE:         return tMS * 1.06;
                        case StellarPhase::BLUE_DWARF:         return tMS * 1.0;
                        default: return 0.0;
                        }
                    };
                    // Edad representativa para saltar a una fase via ComboBox
                    auto phaseRepAge = [tMS](StellarPhase p) -> double {
                        switch (p) {
                        case StellarPhase::PROTOSTAR:          return tMS * 0.002;
                        case StellarPhase::MAIN_SEQUENCE:      return tMS * 0.45;
                        case StellarPhase::SUBGIANT:           return tMS * 0.925;
                        case StellarPhase::RED_GIANT:          return tMS * 1.025;
                        case StellarPhase::SUPERGIANT:         return tMS * 0.975;
                        case StellarPhase::HELIUM_FLASH:       return tMS * 1.100;
                        case StellarPhase::HORIZONTAL_BRANCH:  return tMS * 1.106;
                        case StellarPhase::AGB:                return tMS * 1.131;
                        case StellarPhase::THERMAL_PULSES:     return tMS * 1.161;
                        case StellarPhase::PLANETARY_NEBULA:   return tMS * 1.180;
                        case StellarPhase::SUPERNOVA:          return tMS * 1.005;
                        case StellarPhase::WHITE_DWARF:        return tMS * 1.200;
                        case StellarPhase::BLACK_DWARF:        return tMS * 3.0;
                        case StellarPhase::SUPERNOVA_REMNANT:  return tMS * 1.07;
                        case StellarPhase::NEUTRON_STAR:
                        case StellarPhase::PULSAR:
                        case StellarPhase::MAGNETAR:
                        case StellarPhase::BLACK_HOLE:         return tMS * 1.08;
                        case StellarPhase::BLUE_DWARF:         return tMS * 1.02;
                        default: return 0.0;
                        }
                    };

                    // Auto-evolución: ON por defecto. Editar edad/fase abajo NO
                    // la detiene -- son "saltos" puntuales, la simulacion sigue
                    // avanzando desde el nuevo punto. Solo esta casilla la
                    // congela de verdad (stellarManualOverride).
                    bool autoEvo = !b.stellarManualOverride;
                    if (ImGui::Checkbox("Auto-evolucion", &autoEvo))
                        b.stellarManualOverride = !autoEvo;

                    double ageMyrs = b.stellarAge / 1.0e6;
                    double tMSmyrs = tMS / 1.0e6;
                    double maxAge  = tMSmyrs * 2.0;
                    ImGui::Text("Edad estelar:");
                    ImGui::SetNextItemWidth(-1);
                    double minAge = 0.0;
                    if (ImGui::SliderScalar("##stellarAge", ImGuiDataType_Double,
                                            &ageMyrs, &minAge, &maxAge, "%.1f Myr")) {
                        b.stellarAge = ageMyrs * 1.0e6;
                        double mRatio = std::max(0.08, b.initialStellarMass / M_SUN);
                        StellarPhase newPhase;
                        if (b.stellarAge < tMS * 0.005)
                            newPhase = StellarPhase::PROTOSTAR;
                        else if (b.stellarAge < tMS * 0.90)
                            newPhase = StellarPhase::MAIN_SEQUENCE;
                        else if (b.stellarAge < tMS * 0.95)
                            newPhase = StellarPhase::SUBGIANT;
                        else if (mRatio >= b.effectiveSNThreshold) {
                            newPhase = StellarPhase::SUPERGIANT; // SN solo via boton
                        } else {
                            if      (b.stellarAge < tMS * 1.10)  newPhase = StellarPhase::RED_GIANT;
                            else if (b.stellarAge < tMS * 1.101) newPhase = StellarPhase::HELIUM_FLASH;
                            else if (b.stellarAge < tMS * 1.111) newPhase = StellarPhase::HORIZONTAL_BRANCH;
                            else if (b.stellarAge < tMS * 1.151) newPhase = StellarPhase::AGB;
                            else if (b.stellarAge < tMS * 1.171) newPhase = StellarPhase::THERMAL_PULSES;
                            else if (b.stellarAge < tMS * 1.191) newPhase = StellarPhase::PLANETARY_NEBULA;
                            else                                  newPhase = StellarPhase::WHITE_DWARF;
                        }
                        b.stellarPhase    = newPhase;
                        b.stellarPhaseAge = std::max(0.0, b.stellarAge - phaseEntryAge(newPhase));
                    }

                    // ComboBox de fase
                    ImGui::Text("Fase manual:");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##phaseCombo", currentPhaseName())) {
                        for (StellarPhase p : phaseDisplayOrder) {
                            int i = (int)p;
                            if (!IsStellarPhaseValid(b, p)) continue;
                            bool sel = (phaseIdx == i);
                            if (ImGui::Selectable(phaseNames[i], sel)) {
                                double repAge     = phaseRepAge(p);
                                b.stellarPhase    = p;
                                b.stellarAge      = repAge;
                                b.stellarPhaseAge = std::max(0.0, repAge - phaseEntryAge(p));
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    // Boton detonar supernova
                    bool canSupernova = IsStellarPhaseValid(b, StellarPhase::SUPERNOVA);
                    if (canSupernova) {
                        if (ImGui::Button("Detonar Supernova", {-1, 0})) {
                            b.stellarPhase        = StellarPhase::SUPERNOVA;
                            b.stellarPhaseAge     = 0.0;
                            b.supernovaProgress   = 0.0;
                            b.supernovaRadius     = 0.0;
                        }
                    } else {
                        ImGui::BeginDisabled();
                        ImGui::Button("Detonar Supernova (masa insuf.)", {-1, 0});
                        ImGui::EndDisabled();
                    }
                } // end !isCompactRemnant

                // Propiedades fisicas ESPECIFICAS de estrella (temperatura, masa
                // y radio ya se muestran abajo en el bloque general comun a todos
                // los cuerpos -- no duplicarlas aqui).
                ImGui::Separator();
                ImGui::Text("Luminosidad: %.3f Lsol", b.luminosity / L_SUN);
                if (b.pulsationAmplitude > 0.01f)
                    ImGui::Text("Pulsacion: +-%.0f%%  |  Actividad: %.2f",
                        b.pulsationAmplitude * 100.0f, b.stellarActivity);
                ImGui::Text("Metalicidad Z: %.4f  |  Rot.crit: %.2f",
                    b.metallicityZ, b.criticalRotationFraction);
                if (b.supernovaProgress > 0.001)
                    ImGui::Text("SN progreso: %.1f%%  |  Radio shell: %.2e m",
                        b.supernovaProgress * 100.0, b.supernovaRadius);
            }
        } // end isStar / isSupernovaRemnant
        else if (b.accreteCount > 0)
            ImGui::Text("Acrecion: %d cuerpos", b.accreteCount);

        ImGui::Separator();

        // ── Propiedades fisicas ──────────────────────────────────────────
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##massUnit", massUnitNames[gui.massUnit])) {
            for (int i = 0; i < MASS_UNIT_COUNT; ++i) {
                bool sel = (gui.massUnit == i);
                if (ImGui::Selectable(massUnitNames[i], sel)) gui.massUnit = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Masa (%s)", massUnitNames[gui.massUnit]);
        double massDisplay = b.mass / massUnitDiv[gui.massUnit];
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputDouble("##mass", &massDisplay, 0.0, 0.0, "%.6g")) {
            double newMass = std::max(1.0, massDisplay * massUnitDiv[gui.massUnit]);
            b.mass       = newMass;
            b.intactMass = newMass;
        }

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##radiusUnit", radiusUnitNames[gui.radiusUnit])) {
            for (int i = 0; i < RADIUS_UNIT_COUNT; ++i) {
                bool sel = (gui.radiusUnit == i);
                if (ImGui::Selectable(radiusUnitNames[i], sel)) gui.radiusUnit = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Text("Radio (%s)", radiusUnitNames[gui.radiusUnit]);
        double radiusDisplay = b.radius / radiusUnitDiv[gui.radiusUnit];
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputDouble("##radius", &radiusDisplay, 0.0, 0.0, "%.6g"))
            b.radius = std::max(1.0, radiusDisplay * radiusUnitDiv[gui.radiusUnit]);

        double density = b.mass / ((4.0 / 3.0) * PI_D * std::pow(b.radius, 3));
        ImGui::Text("Densidad: %.0f kg/m3 (%.2f g/cm3)", density, density / 1000.0);

        // ── Campo Magnetico ──────────────────────────────────────────────
        if (b.magneticFieldStrength > 0.0) {
            ImGui::Separator();
            double B = b.magneticFieldStrength;
            char Bbuf[48];
            if      (B < 1.0e-6)  snprintf(Bbuf, sizeof(Bbuf), "%.2f nT",  B * 1.0e9);
            else if (B < 1.0e-3)  snprintf(Bbuf, sizeof(Bbuf), "%.2f µT",  B * 1.0e6);
            else if (B < 1.0)     snprintf(Bbuf, sizeof(Bbuf), "%.3f mT",  B * 1.0e3);
            else if (B < 1.0e3)   snprintf(Bbuf, sizeof(Bbuf), "%.3f T",   B);
            else if (B < 1.0e6)   snprintf(Bbuf, sizeof(Bbuf), "%.2f kT",  B * 1.0e-3);
            else if (B < 1.0e9)   snprintf(Bbuf, sizeof(Bbuf), "%.2f MT",  B * 1.0e-6);
            else                  snprintf(Bbuf, sizeof(Bbuf), "%.2e T",   B);
            ImGui::Text("Campo mag.: %s", Bbuf);
            if (b.magneticAxisTilt > 0.5f)
                ImGui::Text("Incl. eje mag.: %.1f deg", b.magneticAxisTilt);
            else
                ImGui::Text("Incl. eje mag.: ~0 (alineado)");
        }

        ImGui::EndTabItem();
    } // end General tab

    // ── Movimiento ────────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Movimiento")) {
        ImGui::Text("Velocidad: %.2f km/s", b.vel.length() / 1000.0);
        ImGui::Text("Periodo orbital: %s", OrbitalPeriod(b, bodies).c_str());
        ImGui::Text("Periodo de rotacion: %s", RotationPeriod(b.spinRateDeg).c_str());
        ImGui::EndTabItem();
    }

    // ── Composicion ───────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Composicion")) {
        bool anyComp = hasSolidComp || hasAtmoComp || hasHydro;
        if (!anyComp) {
            ImGui::TextDisabled("Sin datos de composicion");
        } else {
            if (hasSolidComp) {
                ImGui::TextDisabled("Corteza / Manto");
                DrawCompositionList(b.solid_composition);
            }
            if (hasAtmoComp) {
                if (hasSolidComp) ImGui::Separator();
                // Para estrellas/remanentes el mapa guarda composición estelar
                // estimada (nucleo/capas), no una atmósfera planetaria.
                ImGui::TextDisabled(isStellarObject ? "Composicion estelar estimada"
                                                    : "Atmosfera");
                DrawCompositionList(b.atmospheric_composition);
            }
            if (hasHydro) {
                if (hasSolidComp || hasAtmoComp) ImGui::Separator();
                ImGui::TextDisabled("Hidrosfera / Volatiles");
                float liquid = ClampF(
                    b.volatileBudget - b.iceFraction - b.vaporFraction,
                    0.0f, b.volatileBudget
                );
                ImGui::Text("Volatiles totales: %.2f%%", b.volatileBudget * 100.0f);
                ImGui::Text("Agua liquida:      %.2f%%", liquid * 100.0f);
                ImGui::Text("Hielo:             %.2f%%", b.iceFraction * 100.0f);
                ImGui::Text("Vapor:             %.2f%%", b.vaporFraction * 100.0f);
                ImGui::Text("Nivel visual mar:  %.2f%%", b.rockyPlanet.waterLevel * 100.0f);
            }
        }
        ImGui::EndTabItem();
    }

    // ── Visuales ──────────────────────────────────────────────────────────
    if (ImGui::BeginTabItem("Visuales")) {
        if (!hasAtmo && !hasClouds) {
            ImGui::TextDisabled("Sin elementos visuales editables");
        } else {
            if (hasAtmo) {
                bool show = !b.hideAtmosphere;
                if (ImGui::Checkbox("Mostrar atmosfera", &show)) b.hideAtmosphere = !show;
            }
            if (hasClouds) {
                bool show = !b.hideClouds;
                if (ImGui::Checkbox("Mostrar nubes", &show)) b.hideClouds = !show;
            }
        }
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    } // end BeginTabBar

    ImGui::End();
}

// Vacia la simulacion: borra todos los cuerpos, libera el campo de polvo
// (mismo patron que CTRL+D en camera_input.h: 'active=false' por slot, sin
// destruir la preasignacion), limpia la seleccion y reinicia la camara
// orbital a sus valores por defecto (OrbitCamera::OrbitCamera).
inline void ResetSimulation(std::vector<Body>& bodies, std::vector<DustParticle>& dustField,
                             OrbitCamera& cam, InputState& input)
{
    bodies.clear();
    for (DustParticle& d : dustField) d.active = false;

    input.selectedBodyIdx = -1;
    input.selectedBodyId  = 0;
    input.followSelected  = false;

    cam.radius = 30.0f;
    cam.angleX = 0.4f;
    cam.angleY = 0.35f;
    cam.target = {0, 0, 0};
    cam.Update();
}

// ── Menu principal (hamburguesa, esquina superior izquierda) ───
inline void DrawMainMenu(GuiState& gui, std::vector<Body>& bodies,
                          std::vector<DustParticle>& dustField,
                          OrbitCamera& cam, InputState& input)
{
    const float pad  = 12.0f;
    const float size = 40.0f;

    ImGui::SetNextWindowPos(ImVec2(pad, pad));
    ImGui::SetNextWindowSize(ImVec2(size + 16.0f, size + 16.0f));
    ImGui::Begin("##MainMenuButton", nullptr, HUD_FLAGS);

    bool clicked = ImGui::Button("##hamburger", ImVec2(size, size));
    ImVec2 rmin = ImGui::GetItemRectMin();
    ImVec2 rmax = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cx = (rmin.x + rmax.x) * 0.5f;
    float cy = (rmin.y + rmax.y) * 0.5f;
    for (int i = -1; i <= 1; ++i)
        dl->AddLine(ImVec2(cx - 10.0f, cy + i * 7.0f), ImVec2(cx + 10.0f, cy + i * 7.0f),
                     IM_COL32(230, 230, 230, 255), 2.0f);
    if (clicked) gui.showMainMenu = !gui.showMainMenu;

    ImGui::End();

    if (!gui.showMainMenu) return;

    ImGui::SetNextWindowPos(ImVec2(pad, pad + size + 16.0f + 8.0f));
    ImGui::SetNextWindowSize(ImVec2(300.0f, 300.0f));
    ImGui::Begin("Menu Principal", &gui.showMainMenu, ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(0.36f, 0.45f, 0.93f, 1.0f), "GRITADOR UNIVERSE");
    ImGui::TextColored(ImVec4(0.36f, 0.45f, 0.93f, 1.0f), "Edición mórbida");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Nueva Simulacion Vacia", ImVec2(-1, 32)))
        ResetSimulation(bodies, dustField, cam, input);

    if (ImGui::Button("Eliminar Simulacion Actual", ImVec2(-1, 32)))
        ResetSimulation(bodies, dustField, cam, input);

    // Plantilla: Sistema Solar Realista (ver LoadRealisticSolarSystem en
    // solar_system_template.h, llamada desde main.cpp porque necesita
    // Database/tex, que gui.h no conoce). Borra la simulacion actual --
    // pide confirmacion antes, igual que cualquier accion destructiva.
    if (ImGui::Button("Cargar Sistema Solar Real", ImVec2(-1, 32)))
        ImGui::OpenPopup("ConfirmarSistemaSolar");

    if (ImGui::BeginPopupModal("ConfirmarSistemaSolar", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Esto borra la simulacion actual y carga");
        ImGui::TextUnformatted("todos los cuerpos reales en sus orbitas,");
        ImGui::TextUnformatted("mas los cinturones de asteroides/Kuiper/Oort.");
        ImGui::Spacing();
        if (ImGui::Button("Cargar", ImVec2(120, 0))) {
            gui.requestLoadRealisticSystem = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Buscador: lista/filtra TODOS los cuerpos de la escena (sin anillos
    // ni polvo -- eso es DustParticle, ni siquiera pasa por aqui) y al
    // clickear uno mueve la camara hacia el y lo deja como objetivo de
    // seguimiento (ver DrawObjectSearchPanel mas abajo).
    if (ImGui::Button("Buscar Objetos", ImVec2(-1, 32)))
        gui.showObjectSearch = !gui.showObjectSearch;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Checkbox("Iluminacion ambiental (falsa)", &g_fakeLightEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Desactivada: solo iluminan estrellas reales y\ncuerpos lo bastante calientes como para brillar\npor si mismos -- todo lo demas queda en negro.");

    ImGui::Spacing();
    ImGui::Text("Velocidad de movimiento (WASD/QE)");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##panSpeed", &cam.panSpeedFactor, 0.05f, 2.0f, "%.2fx");

    ImGui::Spacing();
    ImGui::Text("Velocidad de rotacion (arrastre)");
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##rotateSpeed", &cam.rotateSpeedFactor, 0.1f, 2.0f, "%.2fx");

    ImGui::End();
}

// ── Buscador de objetos en escena ───────────────────────────
// Lista TODOS los Body activos (planetas/lunas/estrellas/asteroides --
// nunca anillos ni polvo, que ni siquiera son Body, ver DustParticle en
// body.h) con un filtro de texto por nombre. Clickear uno lo selecciona,
// lo deja como objetivo de seguimiento (igual que doble-click sobre el en
// la escena) y mueve la camara hacia el de inmediato -- pensado para
// cuerpos lejanos/pequenos que son dificiles o imposibles de encontrar a
// ojo (Neptuno, Sedna, un asteroide perdido en el cinturon...). Excluye
// fragmentos de colision (b.isFragment): no tiene sentido "buscar" entre
// cientos de esquirlas sin nombre util.
inline void DrawObjectSearchPanel(GuiState& gui, InputState& input, OrbitCamera& cam,
                                    std::vector<Body>& bodies)
{
    if (!gui.showObjectSearch) return;

    const float pad  = 12.0f;
    const float winW = 320.0f;
    const float winH = 420.0f;
    ImGui::SetNextWindowPos(ImVec2(pad, (float)GetScreenHeight() - winH - pad), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
    ImGui::Begin("Buscar Objetos", &gui.showObjectSearch, ImGuiWindowFlags_NoCollapse);

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##objSearch", "Buscar por nombre...", gui.searchBuf, sizeof(gui.searchBuf));
    ImGui::Separator();

    std::string filter = gui.searchBuf;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });

    ImGui::BeginChild("##ObjSearchList", ImVec2(0, 0), false);
    for (int i = 0; i < (int)bodies.size(); ++i) {
        const Body& b = bodies[i];
        if (b.mass <= 0.0 || b.isFragment) continue;

        if (!filter.empty()) {
            std::string lname = b.name;
            std::transform(lname.begin(), lname.end(), lname.begin(),
                            [](unsigned char c) { return (char)std::tolower(c); });
            if (lname.find(filter) == std::string::npos) continue;
        }

        ImGui::PushID(i);

        const float iconR = 8.0f;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(cursor.x + iconR, cursor.y + iconR), iconR,
            IM_COL32(b.color.r, b.color.g, b.color.b, 255));
        ImGui::Dummy(ImVec2(iconR * 2.0f + 6.0f, iconR * 2.0f));
        ImGui::SameLine();

        bool selected = (i == input.selectedBodyIdx);
        if (ImGui::Selectable(b.name.c_str(), selected, 0, ImVec2(0, iconR * 2.0f))) {
            input.selectedBodyIdx = i;
            input.selectedBodyId  = b.id;
            input.followSelected  = true;
            cam.FocusOn(b.pos, b.radius);
            gui.showObjectSearch = false;
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::End();
}
