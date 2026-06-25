#pragma once

// ============================================================
//  Gritador Universe — Constantes físicas y de simulación
// ============================================================

// Física universal
static constexpr double G       = 6.67430e-11;
static constexpr double PI_D    = 3.14159265358979323846;
static constexpr double SIGMA   = 5.670374419e-8;
static constexpr double M_SUN   = 1.98847e30;
static constexpr double L_SUN   = 3.828e26;
static constexpr double R_SUN   = 6.957e8;

// Renderizado
static constexpr float  RENDER_SCALE  = 1.0e-8f;

// Simulación
extern double TIME_STEP;   // mutable (ajustable en runtime)

// Iluminacion ambiental "falsa": un piso de luz parejo (ver ambientStrength
// en UploadLightUniforms, renderer.h) que evita que el lado no iluminado de
// los cuerpos quede en negro absoluto, sin relacion con ninguna fuente de
// luz real de la escena. Activable/desactivable desde el Menu Principal
// (gui.h) -- desactivada, solo iluminan estrellas reales y cuerpos lo
// bastante calientes como para emitir luz propia (ver UploadLightUniforms),
// dejando todo lo demas en negro real.
extern bool g_fakeLightEnabled;

// Sub-pasos de integracion ADAPTATIVOS (ver StepPhysics, physics.h). Antes
// el numero de sub-pasos era un entero FIJO (8): el tamano de paso real
// h = TIME_STEP/8 crecia SIN LIMITE al subir la velocidad de simulacion, y
// el error del integrador Leapfrog (O(h^2)) podia crecer miles de veces a
// velocidades altas -- causa raiz de que los anillos salgan disparados y
// las orbitas se descompongan al acelerar mucho la simulacion. Ahora
// StepPhysics calcula cuantos sub-pasos hacen falta para mantener 'h' cerca
// de este valor seguro, sin importar TIME_STEP, con un techo de
// PHYS_MAX_SUBSTEPS_PER_FRAME para acotar el costo de CPU a velocidades
// extremas (ahi 'h' vuelve a crecer por encima del valor seguro, pero
// muchisimo menos que con el esquema fijo anterior).
static constexpr double PHYS_SAFE_SUBSTEP_DT       = 150.0; // = 1200.0/8 (igual que antes a velocidad 1x)
static constexpr int    PHYS_MAX_SUBSTEPS_PER_FRAME = 64;   // techo de costo por frame (8x el valor fijo anterior)

// Reloj de tiempo SIMULADO acumulado (segundos), incrementado por TIME_STEP
// cada frame que la fisica corre (ver main.cpp) -- a diferencia de
// GetTime()/frame count (tiempo REAL), avanza proporcionalmente a la
// velocidad de simulacion elegida. Usado por los trails (TrailPoint::simTime,
// body.h) para que se desvanezcan en una ventana de tiempo SIMULADO
// constante, en vez de un numero de frames constante (ver comentario de
// TRAIL_TIME_SPAN mas abajo).
extern double g_simTime;

// Trayectorias (trails): antes la longitud maxima se media en PUNTOS
// (TRAIL_MAX_BASE/sqrt(TIME_STEP/1200), entre 60 y 300) y se quitaba
// exactamente 1 punto por FRAME cuando se excedia -- el tiempo REAL (en
// pantalla) que tardaba un trail en desvanecerse dependia del numero de
// FRAMES, no del tiempo SIMULADO transcurrido, asi que a velocidades muy
// altas el limite de 60 puntos se alcanzaba siempre y el trail tardaba
// "lo mismo" en desvanecerse sin importar cuanto mas rapido fuera la
// simulacion. Ahora cada punto guarda el tiempo SIMULADO en que se creo
// (TrailPoint::simTime, body.h) y se descartan los puntos mas viejos que
// TRAIL_TIME_SPAN segundos SIMULADOS -- a mayor velocidad, esa ventana de
// tiempo simulado se recorre en menos frames reales (el trail se renueva/
// desvanece mas rapido en pantalla); a menor velocidad, en mas frames
// (se desvanece mas lento). TRAIL_TIME_SPAN=216000s (2.5 dias simulados)
// reproduce el span tipico de antes a velocidad 1x (180 puntos * 1200s).
static constexpr double TRAIL_TIME_SPAN   = 216000.0;
// Tope de SEGURIDAD en cantidad de puntos (rendimiento/memoria): a
// velocidades muy bajas, TRAIL_TIME_SPAN segundos simulados pueden tardar
// muchos frames reales en transcurrir, acumulando muchos puntos -- este
// tope corta el trail a esta cantidad maxima de puntos sin importar la
// edad, igual que el limite superior (300) que ya existia antes.
static constexpr int    MAX_TRAIL_POINTS = 400;

static constexpr double SOFTENING     = 1.0e6;

// Radio minimo para que un cuerpo dibuje su trayectoria (trail). Antes
// se bloqueaba con !isFragment, pero los fragmentos grandes (proto-
// cuerpos/asteroides resultantes de un cataclismo) son tan relevantes
// como cualquier planeta -- el criterio es el tamano fisico, no la
// etiqueta.
static constexpr double MIN_TRAIL_RADIUS = 5.0e4;

// Límites de entidades
static constexpr int    MAX_BODIES    = 2000;
static constexpr int    MAX_FRAGMENTS = 600;
static constexpr int    MAX_LIGHTS    = 8;

// Polvo: sistema visual ligero, separado de 'bodies' (no cuenta para
// MAX_BODIES, no participa en gravedad N-cuerpo ni en colisiones entre si --
// ver UpdateDustGravity/UpdateDustLifecycle en physics.h). Es un Particle
// Pool de tamano FIJO preasignado al arrancar (ver DustParticle::active en
// body.h): al ser mucho mas barato por particula que un Body completo
// (gravedad restringida + un solo draw call instanciado), soporta un
// limite masivo sin caer de 60 FPS.
static constexpr int    MAX_DUST_PARTICLES = 100000;
// Vida util del polvo antes de desvanecerse por completo (segundos).
// Una nube de eyeccion debe disiparse en una escala "jugable" (segundos
// reales a velocidad normal), no en meses simulados. Ligeramente mas
// larga que antes para que los anillos/nubes de escombros permanezcan
// visibles un poco mas de tiempo.
static constexpr double DUST_MAX_LIFE = 3600.0 * 24.0 * 5.0;

// Densidad de referencia
static constexpr double DENSITY_EARTH = 5514.0;

// Densidad estandar de un nucleo rocoso/metalico (kg/m^3), usada por el
// modelo de volumen compuesto de gigantes gaseosos/helados (coreMass en
// body.h, ver SpawnFromCatalog y ApplyTidesAndRoche). ~5500 kg/m^3 es la
// densidad media de la Tierra (mezcla roca/metal), valor de referencia
// razonable para un nucleo planetario generico.
static constexpr double RHO_ROCKY_CORE = 5500.0;

// Densidad de un nucleo metalico (hierro-niquel) diferenciado, como el de
// la Tierra (~10800 kg/m^3 de promedio segun el modelo PREM -- Preliminary
// Reference Earth Model). Usado por el modelo de volumen compuesto cuando
// un planeta rocoso/helado lo bastante grande se diferencia en nucleo
// metalico + manto/corteza (ver DIFFERENTIATION_RADIUS y
// ROCKY_CORE_MASS_FRACTION, y SpawnFromCatalog/ApplyTidesAndRoche).
static constexpr double RHO_METALLIC_CORE = 10800.0;

// Fraccion de la masa total que forma el nucleo metalico de un planeta
// rocoso/helado diferenciado. ~32.5% es la fraccion real medida del
// nucleo terrestre (~1.88e24 kg de 5.97e24 kg) -- se aplica como
// aproximacion generica a cualquier cuerpo diferenciado, igual que el
// modelo de gigantes gaseosos usa un 10% fijo para todos ellos.
static constexpr double ROCKY_CORE_MASS_FRACTION = 0.325;

// Radio minimo para que un cuerpo rocoso/helado se considere
// "diferenciado" (nucleo metalico separado del manto por fusion interna
// durante su formacion). ~400 km es, en planetologia, el umbral
// aproximado de equilibrio hidrostatico para cuerpos rocosos/helados: el
// punto en el que un cuerpo deja de ser un asteroide irregular y pasa a
// ser redondo por su propia gravedad (p.ej. Ceres, 473 km, diferenciado;
// Vesta y asteroides menores, sin diferenciar). Por debajo de este radio
// (asteroides, lunas menores) el cuerpo se mantiene homogeneo y, si se
// desintegra por marea, se desintegra POR COMPLETO (sin nucleo
// remanente).
static constexpr double DIFFERENTIATION_RADIUS = 4.0e5;

// Desplazamiento radial maximo (fraccion del radio) para un cuerpo
// completamente "papa" (potatoFactor=1, radio->0, ver UploadBodyUniforms en
// renderer.h). 0.20 da una razon de ejes pico-a-valle de hasta
// (1+0.20)/(1-0.20)=1.5, en el rango observado entre Vesta (286x279x229 km,
// razon ~1.25 -- transicional, cerca de DIFFERENTIATION_RADIUS) e Hyperion
// (360x266x205 km, razon ~1.76 -- irregular extremo). Ni 1.0 (formas
// auto-intersectantes) ni <0.1 (imperceptible).
static constexpr float MAX_DEFORM = 0.20f;

// ============================================================
//  Generacion aleatoria de "Planeta Rocoso" / "Gigante Procedural"
//  (BuildCatalog, catalog.h). Rangos y relaciones masa-radio anclados en
//  cuerpos reales del sistema solar y exoplanetas confirmados.
// ============================================================

// Tierra: ancla de la relacion masa-radio de rocosos (ver
// ROCKY_MR_EXPONENT).
static constexpr double M_EARTH = 5.972e24;  // kg
static constexpr double R_EARTH = 6.371e6;   // m

// Luna: usada como unidad alternativa de masa en el inspector de
// propiedades (gui.h, dropdown kg / M_Tierra / M_Luna).
static constexpr double M_MOON  = 7.342e22;  // kg

// Rango de masa para "Planeta Rocoso" aleatorio: desde Mercurio (cuerpo
// rocoso diferenciado mas pequeno del catalogo, 3.3011e23 kg) hasta el
// limite "mega-Earth" de Planeta Gritador (1.0152e26 kg, ~17 M_Tierra,
// Kepler-10c -- el rocoso confirmado mas masivo conocido).
static constexpr double ROCKY_RANDOM_MASS_MIN = 3.3011e23;
static constexpr double ROCKY_RANDOM_MASS_MAX = 1.0152e26;

// Exponente de la relacion masa-radio R/R_Tierra = (M/M_Tierra)^x para
// rocosos de composicion tipo Tierra (Zeng & Sasselov 2013; Seager et al.
// 2007). x=0.30 reproduce Kepler-10c (17 M_T -> 2.34 R_T, real 2.35 R_T)
// con <1% de error, y aproxima Mercurio (0.055 M_T -> 0.42 R_T, real
// 0.38 R_T -- Mercurio es anomalo por su nucleo de hierro
// sobredimensionado).
static constexpr double ROCKY_MR_EXPONENT = 0.30;

// Jupiter: ancla de la relacion masa-radio de gigantes gaseosos/helados
// (ver GasGiantRadiusFromMass en gas_giants.h).
static constexpr double M_JUPITER = 1.898e27; // kg
static constexpr double R_JUPITER = 6.9911e7; // m

// Rango de masa para "Gigante Procedural" aleatorio: desde "mini-Neptuno"
// (la mitad de la masa de Neptuno, 1.024e26 kg) hasta "super-Jupiter" (5
// masas de Jupiter), bien por debajo del limite de fusion de deuterio
// (~13 M_Jup) que marca la transicion a enana marron.
static constexpr double GASGIANT_RANDOM_MASS_MIN = 0.5 * 1.024e26;
static constexpr double GASGIANT_RANDOM_MASS_MAX = 5.0 * M_JUPITER;
