# Instalación

🇬🇧 [English](../../../INSTALLATION.md) · 🇫🇷 [Français](../fr/INSTALLATION.md) · 🇪🇸 Español ·
🇨🇳 [中文](../zh-CN/INSTALLATION.md) · 🇮🇳 [हिन्दी](../hi/INSTALLATION.md) · 🇧🇷 [Português](../pt/INSTALLATION.md) · 🇷🇺 [Русский](../ru/INSTALLATION.md) ·
🇸🇦 [العربية](../ar/INSTALLATION.md) · 🇧🇩 [বাংলা](../bn/INSTALLATION.md) · 🇵🇰 [اردو](../ur/INSTALLATION.md)

Usa solo el ejecutable de instalación de la [página de versiones del repositorio oficial](https://github.com/ibrahimbouillet/AetherShell/releases).
La versión actual, Alpha 4 [0.4.0-alpha.4](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.4.0-alpha.4), es
una versión preliminar de instalación manual; la cualificación más amplia de la versión 1 sigue abierta. La versión
anterior [0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2) sigue disponible.

Ejecuta la instalación con tu usuario habitual de Windows. Desde la Alpha 4, la instalación habla el idioma que
elegiste en la Configuración de Aether Shell o, si no hay ninguno, tu idioma de presentación de Windows (cualquiera de
los 41 de la interfaz) y, si no, inglés. Al terminar, la instalación descarga de la versión publicada los idiomas de la
interfaz que tiene tu Windows (unas decenas de KB cada uno); sin conexión, Aether Shell los descarga al iniciarse.
El asistente presenta el producto, muestra la licencia
MIT y te deja elegir una carpeta (de forma predeterminada `%LOCALAPPDATA%\Programs\AetherShell`), los accesos directos y
el inicio opcional. Instala un iniciador estable, un programa de mantenimiento y archivos de programa con versión. Puede
crear un acceso directo en el menú Inicio y una entrada por usuario en las aplicaciones instaladas de Windows. No se
usan permisos de administrador ni se sustituye el Explorador. En una instalación interactiva nueva (desde la Alpha 2),
**Iniciar al iniciar sesión** aparece marcado de forma visible: desmárcalo antes de instalar para rechazarlo. Aceptar la
instalación guarda esa elección; se respetan una elección ya guardada y la desactivación desde el Administrador de
tareas. La instalación silenciosa solo activa el inicio con la opción explícita `--start-with-windows`;
`--no-start-with-windows` lo desactiva. Los perfiles portátiles siguen desactivados hasta que haya consentimiento.
Windows decide el orden de inicio al iniciar sesión y puede retrasarlo.

La interfaz V2 sustituye visualmente la barra de tareas de Windows de forma predeterminada cuando se inicia Aether Shell.
La instalación avisa de este comportamiento; la aplicación mantiene un asistente de recuperación y la opción de
desactivarlo en **Configuración → Escritorio**. Un cierre normal restaura la barra de tareas de Windows. El Explorador
sigue ejecutándose y la Configuración de Windows sigue configurando Windows, no Aether Shell. Las limitaciones de la
recuperación y los comandos de emergencia están en la [guía de uso](USER_GUIDE.md).

Los archivos de la aplicación o de la instalación sin firmar pueden activar SmartScreen. Esta versión alfa no tiene
certificado de editor Authenticode. Comprueba el origen exacto de la versión y su SHA-256 antes de decidir ejecutarla:

```powershell
Get-FileHash .\AetherShell-0.4.0-alpha.4-win-x64-setup.exe -Algorithm SHA256
```

Compáralo con el archivo de sumas de comprobación correspondiente. Una suma de comprobación por sí sola no autentica a un
editor. Las actualizaciones automáticas requieren además el manifiesto de versión firmado que se describe en
[UPDATE_AND_ROLLBACK](../../../UPDATE_AND_ROLLBACK.md) (en inglés).

Usa Aplicaciones instaladas → Aether Shell → Modificar para reparar la versión activa con su instalador correspondiente.
La desinstalación pide a la aplicación que se cierre de forma ordenada y solo quita las entradas y los archivos
registrados de la instalación. Conservar los datos del usuario es lo predeterminado; elige expresamente quitarlos para
borrar la configuración, la caché y los registros. Se rechaza un instalador más antiguo salvo que se haya permitido
expresamente volver a una versión anterior. Guarda una copia de tus datos antes de volver a propósito a una versión
anterior.

Los comandos silenciosos de abajo usan el programa de mantenimiento de la carpeta de instalación; el nombre del archivo
de instalación descargado incluye la versión y la arquitectura. Usa rutas personalizadas absolutas:

```powershell
.\AetherShellSetup.exe --silent --no-launch
.\AetherShellSetup.exe --silent --repair --no-launch
.\AetherShellSetup.exe --silent --uninstall --keep-user-data
```

Opciones: `--install-root <carpeta>`, `--data-dir <carpeta>`, `--no-shortcuts`, `--no-launch`,
`--start-with-windows` (consentimiento explícito), `--repair`, `--uninstall`, `--keep-user-data`,
`--no-start-with-windows` (rechazo explícito), `--remove-user-data`, `--allow-downgrade`, `--log <archivo>`. El modo
silencioso nunca pregunta nada. Códigos de salida: 0 éxito, 1 fallo de la operación, 2 argumentos no válidos o fallo al
preparar las rutas. La interfaz normal impide cerrar durante la operación en curso; la cualificación de la cancelación y
de la autodesinstalación se sigue en [KNOWN_LIMITATIONS](../../../KNOWN_LIMITATIONS.md) (en inglés).
