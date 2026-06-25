#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "imgui.h"
#include "raylib.h"
#include "constants.h"
#include "body.h"
#include "camera_input.h"

// ============================================================
//  GUI estilo "Universe Sandbox" (Dear ImGui + rlImGui)
// ============================================================

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

    // Pestana activa del menu de catalogo: 0=Todos, 1=Estrellas, 2=Planetas,
    // 3=Lunas, 4=Menores (ver BodyCategory en body.h y DrawCatalogMenu).
    int  catalogCategory = 0;

    // ── Inspector de propiedades (DrawObjectInspector) ──
    // Indice del cuerpo seleccionado la ultima vez que se dibujo el
    // inspector: al cambiar, se resincroniza 'nameBuf' con b.name (para no
    // pisar lo que el usuario esta escribiendo mientras edita).
    int  lastSelectedIdx = -1;
    char nameBuf[64]     = "";

    // Unidad de masa mostrada/editada: 0=kg, 1=Masas Terrestres, 2=Masas Lunares.
    int  massUnit = 0;
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
    return rt>=128?"x128":rt>=64?"x64":rt>=32?"x32":rt>=16?"x16":rt>=8?"x8":
           rt>=4?"x4":rt>=2?"x2":rt>=1?"x1":rt>=0.5?"x1/2":rt>=0.25?"x1/4":
           rt>=0.125?"x1/8":rt>=0.0625?"x1/16":rt>=0.03125?"x1/32":"x1/64";
}

// ── HUD de controles de tiempo (esquina inferior izquierda) ─
inline void DrawTimeControlsHUD(GuiState& gui, bool& paused) {
    (void)gui;
    const float pad = 12.0f;
    ImVec2 winSize(280.0f, 56.0f);
    ImGui::SetNextWindowPos(ImVec2(pad, (float)GetScreenHeight() - winSize.y - pad));
    ImGui::SetNextWindowSize(winSize);
    ImGui::Begin("##TimeControlsHUD", nullptr, HUD_FLAGS);

    if (ImGui::Button(paused ? "Reanudar" : "Pausar", ImVec2(80, 32)))
        paused = !paused;

    ImGui::SameLine();
    if (ImGui::Button("<<", ImVec2(36, 32)))
        TIME_STEP = std::max(TIME_STEP * 0.5, 1200.0 / 64.0);

    ImGui::SameLine();
    if (ImGui::Button(">>", ImVec2(36, 32)))
        TIME_STEP = std::min(TIME_STEP * 2.0, 1200.0 * 128.0);

    ImGui::SameLine();
    if (ImGui::Button(TimeScaleLabel(), ImVec2(70, 32)))
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
    const float winH    = 280.0f;
    const float winW    = std::min(920.0f, (float)GetScreenWidth() - 2.0f * pad);
    const float bottomY = (float)GetScreenHeight() - 56.0f - 2.0f * pad - winH;

    ImGui::SetNextWindowPos(ImVec2(((float)GetScreenWidth() - winW) * 0.5f, bottomY));
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
        const float cellSize = 84.0f;
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

            ImVec2 center((rmin.x + rmax.x) * 0.5f, rmin.y + (cellSize - 22.0f) * 0.5f);
            dl->AddCircleFilled(center, 16.0f, IM_COL32(item.color.r, item.color.g, item.color.b, 255));

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

// ── Panel de Propiedades del Objeto (inspector, anclado a la derecha) ─
inline void DrawObjectInspector(GuiState& gui, Body& b,
                                 const std::vector<Body>& bodies, int selectedIdx)
{
    const float winW = 300.0f;
    const float pad  = 12.0f;
    ImGui::SetNextWindowPos(ImVec2((float)GetScreenWidth() - winW - pad, pad), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, (float)GetScreenHeight() - 2.0f * pad), ImGuiCond_Always);
    ImGui::Begin("Propiedades del Objeto", nullptr, ImGuiWindowFlags_NoCollapse);

    // Resincroniza el buffer del nombre solo al cambiar de cuerpo
    // seleccionado (para no pisar lo que el usuario esta escribiendo).
    if (gui.lastSelectedIdx != selectedIdx) {
        std::strncpy(gui.nameBuf, b.name.c_str(), sizeof(gui.nameBuf) - 1);
        gui.nameBuf[sizeof(gui.nameBuf) - 1] = '\0';
        gui.lastSelectedIdx = selectedIdx;
    }

    // ── Cabecera: icono de color + nombre editable ──
    {
        ImDrawList* dl     = ImGui::GetWindowDrawList();
        ImVec2 cursor      = ImGui::GetCursorScreenPos();
        const float iconR  = 14.0f;
        dl->AddCircleFilled(ImVec2(cursor.x + iconR, cursor.y + iconR), iconR,
                             IM_COL32(b.color.r, b.color.g, b.color.b, 255));
        ImGui::Dummy(ImVec2(iconR * 2.0f + 8.0f, iconR * 2.0f));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##name", gui.nameBuf, sizeof(gui.nameBuf)))
            b.name = gui.nameBuf;
    }

    ImGui::Separator();

    // ── Vision General ──
    if (ImGui::CollapsingHeader("Vision General", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* matStr = b.material == MAT_ICY      ? "Helado"
                            : b.material == MAT_GASEOUS  ? "Gaseoso"
                            : b.material == MAT_METALLIC ? "Metalico" : "Rocoso";
        ImGui::Text("Material: %s", matStr);
        ImGui::Text("Temperatura: %.0f K", b.temperature);
        ImGui::Text("Marea: %.2f   Dano: %.0f%%", b.tideStretch, (float)b.tidalDamage * 100.0f);
        if (b.tidalLock > 0.02f)
            ImGui::Text("Bloqueo de marea: %.0f%%", b.tidalLock * 100.0f);
        if (b.accreteCount > 0)
            ImGui::Text("Acrecion: %d cuerpos", b.accreteCount);
        else if (b.isStar)
            ImGui::Text("Edad: %.1f Myr", b.stellarAge / 1.0e6);
    }

    // ── Propiedades / Fisicas ──
    if (ImGui::CollapsingHeader("Propiedades / Fisicas", ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char*  massUnitNames[] = {"kg", "Masas Terrestres", "Masas Lunares"};
        static const double massUnitDiv[]   = {1.0, M_EARTH, M_MOON};

        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##massUnit", massUnitNames[gui.massUnit])) {
            for (int i = 0; i < 3; ++i) {
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

        ImGui::Text("Radio (km)");
        double radiusKm = b.radius / 1000.0;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputDouble("##radius", &radiusKm, 0.0, 0.0, "%.3f"))
            b.radius = std::max(1.0, radiusKm * 1000.0);

        double density = b.mass / ((4.0 / 3.0) * PI_D * std::pow(b.radius, 3));
        ImGui::Text("Densidad: %.0f kg/m3 (%.2f g/cm3)", density, density / 1000.0);
    }

    // ── Movimiento ──
    if (ImGui::CollapsingHeader("Movimiento", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Velocidad: %.2f km/s", b.vel.length() / 1000.0);
        ImGui::Text("Periodo orbital: %s", OrbitalPeriod(b, bodies).c_str());
    }

    // ── Composicion: dos inventarios separados (ver body.h) -- manto/
    //    nucleo SOLIDO vs. envoltura GASEOSA, cada uno suma ~1.0 por
    //    separado (real para cuerpos del catalogo, interpolado entre
    //    anclas reales para procedurales -- ver composition.h). ──
    if (ImGui::CollapsingHeader("Composicion Solida / Terrestre")) {
        DrawCompositionList(b.solid_composition);
    }
    if (ImGui::CollapsingHeader("Composicion Atmosferica")) {
        DrawCompositionList(b.atmospheric_composition);
    }

    // ── Visuales: toggles de atmosfera/nubes (ver hideAtmosphere/
    //    hideClouds en body.h) -- solo afectan el dibujado. ──
    bool hasAtmo   = b.atmosphereDensity > 0.001f;
    bool hasClouds = (b.isRockyPlanet && b.rockyPlanet.cloudDensity > 0.001f) || b.cloudTex != nullptr;
    if ((hasAtmo || hasClouds) && ImGui::CollapsingHeader("Visuales", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (hasAtmo) {
            bool show = !b.hideAtmosphere;
            if (ImGui::Checkbox("Mostrar atmosfera", &show)) b.hideAtmosphere = !show;
        }
        if (hasClouds) {
            bool show = !b.hideClouds;
            if (ImGui::Checkbox("Mostrar nubes", &show)) b.hideClouds = !show;
        }
    }

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
    ImGui::SetNextWindowSize(ImVec2(300.0f, 210.0f));
    ImGui::Begin("Menu Principal", &gui.showMainMenu, ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(0.36f, 0.45f, 0.93f, 1.0f), "GRITADOR UNIVERSE");
    ImGui::TextColored(ImVec4(0.36f, 0.45f, 0.93f, 1.0f), "Edición mórbida");
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Nueva Simulacion Vacia", ImVec2(-1, 32)))
        ResetSimulation(bodies, dustField, cam, input);

    if (ImGui::Button("Eliminar Simulacion Actual", ImVec2(-1, 32)))
        ResetSimulation(bodies, dustField, cam, input);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Checkbox("Iluminacion ambiental (falsa)", &g_fakeLightEnabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Desactivada: solo iluminan estrellas reales y\ncuerpos lo bastante calientes como para brillar\npor si mismos -- todo lo demas queda en negro.");

    ImGui::End();
}
