# Installation

🇬🇧 [English](../../../INSTALLATION.md) · 🇫🇷 Français · 🇪🇸 [Español](../es/INSTALLATION.md) ·
🇨🇳 [中文](../zh-CN/INSTALLATION.md) · 🇮🇳 [हिन्दी](../hi/INSTALLATION.md)

N'utilisez que l'exécutable d'installation de la [page des versions du dépôt officiel](https://github.com/ibrahimbouillet/AetherShell/releases).
La version actuelle, 3.5 [0.3.5-alpha.3](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.3.5-alpha.3), est
une préversion à installer manuellement ; la qualification plus large de la version 1 reste ouverte. La version précédente
[0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2) reste disponible.

Lancez l'installation avec votre compte Windows habituel. Depuis l'Alpha 4, l'installation parle la langue d'affichage
de Windows quand c'est l'anglais, le chinois, l'hindi, l'espagnol ou le français, et l'anglais sinon. L'assistant
présente le produit, affiche la licence MIT et vous laisse choisir un dossier (par défaut
`%LOCALAPPDATA%\Programs\AetherShell`), les raccourcis et le démarrage facultatif. Il installe un lanceur stable, un
programme de maintenance et des fichiers de programme versionnés. Il peut créer un raccourci dans le menu Démarrer et une
entrée propre à l'utilisateur dans les applications installées de Windows. Aucun droit d'administrateur n'est utilisé et
l'Explorateur n'est pas remplacé. Lors d'une nouvelle installation interactive (depuis l'Alpha 2), **Démarrer à
l'ouverture de session** est visiblement coché : décochez-le avant d'installer pour refuser. Accepter l'installation
enregistre ce choix ; un choix déjà enregistré et une désactivation par le Gestionnaire des tâches sont respectés.
L'installation silencieuse n'active le démarrage qu'avec l'option explicite `--start-with-windows` ;
`--no-start-with-windows` le désactive. Les profils portables restent désactivés tant qu'il n'y a pas de consentement.
Windows décide de l'ordre de lancement à l'ouverture de session et peut le retarder.

L'interface V2 remplace visuellement la barre des tâches de Windows par défaut quand Aether Shell démarre. L'installation
annonce ce comportement ; l'application garde un assistant de récupération et la possibilité de le désactiver dans
**Paramètres → Bureau**. Une fermeture normale rétablit la barre des tâches de Windows. L'Explorateur continue de
fonctionner, et les Paramètres de Windows continuent de régler Windows et non Aether Shell. Les limites de la
récupération et les commandes d'urgence se trouvent dans le [guide d'utilisation](USER_GUIDE.md).

Des fichiers d'application ou d'installation non signés peuvent déclencher SmartScreen. Cette alpha n'a pas de
certificat d'éditeur Authenticode. Vérifiez la source exacte de la version et son SHA-256 avant de décider de la lancer :

```powershell
Get-FileHash .\AetherShell-0.3.5-alpha.3-win-x64-setup.exe -Algorithm SHA256
```

Comparez avec le fichier de sommes de contrôle correspondant. Une somme de contrôle seule n'authentifie pas un éditeur.
Les mises à jour automatiques exigent en plus le manifeste de version signé décrit dans
[UPDATE_AND_ROLLBACK](../../../UPDATE_AND_ROLLBACK.md) (en anglais).

Utilisez Applications installées → Aether Shell → Modifier pour réparer la version active avec son installateur
correspondant. La désinstallation demande une fermeture propre de l'application et ne supprime que les entrées et les
fichiers enregistrés de l'installation. Conserver les données de l'utilisateur est le comportement par défaut ;
choisissez explicitement la suppression pour effacer les paramètres, le cache et les journaux. Un installateur plus
ancien est refusé, sauf si le retour en arrière a été explicitement autorisé. Gardez une sauvegarde de vos données avant
tout retour volontaire à une version antérieure.

Les commandes silencieuses ci-dessous utilisent le programme de maintenance situé dans le dossier d'installation ; le nom
du fichier d'installation téléchargé contient la version et l'architecture. Utilisez des chemins personnalisés absolus :

```powershell
.\AetherShellSetup.exe --silent --no-launch
.\AetherShellSetup.exe --silent --repair --no-launch
.\AetherShellSetup.exe --silent --uninstall --keep-user-data
```

Options : `--install-root <dossier>`, `--data-dir <dossier>`, `--no-shortcuts`, `--no-launch`,
`--start-with-windows` (consentement explicite), `--repair`, `--uninstall`, `--keep-user-data`,
`--no-start-with-windows` (refus explicite), `--remove-user-data`, `--allow-downgrade`, `--log <fichier>`. Le mode
silencieux ne pose jamais de question. Codes de sortie : 0 succès, 1 échec de l'opération, 2 arguments non valides ou
échec de préparation des chemins. L'interface normale empêche la fermeture pendant l'opération en cours ; la
qualification de l'annulation et de l'auto-désinstallation est suivie dans
[KNOWN_LIMITATIONS](../../../KNOWN_LIMITATIONS.md) (en anglais).
