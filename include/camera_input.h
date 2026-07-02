#pragma once
#include "raylib.h"
#include "raymath.h"
#include "imgui.h"
#include "body.h"
#include "math_utils.h"
#include "ui.h"

// ============================================================
//  Cámara orbital e input de teclado/ratón
// ============================================================

struct OrbitCamera {
    Camera3D cam;
    float radius     = 30.0f;
    float angleX     = 0.4f;  // azimut
    float angleY     = 0.35f; // elevación
    Vector3D target  = {0, 0, 0};

    // ── Vuelo libre (WASD/QE) ────────────────────────────────────
    // 'panSpeedFactor' es el multiplicador configurable (slider en el
    // Menu Principal, ver gui.h DrawMainMenu) -- antes 1.5 fijo se
    // sentia demasiado rapido/abrupto sin forma de ajustarlo.
    // 'panVelocityPhys' es la velocidad SUAVIZADA actual (m/s, fisica):
    // en vez de saltar de golpe a velocidad maxima al presionar una
    // tecla (y frenar de golpe al soltarla), se acelera/frena
    // exponencialmente hacia la velocidad objetivo cada frame -- ver
    // HandlePan().
    float    panSpeedFactor   = 0.5f;
    Vector3D panVelocityPhys  = {0, 0, 0};

    // ── Rotación (arrastre con botón derecho) ───────────────────
    // Multiplicador configurable (slider en el Menu Principal, ver gui.h
    // DrawMainMenu) sobre la sensibilidad base de 0.005 rad/pixel --
    // antes era fija y se sentia demasiado rapida sin forma de
    // ajustarla, igual que pasaba con panSpeedFactor.
    float rotateSpeedFactor = 0.6f;

    // ── Vuelo suave de enfoque ("fly-to") ───────────────────────
    // Al seleccionar/colocar un cuerpo, en vez de teletransportar target/
    // radius de golpe (TrackBody/asignacion directa), se anima una
    // transicion breve y suave hacia el punto y zoom objetivo. Corre en
    // tiempo REAL (GetFrameTime), no simulado -- es un efecto de
    // camara/UI, debe sentirse igual de rapido sin importar TIME_STEP o
    // si la simulacion esta en pausa.
    bool      flying        = false;
    Vector3D  flyTargetGoal = {0, 0, 0};
    float     flyRadiusGoal = 30.0f;

    OrbitCamera() {
        cam.up         = {0, 1, 0};
        cam.fovy       = 45.0f;
        cam.projection = CAMERA_PERSPECTIVE;
    }

    // Inicia (o redirige, si ya estaba en vuelo) una transicion suave
    // hacia 'pos'. 'bodyRadiusPhys' > 0 tambien recalcula el zoom
    // objetivo (factor 6x el radio de dibujado, calibrado contra la
    // camara inicial por defecto: 30 draw-units para encuadrar el Sol);
    // <= 0 conserva el zoom actual (solo mueve el target, p.ej. un
    // reencuadre que no debe alterar el nivel de zoom elegido).
    void FocusOn(Vector3D pos, double bodyRadiusPhys = -1.0) {
        flyTargetGoal = pos;
        flyRadiusGoal = (bodyRadiusPhys > 0.0)
            ? ClampF((float)(bodyRadiusPhys * RENDER_SCALE) * 6.0f, 0.0001f, 2000000.0f)
            : radius;
        flying = true;
    }

    // Llamar UNA vez por frame (antes de Update()), con dt REAL
    // (GetFrameTime()). k=10 converge en ~0.3s -- "rapido pero suave"
    // sin sentirse como un salto brusco ni como una persecucion eterna.
    // Al llegar (distancia/diferencia de zoom despreciables) ajusta el
    // valor EXACTO y apaga 'flying', para no quedar oscilando por
    // redondeo de punto flotante para siempre.
    void UpdateFlight(float dt) {
        if (!flying) return;
        const float k = 10.0f;
        float a = 1.0f - std::exp(-k * dt);
        target = target + (flyTargetGoal - target) * (double)a;
        radius = radius + (flyRadiusGoal - radius) * a;

        double distLeft = (flyTargetGoal - target).length();
        bool arrived = distLeft < std::max(0.001, (double)radius * 0.002)
                    && std::fabs(flyRadiusGoal - radius) < flyRadiusGoal * 0.002f + 0.0001f;
        if (arrived) {
            target = flyTargetGoal;
            radius = flyRadiusGoal;
            flying = false;
        }
    }

    // Actualiza la posición de la cámara respecto a su target.
    // Origen flotante: el target de la cámara SIEMPRE se mapea al
    // {0,0,0} del espacio de dibujado (g_renderOrigin = target), asi
    // que cam.position = offset puro (radius * direccion), sin sumarle
    // un `t` potencialmente grande -> sin perdida de precision float32
    // del offset cuando radius es muy pequeño y target esta lejos del
    // origen del universo (p.ej. siguiendo un planeta en orbita).
    void Update() {
        g_renderOrigin = target;
        cam.target   = {0.0f, 0.0f, 0.0f};
        cam.position = {
            radius * cosf(angleY) * sinf(angleX),
            radius * sinf(angleY),
            radius * cosf(angleY) * cosf(angleX)
        };
    }

    // 'rotating': estado de "mouse capturado" mientras se arrastra con
    // el boton derecho.
    bool rotating = false;

    // Arrastra el botón derecho para rotar. Los dos ejes usan convenciones
    // DISTINTAS a proposito, confirmado por prueba directa del jugador:
    // - Horizontal (angleX): signo POSITIVO -- convencion "orbita de
    //   camara" (Blender/CAD): arrastrar a la derecha gira la camara
    //   alrededor del objetivo, el contenido se desliza en direccion
    //   CONTRARIA al arrastre.
    // - Vertical (angleY): signo NEGATIVO -- convencion "agarrar y
    //   arrastrar" (Google Earth/Sketchfab): arrastrar hacia arriba hace
    //   que el contenido suba CONTIGO.
    // (Se probo signo negativo tambien en horizontal -- "agarrar y
    // arrastrar" en ambos ejes -- pero se sintio invertido para el
    // horizontal especificamente; revertido solo ese eje.)
    //
    // 'uiBlocksStart' (mouseOverUI, ver main.cpp) solo bloquea EMPEZAR un
    // arrastre nuevo sobre un panel de ImGui -- el release (soltar el
    // boton) y el procesamiento del arrastre en curso se evaluan SIEMPRE,
    // sin importar donde este el mouse ese frame, para nunca dejar el
    // cursor capturado/oculto "pegado" si el mouse pasa sobre la UI a
    // mitad de un arrastre.
    //
    // DisableCursor()/EnableCursor() (raylib): oculta el cursor del
    // sistema y lo fija en su posicion (modo de movimiento relativo, el
    // mismo que usan los ejemplos de camara FPS de raylib) mientras dura
    // el arrastre. Sin esto, el cursor del SO se desplazaba visiblemente
    // por la pantalla y se topaba con el borde de la ventana/monitor en
    // arrastres largos -- al llegar al borde dejaba de generar delta (la
    // rotacion se "atascaba" en seco) en vez de seguir girando mientras
    // se siguiera arrastrando el mouse fisicamente.
    void HandleRotation(bool uiBlocksStart) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !uiBlocksStart) {
            rotating = true;
            DisableCursor();
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && rotating) {
            rotating = false;
            EnableCursor();
        }
        if (rotating) {
            Vector2 delta = GetMouseDelta();
            float   sens  = 0.005f * rotateSpeedFactor;
            angleX += delta.x * sens;
            angleY  = ClampF(angleY - delta.y * sens, -1.45f, 1.45f);
        }
    }

    // Rueda del ratón para zoom: factor multiplicativo constante por
    // "tick" de rueda -> el paso ABSOLUTO se reduce automaticamente a
    // medida que radius disminuye (zoom logaritmico, frena al acercarse).
    // Sin clamp inferior atado al radio del planeta: junto con el
    // nearPlane dinamico (ver main.cpp) permite zoom infinito sobre
    // objetos pequenos como asteroides sin clipping.
    void HandleZoom() {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            // El zoom manual interrumpe cualquier vuelo de enfoque en
            // curso -- sin esto, FocusOn() seguia empujando 'radius'
            // hacia su objetivo cada frame, "peleando" contra la rueda
            // del jugador.
            flying = false;
            // Tope subido de 50,000 a 2,000,000 (draw-units, RENDER_SCALE=1e-8m):
            // 50,000 apenas alcanzaba para ~33 AU (el borde de Neptuno) y solo
            // ~3.3x el radio de Stephenson 2-18 (~14,957 draw-units) -- ni
            // alejarse lo suficiente para ver la estrella completa con margen,
            // ni para encuadrar un sistema planetario construido a su alrededor.
            // farPlane (main.cpp, rlSetClipPlanes) ya escala con orbitCam.radius,
            // asi que no necesita tocarse aparte.
            radius = ClampF(radius * powf(0.88f, wheel), 0.0001f, 2000000.0f);
        }
    }

    // Si se está siguiendo un cuerpo, lo tomamos como target (snap
    // EXACTO, sin suavizado -- usado para el seguimiento CONTINUO de un
    // cuerpo en movimiento, no para el vuelo de enfoque inicial. Ver
    // FocusOn()/UpdateFlight() arriba para la transicion suave, y
    // main.cpp para como se combinan ambos: mientras 'flying' siga
    // activo se redirige el objetivo de vuelo en vez de llamar esto, y
    // una vez que el vuelo termina se vuelve a este snap exacto cuadro a
    // cuadro -- necesario para no acumular un frame de atraso cuando
    // TIME_STEP es muy alto).
    void TrackBody(const Body& b) {
        target = b.pos;
    }

    // Vuelo libre: WASD traslada 'target' (no solo rota/zoomea alrededor
    // de el) en el plano horizontal de la vista actual, Q/E sube/baja en
    // el eje mundial Y. Sin esto, la UNICA forma de "moverse" era
    // seleccionar un cuerpo y hacer zoom -- incomodo, y con cuerpos muy
    // lejos (Neptuno, Sedna...) practicamente imposible: para alcanzarlos
    // had que poder VERLOS primero para seleccionarlos, pero sin
    // movimiento libre no hay forma de acercar la vista hasta ellos.
    // La velocidad escala con 'radius' (el zoom actual) para que el
    // desplazamiento se sienta proporcional sin importar la escala (lento
    // y preciso de cerca, rapido de lejos) -- igual filosofia que
    // HandleZoom(). 'uiHasKeyboardFocus' (ImGui::GetIO().WantCaptureKeyboard,
    // ver main.cpp) evita que escribir un nombre con letras w/a/s/d/q/e en
    // el inspector tambien mueva la camara.
    //
    // 'isFollowing' (InputState::followSelected) bloquea el paneo POR
    // COMPLETO: si se esta siguiendo un cuerpo, ese cuerpo es el pivot --
    // trasladar 'target' lejos de el (lo que hacia la version anterior,
    // ignorando el seguimiento por completo) no tiene sentido, porque
    // TrackBody() lo pisa de vuelta al pos del cuerpo en el MISMO frame
    // de todos modos. Mirar alrededor del cuerpo seguido sigue
    // funcionando igual que siempre: arrastrar con el boton derecho
    // (HandleRotation) y la rueda (HandleZoom), ambos YA giran/alejan
    // alrededor de 'target' sin moverlo.
    //
    // 'panVelocityPhys' se SUAVIZA en vez de saltar a velocidad maxima
    // de golpe (y frenar de golpe al soltar la tecla) -- antes
    // 'if (!moved) return;' dejaba la velocidad como un escalon digital
    // perfecto (0 o maxima, sin transicion), lo que se sentia abrupto.
    // Ahora se acelera/frena exponencialmente hacia la velocidad
    // objetivo cada frame (k=8, converge en ~0.15s), corriendo SIEMPRE
    // (incluso sin teclas) para poder frenar suavemente tambien.
    void HandlePan(bool isFollowing, bool uiHasKeyboardFocus) {
        Vector3D targetVelPhys = {0, 0, 0};

        if (!uiHasKeyboardFocus && !isFollowing) {
            Vector3 forward = Vector3Normalize({ -sinf(angleX), 0.0f, -cosf(angleX) });
            Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, {0.0f, 1.0f, 0.0f}));

            Vector3 move{0, 0, 0};
            bool moved = false;
            if (IsKeyDown(KEY_W)) { move = Vector3Add(move, forward);      moved = true; }
            if (IsKeyDown(KEY_S)) { move = Vector3Subtract(move, forward); moved = true; }
            if (IsKeyDown(KEY_D)) { move = Vector3Add(move, right);        moved = true; }
            if (IsKeyDown(KEY_A)) { move = Vector3Subtract(move, right);   moved = true; }
            if (IsKeyDown(KEY_E)) { move.y += 1.0f; moved = true; }
            if (IsKeyDown(KEY_Q)) { move.y -= 1.0f; moved = true; }

            if (moved) {
                // El paneo manual interrumpe cualquier vuelo de enfoque en
                // curso, por la misma razon que HandleZoom() lo hace con
                // 'radius'.
                flying = false;

                move = Vector3Normalize(move);

                // 'target' esta en unidades FISICAS (metros, igual que
                // Body::pos -- ver g_renderOrigin/ToDrawPos en
                // math_utils.h), pero 'radius' es la distancia de orbita
                // en unidades de DIBUJADO (lo que ve Camera3D). Sin
                // dividir por RENDER_SCALE aqui, la velocidad quedaba en
                // ~45 METROS/seg en vez de draw-units/seg -- a escala
                // planetaria/del sistema solar eso es un desplazamiento
                // completamente imperceptible.
                double speedPhys = (double)(radius / RENDER_SCALE) * (double)panSpeedFactor;
                targetVelPhys = Vector3D{ (double)move.x, (double)move.y, (double)move.z } * speedPhys;
            }
        }

        float  dt = GetFrameTime();
        float  a  = 1.0f - std::exp(-8.0f * dt);
        panVelocityPhys = panVelocityPhys + (targetVelPhys - panVelocityPhys) * (double)a;
        target += panVelocityPhys * (double)dt;
    }
};

// ── Procesamiento de teclas de atajos ───────────────────────
struct InputState {
    bool paused          = true;
    SpawnMode mode       = MODE_SELECT;
    int  selectedBodyIdx = -1;
    // ID estable (Body::id, nunca se reusa) del cuerpo seleccionado. Las
    // fusiones/colisiones/limpieza de remanentes (physics.h, main.cpp)
    // borran elementos del vector 'bodies' SIN avisar a selectedBodyIdx --
    // un borrado en cualquier posicion anterior corre todos los indices
    // posteriores, dejando selectedBodyIdx apuntando a OTRO cuerpo (a veces
    // de tipo/masa totalmente distinta -- causa raiz de paneles de
    // inspector "mezclados", p.ej. una enana roja mostrando de repente
    // opciones de supernova de una estrella masiva). selectedBodyIdx se
    // RE-RESUELVE cada frame buscando este id (ver main.cpp, tras
    // StepPhysics); 0 = ninguna seleccion (Body::id arranca en 1).
    uint64_t selectedBodyId = 0;
    bool followSelected  = false;

    // Parametros de orbita para MODE_ORBIT (ver Fase 3, main.cpp): el punto
    // de spawn se trata como periapsis de una orbita de excentricidad
    // 'orbitEccentricity' (vis-viva); 'orbitRetrograde' invierte el sentido
    // de la velocidad tangencial. Controlados desde el panel "Modos de
    // Colocacion" del menu de catalogo (gui.h).
    bool  orbitRetrograde   = false;
    float orbitEccentricity = 0.0f;

    // Llama cada frame ANTES del bucle de render
    void HandleKeys(std::vector<Body>& bodies, std::vector<DustParticle>& dust) {
        // Bloquea TODOS los atajos mientras un campo de texto/numero de
        // ImGui tiene el foco (p.ej. editando el nombre o la masa en el
        // inspector) -- sin esto, SUPR/BACKSPACE borraba el cuerpo
        // seleccionado en vez de borrar un caracter del campo, ESPACIO
        // pausaba la simulacion al escribirlo en un nombre, los digitos
        // 1-4 cambiaban el modo de colocacion mientras se tecleaba, etc.
        if (ImGui::GetIO().WantCaptureKeyboard) return;

        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

        if (IsKeyPressed(KEY_SPACE))        paused = !paused;
        if (IsKeyPressed(KEY_ONE))          mode = MODE_SELECT;
        if (IsKeyPressed(KEY_TWO))          mode = MODE_STATIC;
        if (IsKeyPressed(KEY_THREE))        mode = MODE_ORBIT;
        if (IsKeyPressed(KEY_FOUR))         mode = MODE_LAUNCH;
        if (!ctrl && IsKeyPressed(KEY_F) && selectedBodyIdx >= 0) followSelected = !followSelected;
        if (IsKeyPressed(KEY_ESCAPE))     { selectedBodyIdx = -1; selectedBodyId = 0; followSelected = false; }
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) TIME_STEP = std::min(TIME_STEP*2.0, 1200.0*32768.0);
        if (IsKeyPressed(KEY_LEFT_BRACKET))  TIME_STEP = std::max(TIME_STEP*0.5, 1200.0/16384.0);
        if (IsKeyPressed(KEY_BACKSLASH))     TIME_STEP = 1200.0;

        bool del = IsKeyPressed(KEY_DELETE) || IsKeyPressed(KEY_BACKSPACE);
        if (del && selectedBodyIdx >= 0 && selectedBodyIdx < (int)bodies.size()) {
            bodies.erase(bodies.begin() + selectedBodyIdx);
            selectedBodyIdx = -1;
            selectedBodyId  = 0;
            followSelected  = false;
        }

        // CTRL+D: vaciar el campo de polvo (escombros/dust trails). 'dust'
        // es un Particle Pool de tamano FIJO (ver DustParticle::active,
        // body.h) -- "vaciar" significa liberar todos los slots, nunca
        // dust.clear() (eso destruiria la preasignacion).
        if (ctrl && IsKeyPressed(KEY_D)) {
            for (DustParticle& d : dust) d.active = false;
        }

        // CTRL+F: eliminar SOLO escombros (isFragment==true) que no
        // orbitan nada ni tienen nada orbitandolos. Un par (i,j) se
        // considera "ligado" si su energia orbital especifica es negativa
        // (orbita cerrada): 0.5*v_rel^2 - G*(m_i+m_j)/r < 0. Un fragmento
        // sin ningun par ligado es errante y se elimina.
        //
        // IMPORTANTE: se restringe a isFragment==true a proposito.
        // Planetas y lunas colocados por el usuario (isFragment==false)
        // NUNCA se borran con esta tecla, sin importar lo que diga la
        // formula de energia -- un "Proto-Cuerpo"/luna recien acrecionado
        // tambien pasa a isFragment==false (ver MergeBodies) y queda a
        // salvo. "Objetos errantes" se referia a escombros sueltos, no a
        // cuerpos celestes legitimos.
        if (ctrl && IsKeyPressed(KEY_F)) {
            std::vector<bool> bound(bodies.size(), false);
            for (size_t i = 0; i < bodies.size(); ++i) {
                if (bodies[i].mass <= 0.0) continue;
                for (size_t j = i + 1; j < bodies.size(); ++j) {
                    if (bodies[j].mass <= 0.0) continue;
                    Vector3D relPos = bodies[i].pos - bodies[j].pos;
                    Vector3D relVel = bodies[i].vel - bodies[j].vel;
                    double r = relPos.length();
                    if (r < 1.0) continue;
                    double specificEnergy = 0.5 * relVel.lengthSqr()
                                          - G * (bodies[i].mass + bodies[j].mass) / r;
                    if (specificEnergy < 0.0) { bound[i] = true; bound[j] = true; }
                }
            }
            for (int i = (int)bodies.size() - 1; i >= 0; --i) {
                size_t ui = (size_t)i;
                if (bodies[ui].mass <= 0.0 || !bodies[ui].isFragment || bound[ui]) continue;
                bodies.erase(bodies.begin() + i);
                if (i == selectedBodyIdx)      { selectedBodyIdx = -1; selectedBodyId = 0; followSelected = false; }
                else if (i < selectedBodyIdx)  selectedBodyIdx--;
            }
        }
    }
};

// ── Raycast sobre un plano horizontal ───────────────────────
inline bool RaycastGroundPlane(const Camera3D& cam, Vector3& outWorld, Vector3D& outPhys,
                                float planeY = 0.0f) {
    Ray ray = GetSafeMouseRay(cam);
    if (std::fabs(ray.direction.y) > 1e-8f) {
        float t = (planeY - ray.position.y) / ray.direction.y;
        if (t > 0.0f) {
            outWorld = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
            outPhys  = ToPhysPos(outWorld);
            return true;
        }
    }
    return false;
}

// ── Selección de cuerpo por click ───────────────────────────
inline int PickBody(const std::vector<Body>& bodies, const Camera3D& cam) {
    Vector3 camFwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    float closestDist = FLT_MAX;
    int hitBody = -1;

    for (int i = 0; i < (int)bodies.size(); ++i) {
        if (bodies[i].mass <= 0.0) continue;
        Vector3 p  = ToDrawPos(bodies[i].pos);
        if (Vector3DotProduct(camFwd, Vector3Subtract(p, cam.position)) <= 0.0f) continue;
        Vector2 sp  = GetWorldToScreen(p, cam);
        // Radio en draw-space SIN minimo artificial: el minimo 0.01 que
        // habia aqui inflaba el hitbox del Pulsar de Cangrejo de 0.00012
        // a 0.01 draw-units (x83), haciendo que fuera seleccionable desde
        // lejos aunque el cuerpo visual fuera de 12km. El minimo de
        // visibilidad/seleccion en PIXELES (max(18.0f, sr) abajo) es
        // suficiente para que cualquier cuerpo, por pequeno que sea, siga
        // siendo clicable cuando esta en pantalla.
        float pickRadius = (float)(bodies[i].radius * RENDER_SCALE);
        Vector2 sp2 = GetWorldToScreen(Vector3Add(p, {pickRadius, 0, 0}), cam);
        float dtm   = Vector2Distance(GetMousePosition(), sp);
        float sr    = Vector2Distance(sp, sp2);
        if (dtm <= std::max(18.0f, sr) && dtm < closestDist) {
            closestDist = dtm;
            hitBody = i;
        }
    }
    return hitBody;
}
