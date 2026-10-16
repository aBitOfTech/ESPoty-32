# Spotify Cover para ESP32-S3 Touch LCD 1.28

Reproductor de Spotify para la pantalla redonda GC9A01 de 240x240: muestra la portada a pantalla completa, el título y el artista de la canción que suena, un anillo de progreso en el borde y tres botones táctiles para controlar la reproducción. Está preparado para **Waveshare ESP32-S3-Touch-LCD-1.28** (CST816S + GC9A01); no requiere cableado externo.

## Funciones

- Portada del álbum recortada al centro y escalada a 240x240, con un degradado oscuro en la parte inferior para que el texto sea legible.
- Título en grande (con desplazamiento horizontal si no cabe) y artista debajo.
- Anillo de progreso alrededor del borde que avanza en tiempo real entre consultas.
- Botones de estilo "cristal" para anterior, reproducir/pausar y siguiente.
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
- Tocar en cualquier punto por encima de los botones también alterna reproducir/pausar.
- Reproducir/pausar se refleja al instante en pantalla; si Spotify rechaza la orden, el estado se revierte y el error aparece en naranja en lugar del artista.

## Cómo funciona

- El estado del reproductor (`GET /v1/me/player`) se consulta cada 3 segundos y justo después de cada orden de anterior/siguiente.
- La red (Wi-Fi, HTTPS y decodificación JPEG) corre en una tarea en el núcleo 0; el táctil y el dibujado, en el `loop()` del núcleo 1, así que la interfaz no se congela durante las peticiones.
- Las conexiones TLS a la API y al CDN de portadas se mantienen abiertas (keep-alive) para evitar un handshake de ~1 s por petición.
- De las imágenes que ofrece Spotify se descarga la más pequeña que cubre los 240 px, y se decodifica en un búfer doble en PSRAM.
- Todo se compone en un sprite en PSRAM y se envía de una vez a la pantalla; el título se redibuja a ~30 fps solo cuando se desplaza y el anillo unas 4 veces por segundo.

## Pines

Los pines están en `include/pins.h`; es el único archivo que hay que cambiar para otra placa ESP32-S3 + GC9A01/CST816S. Son las conexiones internas entre el ESP32-S3 y la pantalla/touch: SPI SCLK/MOSI/MISO/CS/DC/RST/BL = 10/11/12/9/8/14/2; I²C SDA/SCL y touch RST/IRQ = 6/7/13/5 (dirección I²C del CST816S: `0x15`). No tienes que conectarlos manualmente. El USB-C integrado usa el conversor USB-serie de la placa para cargar el firmware.

## Limitaciones conocidas

- Las fuentes incluidas solo tienen ASCII: los acentos latinos se muestran sin tilde (`é` → `e`) y se omiten emojis y otros símbolos.
- La conexión HTTPS usa `setInsecure()` para evitar cargar un bundle de CA en flash; para un dispositivo expuesto a redes no confiables conviene añadir validación de certificado.
- La portada está limitada a 300 KB para no agotar la PSRAM; si la descarga o la decodificación fallan se muestra un fondo oscuro.
- Solo se muestra el primer artista de cada canción.
