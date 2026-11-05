# Spotify Cover para ESP32-S3 Touch LCD 1.28

[English](README.md) | **Español**

Reproductor de Spotify para la pantalla redonda GC9A01 de 240x240: muestra la portada a pantalla completa, el título y el artista de la canción que suena, un anillo de progreso en el borde y tres botones táctiles para controlar la reproducción. Está preparado para **Waveshare ESP32-S3-Touch-LCD-1.28** (CST816S + GC9A01); no requiere cableado externo.

## Funciones

- Portada del álbum recortada al centro y escalada a 240x240, con un degradado oscuro en la parte inferior para que el texto sea legible.
- Título en grande (con desplazamiento horizontal si no cabe) y artista debajo.
- Anillo de progreso alrededor del borde que avanza en tiempo real entre consultas.
- Botones de estilo "cristal" para anterior, reproducir/pausar y siguiente.
- Vista de letra sincronizada línea a línea, que se abre con un botón arriba: la línea que suena en grande en el centro sobre la portada oscurecida, con la anterior y la siguiente atenuadas. La letra viene de [LRCLIB](https://lrclib.net).
- La interfaz gira a cualquier ángulo según la posición de la placa (acelerómetro QMI8658), así que siempre queda derecha.
- Pantallas de estado: `Conectando WiFi`, `Falta Spotify` (no hay credenciales o el token no es válido) y `Nada reproduciéndose`.

## Requisitos

- PlatformIO
- ESP32-S3 con PSRAM (la Waveshare ESP32-S3-Touch-LCD-1.28 trae 16 MB de flash y PSRAM)
- Python 3 (solo librería estándar) para obtener el refresh token
- Una app creada en [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
- Cuenta Spotify Premium para los controles de reproducción (la API de Spotify los rechaza en cuentas gratuitas)
- Cable USB-C de datos para alimentar y subir el firmware

## Configuración

1. En Spotify Dashboard añade como Redirect URI `http://127.0.0.1:8888/callback`.
2. Obtén el refresh token:

```sh
python tools/get_refresh_token.py CLIENT_ID CLIENT_SECRET
```

   El script abre el navegador, espera la autorización en `127.0.0.1:8888` e imprime la línea `#define SPOTIFY_REFRESH_TOKEN "..."` lista para pegar.

3. Copia `include/secrets.example.h` a `include/secrets.h` y completa Wi-Fi, credenciales y el token impreso. `secrets.h` está en `.gitignore`.
4. Conecta la placa y ejecuta:

```sh
pio run -t upload
```

Si la carga automática no entra en modo programación: mantén pulsado **BOOT**, pulsa y suelta **RESET**, suelta **BOOT** y vuelve a ejecutar `pio run -t upload`. **RESET** solo reinicia la placa.

El refresh token se obtiene una vez mediante OAuth Authorization Code con los permisos `user-read-currently-playing`, `user-read-playback-state` y `user-modify-playback-state`. El access token no se guarda en flash: se renueva en RAM al arrancar y cuando Spotify responde 401.

Si los botones muestran `Sin permiso: renueva el token`, el refresh token se generó sin `user-modify-playback-state` (por ejemplo, con una versión anterior del script): vuelve a ejecutar el paso 2 y actualiza `secrets.h`.

## Uso

- La franja inferior tiene tres zonas táctiles: anterior, reproducir/pausar y siguiente. Las zonas son más anchas que los botones dibujados.
- El botón pequeño de arriba (tres líneas) abre la vista de letra y, al tocarlo otra vez, vuelve a la vista normal. Su zona táctil es un círculo algo más ancho que el botón.
- Tocar en cualquier otro punto por encima de los botones también alterna reproducir/pausar, en las dos vistas.
- En la vista de letra aparece `Buscando letra...` durante hasta una consulta (3 s) más la búsqueda; después, la letra, `Sin letra sincronizada`, `Instrumental` o `No se pudo cargar la letra` (falló la búsqueda). En pausa, la línea se queda fija. Si las líneas van adelantadas o atrasadas, ajusta `LYRICS_OFFSET_MS` en `src/app_config.h` (un valor positivo las muestra antes).
- Gira la placa y la interfaz gira con ella para quedar derecha; las zonas táctiles siguen la rotación. Tumbada (a menos de ~30° de la horizontal) mantiene la última orientación.
- Reproducir/pausar se refleja al instante en pantalla; si Spotify rechaza la orden, el estado se revierte y el error aparece en naranja en lugar del artista.

## Cómo funciona

- El estado del reproductor (`GET /v1/me/player`) se consulta cada 3 segundos y justo después de cada orden de anterior/siguiente.
- La red (Wi-Fi, HTTPS y decodificación JPEG) corre en una tarea en el núcleo 0; el táctil y el dibujado, en el `loop()` del núcleo 1, así que la interfaz no se congela durante las peticiones.
- Las conexiones TLS a la API y al CDN de portadas se mantienen abiertas (keep-alive) para evitar un handshake de ~1 s por petición.
- De las imágenes que ofrece Spotify se descarga la más pequeña que cubre los 240 px, y se decodifica en un búfer doble en PSRAM.
- La letra solo se busca con la vista de letra abierta, una vez por canción: `GET https://lrclib.net/api/get` con artista, título, álbum y duración, y un reintento sin el álbum si LRCLIB responde 404. La petición usa su propia conexión TLS, así que el token de Spotify nunca llega a LRCLIB, y HTTP/1.0 para que el cuerpo llegue sin chunks. Del JSON solo se conservan `syncedLyrics` e `instrumental`; el LRC se analiza en un búfer doble en PSRAM. Mientras dura una búsqueda (normalmente 1–2 s; la conexión, el handshake TLS y la descarga tienen cada uno un límite de 5 s), la tarea de red está ocupada y las pulsaciones llegan a Spotify cuando termina.
- El acelerómetro se lee unas 30 veces por segundo; el vector de gravedad se filtra y el sprite se envía girado (`pushRotateZoomWithAA`) solo cuando el ángulo cambia más de 1°. Si la interfaz gira al revés o queda inclinada, ajusta `IMU_ANGLE_SIGN` / `IMU_ANGLE_OFFSET` en `src/app_config.h`.
- Todo se compone en un sprite en PSRAM y se envía de una vez a la pantalla; el título se redibuja a ~30 fps solo cuando se desplaza y el anillo unas 4 veces por segundo. En la vista de letra, la capa de texto solo se rehace cuando cambia la línea activa.

## Estructura del proyecto

| Ruta | Contenido |
|---|---|
| `src/main.cpp` | `setup()` y el bucle de dibujado (núcleo 1): táctil, rotación y cuándo redibujar |
| `src/network_task.*` | Tarea de red (núcleo 0): Wi-Fi, consultas, órdenes y cambio de portada y letra |
| `src/spotify_client.*` | Web API de Spotify: renovación del token, estado del reproductor, órdenes |
| `src/cover_loader.*` | Descarga de portadas, decodificación JPEG y recorte a 240x240 |
| `src/lyrics_client.*` | Búsqueda de letra sincronizada en LRCLIB |
| `src/renderer.*`, `src/display.h` | Dibujado: portada, degradado, título, botones de cristal, vista de letra, anillo de progreso, rotación |
| `src/player_store.*` | Estado compartido entre los dos núcleos (solo accesible con su candado) y la cola de órdenes |
| `src/touch.*`, `src/orientation.*`, `src/i2c_bus.*` | Táctil CST816S, acelerómetro QMI8658 y el bus I²C que comparten |
| `src/app_config.h`, `src/theme.h` | Tiempos, límites y calibración; colores y textos en pantalla |
| `lib/core/` | Lógica independiente del hardware (colores, desenfoque, acentos, geometría, progreso, análisis y ajuste de LRC, codificación de URL), cubierta por los tests |
| `test/test_core/` | Tests unitarios de `lib/core` |

### Tests

La lógica de `lib/core` se prueba en el PC con Unity:

```sh
pio test -e native
```

Hace falta un compilador de C++ para PC (gcc, por ejemplo MinGW-w64 en Windows) en el `PATH`. `pio run` y `pio run -t upload` solo compilan el firmware.

## Pines

Los pines están en `include/pins.h`; es el único archivo que hay que cambiar para otra placa ESP32-S3 + GC9A01/CST816S. Son las conexiones internas entre el ESP32-S3 y la pantalla/touch: SPI SCLK/MOSI/MISO/CS/DC/RST/BL = 10/11/12/9/8/14/2; I²C SDA/SCL y touch RST/IRQ = 6/7/13/5 (direcciones I²C: CST816S `0x15`, acelerómetro QMI8658 `0x6B` en el mismo bus). No tienes que conectarlos manualmente. El USB-C integrado usa el conversor USB-serie de la placa para cargar el firmware.

## Limitaciones conocidas

- Las fuentes incluidas solo tienen ASCII: los acentos latinos se muestran sin tilde (`é` → `e`) y se omiten emojis y otros símbolos.
- La conexión HTTPS usa `setInsecure()` para evitar cargar un bundle de CA en flash; para un dispositivo expuesto a redes no confiables conviene añadir validación de certificado.
- La portada está limitada a 300 KB para no agotar la PSRAM; si la descarga o la decodificación fallan se muestra un fondo oscuro.
- Solo se muestra el primer artista de cada canción.
- La letra viene de LRCLIB, un servicio de terceros: mientras la vista de letra está abierta, recibe el título, el artista, el álbum y la duración de cada canción que suena. No todas las canciones tienen letra sincronizada, y la letra sin sincronizar no se muestra. Las líneas en alfabetos no latinos aparecen como `...`. La conexión a LRCLIB también usa `setInsecure()`.
- La letra se corta en 300 líneas, 16 KB de texto y una respuesta de 96 KB.