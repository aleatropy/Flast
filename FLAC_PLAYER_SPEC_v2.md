# Especificación de Producto y Arquitectura Técnica — v2.0
## Reproductor FLAC Bit-Perfect de Mínimo Consumo Absoluto (Proyecto Open Source)

**Versión:** 2.0 — Reemplaza a v1.0 (uso personal único) tras cambio de alcance a proyecto público
**Fecha:** 2026-07-27
**Cambio de contexto respecto a v1.0:** este documento asume que el proyecto pasa de "herramienta de uso personal" a **software libre, de código abierto, publicado para cualquier usuario**, manteniendo desarrollo y mantenimiento por el equipo original. Esto introduce obligaciones que v1.0 no tenía: soporte a hardware desconocido, comunicación honesta de limitaciones a usuarios que no son el propio equipo, y mantenibilidad del código para posibles contribuidores externos.

---

## 0. Filosofía del proyecto — actualizada para contexto público

Sigue vigente el principio de v1.0: **cada línea de código que no sirve directamente a "decodificar FLAC → entregar PCM intacto al DAC → controles básicos de transporte" es candidata a eliminación.** A esto se añade, por el cambio a proyecto público:

**Principio nuevo — Honestidad ante todo lo demás:** cuando existe tensión entre "prometer una función perfecta" y "admitir públicamente una limitación real", este proyecto elige siempre la segunda opción, incluso si eso genera peores reviews o comparaciones desfavorables con apps comerciales que no son transparentes sobre sus propias limitaciones. Esto no es una postura moral abstracta — es una decisión de arquitectura con consecuencias de diseño concretas, detalladas en la sección 6 (Documento de Transparencia Pública).

**Advertencia de complejidad reconocida:** la decisión de construir la UI completa en C/C++ nativo sobre NDK (sección 3, sin usar el framework de UI de Android) es la decisión de mayor riesgo técnico y de mantenibilidad de todo este documento. Se mantiene porque el usuario la confirmó explícitamente tras conocer el costo completo, no porque sea la ruta más segura. **Cualquier persona retomando este documento debe leer la sección 3.0 completa antes de escribir código**, porque ahí se detalla exactamente qué se está sacrificando a cambio del ahorro de RAM.

---

## 1. Alcance funcional

Sin cambios respecto a v1.0 en la lista de funciones (Play/Pause/Next/Previous, 3 secciones, escaneo recursivo de `Music/`, playlists por ruta de archivo, indicador bit-perfect, sin metadata, sin EQ, sin red). Ver v1.0 secciones 1.1-1.3 para la lista completa y exhaustiva — no se repite aquí para evitar duplicación y desincronización entre documentos. **Este documento v2.0 solo especifica lo que cambia: arquitectura técnica, target de Android, y obligaciones de transparencia.**

Cambio explícito de este documento respecto a v1.0 sección 1.2: se elimina la sugerencia de `MediaSession` mínimo para lockscreen (que en v1.0 quedó como sugerencia abierta) — en la arquitectura 100% nativa de este documento, cada componente del framework Android que se toca (incluido MediaSession) tiene costo de mantenimiento e integración JNI. Se marca como **`[DECISIÓN PENDIENTE — evaluar en Beta]`**, no incluido en el MVP inicial.

---

## 2. Target de plataforma — Decisión tomada

**Android 8.0 (API 26) como mínimo soportado.** `[ACTUALIZADO EN v1.2.0 — reemplaza la decisión original de API 31]`

**Decisión original (v2.0 de este documento):** Android 12 (API 31), excluyendo explícitamente todo dispositivo con Android 11 o anterior.

**Por qué cambió:** la justificación de la sección 2.1 nunca fue "AAudio no existe antes de API 31" — AAudio existe desde API 26. Era una apuesta sobre la *madurez* de las implementaciones de EXCLUSIVE/MMAP en HALs viejos. Esa apuesta tenía sentido cuando un HAL inmaduro significaba que la app **no reproducía absolutamente nada**: el código pedía EXCLUSIVE + PCM_I32 y devolvía error si no lo conseguía. Desde v1.1.0 existe una cadena de degradación explícita (EXCLUSIVE → SHARED, ver sección 3.6) que reproduce igual y reporta `BIT-PERFECT: NO` con honestidad. Con eso, el argumento para excluir Android 8-11 desapareció: esos dispositivos ahora obtienen exactamente el comportamiento que la sección 3.6 ya describía para cualquier HAL que no conceda modo exclusivo.

**Por qué API 26 y no más abajo:** es el piso real de AAudio, no una preferencia. Verificado compilando toda la capa nativa con `-Werror=unguarded-availability` contra API 21, 23, 26, 29, 30 y 31: limpia desde 26, falla en 23 y anteriores sobre `AAudio_createStreamBuilder`, `AAudioStream_close` y compañía. Bajar de ahí exigiría un segundo backend de audio sobre OpenSL ES, que **no puede** ser bit-perfect — sería otro producto, no un port.

**Costo asumido y declarado:** la superficie de prueba crece a Android 8-16. El equipo no puede probar todas esas versiones, y eso se comunica públicamente igual que se comunica la fragmentación de HAL de la sección 5.

### 2.1 Justificación técnica (para el documento de transparencia pública, sección 6)

`[NOTA v1.2.0: el razonamiento que sigue es el que sustentaba la decisión original de API 31. Se conserva textualmente por transparencia — la sección 6 exige no reescribir la historia de las decisiones — pero ya no es la decisión vigente. Ver arriba.]`

- Las APIs de AAudio EXCLUSIVE/MMAP existen desde API 26, pero su estabilidad práctica y la calidad de implementación por parte de los fabricantes de HAL mejora notablemente en versiones más recientes — API 31 es un punto de corte razonable donde la mayoría de HALs activos en el mercado ya tienen implementaciones más maduras, aunque **esto no es una garantía absoluta y varía por fabricante** (ver sección 5, la fragmentación del HAL no desaparece por elegir una API más alta, solo se reduce estadísticamente).
- `[VERIFICAR EN IMPLEMENTACIÓN]`: confirmar si `MIXER_BEHAVIOR_BIT_PERFECT` (introducida en API 34) debe ser un requisito adicional de versión mínima, o si se trata como mejora opcional en dispositivos que la soporten, con AAudio EXCLUSIVE (disponible desde API 26) como base mínima funcional en toda la API 31+.
- Excluir Android 11 y anteriores es una decisión consciente de reducir alcance de mercado a cambio de reducir superficie de pruebas y de bugs de compatibilidad — se declara así de forma explícita y pública, no oculta.

### 2.2 Dispositivo de desarrollo vs. audiencia objetivo

El **Galaxy A55 (Exynos 1480)** es el dispositivo de desarrollo y pruebas del equipo — es donde se valida la arquitectura, no una limitación de a quién sirve la app. La audiencia objetivo es **cualquier dispositivo Android con API 26+** (actualizado en v1.2.0; era API 31+), sin exclusión por marca o SoC, con la única excepción reconocida y documentada de dispositivos donde el fabricante bloqueó el modo AAudio EXCLUSIVE a nivel de HAL/firmware (ver sección 5, fragmentación de hardware) — en esos casos la app sigue funcionando en modo SHARED, solo que sin bit-perfect, comunicado con total claridad vía el indicador (sección 3.6).

---

## 3. Arquitectura técnica — Cambio fundamental respecto a v1.0

### 3.0 Qué se está decidiendo aquí y qué implica (leer antes de cualquier otra subsección)

v1.0 dejaba abierta la pregunta de Compose vs Views (ambas corren sobre el framework estándar de Android con runtime Kotlin/Java). Este documento v2.0 va más allá: **la interfaz de usuario completa se implementa en C/C++ vía NDK, sin el framework de UI de Android (`android.view`, Compose, o cualquier variante).**

Esto significa, en términos concretos de qué hay que construir que normalmente Android da gratis:

| Lo que Android normalmente provee | Lo que este proyecto debe construir desde cero |
|---|---|
| Manejo de touch events (`onTouchEvent`, gestos) | Lectura de eventos táctiles crudos vía `AInputQueue`/`ANativeActivity`, interpretación manual de tap/hold |
| Renderizado de texto con fuentes del sistema | Motor de renderizado de texto propio (ej. `stb_truetype` o similar librería mínima en C, o rasterización manual de una fuente bitmap embebida — ver 3.3) |
| Ciclo de vida (`onPause`/`onResume`/rotación/multitarea) | Manejo manual de callbacks de `ANativeActivity` (`onPause`, `onResume`, `onWindowFocusChanged`) |
| Layout automático (constraints, flexbox) | Posicionamiento manual de elementos de texto en coordenadas fijas o calculadas a mano |
| Notificaciones / Foreground Service | **Esto NO tiene equivalente NDK puro** — requiere un mínimo de código Java/Kotlin (ver 3.1) porque `NotificationManager` y `Service` son clases del framework sin bypass nativo documentado ni soportado |
| Detección USB (`UsbManager`) | Mismo caso — requiere puente JNI hacia la API Java de `UsbManager`, no hay forma de evitar esto completamente |

**Conclusión honesta:** "100% nativo sin ningún Java/Kotlin" no es alcanzable al 100% en Android — el sistema operativo exige algunas interacciones (servicios, notificaciones, USB host) a través de su API Java. Lo que sí es alcanzable, y es lo que este documento especifica, es **minimizar esa capa Java/Kotlin al mínimo absoluto indispensable** (un `ANativeActivity` o Activity-shim mínima, sin ningún framework de UI sobre ella) y hacer que **toda la lógica de negocio, UI, decodificación y audio vivan en C/C++.**

### 3.1 Estructura de capas

```
┌─────────────────────────────────────────────────┐
│ Capa Java/Kotlin MÍNIMA (indispensable, no evitable) │
│ - ANativeActivity (o Activity shim mínima)            │
│ - Foreground Service + Notification (mínima)          │
│ - Puente JNI hacia UsbManager para detección de DAC   │
│ - Nada más. Sin ViewModels, sin Fragments, sin        │
│   ninguna librería de UI de Android.                   │
└─────────────────────────────────────────────────┘
                        │ JNI
┌─────────────────────────────────────────────────┐
│ Capa C/C++ (NDK) — TODO lo demás                       │
│ - Renderizado de UI directo sobre ANativeWindow/Surface│
│ - Motor de texto mínimo (fuente bitmap embebida)       │
│ - Manejo de input táctil                                │
│ - Escaneo de sistema de archivos (POSIX directo,        │
│   opendir/readdir recursivo — no File API de Java)      │
│ - Decodificación FLAC (libFLAC)                          │
│ - AAudio (apertura de stream, EXCLUSIVE, gestión)        │
│ - Lectura de VID/PID de dispositivo USB conectado         │
│   (metadato básico, no parsing de descriptor de audio)    │
│ - Persistencia de playlists (lectura/escritura de        │
│   archivos planos vía POSIX, sin SQLite)                 │
└─────────────────────────────────────────────────┘
```

### 3.2 Motor gráfico — decisión de implementación

Dos rutas viables, ambas deben evaluarse con una prueba de concepto antes de comprometerse:

**Opción A — `ANativeWindow` + rasterización manual (máximo control, máximo esfuerzo):**
Dibujar directamente sobre el buffer de píxeles del `Surface` vía `ANativeWindow_lock`/`ANativeWindow_unlockAndPost`, sin ninguna librería gráfica intermedia. El texto se dibuja copiando glyphs de una fuente bitmap pre-rasterizada (embebida como array de bytes en el binario, generada una vez en build-time desde una fuente monoespaciada libre como *Terminus* o *Spleen*, ambas diseñadas específicamente para ser bitmap-friendly y extremadamente ligeras). Esto evita cualquier dependencia de librería de fuentes en runtime (nada de FreeType, que pesa varios cientos de KB).

**Opción B — SDL2 mínimo (descartada):**
Usar SDL2 como capa de abstracción sobre `ANativeWindow` e input hubiera añadido ~300-500KB a cambio de menos código propio que mantener. Se descarta explícitamente en favor de la Opción A.

**DECISIÓN CONFIRMADA: Opción A (rasterización manual, sin SDL2 ni ninguna librería gráfica intermedia).** Es la elección final, no una prueba de concepto pendiente. Esto es consistente con la decisión ya tomada en la sección 3.0 de aceptar la complejidad de "C/C++ para todo" a cambio del máximo ahorro posible de tamaño y RAM — introducir SDL2 en este punto reintroduciría parte del peso que la decisión de UI 100% nativa buscaba eliminar. Quien implemente esta sección debe construir, desde cero: lectura de eventos táctiles vía `AInputQueue`/`ANativeActivity`, rasterización de texto copiando glyphs de la fuente bitmap embebida (sección 3.3) directamente sobre el buffer de `ANativeWindow`, y posicionamiento manual de los elementos de la UI (sin motor de layout).

### 3.3 Fuente tipográfica

Dado que no se usan fuentes del sistema (evitar esa dependencia de framework), se debe **embeber una única fuente monoespaciada bitmap, libre de licencia restrictiva** (candidatas: *Spleen*, *Terminus*, *Tamzen* — todas diseñadas para terminal/consola, extremadamente ligeras, con licencias permisivas compatibles con distribución open source). El tamaño configurable (sección de UI en v1.0, sección 2.3) se logra reescalando la rasterización bitmap o, si se opta por más de un tamaño base pre-rasterizado, embebiendo 2-3 variantes de tamaño fijo (ej. pequeño/mediano/grande) en vez de escalado dinámico con pérdida de nitidez — a decidir en implementación según qué tan aceptable sea visualmente el escalado simple de bitmap.

### 3.4 Todo lo especificado en v1.0 sección 3 (audio) permanece sin cambios

La arquitectura de audio (libFLAC vía JNI/NDK, AAudio EXCLUSIVE con verificación real de `isMMapUsed()`, control de volumen vía USB Audio Class con las mismas reglas de no-atenuación-digital, indicador bit-perfect) **no cambia en absoluto** con este rediseño de UI — de hecho se simplifica ligeramente, porque ahora toda la cadena desde decodificación hasta entrega a AAudio vive en la misma capa C/C++, sin cruzar el límite JNI para cada operación de audio (solo se cruza para las interacciones indispensables con el framework: Service/Notification/USB, como se detalla en 3.1).

### 3.5 Control de volumen — decisión explícita del usuario, no detección automática

**Cambio de enfoque respecto a la versión anterior de esta sección:** se descarta la detección automática vía parsing del descriptor USB Audio Class (`FU_VOLUME_CONTROL`). La razón del cambio: parsear descriptores USB crudos es código no trivial con superficie real de error (interpretación incorrecta de bytes, DACs que responden de forma no estándar, bugs de parsing) — exactamente el tipo de complejidad frágil que este proyecto busca evitar. Se reemplaza por una pregunta explícita al usuario, que es simultáneamente más simple de implementar, más confiable (el usuario que tiene el DAC en la mano sabe la respuesta con certeza), y más coherente con el principio de transparencia del proyecto (el usuario entiende por qué la app se comporta como se comporta, porque él mismo lo definió).

**Por qué esta decisión no puede depender de un default silencioso:** hasta que el usuario responde, no hay forma segura de asumir un comportamiento. Un default de "botones activos, volumen de software" contradice la premisa de bit-perfect sin que el usuario lo sepa. Un default de "botones desactivados, PCM al 100%" es un riesgo real de daño auditivo si el DAC/amplificador conectado no tiene ningún control físico propio y el usuario no ha sido advertido — algunos DACs de este segmento entregan varios cientos de mW de potencia de salida, suficiente para ser un problema real con IEMs sensibles a volumen máximo sin control disponible. Por esto, la reproducción se bloquea hasta que la pregunta se responde explícitamente.

**Flujo de primer uso con un DAC:**

1. Al detectar un dispositivo de audio USB conectado por primera vez (identificado por vendor ID + product ID, leídos vía `UsbManager` — esto es lectura estándar y trivial de metadatos USB, no requiere parsear el descriptor de audio completo), la app **no reproduce nada todavía** y presenta, en texto plano, dos líneas de contexto seguidas de la pregunta — no es un tutorial ni onboarding (siguen prohibidos por la sección 0), es la explicación mínima de por qué se bloquea la reproducción para preguntar algo, coherente con el principio de gentileza hacia un usuario que abre la app sin haber leído este documento:

   ```
   Para proteger tu audición y mantener
   bit-perfect, necesitamos saber esto
   una sola vez por cada DAC:

   ¿Tu DAC permite subir/bajar el volumen
   directamente (rueda, botones físicos,
   o app propia del fabricante)?

        [ SI ]        [ NO ]
   ```

2. Según la respuesta:
   - **SI** → los botones físicos de volumen del teléfono se interceptan y se deshabilitan por completo para esta app (no hacen nada, ni suben ni bajan nada). `STREAM_MUSIC` se fija al 100% y se bloquea ahí. El usuario controla el volumen directamente desde su DAC (rueda, botón, o su app propietaria — fuera del alcance de esta app). Con esta respuesta, el indicador de la sección 3.6 puede mostrar `BIT-PERFECT: SI` cuando AAudio consiga EXCLUSIVE, ya que el volumen no interfiere con la señal digital.
   - **NO** → los botones físicos de volumen del teléfono permanecen activos y controlan el volumen estándar de Android. La UI debe mostrar, de forma visible y permanente mientras este DAC esté conectado (sugerido: en la sección CONFIG, junto al detalle del indicador bit-perfect de la sección 3.6), el texto: `Volumen: control por software del sistema — este DAC no tiene control propio configurado, el volumen se ajusta digitalmente antes de llegar al DAC`. Con esta respuesta, el indicador de la sección 3.6 mostrará `BIT-PERFECT: PARCIAL` en cualquier nivel de volumen distinto al 100%, incluso si AAudio consiguió EXCLUSIVE — ver sección 3.6 para el detalle completo de este cruce de estados.

3. **La elección se persiste por dispositivo**, indexada por vendor ID + product ID del USB (almacenamiento local mínimo, igual mecanismo que las playlists de la sección 4.3 — un archivo de texto plano). Si el usuario conecta un DAC ya configurado antes, la app aplica la elección guardada sin volver a preguntar. Si conecta un DAC distinto no visto antes, se repite el flujo del punto 1.

4. Configuración incluye una opción para revisar/cambiar la respuesta guardada para el DAC actualmente conectado (por si el usuario se equivocó al responder, o el comportamiento real del DAC no coincide con lo que asumió).

**Regla que se mantiene sin cambios respecto a la versión anterior:** bajo ninguna circunstancia la app implementa atenuación digital de software como aproximación a "control de volumen propio" — la única alternativa a control por hardware del DAC es el volumen estándar del sistema Android (rama NO), nunca una implementación propia de atenuación dentro de la app.

### 3.6 Indicador Bit-Perfect en la UI — simplificado con detalle en Configuración, considerando AMBAS fuentes de no-bit-perfect

**Corrección importante respecto al diseño original de esta sección:** bit-perfect no depende únicamente del resultado de AAudio (`isMMapUsed()`) — también depende del estado de volumen de la sección 3.5. Si el usuario respondió "NO" en la pregunta de volumen (su DAC no tiene control propio), el volumen se ajusta mediante atenuación de software del sistema Android, lo cual altera digitalmente la señal PCM en cualquier nivel de volumen distinto al 100% — **incluso si AAudio consiguió EXCLUSIVE perfectamente**. Un indicador que solo refleje el estado de AAudio, ignorando esto, mostraría "SI" en una situación donde el audio real que llega al DAC ya no es bit-perfect, lo cual contradice directamente el principio de honestidad de la sección 0.

La UI del reproductor muestra uno de tres textos posibles (no dos):

```
BIT-PERFECT: SI
```
```
BIT-PERFECT: PARCIAL
```
```
BIT-PERFECT: NO
```

Los tres son **tocables**. Al tocar cualquiera, la app navega a la sección CONFIG, donde se muestra el detalle completo sin ocultar nada — la tabla de estados posibles, ampliada respecto a los 5 de v1.0 sección 3.4 para incluir el cruce con el estado de volumen:

| Estado real | Indicador en player | Texto explicativo en Configuración |
|---|---|---|
| EXCLUSIVE conseguido, y (volumen del DAC configurado como "SI tiene control propio" **o** volumen del sistema al 100%) | `BIT-PERFECT: SI` | El sistema entrega el audio sin remuestreo ni mezcla, y el volumen no está alterando la señal digital. |
| EXCLUSIVE conseguido, pero volumen del DAC configurado como "NO tiene control propio" y el volumen actual del sistema **no** está al 100% | `BIT-PERFECT: PARCIAL` | El modo de audio exclusivo funciona, pero el volumen se está ajustando por software antes de llegar al DAC — esto altera la señal digital. Sube el volumen al máximo para bit-perfect real, o usa un DAC con control de volumen propio. |
| Degradado a SHARED (mixer de Android en uso) | `BIT-PERFECT: NO` | Tu dispositivo está usando el modo compartido del sistema — el audio puede ser remuestreado antes de llegar al DAC. |
| EXCLUSIVE solicitado pero el dispositivo/HAL no lo soporta en absoluto (`AAudio_getPlatformMMapExclusivePolicy` devuelve `NEVER`) | `BIT-PERFECT: NO` | Este dispositivo no soporta el modo de audio exclusivo a nivel de fabricante/firmware. |
| Reproduciendo vía Bluetooth | `BIT-PERFECT: NO` | Bluetooth siempre recodifica el audio — bit-perfect no es posible por diseño del protocolo, sin importar la app. |
| Sin dispositivo de audio USB conectado, usando salida interna del teléfono | `BIT-PERFECT: NO` | Estás usando la salida de audio interna del dispositivo, no un DAC externo. |

Este diseño reduce el ruido visual en la pantalla principal (tres palabras posibles, no una explicación completa) sin perder ni un ápice de transparencia — el detalle completo, incluyendo el cruce correcto entre estado de AAudio y estado de volumen, sigue estando a un solo tap de distancia, nunca oculto ni resumido de forma engañosa.

**Nota de implementación:** el estado PARCIAL requiere que la app pueda leer el volumen actual del stream `STREAM_MUSIC` en tiempo real (no solo al momento de conectar el DAC) para saber si está al 100% o no — esto es una lectura estándar de `AudioManager`, sin complejidad adicional relevante, pero debe evaluarse con qué frecuencia se refresca esta lectura para no introducir polling innecesario (contradiría la sección 4.6 sobre evitar timers periódicos) — la lectura debe ser reactiva a cambios de volumen (`ACTION_VOLUME_CHANGED` o el callback equivalente), no un chequeo periódico.

---

### 3.7 Acceso a la carpeta Music/ — Confirmado, sin bloqueo de Scoped Storage

`Music/` es una carpeta de medios reconocida por el propio Android (junto con Alarms/, Audiobooks/, Notifications/, Podcasts/, Ringtones/), lo que la coloca en un régimen distinto al de una carpeta arbitraria del sistema de archivos. Desde Android 11, el kernel virtual FUSE permite que apps bajo Scoped Storage sigan usando File APIs con rutas de archivo directas para este tipo de carpetas reconocidas — es decir, **el escaneo recursivo vía `opendir`/`readdir` en C especificado en la sección 3.1 funciona sin cambios, sin necesidad de Storage Access Framework ni de que el usuario seleccione la carpeta manualmente con un selector del sistema.**

Lo único que varía por versión de Android es qué permiso declarar y solicitar en runtime:
- **API 26-32 (Android 8 a 12L):** `READ_EXTERNAL_STORAGE`.
- **API 29 (Android 10) además:** `android:requestLegacyExternalStorage="true"`. Es la única versión donde Scoped Storage bloquea el acceso por ruta directa sin capa FUSE de compatibilidad; sin ese atributo el escaneo no encuentra nada en Android 10 específicamente.
- **API 33+ (Android 13+):** `READ_MEDIA_AUDIO` (permiso granular específico para audio, más alineado con el principio de pedir solo lo estrictamente necesario).

**No se solicita `MANAGE_EXTERNAL_STORAGE`** ("acceso a todos los archivos") — no es necesario para este caso de uso, y solicitarlo sin necesidad real contradice el principio de transparencia y permisos mínimos de la sección 6. Además, ese permiso requiere un proceso de aprobación especial de Google Play, que no aplica ni conviene a este proyecto.

**Actualización a v1.0 sección 4.2:** el punto marcado como `[VERIFICAR EN IMPLEMENTACIÓN]` sobre Scoped Storage queda resuelto — la arquitectura original de v1.0 (File API directo, sin MediaStore) es correcta y se mantiene sin cambios.

---

### 3.8 Optimizaciones de compilación — último tramo de reducción de tamaño

Estas son optimizaciones concretas, no exploratorias, a aplicar sobre la arquitectura ya definida en las secciones anteriores. A diferencia de las decisiones de 3.0-3.7 (arquitectura), esto es *cómo compilar* esa arquitectura para exprimir el resultado final.

### 3.8.1 libFLAC — compilar solo el decoder, nunca el encoder

libFLAC incluye tanto encoder como decoder en su código base, pero esta app **nunca necesita codificar FLAC** (solo reproduce archivos ya existentes) — el encoder es peso muerto que nunca se ejecuta. libFLAC soporta compilación condicional para excluir el encoder del binario final (flags de build tipo `--disable-flac-encoder` o el equivalente en el sistema de build usado para compilar contra NDK, según la versión de libFLAC). Un decoder-only build reduce el tamaño del `.so` resultante de forma directa, ya que buena parte del código de predicción LPC, cálculo de tamaño óptimo de frame, y estimación de parámetros de compresión pertenece exclusivamente al lado de codificación.

También reafirma lo ya especificado en v1.0 sección 3.1: compilar sin soporte Ogg-FLAC (`--disable-ogg` o equivalente), ya que esta app no soporta el contenedor Ogg, solo FLAC nativo.

`[VERIFICAR EN IMPLEMENTACIÓN]`: el mecanismo exacto de build depende de si se usa el sistema de build original de libFLAC (autotools/CMake) cross-compilado para Android vía NDK, o si se opta por extraer manualmente solo los archivos fuente `.c` del decoder (`stream_decoder.c` y sus dependencias directas, excluyendo `stream_encoder.c` y todo lo que solo este último referencia) para compilarlos directamente dentro del proyecto Android sin pasar por el sistema de build genérico de libFLAC — esta segunda opción da más control pero requiere mapear manualmente el árbol de dependencias internas de la librería.

### 3.8.2 Flags de compilador orientadas a tamaño

Compilar el código nativo (capa C/C++ completa: libFLAC decoder-only, AAudio, motor gráfico, lógica de UI) con `-Os` (optimizar para tamaño) en vez de `-O2` (optimizar para velocidad) como flag por defecto del NDK. Dado que la app es ligera en cómputo (decodificar FLAC no es costoso, según se estableció al inicio de este proyecto) y el objetivo explícito es tamaño mínimo, la posible pérdida marginal de velocidad de `-Os` frente a `-O2` no tiene impacto perceptible en la experiencia — sobra margen de CPU de cualquier forma. `[VERIFICAR EN IMPLEMENTACIÓN]`: confirmar con una build de prueba que `-Os` no introduce ninguna regresión perceptible en la decodificación en tiempo real (no debería, dado el bajo costo computacional de FLAC, pero se verifica antes de fijarlo como estándar del proyecto).

### 3.8.3 Strip de símbolos de debug

El binario de release debe compilarse con símbolos de debug eliminados (`strip` sobre el `.so` final, o las flags equivalentes `-s`/`--strip-all` en el linker, y asegurar que Gradle/CMake no empaquete símbolos de debug en el APK de release — revisar configuración de `debuggable false` y `ndk.debugSymbolLevel` según corresponda). Esto no afecta funcionalidad en absoluto, solo reduce el tamaño del binario final al quitar información usada únicamente para depuración con herramientas como `gdb`/`lldb`, irrelevante para un usuario final.

### 3.8.4 R8/ProGuard sobre la capa Java/Kotlin mínima

Aunque la capa Java/Kotlin de este proyecto es mínima (sección 3.1: solo Activity/Service/Notification/puente USB), sigue valiendo la pena aplicar R8 con shrinking agresivo (`minifyEnabled true`, `shrinkResources true`) sobre esa porción — es una ganancia pequeña en términos absolutos dado lo poco que hay que reducir, pero es coherente con el principio de no dejar nada sin optimizar, y el costo de habilitarlo es prácticamente cero (una línea de configuración en el build de release).

---

### 3.9 Mejoras de usabilidad de biblioteca — confirmadas, con costo de recursos verificado como despreciable

Tras evaluar el costo real de estas tres adiciones (detallado abajo), se confirma su inclusión. El costo combinado (~130-150 KB de RAM adicional, ~15-45 KB de APK adicional) representa menos del 1% del presupuesto de RAM y ~5-10% del presupuesto de APK establecidos en la sección 4 — no compromete el objetivo de mínimo consumo del proyecto. Se descarta explícitamente, por las razones detalladas más abajo, añadir metadata completa (álbum/artista/número de pista) — el costo de recursos de esa adición también sería pequeño, pero el costo real no es de recursos: es de alcance y de expectativa de navegación que empujaría el proyecto hacia el terreno ya ocupado por reproductores convencionales, diluyendo su diferenciador único (bit-perfect + minimalismo radical).

### 3.9.1 Estructura de carpetas como jerarquía de navegación

Reemplaza la lista plana especificada en v1.0 sección 2.2.1. El escaneo recursivo de `Music/` (sección 4.1) ya recorre la estructura de carpetas completa — en vez de aplanar el resultado a una sola lista alfabética, se preserva la jerarquía (carpeta → subcarpetas → archivos) como estructura de navegación en la sección MÚSICA. Esto no introduce ninguna dependencia nueva ni requiere leer metadata — es una reestructuración de cómo se almacena y navega la misma información que el escaneo ya produce.

**Costo verificado:** ~10-20 KB de RAM adicional (punteros de árbol sobre una biblioteca de referencia de ~3,000 archivos en ~200 carpetas), ~5-10 KB de APK adicional. Despreciable.

### 3.9.2 Título del tag Vorbis Comment, con fallback obligatorio al nombre de archivo

Se lee **únicamente el campo `TITLE`** del bloque de metadata Vorbis Comment de cada FLAC (no artista, no álbum, no ningún otro campo) durante el escaneo. Si el campo no existe en el archivo, se muestra el nombre de archivo tal cual (comportamiento de v1.0 sección 2.5, que se mantiene como fallback, no se elimina).

- El parser de Vorbis Comments ya es parte de libFLAC, la misma librería enlazada para decodificar audio — no se añade ninguna dependencia nueva. `[VERIFICAR EN IMPLEMENTACIÓN]`: confirmar que el build decoder-only especificado en la sección 3.8.1 no excluye accidentalmente las funciones de parsing de metadata junto con el encoder — son rutas de código distintas dentro de libFLAC y deben verificarse por separado.
- **Costo verificado:** ~120 KB de RAM adicional (3,000 canciones × ~40 bytes por título), incremento de APK despreciable dado que el código ya está en libFLAC.
- **Costo real a gestionar:** no es de RAM ni de APK, es de tiempo de escaneo — leer el bloque de metadata de cada archivo durante el escaneo inicial añade I/O que no existe en la v1.0 original (que solo lee el nombre de archivo del sistema de archivos, sin abrir el contenido). Esto se neutraliza con la sección 3.9.3 (caché).

### 3.9.3 Caché de resultado de escaneo entre sesiones

Actualiza v1.0 sección 4.1, que dejaba esto como pendiente de verificar según el volumen de archivos esperado. Se confirma su inclusión desde el diseño inicial, dado que ahora el escaneo incluye lectura de metadata (3.9.2), lo que hace el caché más valioso que en la versión solo-nombre-de-archivo.

- Persistencia en almacenamiento interno de la app: un archivo de texto plano con una entrada por canción (ruta absoluta + título cacheado), estructurado de forma simple para lectura/escritura rápida.
- Invalidación: comparar timestamp de última modificación de la carpeta `Music/` (y subcarpetas relevantes) contra la fecha del caché guardado; si hay archivos nuevos o modificados, re-escanear solo lo necesario en vez de la biblioteca completa (`[VERIFICAR EN IMPLEMENTACIÓN]`: definir si la invalidación es de grano fino, por subcarpeta, o de grano grueso, biblioteca completa — grano fino es más eficiente pero más complejo de implementar correctamente).
- Se mantiene la opción manual de refresco (v1.0 sección 4.1, `[ACTUALIZAR]`) para el caso donde el usuario añade archivos y quiere verlos sin esperar a la próxima detección automática de cambios.
- **Costo verificado:** ~0 KB de RAM adicional en tiempo de ejecución (es la misma información ya en memoria, solo que además persistida), ~240 KB en disco del dispositivo del usuario (no cuenta contra el tamaño del APK), ~10-15 KB de APK adicional por la lógica de lectura/escritura/invalidación.

### 3.9.4 Metadata completa (álbum, artista, número de pista) — descartada, y por qué (registro para el documento de transparencia, sección 6)

Se evaluó explícitamente y se descarta, **no por costo de recursos** (verificado como pequeño, del mismo orden que 3.9.1-3.9.3) sino por costo de alcance: mostrar álbum y artista crea una expectativa natural de navegación por álbum/artista, lo que empujaría el rediseño de la sección MÚSICA hacia un paradigma de biblioteca convencional — el terreno donde Musicolet, Poweramp y otros reproductores ya compiten bien. El diferenciador de este proyecto es la combinación específica de bit-perfect real y minimalismo radical; diluir el segundo para parecerse más a la competencia en organización de biblioteca reduce la razón de ser del proyecto en vez de fortalecerla. Esta decisión debe comunicarse explícitamente en el documento de transparencia pública (sección 6) como una elección deliberada de producto, no como una limitación técnica no resuelta ni una omisión por descuido.

---

## 4. Estimaciones de tamaño y RAM — versión C/C++ nativo puro

Con la advertencia de que estas cifras dependen fuertemente de cuál opción de motor gráfico (3.2) se elija, y siguen siendo estimaciones de ingeniería, no medición real:

| Componente | Estimación |
|---|---|
| libFLAC (solo decoder, sin encoder, sin soporte Ogg) | ~80-150 KB (reducido desde ~150-300 KB de un build completo encoder+decoder, ver sección 3.8.1) |
| Motor gráfico (Opción A: rasterización manual) | ~20-50 KB de código propio |
| Fuente bitmap embebida | ~10-30 KB (una fuente bitmap monoespaciada completa es pequeña) |
| Capa Java/Kotlin mínima (Service + Notification + puente USB, con R8 shrinking) | ~50-150 KB (esto es lo mínimo irreducible del lado framework) |
| AndroidManifest + firma + overhead de empaquetado | ~50-100 KB |
| **TOTAL APK (Opción A confirmada, decoder-only, -Os, símbolos strippeados)** | **~210-480 KB** |

**RAM en uso activo:**

| | Estimación |
|---|---|
| Proceso base de Android (Zygote fork mínimo, sin runtime pesado de Kotlin/Compose) | ~5-8 MB |
| Buffers de audio (PCM en streaming, cola) | ~2-5 MB |
| Buffer gráfico (framebuffer de la UI, pequeño dado que es solo texto sin capas complejas) | ~1-3 MB |
| Biblioteca en memoria: jerarquía de carpetas + títulos cacheados (sección 3.9, ~3,000 canciones de referencia) | ~0.15 MB |
| **TOTAL RAM estimado** | **~8-16 MB** (el incremento de 3.9 es inferior al margen de redondeo de las demás estimaciones) |

Esto sí representa una reducción real y significativa frente a la versión Kotlin/Views (~15-25MB) o Compose (~30-45MB) de la iteración anterior — la diferencia viene principalmente de evitar el runtime de ART/Kotlin cargando clases de framework y el overhead de cualquier sistema de UI administrado, que es exactamente el costo que identificamos en el análisis previo.

**Nota de honestidad para el documento de transparencia (sección 6):** estas cifras de "~210-480KB de APK y ~8-16MB de RAM" son las que se pueden comunicar públicamente **solo después de verificarlas con una build real** — no antes. Publicar una cifra de marketing sin haberla medido en un build de release firmado sería contrario al principio de honestidad que motiva este documento.

---

## 5. Fragmentación de hardware — Obligación de comunicación pública

Este es el punto más delicado del proyecto como software público, y merece tratamiento explícito porque la promesa central del producto (bit-perfect) no es algo que el equipo pueda garantizar universalmente.

### 5.1 Lo que se sabe con evidencia (de esta conversación)

- El comportamiento de AAudio EXCLUSIVE/MMAP varía por fabricante de SoC y por cómo cada OEM configuró su HAL — confirmado con evidencia real de issues de GitHub del proyecto Oboe de Google mostrando fallos silenciosos específicos en hardware Samsung/Exynos bajo ciertas condiciones (altavoz interno, Dolby Atmos activo).
- **Este modo de fallo no es solo "degradación silenciosa a SHARED" — puede ser silencio total de audio**, un fallo más grave que simplemente perder bit-perfect. Los reportes documentados (Exynos con altavoz interno + MMAP habilitado) muestran ausencia completa de sonido, no solo pérdida de calidad. Esto debe comunicarse explícitamente en el documento de transparencia pública (sección 6) y, idealmente, como una nota práctica de troubleshooting: si la app no produce ningún sonido en un dispositivo Samsung/Exynos específico, una causa conocida es la interacción entre MMAP y mejoras de audio de fábrica como Dolby Atmos — desactivarlas en Ajustes de sonido del sistema es un paso de diagnóstico razonable antes de asumir que la app está rota.
- No existe una lista pública confiable, mantenida por Google o por los fabricantes, de qué chipsets/dispositivos soportan exclusive de forma consistente — la única forma confiable de saberlo es probar en el dispositivo específico.
- Esto significa que **el propio equipo del proyecto no puede, de buena fe, publicar una lista de "dispositivos compatibles"** sin haberlos probado uno por uno, lo cual no es viable para un proyecto pequeño de código abierto.

### 5.2 Estrategia de comunicación requerida (no opcional)

1. El indicador bit-perfect en la UI (v1.0 sección 3.4) es la primera línea de honestidad — se mantiene sin cambios y es innegociable.
2. El README público del repositorio debe incluir, de forma prominente (no al final, no en letra pequeña): una sección explícita titulada algo como **"Sobre la promesa de bit-perfect"** que explique, en lenguaje simple, que el resultado depende del hardware del usuario, por qué (fragmentación de HAL entre fabricantes), y cómo el usuario puede verificar su propio caso (el indicador en tiempo real de la app).
3. **`[DECISIÓN PENDIENTE]`**: evaluar si el proyecto debe mantener una lista pública, alimentada por reportes de la comunidad (ej. un archivo `COMPATIBILITY.md` donde los propios usuarios reporten vía pull request o issue si consiguieron bit-perfect real en su dispositivo específico) — esto sería coherente con el espíritu open source (la comunidad genera el dato que el equipo no puede generar solo) pero requiere definir un proceso de verificación mínimo para evitar reportes falsos o mal interpretados.
4. Ningún material de marketing/README puede usar frases como "bit-perfect garantizado" o "compatible con cualquier DAC" sin calificarlas inmediatamente con la limitación real. La redacción sugerida por defecto es del tipo: *"[Nombre del proyecto] solicita al sistema operativo el modo de audio más directo posible (bit-perfect) disponible en tu dispositivo. Esto depende de tu hardware específico — la app te dice en tiempo real si lo consiguió y por qué, si no."*

---

## 6. Documento de Transparencia Pública — Especificación de su contenido

Se confirmó que el proyecto requiere, además del código abierto en sí, un documento de transparencia técnica explícito acompañando cada release. Este documento (llamémoslo `TRANSPARENCY.md` o `DESIGN_DECISIONS.md` en el repositorio) debe contener, como mínimo:

1. **Qué hace la app, exhaustivamente** — equivalente a la sección 1 de v1.0, en lenguaje accesible a usuarios no técnicos.
2. **Qué NO hace la app, y por qué fue una decisión deliberada, no una limitación técnica no resuelta** — equivalente a v1.0 sección 1.2, reescrito para público general (ej. "no tenemos ecualizador porque cada etapa de procesamiento de audio adicional es una oportunidad de romper bit-perfect — si quieres EQ, esta no es tu app, y está bien, para eso existen Poweramp/Neutron").
3. **Los trade-offs de arquitectura tomados y su costo real** — esto incluye explícitamente la decisión de UI 100% nativa en C/C++ (sección 3 de este documento) con su costo de mantenibilidad reconocido (menos contribuidores potenciales, más superficie de bugs de bajo nivel como manejo manual de memoria e input) frente a su beneficio (RAM/tamaño mínimos). No ocultar que esta decisión tiene un costo, incluso cuando se está orgulloso del resultado.
4. **Los límites de la promesa bit-perfect**, como se detalla en la sección 5.2 de este documento, incluyendo la nota práctica de troubleshooting sobre silencio total en hardware Samsung/Exynos con Dolby Atmos u otras mejoras de audio de fábrica activas (sección 5.1).
5. **Permisos solicitados y por qué cada uno es estrictamente necesario** — lista exhaustiva (equivalente a v1.0 sección 4.5), con una línea de justificación por permiso, para que cualquier usuario técnico o no técnico entienda exactamente qué puede tocar la app y qué no (ej. "no pedimos permiso de Internet porque la app nunca se conecta a ninguna red, verificable en el código fuente en [ruta del archivo de manifiesto]").
6. **Versión mínima de Android soportada y la razón de esa elección** (sección 2 de este documento), sin ocultar que es una decisión de alcance reducido a cambio de estabilidad, y que excluye dispositivos más viejos deliberadamente.
7. **Estimaciones de tamaño/RAM, marcadas explícitamente como medidas reales de una build específica (con número de versión y fecha) o como estimaciones de diseño aún no verificadas** — nunca presentar una estimación como si fuera una medición confirmada.

---

## 7. Preguntas abiertas actualizadas (reemplaza la sección 5 de v1.0)

**Resueltas en esta revisión:**
- ~~Compose vs Views~~ — obsoleta, la UI es 100% C/C++ nativo, no aplica ninguna de las dos.
- ~~Scoped Storage bloqueando acceso a Music/~~ — resuelto en sección 3.7: `Music/` es carpeta de medios reconocida, FUSE permite File API directo, no se necesita SAF ni MediaStore.
- ~~Opción A vs B de motor gráfico~~ — resuelto en sección 3.2: Opción A (rasterización manual) confirmada como decisión final.
- ~~Si la lógica de volumen/DAC es específica a un fabricante~~ — resuelto en sección 3.5: ya no depende de detectar el fabricante ni el estándar que expone — es una pregunta explícita al usuario, universal por construcción, sin importar qué DAC conecten.
- ~~Optimización final de tamaño de libFLAC~~ — resuelto en sección 3.8: decoder-only, sin Ogg, `-Os`, símbolos strippeados.
- ~~Parsing de descriptor USB Audio Class para detectar `FU_VOLUME_CONTROL`~~ — descartado por completo en sección 3.5. Se reemplaza por confirmación explícita del usuario, con bloqueo de reproducción hasta responder en el primer uso de cada DAC nuevo, y memoria por vendor/product ID.
- ~~El indicador bit-perfect no reflejaba el efecto del volumen por software~~ — resuelto en sección 3.6: se añade el estado `BIT-PERFECT: PARCIAL`, cruzando el resultado de AAudio con el estado de volumen configurado en la sección 3.5.
- ~~Falta de contexto en la pregunta bloqueante de volumen para un usuario primerizo~~ — resuelto en sección 3.5: se añaden dos líneas de contexto antes de la pregunta, sin convertirlo en onboarding.
- ~~El documento no distinguía "degradación silenciosa a SHARED" de "silencio total de audio" como modos de fallo distintos~~ — resuelto en sección 5.1: se documenta explícitamente el riesgo de silencio total en Samsung/Exynos con MMAP + Dolby Atmos, con nota de troubleshooting para el documento de transparencia.

**Aún pendientes:**

1. **(Sección 3.1)** Confirmar el patrón exacto de comunicación JNI mínima entre la Activity/Service de Java y la capa C/C++ — específicamente cómo se le pasan al código nativo los callbacks de ciclo de vida y los eventos de conexión/desconexión USB sin introducir overhead innecesario en ese puente.
2. **(Sección 3.3)** Elegir la fuente bitmap específica a embeber (Spleen/Terminus/Tamzen u otra) verificando que su licencia exacta es compatible con distribución en un proyecto open source (revisar si son de dominio público, MIT, o licencia similar permisiva).
3. **(Sección 5.2, punto 3)** Decidir si se implementa el archivo `COMPATIBILITY.md` alimentado por la comunidad desde el lanzamiento inicial o se pospone a una iteración posterior.
4. Confirmar en pruebas reales con el UA7 que el flujo de la sección 3.5 (pregunta explícita, bloqueo hasta responder, persistencia por vendor/product ID) funciona correctamente end-to-end — dado que ya no depende de parsear el descriptor de audio, la superficie de prueba se reduce a: lectura correcta de vendor/product ID vía `UsbManager`, y que la persistencia por dispositivo funcione al reconectar el mismo DAC.
5. **Nueva pregunta introducida por el cambio a proyecto público:** ¿bajo qué licencia de código abierto se publica el proyecto (MIT, GPL, Apache 2.0, etc.)? Esto no es un detalle menor — afecta si terceros pueden hacer forks comerciales, si contribuciones externas requieren cesión de derechos, y es información que un desarrollador retomando este documento necesita antes de subir el primer commit público.

---

## 8. Resumen ejecutivo actualizado

- **Cambio de alcance:** de herramienta personal a proyecto open source público — introduce obligación de transparencia formal (sección 6) y soporte a hardware no controlado por el equipo (sección 5).
- **UI:** 100% C/C++ nativo vía NDK, sin framework de UI de Android, con motor gráfico propio confirmado como rasterización manual (Opción A, sección 3.2) — decisión de máximo ahorro de recursos, con costo reconocido de mantenibilidad y superficie de bugs de bajo nivel.
- **Target:** Android 8.0 (API 26) como `minSdk` (actualizado en v1.2.0; era API 31 — ver sección 2 para el razonamiento completo); `targetSdk` debe seguir el mínimo que exija Google Play/F-Droid al momento de cada release (API 35-36 en 2026), lo cual implica mantenimiento continuo del proyecto para no quedar obsoleto ante usuarios nuevos.
- **Tamaño estimado:** ~210-480 KB de APK (tras optimizaciones de la sección 3.8: libFLAC decoder-only, `-Os`, símbolos strippeados) más ~15-45 KB por las mejoras de biblioteca de la sección 3.9; ~8-16 MB de RAM en uso — cifras a verificar con build real antes de comunicarlas públicamente.
- **Audio:** libFLAC decoder-only vía JNI/NDK + AAudio EXCLUSIVE con verificación real de `isMMapUsed()` + indicador bit-perfect de tres estados (SI/PARCIAL/NO) que cruza el resultado de AAudio con el estado real de volumen, con detalle completo en Configuración (sección 3.6).
- **Volumen:** confirmación explícita del usuario por DAC conectado, sin parsing de descriptores USB, con bloqueo de reproducción hasta responder en cada DAC nuevo (sección 3.5) — nunca atenuación digital de software como alternativa.
- **Biblioteca:** escaneo recursivo de `Music/` con jerarquía de carpetas preservada, título de tag con fallback a nombre de archivo, y caché entre sesiones (sección 3.9) — metadata completa (álbum/artista) descartada deliberadamente.
- **Distribución:** GitHub (código y documentación) + F-Droid (APK) como canales principales; Play Store queda como opción secundaria posible más adelante, dado el costo de mantenimiento continuo de `targetSdk` y la barrera de 12 testers/14 días para cuentas nuevas.
- **Transparencia:** documento público obligatorio (`TRANSPARENCY.md`) detallando qué se hace, qué no se hace y por qué, límites reales de bit-perfect, y todos los trade-offs de arquitectura sin ocultar sus costos.
