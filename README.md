# Spotify Cover para ESP32-S3 Touch LCD 1.28

Muestra la canción que está sonando en Spotify y su portada en la pantalla redonda integrada GC9A01 de 240x240. Está preparado para **Waveshare ESP32-S3-Touch-LCD-1.28** (CST816S + GC9A01); no requiere cableado externo.

## Requisitos

- PlatformIO
- ESP32-S3 con PSRAM
- Cuenta Spotify Premium con reproducción activa para que exista `currently-playing`
- Una app creada en [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
- Cable USB-C de datos para alimentar, subir el firmware y ver el monitor serie

## Configuración

1. En Spotify Dashboard añade como Redirect URI `http://127.0.0.1:8888/callback`.
2. Obtén el refresh token con Python:

```sh
python tools/get_refresh_token.py CLIENT_ID CLIENT_SECRET
```

3. Copia `include/secrets.example.h` a `include/secrets.h` y completa Wi-Fi, credenciales y el token impreso.
4. Conecta la placa y ejecuta:

```sh
pio run -t upload
pio device monitor
```

Si la carga automática no entra en modo programación: mantén pulsado **BOOT**, pulsa y suelta **RESET**, suelta **BOOT** y vuelve a ejecutar `pio run -t upload`. **RESET** solo reinicia la placa.

El refresh token se obtiene una vez mediante OAuth Authorization Code con los permisos `user-read-currently-playing` y `user-read-playback-state`. No se guarda el access token en flash: se renueva en RAM.

## Pines

Los pines están en `include/pins.h`. Son las conexiones internas entre el ESP32-S3 y la pantalla/touch: SPI SCLK/MOSI/MISO/CS/DC/RST/BL = 10/11/12/9/8/14/2; I²C SDA/SCL y touch RST/IRQ = 6/7/13/5. No tienes que conectarlos manualmente. El USB-C integrado usa el conversor USB-serie de la placa para cargar el firmware.

## Limitaciones conocidas

- La pantalla se actualiza cada 5 segundos y un toque fuerza un refresco.
- La franja inferior tiene tres controles táctiles: anterior, pausa/reanudar y siguiente.
- La conexión HTTPS usa `setInsecure()` para evitar cargar un bundle de CA en flash; para un dispositivo expuesto a redes no confiables conviene añadir validación de certificado.
- La portada está limitada a 300 KB para no agotar la PSRAM.

