# KickDuck (VST3, JUCE)

Ducking de bajo disparado por el kick, con corrección de fase/tiempo.

## Compilar
Requisitos: CMake 3.22+, Git y un compilador C++17 (Visual Studio 2022 en Windows, Xcode en Mac).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --target KickDuck_VST3

El primer build descarga JUCE 8.0.6. El VST3 se copia solo a la carpeta de plugins del sistema
(en Windows, ejecuta la terminal como administrador o copia `KickDuck.vst3` a mano).

## Ruteo
Pon KickDuck en la pista del bajo y envia el kick a su entrada sidechain:
- FL Studio: en el mixer, clic derecho en el canal del kick > "Sidechain to this track"; luego elige esa entrada en las opciones del plugin.
- Ableton: usa el selector de sidechain del plugin y elige la pista del kick.
- Logic / Reaper / Pro Tools: envio del kick a la entrada sidechain/key.

## Modos de disparo
- Preciso (por defecto): detecta el kick por audio con 14 ms de lookahead; la latencia se informa al DAW.
- Rapido: audio sin lookahead, latencia 0.
- MIDI: dispara con notas MIDI, latencia 0 (el DAW debe enviar MIDI al plugin).

## Fase y tiempo
- Fase: rotacion con all-pass de primer orden, exacta en la frecuencia "Graves LP".
- Delay: -14 a +20 ms (negativo solo en modo Preciso, usa el lookahead). No se informa como latencia.
- Auto-fase: captura 4 kicks, prueba polaridad/fase/delay y deja el de mayor correlacion. Los bloqueos protegen lo que ajustaste a mano. "Deshacer" vuelve al ajuste anterior.

## Estado
Compila en Linux (JUCE 8.0.6). Sin probar en un DAW: ajusta valores y revisa el audio con cuidado.

## Compilar con GitHub Actions
Sube la carpeta a un repositorio. En la pestana **Actions** corre "Build VST3" y deja el plugin para Windows, macOS y Linux
en la seccion **Artifacts** del run. En macOS el plugin no esta firmado: si el DAW lo bloquea, ejecuta
`xattr -cr KickDuck.vst3`.
