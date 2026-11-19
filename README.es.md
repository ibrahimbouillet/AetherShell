![Aether Shell — una interfaz de escritorio nativa y modular para Windows](resources/branding/png/readme-banner.png)

<p align="center">
  🇬🇧 <a href="README.md">English</a> · 🇫🇷 <a href="README.fr.md">Français</a> · 🇪🇸 <strong>Español</strong>
  · 🇨🇳 <a href="README.zh-CN.md">中文</a> · 🇮🇳 <a href="README.hi.md">हिन्दी</a>
  · 🇧🇷 <a href="README.pt.md">Português</a> · 🇷🇺 <a href="README.ru.md">Русский</a> · 🇸🇦 <a href="README.ar.md">العربية</a>
  · 🇧🇩 <a href="README.bn.md">বাংলা</a> · 🇵🇰 <a href="README.ur.md">اردو</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/releases/latest"><strong>Descargar la última versión</strong></a>
  · <a href="docs/i18n/es/USER_GUIDE.md">Guía de uso</a>
  · <a href="KNOWN_LIMITATIONS.md">Limitaciones conocidas (en inglés)</a>
  · <a href="docs/knowledge/TEST_STATUS.md">Resultados de las pruebas (en inglés)</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml"><img alt="Windows CI" src="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="Licencia MIT" src="https://img.shields.io/badge/license-MIT-2ea44f.svg"></a>
  <img alt="Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg">
  <a href="https://www.youtube.com/@IbrahimBouillet"><img alt="Canal de YouTube" src="https://img.shields.io/badge/YouTube-Subscribe-red?logo=youtube&logoColor=white"></a>
</p>

![Aether Shell en uso: el Dock flotante y la barra de estado superior. Ibrahim Bouillet](./resources/branding/gif/AetherShell.gif)

# Aether Shell

Una interfaz de escritorio nativa para Windows creada por **Ibrahim Bouillet**. Aether Shell combina un Dock
flotante con una barra de estado superior compacta, en un material translúcido con un desenfoque suave. Está
escrito en C++20 con Win32, Direct2D/DirectWrite y Windows.UI.Composition, y se publica con licencia MIT.

> **Software alfa:** 0.4.0-alpha.4 (Alpha 4) es una versión preliminar pública. Todavía no es un
> sustituto totalmente cualificado de la interfaz de Windows. Guarda tu trabajo antes de probarlo y lee las
> [limitaciones conocidas](KNOWN_LIMITATIONS.md).

<h2 align="center">Un nuevo escritorio. Una nueva atmósfera.</h2>

<p align="center">
  <img src="./resources/preview/aether-desktops.webp" alt="Una presentación de cinco escritorios con Aether Shell: el mismo Dock y la misma barra superior sobre un viaducto entre colinas verdes, un lago de montaña turquesa, un cañón de arenisca, montañas rojas al atardecer y un fondo ilustrado" width="100%">
</p>

<table>
  <tr>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-1.png"><img src="./resources/preview/thumbs/desktop-1.webp" alt="Aether Shell sobre un viaducto entre colinas verdes" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-5.webp"><img src="./resources/preview/thumbs/desktop-2.webp" alt="Aether Shell sobre un lago de montaña turquesa" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-4.webp"><img src="./resources/preview/thumbs/desktop-3.webp" alt="Aether Shell sobre un cañón de arenisca" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-3.webp"><img src="./resources/preview/thumbs/desktop-4.webp" alt="Aether Shell sobre montañas rojas al atardecer" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-2.png"><img src="./resources/preview/thumbs/desktop-5.webp" alt="Aether Shell en francés sobre un fondo ilustrado" width="100%"></a></td>
  </tr>
</table>

<p align="center"><sub>Un Dock y una barra superior que toman los colores del fondo de pantalla que elijas. Selecciona una imagen para verla a tamaño completo.<br>
Escritorios reales, capturados en Windows 11. Los fondos de pantalla y los iconos de las aplicaciones pertenecen a sus propietarios y no están cubiertos por la licencia de este proyecto.</sub></p>

## Qué hace

- **Dock:** abrir aplicaciones y cambiar entre ellas, conservar las favoritas, reordenar los iconos anclados, ver
  las aplicaciones abiertas con marcadores animados y vistas previas reales de sus ventanas al pasar el puntero,
  abrir la búsqueda y acceder a Inicio y a la Vista de tareas.
- **Barra de estado superior:** el reloj, el idioma de entrada, la red, el sonido y la batería, tal como los da
  Windows. La fecha y la hora abren las notificaciones y el calendario de Windows, una flecha abre los iconos
  ocultos del área de notificación, y el grupo de estado abre el panel de controles rápidos de Aether, con su lista
  de redes Wi-Fi.
- **Búsqueda:** encontrar y abrir aplicaciones y herramientas propias de Windows, como el Administrador de
  dispositivos, los Servicios o los elementos del Panel de control.
- **Ocultación automática:** el Dock y la barra superior, cada uno por su lado, se apartan cuando una ventana los
  cubre y vuelven cuando el puntero llega a su borde de la pantalla, para trabajar a pantalla completa.
- **41 idiomas:** toda la interfaz, instalación incluida, en los 38 idiomas que ofrece Windows 11, más el bengalí, el
  hindi y el urdu. Sigue tus idiomas de Windows o el que elijas en Configuración → Idioma, y recuerda esa elección de
  una versión a otra. Cada idioma se descarga de la versión publicada cuando hace falta. Consulta [Idiomas](#idiomas).
- **Bienvenida:** un saludo opcional, escrito a mano en francés, inglés o español sobre tu fondo de pantalla
  desenfocado, al iniciar sesión o desbloquear el PC. La instalación puede pedir el nombre con el que te saluda.
- **Tiempo:** opcionalmente, la ubicación de Windows (con tu permiso) da una posición aproximada y una previsión.
  Si no hay ubicación ni red, el tiempo no se muestra en lugar de inventarse.
- **Movimiento y apariencia:** un mismo cristal esmerilado neutro para las barras, la Configuración, los menús y
  la instalación; desplazamiento fluido en la Configuración; ventanas maximizadas con esquinas redondeadas cuando
  Aether sustituye la barra de tareas; modos claro y oscuro, navegación con teclado y movimiento reducido.
- **Recuperación:** una instalación por usuario, un asistente de recuperación independiente, reparación y
  desinstalación, y un comando para restaurar la barra de tareas de Windows.

## Novedades de la Alpha 4

Dos estilos: *Líquido*, un cristal de agua transparente sobre una capa de desenfoque que tú ajustas, y *Grano*, el
material esmerilado de las versiones anteriores. En Líquido, la ventana de Configuración solo se desenfoca bajo sus
propios textos y controles y queda transparente en el resto. La interfaz habla 41 idiomas, que se descargan de la
versión publicada cuando los necesitas y siguen tu idioma de presentación de Windows. La búsqueda encuentra por nombre
lo mismo que Inicio. El tiempo es preciso y animado, con una ventana propia. La barra superior puede ser simple texto
sobre tu escritorio, un icono arrastrado estira el Dock y Aether arranca antes al iniciar sesión. Lee las
[notas de la versión](docs/releases/0.4.0-alpha.4.md) y el [registro de cambios](CHANGELOG.md) (en inglés).

## Idiomas

La interfaz de Aether Shell habla 41 idiomas: los 38 que ofrece Windows 11, más el bengalí, el hindi y el urdu. Cada
menú, panel, página de Configuración, mensaje y pantalla de instalación está traducido. Aether sigue tu idioma de
presentación de Windows, o el que elijas en Configuración → Idioma; cada idioma se descarga de la versión publicada
cuando hace falta. Esta documentación está disponible en los diez idiomas siguientes.

| | Idioma | Interfaz | Documentación |
|---|---|---|---|
| 🇬🇧 | English | ✅ | [README](README.md) · [User guide](USER_GUIDE.md) · [Installation](INSTALLATION.md) |
| 🇨🇳 | 中文（简体） | ✅ | [README](README.zh-CN.md) · [使用指南](docs/i18n/zh-CN/USER_GUIDE.md) · [安装](docs/i18n/zh-CN/INSTALLATION.md) |
| 🇮🇳 | हिन्दी | ✅ | [README](README.hi.md) · [उपयोग गाइड](docs/i18n/hi/USER_GUIDE.md) · [इंस्टॉलेशन](docs/i18n/hi/INSTALLATION.md) |
| 🇪🇸 | Español | ✅ | [README](README.es.md) · [Guía de uso](docs/i18n/es/USER_GUIDE.md) · [Instalación](docs/i18n/es/INSTALLATION.md) |
| 🇫🇷 | Français | ✅ | [README](README.fr.md) · [Guide d'utilisation](docs/i18n/fr/USER_GUIDE.md) · [Installation](docs/i18n/fr/INSTALLATION.md) |
| 🇧🇷 | Português | ✅ | [README](README.pt.md) · [Guia do usuário](docs/i18n/pt/USER_GUIDE.md) · [Instalação](docs/i18n/pt/INSTALLATION.md) |
| 🇷🇺 | Русский | ✅ | [README](README.ru.md) · [Руководство](docs/i18n/ru/USER_GUIDE.md) · [Установка](docs/i18n/ru/INSTALLATION.md) |
| 🇸🇦 | العربية | ✅ (de derecha a izquierda) | [README](README.ar.md) · [دليل الاستخدام](docs/i18n/ar/USER_GUIDE.md) · [التثبيت](docs/i18n/ar/INSTALLATION.md) |
| 🇧🇩 | বাংলা | ✅ | [README](README.bn.md) · [ব্যবহারকারী নির্দেশিকা](docs/i18n/bn/USER_GUIDE.md) · [ইনস্টলেশন](docs/i18n/bn/INSTALLATION.md) |
| 🇵🇰 | اردو | ✅ (de derecha a izquierda) | [README](README.ur.md) · [صارف گائیڈ](docs/i18n/ur/USER_GUIDE.md) · [انسٹالیشن](docs/i18n/ur/INSTALLATION.md) |

**Igual que Windows** (la opción predeterminada) recorre tus idiomas de Windows (Configuración → Hora e idioma) en el
orden en que los colocaste y usa el primero que hable Aether Shell (cualquiera de los 41); si no, el inglés. Elige otro en
**Configuración → Idioma**; el cambio se aplica al momento y se guarda en tu configuración, que lee cada versión:
instalar una nueva versión, reparar o reinstalar lo conserva, y la instalación, el actualizador y el lanzador también
lo hablan. Los nombres que son tuyos o de otros programas (aplicaciones, archivos, redes Wi-Fi) nunca se traducen. La
bienvenida escrita a mano traza letras latinas, así que saluda en francés, inglés o español. El árabe, el hebreo y el urdu se
leen de derecha a izquierda: el texto se forma y se alinea en consecuencia, mientras que las barras y los paneles conservan
su disposición. Las traducciones están en [tools/i18n/languages](tools/i18n/languages); las correcciones son bienvenidas.

## Sustituir la barra de tareas de Windows

La experiencia V2 oculta la barra de tareas de Windows cuando se inicia Aether Shell y muestra en su lugar el Dock
de Aether. Este comportamiento está activado de forma predeterminada y se desactiva en **Configuración de Aether
Shell → Escritorio**. El Explorador sigue ejecutándose; Aether Shell no sustituye `explorer.exe`, no cambia el
shell de Winlogon ni se registra como barra de tareas nativa en la Configuración de Windows. El asistente de
recuperación restaura la barra de tareas de Windows cuando Aether Shell se cierra. Lee la
[guía de recuperación](docs/i18n/es/USER_GUIDE.md) antes de probar la versión alfa.

## Descargar e instalar

Descarga el **ejecutable de instalación x64** desde la [última versión en GitHub](https://github.com/ibrahimbouillet/AetherShell/releases/latest)
y compara su SHA-256 con el archivo de sumas de comprobación de la versión. Ejecuta la instalación con tu usuario
habitual de Windows. El instalador explica el comportamiento de la barra de tareas y no necesita permisos de
administrador. Una instalación nueva marca de forma visible **iniciar al iniciar sesión**; puedes desmarcar esa
opción antes de aceptar. Se respetan las elecciones guardadas y las desactivaciones del Administrador de tareas. La
instalación también ofrece un campo opcional para el nombre con el que te saluda la bienvenida; se queda en tu PC.
El iniciador instalado puede volver a abrir la aplicación tras un número limitado de fallos anómalos; Salir, la
recuperación y la desinstalación siguen disponibles. Windows decide cuándo se inicia, así que Aether no puede
garantizar que su interfaz aparezca antes que la barra de tareas nativa al iniciar sesión.

Esta versión alfa se instala manualmente. Las actualizaciones automáticas requieren un manifiesto de versión
firmado; no se publica ningún manifiesto sin firmar. El instalador no tiene firma Authenticode, así que Windows
puede mostrar una advertencia de SmartScreen. Consulta [instalación y verificación](docs/i18n/es/INSTALLATION.md).

## Compatibilidad

- Windows 11, x64: objetivo actual de desarrollo.
- Windows 10 2004+ x64: previsto por el diseño, pero aún no cualificado en ejecución en esta versión.
- ARM64: aún no cualificado en ejecución.

En Windows 10, el desenfoque del fondo usa un tinte más sencillo. El Dock no reproduce todas las funciones de la
barra de tareas nativa, y la Configuración de Windows sigue gestionando la barra de tareas de Windows. Consulta
todas las [limitaciones conocidas](KNOWN_LIMITATIONS.md).

## Compilar y probar

Compila en Windows con las Build Tools de C++ de Visual Studio, un SDK de Windows, CMake, Ninja y Python:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/release -Test -TestExclude 'network|integration' `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

Para crear el instalador por usuario y ejecutar su auditoría de privacidad y actualidad del paquete:

```powershell
./scripts/package.ps1 -BuildDir build/release
```

Consulta [BUILDING.md](BUILDING.md) para pruebas por módulo, [TESTING.md](TESTING.md) para el aislamiento de las
pruebas y la cualificación manual, y [el registro de pruebas ejecutadas](docs/knowledge/TEST_STATUS.md) para los
resultados exactos (documentos en inglés). Una compilación o un conjunto de pruebas correctos no cualifican por sí
solos cada versión de Windows ni cada dispositivo.

## Documentos del proyecto

- [Guía de uso](docs/i18n/es/USER_GUIDE.md) · [Instalación](docs/i18n/es/INSTALLATION.md)
- En inglés: [Arquitectura](ARCHITECTURE.md) · [Especificación del proyecto](PROJECT_SPEC.md) · [Estado de las tareas](TASKS.md)
  · [Actualización y reversión](UPDATE_AND_ROLLBACK.md) · [Seguridad](SECURITY.md) · [Contribuir](CONTRIBUTING.md)
  · [Avisos de terceros](THIRD_PARTY_NOTICES.md)

Para colaboradores y asistentes de código, empieza por [AGENTS.md](AGENTS.md) y [HANDOFF.md](HANDOFF.md).

## Licencia

MIT. Consulta [LICENSE](LICENSE).
