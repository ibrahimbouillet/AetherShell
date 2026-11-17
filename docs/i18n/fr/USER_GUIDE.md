# Utiliser Aether Shell

🇬🇧 [English](../../../USER_GUIDE.md) · 🇫🇷 Français · 🇪🇸 [Español](../es/USER_GUIDE.md) ·
🇨🇳 [中文](../zh-CN/USER_GUIDE.md) · 🇮🇳 [हिन्दी](../hi/USER_GUIDE.md) · 🇧🇷 [Português](../pt/USER_GUIDE.md) · 🇷🇺 [Русский](../ru/USER_GUIDE.md) ·
🇸🇦 [العربية](../ar/USER_GUIDE.md) · 🇧🇩 [বাংলা](../bn/USER_GUIDE.md) · 🇵🇰 [اردو](../ur/USER_GUIDE.md)

Lancez Aether Shell depuis son raccourci du menu Démarrer ou depuis le lanceur installé. Il ouvre deux éléments de
bureau : le Dock en bas et la barre d'état en haut. L'Explorateur Windows continue de fonctionner normalement. Les
éléments du Dock ont des menus contextuels. Sélectionnez le logo de la barre du haut pour ouvrir son menu, qui
propose aussi les Paramètres et Quitter.

Ouvrez la recherche depuis le Dock ou avec Ctrl+Alt+Espace. Tapez le nom d'une application, utilisez Haut/Bas pour
la sélectionner, puis Entrée pour l'ouvrir. Échap ferme la fenêtre de recherche, tout comme utiliser autre chose, et
chaque ouverture repart de zéro. Les outils de Windows comme le Gestionnaire de périphériques sont trouvés aussi.
Seules les applications découvertes peuvent être lancées ; le texte tapé n'est jamais une commande. Ctrl+Alt+D donne
le focus au Dock. Les deux raccourcis se règlent dans Paramètres → Général ; les conflits y sont signalés.

Le Dock distingue les applications épinglées des autres applications ouvertes. Utilisez son menu contextuel ou
Paramètres → Dock pour épingler, désépingler ou réordonner les éléments, et régler la taille, l'agrandissement et le
mouvement. Une application ouverte peut être activée depuis son élément du Dock. L'attribution de certaines fenêtres
UWP doit encore être qualifiée. Alpha 3 : survoler une application ouverte élargit son marqueur et, après une courte
pause, affiche une carte avec les aperçus de ses fenêtres. Cliquez sur un aperçu pour passer à cette fenêtre, ou sur
son bouton de fermeture pour demander à cette seule fenêtre de se fermer normalement ; l'application décide si elle
se ferme (par exemple après avoir proposé d'enregistrer).

La barre du haut affiche des relevés provenant d'API Windows documentées. Un état inconnu reste inconnu. Un PC de
bureau sans batterie peut masquer l'élément batterie. Un indicateur « microphone en cours d'utilisation » n'est pas
pris en charge ; la disponibilité et la coupure du périphérique sont des informations distinctes. Ne cliquez sur les
commandes audio disponibles que si vous voulez vraiment changer le volume ou couper le son. La barre du haut n'affiche
jamais le nom d'un réseau. Depuis la 3.3 : dans les réglages rapides, la flèche de la tuile Wi-Fi liste les réseaux
autour de vous avec leur signal. Choisissez-en un pour le rejoindre ; Aether demande son mot de passe s'il en faut un,
et Windows le conserve. Les noms et les mots de passe ne sont jamais journalisés ni envoyés. L'Alpha 2 ajoute le
changement de langue de saisie avec Win+Espace depuis l'élément clavier, et les paramètres Météo depuis l'élément météo
visible. Le groupe réseau/son/batterie ouvre les commandes rapides d'Aether, avec le contrôle multimédia quand Windows
fournit une session active, le volume, la luminosité prise en charge et des tuiles d'action système. Les bascules non
prises en charge ouvrent les commandes Windows correspondantes au lieu de faire semblant de modifier un appareil. La
qualification sur de vrais appareils reste limitée. Un clic sur le microphone coupe ou rétablit le périphérique ; son
menu contextuel propose les paramètres de confidentialité et **Lancer la saisie vocale de Windows (Win+H)**. L'action
vocale rend d'abord le focus à l'application précédente, puis demande à Windows d'ouvrir la dictée dans son champ
actif. Windows contrôle la disponibilité de la dictée, l'autorisation du microphone et le traitement ; Aether Shell
ne capture ni ne stocke la parole. Les tests automatisés ne déclenchent pas de vraie dictée.

L'Alpha 3 ajoute trois points d'entrée à la barre du haut. Cliquez sur la date et l'heure pour ouvrir le centre de
notifications de Windows (Win+N sous Windows 11, Win+A sous Windows 10) ; son menu ouvre aussi le calendrier. Aether
ne lit, n'efface ni ne compte jamais vos notifications. Le petit chevron ouvre juste en dessous les icônes cachées de
la zone de notification de Windows ; la flèche pointe vers le haut tant qu'elles sont ouvertes, et un second clic les
ferme. Masquez le chevron avec Paramètres → Barre du haut → **Afficher le bouton des icônes cachées**. Le groupe
réseau/son/batterie ouvre le panneau de réglages rapides d'Aether, avec une seconde page et des vues détaillées pour
le Wi-Fi, le Bluetooth, l'accessibilité et la sortie audio ; les interrupteurs que Windows garde pour lui ouvrent la
page correspondante des Paramètres de Windows. Sous Windows 11, **Utiliser les paramètres rapides de Windows
(Windows 11)** sur la même page ouvre plutôt le panneau de Windows.

**Langue** (Alpha 4) : Aether Shell, installation comprise, parle anglais, chinois (simplifié), hindi, espagnol,
français, portugais, russe, arabe, bengali et ourdou. Paramètres → **Langue** permet d'en choisir une ; **Comme
Windows**, le réglage par défaut, parcourt vos langues d'affichage de Windows dans l'ordre où vous les avez classées et
prend la première qui fait partie des dix, et l'anglais sinon. Le changement s'applique aussitôt aux barres, aux menus,
à la recherche et aux Paramètres, et votre choix est gardé dans vos paramètres, que chaque version lit : une nouvelle
version, une réparation ou une réinstallation le retrouve, et l'installation, la mise à jour et le lanceur parlent
cette langue. Avec une langue choisie, la barre du haut écrit la date dans cette langue ; avec « Comme Windows », la
date garde votre format régional de Windows. Les noms des applications, des fichiers et des réseaux ne sont jamais
traduits. La recherche affiche les outils de Windows et les pages des Paramètres dans la langue de l'interface, et les
trouve par leur nom dans chacune des dix. L'arabe et l'ourdou se lisent de droite à gauche.

Les Paramètres comportent les pages Dock, Barre du haut, Météo, Mises à jour, Général, À propos, Bureau, Apparence,
Bienvenue et Langue. La météo est désactivée au départ. Choisissez la position automatique de Windows (avec votre accord) ou
une position manuelle avant de l'activer. Windows peut refuser la localisation ; dans ce cas, aucune position ni
météo n'est inventée. La recherche manuelle d'un lieu envoie le nom saisi à Open-Meteo ; les prévisions envoient les
coordonnées approximatives choisies. La position n'est jamais déduite d'un nom de réseau Wi-Fi ni d'une adresse IP
brute. Les prévisions en cache disparaissent au bout de six heures. Attribution des données : Open-Meteo.com, CC BY 4.0.

**La fenêtre météo** (Alpha 4) : cliquez sur la météo dans la Top Bar (son menu du clic droit garde **Paramètres
météo…**), ou Paramètres → Météo → **Ouvrir la météo**. Elle montre le lieu, son heure locale et le ciel de son heure,
la température et le ressenti, les 24 prochaines heures, 7 jours, l'arc du soleil, l'humidité, le vent, l'UV et la
pression, et une carte du pays avec ses températures actuelles. Recherchez une autre ville, ou choisissez **Ma
position** ; si la position n'est pas encore autorisée, la fenêtre la demande à Windows ou ouvre ses paramètres de
localisation. **Position précise** (activée par défaut) utilise la position de Windows à environ 100 m près ;
désactivée, elle est arrondie à environ 10 km. L'icône météo de la Top Bar ne bouge que sous le pointeur. La carte
vient de Natural Earth (domaine public) et est enregistrée dans l'application. Avec **Liquid** choisi dans Apparence,
la fenêtre météo, les Réglages rapides, la Recherche, les menus et les aperçus du Dock sont en verre comme les barres,
et les Paramètres prennent un aspect Liquid ; avec **Grain**, tous sont granuleux.

Général regroupe le consentement au démarrage, les préférences de mouvement et la modification des raccourcis. Une
nouvelle installation interactive (depuis l'Alpha 2) coche visiblement le démarrage à l'ouverture de session ;
décochez cette option avant d'installer pour la refuser. Les choix existants sont conservés. L'installation
silencieuse exige l'option de démarrage explicite. Le démarrage est propre à chaque utilisateur et facultatif ;
Paramètres → Général → Appliquer peut le modifier, et le Gestionnaire des tâches peut le désactiver de son côté.
Windows décide de l'ordre et du délai de lancement après l'ouverture de session ; il peut afficher le bureau et la
barre des tâches natifs avant qu'Aether soit prêt. Le lanceur stable installé réessaie un nombre limité de fois après
des plantages anormaux. Quitter volontairement, une mise à jour, une désinstallation ou une erreur ordinaire
l'arrêtent ; aucun processus ne peut garantir de survivre à toutes les pannes. Le contraste élevé et le mouvement
réduit suivent Windows quand c'est possible. La page À propos contient la licence MIT et l'attribution du projet.

Dans la version V2, la page Bureau contient **Remplacer la barre des tâches Windows**, activé par défaut (D23). Un
refus explicite enregistré est conservé. Décochez-le et appuyez sur Appliquer pour revenir à la cohabitation ;
cochez-le et appuyez sur Appliquer pour réactiver le remplacement. Fermer la fenêtre ou changer de page abandonne un
changement non appliqué. En mode remplacement, le Dock réserve de l'espace sous les applications agrandies. Les
fenêtres placées à la main peuvent encore devoir être déplacées. Le message d'état indique si le remplacement est
actif ou indisponible. Les Paramètres de Windows gardent leur page Barre des tâches native ; réglez le comportement
d'Aether Shell ici. En mode remplacement de l'Alpha 3, les fenêtres agrandies admissibles reçoivent des coins arrondis
et un fin liseré, sans changer leur taille ni leur état agrandi ; Aether annule cela en se fermant. Certaines
applications et certains types de fenêtres gardent des coins carrés. Depuis la 3.3, seulement entre les barres. Quand
les deux barres sont masquées, une fenêtre agrandie occupe tout l'écran, avec des coins carrés.

Depuis l'Alpha 4 (D72) : toute fenêtre agrandie entre les barres a ses coins arrondis, quelle que soit l'application
(navigateurs, Explorateur de fichiers, applications empaquetées ou lancées en administrateur), et elle garde ses propres
boutons de titre. Aether ne touche plus du tout à la fenêtre de l'application : de petites pièces qui lui appartiennent,
posées sur les quatre coins, montrent le bureau qui se trouve là, en direct, avec le fin liseré autour. Les coins ne sont
pas lissés. Une fenêtre placée devant la fenêtre agrandie les recouvre comme le reste.

**La barre du haut en texte sur le bureau** (Alpha 4, D71) : Paramètres → Barre du haut → **Présentation** → **Texte sur le
bureau** supprime la barre elle-même. Les mêmes éléments sont écrits tout en haut de l'écran, directement sur le fond
d'écran, à l'encre claire ou sombre selon l'image qui se trouve dessous, avec un halo doux. Cette bande reste libre de
fenêtres ; la marge supérieure et l'opacité du verre ne s'appliquent pas. Avec **Masquer automatiquement la barre du
haut**, le texte revient sur une bande de verre tant qu'une fenêtre se trouve dessous. Le contraste élevé garde la barre.

**Bienvenue** (depuis la 3.3) : après l'ouverture de session ou le déverrouillage, Aether Shell écrit à la main un
court message qui remplit l'écran, sur votre fond d'écran flouté, et les barres apparaissent quand il se termine :
« Salut », « Hello » ou « Hola », avec votre prénom si vous en avez saisi un. Au déverrouillage, il est déjà là, à la
place du bureau. Touchez, cliquez ou appuyez sur Échap pour le passer. L'installation demande le prénom, de façon
facultative (depuis la 3.5) ; Paramètres → Bienvenue permet de le désactiver et de régler la langue et le prénom. Si
le fichier de l'image a été déplacé ou supprimé, l'accueil utilise la copie que Windows en garde. Le prénom et votre
image restent sur ce PC ; ils ne sont jamais envoyés nulle part ni journalisés, et vider le champ retire le prénom.
Il n'apparaît qu'après que Windows vous a connecté ; l'écran de connexion de Windows lui-même n'est pas modifié. À
l'ouverture de session, il apparaît dès que Windows démarre Aether, ce qui peut arriver après le bureau.

**Tactile** (depuis la 3.3) : faites défiler toute liste ou page longue du doigt ; elle suit directement, glisse
encore quand vous relâchez et revient en douceur au-delà des extrémités. Un appui agit quand vous levez le doigt, un
glissement n'agit jamais, et maintenir un élément de la barre du haut ou du Dock ouvre son menu (maintenez et
déplacez pour réordonner le Dock). Balayez depuis le bord haut ou bas de l'écran pour faire revenir une barre masquée.
Dans les réglages rapides, glissez vers le haut ou le bas pour changer de page. Sur un curseur, glissez sur le côté
pour le modifier ou appuyez à l'endroit voulu ; un glissement vers le haut ou le bas fait défiler la page. Un curseur
des Paramètres s'applique quand vous relâchez.

Le **masquage automatique** (Alpha 3) se règle séparément pour chaque barre : Paramètres → Dock → **Masquer
automatiquement le Dock** et Paramètres → Barre du haut → **Masquer automatiquement la barre du haut**. Les deux sont
désactivés par défaut. Une barre qui se masque s'efface quand une fenêtre la recouvre. Posez le pointeur sur le bord
inférieur de l'écran pour faire revenir le Dock, ou sur le tout premier pixel du haut, sans marge, pendant un quart de
seconde pour faire revenir la barre du haut ; partir plus tôt annule, si bien que les onglets et les boutons de
fermeture des applications restent libres. Elle reste tant que vous l'utilisez ou que l'un de ses menus ou panneaux
est ouvert, puis se masque peu après. Sur le bureau, les barres reviennent, même avec des fenêtres derrière (Afficher
le bureau). Une barre qui se masque ne réserve pas d'espace : les fenêtres agrandies remplissent l'écran et la barre
apparaît par-dessus, comme la barre des tâches de Windows en masquage automatique.

La page Bureau propose aussi Importer les épingles de la barre des tâches Windows. Elle lit les raccourcis épinglés
pris en charge et ajoute ceux qui manquent, sans modifier les épingles de Windows ni remplacer vos épingles Aether.
Certaines épingles de Windows 11 et leur ordre d'origine ne sont pas accessibles. Le mode remplacement importe les
épingles prises en charge et ajoute une fois l'Explorateur de fichiers. La synchronisation automatique ajoute les
nouvelles épingles disponibles au démarrage, à l'activation de la synchronisation ou au redémarrage de l'Explorateur ;
elle respecte votre ordre et vos retraits explicites. Un import manuel peut rétablir des épingles retirées. La page
Apparence contient l'interrupteur de synchronisation automatique, le thème et l'accent, les préréglages d'icônes et
les réglages de mouvement.

Le Dock a des éléments Démarrer, Rechercher et Vue des tâches. Faites glisser les icônes épinglées pour changer leur
ordre enregistré ; faire glisser une icône seulement ouverte change l'ordre de cette session sans l'épingler. Les
métadonnées d'icône de profil par fenêtre, quand elles existent, permettent de distinguer les profils de Chrome ; sans
elles, l'icône reste générique. Les menus contextuels du Dock proposent les actions de fenêtre, de nouvelle instance
et d'épinglage applicables, sans deviner de profil.

Les menus de la barre du haut donnent accès aux paramètres de l'horloge, de l'alimentation, du son et du réseau. La
flèche de fin qui montrait la barre des tâches native a été retirée dans l'Alpha 2 ; depuis l'Alpha 3, le chevron des
icônes cachées donne accès au dépassement de la zone de notification de Windows. La récupération reste disponible par
les paramètres Bureau, Quitter et la commande de récupération autonome. Le chemin interne de suspension/reprise reste
interne.

Pour rétablir la barre native, désactivez l'option de la page Bureau ou choisissez Quitter dans le menu de la barre
du haut.

Depuis le dossier d'installation, `AetherShell.exe --restore-taskbar` demande à une instance en cours de désactiver
le mode. Quand Aether Shell ne fonctionne pas, la même commande rétablit elle-même la barre des tâches de Windows :
- elle utilise l'état enregistré avant que la barre des tâches soit masquée ;
- elle désactive le mode ;
- elle affiche le résultat.

`AetherShell.exe --quit` demande à une instance en cours de se fermer et ne fait rien sinon.

Un assistant de récupération séparé rétablit la barre des tâches après une défaillance de l'application, et
l'application fait de même si son assistant se ferme. **Réparer** ou **Désinstaller** dans Paramètres Windows >
Applications > Applications installées rétablissent aussi une barre des tâches laissée masquée par Aether Shell. La
réparation conserve vos paramètres, y compris le choix de la page Bureau : Aether Shell masquera donc de nouveau la
barre des tâches à son prochain démarrage, sauf si vous désactivez l'option. Si rien de tout cela ne fonctionne,
terminez les processus d'Aether Shell dans le Gestionnaire des tâches et redémarrez l'Explorateur Windows. La
récupération dans tous les cas de panne et toutes les versions de Windows n'est pas encore qualifiée ; lisez les
limites connues avant d'activer ce mode.

Les données locales se trouvent dans `%LOCALAPPDATA%\AetherShell` : paramètres, cache, mises à jour et journaux de
taille limitée. Ouvrez le dossier des journaux depuis Général. L'export du diagnostic de base contient la version du
produit et la build de Windows ; relisez vous-même tout fichier avant de le partager. Rétablir les valeurs par défaut
demande une confirmation et garde une sauvegarde des paramètres. La désinstallation conserve les données de
l'utilisateur, sauf si leur suppression est explicitement choisie.

Consultez les [limites connues](../../../KNOWN_LIMITATIONS.md) (en anglais) pour les qualifications encore manquantes.
