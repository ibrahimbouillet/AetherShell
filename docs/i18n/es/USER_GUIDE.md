# Usar Aether Shell

🇬🇧 [English](../../../USER_GUIDE.md) · 🇫🇷 [Français](../fr/USER_GUIDE.md) · 🇪🇸 Español ·
🇨🇳 [中文](../zh-CN/USER_GUIDE.md) · 🇮🇳 [हिन्दी](../hi/USER_GUIDE.md) · 🇧🇷 [Português](../pt/USER_GUIDE.md) · 🇷🇺 [Русский](../ru/USER_GUIDE.md) ·
🇸🇦 [العربية](../ar/USER_GUIDE.md) · 🇧🇩 [বাংলা](../bn/USER_GUIDE.md) · 🇵🇰 [اردو](../ur/USER_GUIDE.md)

Abre Aether Shell desde su acceso directo del menú Inicio o desde el iniciador instalado. Abre dos elementos de
escritorio: el Dock en la parte inferior y la barra de estado en la superior. El Explorador de Windows sigue
funcionando con normalidad. Los elementos del Dock tienen menús contextuales. Selecciona el logotipo de la barra
superior para abrir su menú, que también ofrece Configuración y Salir.

Abre la búsqueda desde el Dock o con Ctrl+Alt+Espacio. Escribe el nombre de una aplicación, usa Arriba/Abajo para
seleccionarla e Intro para abrirla. Esc cierra la ventana de búsqueda, igual que usar cualquier otra cosa, y cada
apertura empieza vacía. También encuentra herramientas de Windows como el Administrador de dispositivos. Solo se pueden
abrir aplicaciones descubiertas; el texto escrito nunca es un comando. Ctrl+Alt+D pone el foco en el Dock. Ambos atajos
se configuran en Configuración → General, donde se avisa de los conflictos.

El Dock distingue las aplicaciones ancladas de las demás aplicaciones abiertas. Usa su menú contextual o Configuración
→ Dock para anclar, desanclar o reordenar elementos y ajustar el tamaño, la ampliación y el movimiento. Una aplicación
abierta se puede activar desde su elemento del Dock. La atribución de algunas ventanas UWP aún debe cualificarse.
Alpha 3: al pasar el puntero sobre una aplicación abierta, su marcador se ensancha y, tras una breve pausa, aparece una
tarjeta con vistas previas de sus ventanas. Haz clic en una vista previa para cambiar a esa ventana, o en su botón de
cierre para pedir a esa ventana que se cierre normalmente; la aplicación decide si se cierra (por ejemplo, tras pedir
guardar).

La barra superior muestra datos obtenidos de API documentadas de Windows. Un estado desconocido sigue siendo
desconocido. Un PC de sobremesa sin batería puede ocultar el elemento de batería. No hay indicador de micrófono en uso;
la disponibilidad del dispositivo y el silencio son datos aparte. Haz clic en los controles de audio disponibles solo
cuando quieras cambiar el volumen o silenciar. La barra superior nunca muestra el nombre de una red. Desde la 3.3: en
los controles rápidos, la flecha del mosaico Wi-Fi muestra las redes cercanas con su señal. Elige una para unirte;
Aether pide su contraseña si la necesita, y Windows la guarda. Los nombres y las contraseñas nunca se registran ni se
envían. La Alpha 2 añade el cambio de idioma de entrada con Win+Espacio desde el elemento de teclado, y la
configuración del Tiempo desde el elemento del tiempo. El grupo de red/sonido/batería abre los controles rápidos de
Aether, con el control multimedia cuando Windows ofrece una sesión activa, el volumen, el brillo compatible y mosaicos
de acciones del sistema. Los interruptores no compatibles abren los controles de Windows correspondientes en lugar de
fingir que cambian un dispositivo. La cualificación con dispositivos reales sigue siendo limitada. Un clic en el
micrófono lo silencia o lo activa; su menú contextual ofrece la configuración de privacidad e **Iniciar el dictado por
voz de Windows (Win+H)**. La acción de voz devuelve primero el foco a la aplicación anterior y luego pide a Windows que
abra el dictado en su campo activo. Windows controla la disponibilidad del dictado, el permiso del micrófono y el
procesamiento; Aether Shell no captura ni guarda la voz. Las pruebas automáticas no usan un dictado real.

La Alpha 3 añade tres puntos de acceso a la barra superior. Haz clic en la fecha y la hora para abrir el centro de
notificaciones de Windows (Win+N en Windows 11, Win+A en Windows 10); su menú también abre el calendario. Aether nunca
lee, borra ni cuenta tus notificaciones. La pequeña flecha abre justo debajo los iconos ocultos del área de
notificación de Windows; apunta hacia arriba mientras están abiertos, y un segundo clic los cierra. Oculta la flecha con
Configuración → Barra superior → **Mostrar el botón de iconos ocultos**. El grupo de red/sonido/batería abre el panel de
controles rápidos de Aether, con una segunda página y vistas de detalle para Wi-Fi, Bluetooth, accesibilidad y salida de
sonido; los interruptores que Windows se reserva abren la página correspondiente de la Configuración de Windows. En
Windows 11, **Usar la configuración rápida de Windows (Windows 11)** en la misma página abre en su lugar el panel de
Windows.

**Idioma** (Alpha 4): Aether Shell, instalación incluida, habla inglés, chino (simplificado), hindi, español,
francés, portugués, ruso, árabe, bengalí y urdu. Configuración → **Idioma** permite elegir uno; **Igual que
Windows**, la opción predeterminada, recorre tus idiomas de Windows en el orden en que los colocaste y usa el primero
que sea uno de los diez y, si no, el inglés. El cambio se aplica al momento a las barras, los menús, la búsqueda y la
Configuración, y tu elección se guarda en tu configuración, que lee cada versión: una nueva versión, una reparación o
una reinstalación la vuelve a encontrar, y la instalación, el actualizador y el lanzador hablan ese idioma. Con un
idioma elegido, la barra superior escribe la fecha en ese idioma; con «Igual que Windows», la fecha conserva tu
formato regional de Windows. Los nombres de aplicaciones, archivos y redes nunca se traducen. La búsqueda muestra las
herramientas de Windows y las páginas de Configuración en el idioma de la interfaz y las encuentra por su nombre en
cualquiera de los diez. El árabe y el urdu se leen de derecha a izquierda.

La Configuración tiene las páginas Dock, Barra superior, Tiempo, Actualizaciones, General, Acerca de, Escritorio,
Apariencia, Bienvenida e Idioma. El tiempo empieza desactivado. Antes de activarlo, elige la ubicación automática de Windows
(con tu consentimiento) o una ubicación manual. Windows puede denegar la ubicación; en ese caso no se inventa ninguna
ubicación ni previsión. La búsqueda manual de un lugar envía el nombre escrito a Open-Meteo; las previsiones envían las
coordenadas aproximadas elegidas. La ubicación nunca se deduce del nombre de una red Wi-Fi ni de una dirección IP. Las
previsiones en caché desaparecen a las seis horas. Atribución de los datos: Open-Meteo.com, CC BY 4.0.

General reúne el consentimiento de inicio, las preferencias de movimiento y la edición de atajos. Una instalación
interactiva nueva (desde la Alpha 2) marca de forma visible el inicio al iniciar sesión; desmarca esa opción antes de
instalar para rechazarlo. Se conservan las elecciones existentes. La instalación silenciosa requiere la opción de inicio
explícita. El inicio es por usuario y opcional; Configuración → General → Aplicar puede cambiarlo, y el Administrador
de tareas puede desactivarlo por su cuenta. Windows decide el orden y el retraso del inicio tras iniciar sesión; puede
mostrar el escritorio y la barra de tareas nativos antes de que Aether esté listo. El iniciador estable instalado
reintenta un número limitado de veces tras fallos anómalos. Salir a propósito, una actualización, una desinstalación o
un error normal lo detienen; ningún proceso puede garantizar que sobrevive a cualquier fallo. El contraste alto y el
movimiento reducido siguen a Windows cuando es posible. La página Acerca de contiene la licencia MIT y la atribución
del proyecto.

En la versión V2, Escritorio contiene **Sustituir la barra de tareas de Windows**, activado de forma predeterminada
(D23). Se conserva un rechazo explícito guardado. Desmárcalo y pulsa Aplicar para volver a la convivencia; márcalo y
pulsa Aplicar para volver a activar la sustitución. Cerrar o cambiar de página descarta un cambio sin aplicar. En el
modo de sustitución, el Dock reserva espacio bajo las aplicaciones maximizadas. Puede que haya que mover las ventanas
colocadas a mano. El mensaje de estado indica si la sustitución está activa o no disponible. La Configuración de Windows
conserva su página nativa de la barra de tareas; ajusta aquí el comportamiento de Aether Shell. En el modo de
sustitución de la Alpha 3, las ventanas maximizadas admitidas reciben esquinas redondeadas y un borde fino, sin cambiar
su tamaño ni su estado maximizado; Aether lo deshace al cerrarse. Algunas aplicaciones y tipos de ventana mantienen
esquinas rectas. Desde la 3.3, solo entre las barras. Con las dos barras ocultas, una ventana maximizada ocupa toda la
pantalla, con esquinas rectas.

Desde la Alpha 4 (D72): toda ventana maximizada entre las barras tiene las esquinas redondeadas, sea cual sea la
aplicación (navegadores, Explorador de archivos, aplicaciones empaquetadas o con privilegios elevados), y conserva sus
propios botones de título. Aether ya no toca la ventana de la aplicación: unas piezas pequeñas suyas, colocadas sobre las
cuatro esquinas, muestran el escritorio que hay ahí, en directo, con el borde fino alrededor. Las esquinas no están
suavizadas. Una ventana situada delante de la maximizada las cubre como a lo demás.

**La barra superior como texto sobre el escritorio** (Alpha 4, D71): Configuración → Barra superior → **Presentación** →
**Texto sobre el escritorio** quita la barra en sí. Los mismos elementos se escriben en lo más alto de la pantalla,
directamente sobre el fondo de escritorio, con tinta clara u oscura según la imagen que haya debajo, y un halo suave. Esa
franja queda libre de ventanas; el margen superior y la opacidad del cristal no se aplican. Con **Ocultar la barra
superior automáticamente**, el texto vuelve sobre una franja de cristal mientras haya una ventana debajo. El contraste
alto conserva la barra.

**Bienvenida** (desde la 3.3): tras iniciar sesión o desbloquear, Aether Shell escribe a mano un breve saludo que llena
la pantalla, sobre tu fondo de pantalla desenfocado, y las barras aparecen cuando termina: «Salut», «Hello» o «Hola»,
con tu nombre si lo has escrito. Al desbloquear ya está ahí, en lugar del escritorio. Toca, haz clic o pulsa Esc para
omitirla. La instalación pide el nombre, de forma opcional (desde la 3.5); Configuración → Bienvenida permite
desactivarla y elegir el idioma y el nombre. Si el archivo de la imagen se ha movido o borrado, la bienvenida usa la
copia que guarda Windows. El nombre y tu imagen se quedan en este PC; nunca se envían a ningún sitio ni se registran, y
vaciar el campo quita el nombre. Solo aparece después de que Windows haya iniciado tu sesión; la pantalla de inicio de
sesión de Windows no cambia. Al iniciar sesión aparece en cuanto Windows inicia Aether, lo que puede ser después del
escritorio.

**Táctil** (desde la 3.3): desplaza cualquier lista o página larga con el dedo; sigue al dedo, se desliza un poco al
soltar y vuelve con suavidad más allá de los extremos. Un toque actúa al levantar el dedo, un desplazamiento nunca
actúa, y mantener pulsado un elemento de la barra superior o del Dock abre su menú (mantén y mueve para reordenar el
Dock). Desliza desde el borde superior o inferior de la pantalla para traer de vuelta una barra oculta. En los controles
rápidos, desliza hacia arriba o abajo para cambiar de página. En un control deslizante, muévete de lado para cambiarlo o
toca donde lo quieras; deslizar hacia arriba o abajo desplaza la página. Un control deslizante de la Configuración se
aplica al soltar.

La **ocultación automática** (Alpha 3) se configura por separado para cada barra: Configuración → Dock → **Ocultar el
Dock automáticamente** y Configuración → Barra superior → **Ocultar la barra superior automáticamente**. Ambas están
desactivadas de forma predeterminada. Una barra que se oculta se aparta cuando una ventana la cubre. Apoya el puntero
en el borde inferior de la pantalla para traer el Dock, o en el borde superior exacto, sin margen, durante un cuarto de
segundo para traer la barra superior; si te apartas antes se cancela, así que las pestañas y los botones de cierre de
las aplicaciones quedan libres. Se queda mientras la usas o tiene abierto un menú o panel, y se oculta poco después. En
el escritorio las barras vuelven, aunque haya ventanas detrás (Mostrar escritorio). Una barra que se oculta no reserva
espacio: las ventanas maximizadas llenan la pantalla y la barra aparece encima, como la barra de tareas de Windows con
ocultación automática.

Escritorio ofrece también Importar los anclajes de la barra de tareas de Windows. Lee los accesos directos anclados
compatibles y añade los que faltan, sin cambiar los anclajes de Windows ni sustituir tus anclajes de Aether. Algunos
anclajes de Windows 11 y su orden original no están disponibles. El modo de sustitución importa los anclajes compatibles
y añade una vez el Explorador de archivos. La sincronización automática añade los nuevos anclajes disponibles al
iniciar, al activar la sincronización o al reiniciar el Explorador; respeta tu orden y tus eliminaciones explícitas.
Una importación manual puede recuperar anclajes eliminados. Apariencia contiene el interruptor de sincronización
automática, el tema y el color de énfasis, los tamaños predefinidos de iconos y los ajustes de movimiento.

El Dock tiene elementos de Inicio, Buscar y Vista de tareas. Arrastra los iconos anclados para cambiar su orden
guardado; arrastrar un icono solo abierto cambia el orden de esta sesión sin anclarlo. Los metadatos de icono de perfil
por ventana, cuando existen, permiten distinguir los perfiles de Chrome; sin ellos, el icono sigue siendo genérico. Los
menús contextuales del Dock ofrecen las acciones de ventana, nueva instancia y anclaje aplicables, sin adivinar un
perfil.

Los menús de la barra superior dan acceso a la configuración del reloj, la energía, el sonido y la red. La flecha final
que mostraba la barra de tareas nativa se quitó en la Alpha 2; desde la Alpha 3, la flecha de iconos ocultos da acceso
al desbordamiento del área de notificación de Windows. La recuperación sigue disponible desde la configuración de
Escritorio, Salir y el comando de recuperación independiente. La ruta interna de suspensión y reanudación sigue siendo
interna.

Para restaurar la barra nativa, desactiva la opción de Escritorio o elige Salir en el menú de la barra superior.

Desde la carpeta de instalación, `AetherShell.exe --restore-taskbar` pide a una instancia en ejecución que desactive el
modo. Cuando Aether Shell no se está ejecutando, el mismo comando restaura la barra de tareas de Windows por sí mismo:
- usa el estado guardado antes de ocultar la barra de tareas;
- desactiva el modo;
- muestra el resultado.

`AetherShell.exe --quit` pide a una instancia en ejecución que se cierre y, si no hay ninguna, no hace nada.

Un asistente de recuperación independiente restaura la barra de tareas tras un fallo de la aplicación, y la aplicación
hace lo mismo si su asistente se cierra. **Reparar** o **Desinstalar** en Configuración de Windows > Aplicaciones >
Aplicaciones instaladas también restauran una barra de tareas que Aether Shell haya dejado oculta. La reparación
conserva tu configuración, incluida la elección de Escritorio, así que Aether Shell volverá a ocultar la barra de tareas
en su próximo inicio salvo que desactives la opción. Si nada de esto funciona, termina los procesos de Aether Shell en
el Administrador de tareas y reinicia el Explorador de Windows. La recuperación ante todos los fallos y en todas las
versiones de Windows aún no está cualificada; lee las limitaciones conocidas antes de activar este modo.

Los datos locales están en `%LOCALAPPDATA%\AetherShell`: configuración, caché, actualizaciones y registros de tamaño
limitado. Abre la carpeta de registros desde General. La exportación del diagnóstico básico contiene la versión del
producto y la compilación de Windows; revisa tú mismo cualquier archivo antes de compartirlo. Restablecer los valores
predeterminados pide confirmación y guarda una copia de la configuración. La desinstalación conserva los datos del
usuario salvo que se elija expresamente quitarlos.

Consulta las [limitaciones conocidas](../../../KNOWN_LIMITATIONS.md) (en inglés) para ver lo que aún falta por cualificar.
