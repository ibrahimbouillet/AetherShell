![Aether Shell — une interface de bureau native et modulaire pour Windows](resources/branding/png/readme-banner.png)

<p align="center">
  🇬🇧 <a href="README.md">English</a> · 🇫🇷 <strong>Français</strong> · 🇪🇸 <a href="README.es.md">Español</a>
  · 🇨🇳 <a href="README.zh-CN.md">中文</a> · 🇮🇳 <a href="README.hi.md">हिन्दी</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/releases/latest"><strong>Télécharger la dernière version</strong></a>
  · <a href="docs/i18n/fr/USER_GUIDE.md">Guide d'utilisation</a>
  · <a href="KNOWN_LIMITATIONS.md">Limites connues (en anglais)</a>
  · <a href="docs/knowledge/TEST_STATUS.md">Résultats des tests (en anglais)</a>
</p>

<p align="center">
  <a href="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml"><img alt="Windows CI" src="https://github.com/ibrahimbouillet/AetherShell/actions/workflows/ci.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="Licence MIT" src="https://img.shields.io/badge/license-MIT-2ea44f.svg"></a>
  <img alt="Windows x64" src="https://img.shields.io/badge/platform-Windows%20x64-0078D4.svg">
  <a href="https://www.youtube.com/@IbrahimBouillet"><img alt="Chaîne YouTube" src="https://img.shields.io/badge/YouTube-Subscribe-red?logo=youtube&logoColor=white"></a>
</p>

![Aether Shell en action : le Dock flottant et la barre d'état du haut. Ibrahim Bouillet](./resources/branding/gif/AetherShell.gif)

# Aether Shell

Une interface de bureau Windows native créée par **Ibrahim Bouillet**. Aether Shell associe un Dock flottant
à une barre d'état compacte en haut de l'écran, dans une matière translucide au flou délicat. Il est écrit en
C++20 avec Win32, Direct2D/DirectWrite et Windows.UI.Composition, et publié sous licence MIT.

> **Logiciel alpha :** 0.3.5-alpha.3 (Alpha 3, version 3.5) est une préversion publique. Ce n'est pas encore
> un remplacement entièrement qualifié de l'interface de Windows. Enregistrez votre travail avant d'essayer et
> lisez les [limites connues](KNOWN_LIMITATIONS.md).

> **Bientôt : Alpha 4.** La prochaine version est en préparation : deux styles au choix, *Liquide* (un verre
> d'eau limpide) et *Grain* (la matière dépolie actuelle), une recherche qui trouve aussi les pages des Paramètres
> de Windows, les fichiers et les dossiers, une interface en cinq langues, et une interface plus légère et plus
> fluide. [Suivez les versions](https://github.com/ibrahimbouillet/AetherShell/releases) pour savoir quand elle sort.

<h2 align="center">Un nouveau bureau. Une nouvelle atmosphère.</h2>

<p align="center">
  <img src="./resources/preview/aether-desktops.webp" alt="Un diaporama de cinq bureaux sous Aether Shell : le même Dock et la même barre du haut au-dessus d'un viaduc dans des collines vertes, d'un lac de montagne turquoise, d'un canyon de grès, de montagnes rouges au coucher du soleil et d'un fond d'écran illustré" width="100%">
</p>

<table>
  <tr>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-1.png"><img src="./resources/preview/thumbs/desktop-1.webp" alt="Aether Shell au-dessus d'un viaduc dans des collines vertes" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-5.webp"><img src="./resources/preview/thumbs/desktop-2.webp" alt="Aether Shell au-dessus d'un lac de montagne turquoise" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-4.webp"><img src="./resources/preview/thumbs/desktop-3.webp" alt="Aether Shell au-dessus d'un canyon de grès" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-3.webp"><img src="./resources/preview/thumbs/desktop-4.webp" alt="Aether Shell au-dessus de montagnes rouges au coucher du soleil" width="100%"></a></td>
    <td align="center" width="20%"><a href="./resources/preview/setup_preview-2.png"><img src="./resources/preview/thumbs/desktop-5.webp" alt="Aether Shell en français au-dessus d'un fond d'écran illustré" width="100%"></a></td>
  </tr>
</table>

<p align="center"><sub>Un Dock et une barre du haut qui prennent les couleurs du fond d'écran que vous choisissez. Sélectionnez une image pour la voir en taille réelle.<br>
Vrais bureaux, capturés sous Windows 11. Les fonds d'écran et les icônes d'applications appartiennent à leurs propriétaires et ne sont pas couverts par la licence du projet.</sub></p>

## Ce qu'il fait

- **Dock :** lancer les applications et passer de l'une à l'autre, garder ses favorites, réordonner les icônes
  épinglées, voir les applications ouvertes avec des marqueurs animés et de vrais aperçus de leurs fenêtres au
  survol, ouvrir la recherche, et accéder à Démarrer et à la vue des tâches.
- **Barre d'état du haut :** l'horloge, la langue de saisie, le réseau, le son et la batterie, tels que Windows les
  fournit. La date et l'heure ouvrent les notifications et le calendrier de Windows, un chevron ouvre les icônes
  cachées de la zone de notification, et le groupe d'état ouvre le panneau de réglages rapides d'Aether, avec sa
  liste des réseaux Wi-Fi.
- **Recherche :** trouver et ouvrir les applications et les outils de Windows, comme le Gestionnaire de
  périphériques, les Services ou les éléments du Panneau de configuration.
- **Masquage automatique :** le Dock et la barre du haut, chacun de son côté, s'effacent quand une fenêtre les
  recouvre et reviennent quand le pointeur atteint leur bord de l'écran, pour un espace de travail plein écran.
- **Cinq langues :** toute l'interface, installation comprise, en anglais, chinois, hindi, espagnol et français.
  Elle suit la langue d'affichage de Windows, ou celle que vous choisissez dans Paramètres → Langue. Voir
  [Langues](#langues).
- **Accueil :** un message facultatif, écrit à la main en français, en anglais ou en espagnol sur votre fond
  d'écran flouté, après l'ouverture de session ou le déverrouillage du PC. L'installation peut demander le prénom
  avec lequel il vous salue.
- **Météo :** en option, la localisation de Windows (avec votre autorisation) fournit une position approximative
  et des prévisions. Sans localisation ni réseau, la météo reste indisponible plutôt que d'être inventée.
- **Mouvement et apparence :** une même matière de verre dépoli et neutre pour les barres, les Paramètres, les
  menus et l'installation ; un défilement fluide des Paramètres ; des fenêtres agrandies aux coins arrondis quand
  Aether remplace la barre des tâches ; modes clair et sombre, navigation au clavier et mouvement réduit.
- **Récupération :** une installation par utilisateur, un assistant de récupération séparé, la réparation et la
  désinstallation, et une commande pour rétablir la barre des tâches de Windows.

## Nouveautés de la 3.5

La version 3.5 laisse tranquilles les onglets et les boutons de fermeture des applications (la barre du haut ne
répond qu'au tout premier pixel de l'écran), ferme les panneaux dès que vous utilisez autre chose, et trouve les
outils de Windows dans la recherche. L'accueil vous salue désormais par votre prénom dès la première fois, car
l'installation le demande ; il affiche votre fond d'écran même si le fichier de l'image a été déplacé ou supprimé,
et sa plume coule avec moins de travail par image. Lisez les [notes de version](docs/releases/0.3.5-alpha.3.md) et
le [journal des modifications](CHANGELOG.md) (en anglais).

## Langues

Aether Shell parle les cinq langues les plus parlées au monde. Chaque menu, panneau, page des Paramètres,
message et écran d'installation est traduit, tout comme cette documentation.

| | Langue | Interface | Documentation |
|---|---|---|---|
| 🇬🇧 | English | ✅ | [README](README.md) · [User guide](USER_GUIDE.md) · [Installation](INSTALLATION.md) |
| 🇨🇳 | 中文（简体） | ✅ | [README](README.zh-CN.md) · [使用指南](docs/i18n/zh-CN/USER_GUIDE.md) · [安装](docs/i18n/zh-CN/INSTALLATION.md) |
| 🇮🇳 | हिन्दी | ✅ | [README](README.hi.md) · [उपयोग गाइड](docs/i18n/hi/USER_GUIDE.md) · [इंस्टॉलेशन](docs/i18n/hi/INSTALLATION.md) |
| 🇪🇸 | Español | ✅ | [README](README.es.md) · [Guía de uso](docs/i18n/es/USER_GUIDE.md) · [Instalación](docs/i18n/es/INSTALLATION.md) |
| 🇫🇷 | Français | ✅ | [README](README.fr.md) · [Guide d'utilisation](docs/i18n/fr/USER_GUIDE.md) · [Installation](docs/i18n/fr/INSTALLATION.md) |

**Comme Windows** (le réglage par défaut) utilise la langue d'affichage de Windows si c'est l'une de ces cinq, et
l'anglais sinon. Choisissez-en une autre dans **Paramètres → Langue** ; le changement
s'applique aussitôt. Les noms qui vous appartiennent ou qui appartiennent à d'autres programmes (applications,
fichiers, réseaux Wi-Fi) ne sont jamais traduits. L'accueil écrit à la main trace des lettres latines : il salue
donc en français, en anglais ou en espagnol. Les traductions se trouvent dans
[tools/i18n/catalog](tools/i18n/catalog) ; les corrections sont les bienvenues.

## Remplacer la barre des tâches de Windows

L'expérience V2 masque la barre des tâches de Windows au démarrage d'Aether Shell et affiche le Dock d'Aether à
sa place. Ce comportement est activé par défaut et se désactive dans **Paramètres d'Aether Shell → Bureau**.
L'Explorateur continue de fonctionner ; Aether Shell ne remplace pas `explorer.exe`, ne modifie pas l'interpréteur
de commandes de Winlogon et ne s'inscrit pas comme barre des tâches native dans les Paramètres de Windows.
L'assistant de récupération rétablit la barre des tâches de Windows quand Aether Shell se ferme. Lisez le
[guide de récupération](docs/i18n/fr/USER_GUIDE.md) avant d'essayer l'alpha.

## Télécharger et installer

Téléchargez l'**exécutable d'installation x64** depuis la [dernière version sur GitHub](https://github.com/ibrahimbouillet/AetherShell/releases/latest)
et comparez son empreinte SHA-256 avec le fichier de sommes de contrôle de la version. Lancez l'installation avec
votre compte Windows habituel. L'installateur explique le comportement de la barre des tâches et ne demande pas
de droits d'administrateur. Une nouvelle installation coche visiblement **démarrer à l'ouverture de session** ;
vous pouvez décocher cette option avant d'accepter. Les choix déjà enregistrés et les désactivations du
Gestionnaire des tâches sont respectés. L'installation propose aussi un champ facultatif pour le prénom avec
lequel l'accueil vous salue ; il reste sur votre PC. Le lanceur installé peut relancer l'application après un
nombre limité de plantages anormaux ; Quitter, la récupération et la désinstallation restent disponibles. Windows
décide du moment du démarrage : Aether ne peut pas garantir que son interface apparaît avant la barre des tâches
native à l'ouverture de session.

Cette alpha s'installe manuellement. Les mises à jour automatiques exigent un manifeste de version signé ; aucun
manifeste non signé n'est publié. L'installateur n'est pas signé Authenticode : Windows peut donc afficher un
avertissement SmartScreen. Voir [installation et vérification](docs/i18n/fr/INSTALLATION.md).

## Compatibilité

- Windows 11, x64 : cible de développement actuelle.
- Windows 10 2004+ x64 : prévu par la conception, mais pas encore qualifié à l'exécution dans cette version.
- ARM64 : pas encore qualifié à l'exécution.

Sous Windows 10, le flou d'arrière-plan utilise une teinte plus simple. Le Dock ne reproduit pas toutes les
fonctions de la barre des tâches native, et les Paramètres de Windows continuent de gérer la barre des tâches de
Windows. Voir toutes les [limites connues](KNOWN_LIMITATIONS.md).

## Compiler et tester

Compilez sous Windows avec les Build Tools C++ de Visual Studio, un SDK Windows, CMake, Ninja et Python :

```powershell
./scripts/build.ps1 -Config release -BuildDir build/release -Test -TestExclude 'network|integration' `
    -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

Pour créer l'installateur par utilisateur et lancer son audit de confidentialité et de fraîcheur du paquet :

```powershell
./scripts/package.ps1 -BuildDir build/release
```

Voir [BUILDING.md](BUILDING.md) pour les tests ciblés par module, [TESTING.md](TESTING.md) pour l'isolation des
tests et la qualification manuelle, et [le relevé des tests exécutés](docs/knowledge/TEST_STATUS.md) pour les
résultats exacts (documents en anglais). Une compilation ou une suite de tests réussie ne qualifie pas à elle seule
chaque version de Windows ou chaque appareil.

## Documents du projet

- [Guide d'utilisation](docs/i18n/fr/USER_GUIDE.md) · [Installation](docs/i18n/fr/INSTALLATION.md)
- En anglais : [Architecture](ARCHITECTURE.md) · [Spécification du projet](PROJECT_SPEC.md) · [État des tâches](TASKS.md)
  · [Mise à jour et retour arrière](UPDATE_AND_ROLLBACK.md) · [Sécurité](SECURITY.md) · [Contribuer](CONTRIBUTING.md)
  · [Avis tiers](THIRD_PARTY_NOTICES.md)

Pour les contributeurs et les assistants de code, commencez par [AGENTS.md](AGENTS.md) et [HANDOFF.md](HANDOFF.md).

## Licence

MIT. Voir [LICENSE](LICENSE).
