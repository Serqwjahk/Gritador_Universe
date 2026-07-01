# Gritador Universe

Simulador de sistemas estelares en tiempo real — física N-cuerpos, evolución estelar completa, y renderizado espacial de alto detalle.

## Características

### Física y simulación
- Integrador Leapfrog con sub-pasos adaptativos para órbitas estables a cualquier velocidad de simulación.
- Gravedad N-cuerpos O(n²) con softening configurable.
- Colisiones realistas: fragmentación, acreción, eyección de escombros, transferencia de masa por Roche.
- Mareas gravitacionales: deformación elipsoidal, bloqueo de marea progresivo.
- Velocidades de simulación desde x1/64 hasta x128 del tiempo real.

### Evolución estelar completa (17 fases)
Protoestrella → Secuencia Principal → Subgigante → Gigante Roja → Flash de Helio → Rama Horizontal → AGB → Pulsos Térmicos → Nebulosa Planetaria → Enana Blanca → [Enana Negra]

Ruta masiva: → Supergigante → Supernova → Remanente de SN → **Estrella de Neutrones** o **Agujero Negro**

Ruta hipotética: → Enana Azul → Enana Blanca → [Enana Negra]

### Renderizado
- Shader estelar procedural: llamaradas, prominencias, jets, convección superficial, gravity darkening.
- Pulsaciones estelares: Delta Scuti, Cefeidas, Miras, Pulsos Térmicos AGB.
- Deformación elipsoidal por rotación (Maclaurin) y mareas.
- Anillos planetarios físicos con partículas instanciadas.
- Skybox cúbico de nebulosa oscura (6 caras, `skybox_nebula_dark`).

### Agujero Negro — distorsión gravitacional de pantalla
Cuando un agujero negro existe en la escena, se aplica automáticamente un post-proceso de distorsión UV sobre el framebuffer completo:

- La escena se renderiza a una textura intermedia.
- Un shader de pantalla completa desplaza las UV de cada píxel según su distancia al centro del agujero negro en pantalla: cuanto más cerca, mayor la desviación.
- El radio del horizonte de eventos se proyecta como un disco negro perfecto.
- El efecto se atenúa suavemente hacia el borde de la zona de influencia.
- Incluye el catálogo de **Sagitario A\*** (4.15 millones M☉, agujero negro supermasivo del centro de la Vía Láctea).

### Catálogo de cuerpos reales
Sistema Solar completo + Alpha Centauri A/B, Proxima Centauri, UY Scuti, Betelgeuse, Stephenson 2-18, Sagitario A*.

---

## Créditos y atribuciones

### Motor de simulación y renderizado
Desarrollado sobre [raylib](https://www.raylib.com/) (Ramon Santamaria, MIT) y [Dear ImGui](https://github.com/ocornut/imgui) (Omar Cornut, MIT).

### Skybox de nebulosa
El cubemap `skybox_nebula_dark` (6 caras PNG) usado como fondo espacial proviene del proyecto:

> **"Real-time Black Hole Rendering in OpenGL"**
> Ross Ning (rossning92@gmail.com)
> https://github.com/rossning92/Blackhole — Licencia: MIT

---

## Construcción

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/gritador_universe
```

**Requisitos**: CMake >= 3.15, C++17, OpenGL 3.3+. raylib 5.x se descarga automáticamente si no está instalado.

---

## Controles

| Acción | Tecla / Input |
|---|---|
| Pausar / reanudar | Espacio |
| Seleccionar cuerpo | Click izquierdo |
| Seguir cuerpo | Doble click / F |
| Rotar cámara | Click derecho + arrastrar |
| Zoom | Rueda del ratón |
| Mover cámara | WASD / QE |
| Borrar seleccionado | Supr / Backspace |
| Cambiar velocidad | `[` / `]` / `\` (reset) |
| Modo colocación | 1–4 |
| Limpiar escombros | Ctrl+F |
| Limpiar polvo | Ctrl+D |
