# BusNotifier ESP32 — SETRAM / CrowPanel e-paper 2,13 pouces

Afficheur autonome des prochains passages SETRAM, développé avec PlatformIO et Arduino pour l’Elecrow CrowPanel ESP32-S3. Le firmware lit directement le flux officiel GTFS-Realtime en HTTPS ; aucun serveur intermédiaire ni abonnement à une API privée n’est nécessaire.

## Fonctions

- Accueil : bandeau « Mes lignes favorites » en multivue, ligne et arrêt en vue simple ; destination, compte à rebours et passages suivants.
- Liste : jusqu’à quinze passages, cinq par page, pagination dans le bandeau.
- Horloge à droite du bandeau ; icône Wi-Fi en arcs juste à gauche, alimentée par le RSSI réel.
- Complément prévu J+1/J+2 explicite, avec P = prévu et R = annoncé en temps réel.
- Configuration Wi-Fi par portail captif et deux QR, choix de quai/ligne/sens dans un catalogue embarqué.
- Mise à jour OTA uniquement après activation physique dans les options, avec écran dédié et clé temporaire.
- Interface française, stockage de la configuration en NVS, navigation indépendante des transferts réseau et e-paper.
- Rafraîchissement complet uniquement ; aucun transfert si les pixels normalisés sont identiques.
- Icône et pourcentage de batterie masqués ; aucune mesure de batterie inventée.

## Matériel et compatibilité

| Élément | Valeur |
| --- | --- |
| Carte | CrowPanel ESP32-S3, module avec flash 8 Mo et PSRAM OPI 8 Mo |
| Écran visible | 250 × 122 pixels, noir et blanc |
| RAM du contrôleur | 128 × 250, 4 000 octets ; padding hors écran blanc |
| Contrôleurs | JD79661 et SSD1680, firmwares distincts |
| Wi-Fi | 2,4 GHz |
| Fuseau | Europe/Paris, changement été/hiver |
| SPI logiciel | SCK 12, MOSI 11, RESET 10, DC 13, CS 14, BUSY 9 |
| Alimentation écran | GPIO7, HIGH |
| BUSY | Actif LOW JD79661, HIGH SSD1680 |

Le JD79661 est la variante par défaut et celle utilisée pour le panneau du projet. Une compilation SSD1680 réussie ne signifie pas que ce firmware convient à un panneau JD79661. Identifier le contrôleur avant tout flash, y compris OTA.

Le wrapper JD conserve le cycle constructeur : initialisation/effacement des deux plans/update une fois, puis mises à jour en standby avec coupure du booster. Il compense la réflexion horizontale rapportée par inversion des rangées natives. SSD conserve ses séquences reset/update/veille. Les sources constructeur restent inchangées. Détails : [pilote](lib/CrowPanel/README_DRIVER.md).

Les modèles de boîtier sont dans [3D/](3D/) ; montage, hypothèses et précautions : [3D/NOTICE.txt](3D/NOTICE.txt). La géométrie exportée ne certifie pas l’ajustement réel, la flexibilité des clips ou la sécurité mécanique d’une batterie.

## Installation et premier flash USB

Installer PlatformIO Core et Python 3. Depuis la racine du projet :

```sh
pio run -e crowpanel_jd79661
# Pour un panneau SSD1680 uniquement :
pio run -e crowpanel_ssd1680
```

Le premier flash du firmware contenant l’OTA doit se faire en USB : l’ancien firmware ne peut pas recevoir une fonction OTA qu’il ne possède pas.

```sh
pio device list
pio run -e crowpanel_jd79661 -t upload
pio device monitor -b 115200
```

Remplacer l’environnement selon le contrôleur et, si nécessaire, sélectionner le port avec `--upload-port`. Pour récupérer un appareil, maintenir BOOT pendant le reset puis refaire un flash USB. Le connecteur utilise un CH340K ; la configuration série USB native existante est conservée ici, et la LED PWR n’est pas modifiée.

Les fichiers sont produits dans `.pio/build/<environnement>/`. Pour OTA, utiliser seulement `firmware.bin`, pas `bootloader.bin`, `partitions.bin`, une image fusionnée ni une image de système de fichiers. Le partitionnement `default_8MB.csv` comporte `otadata`, `app0` et `app1` ; chaque partition application fait `0x330000` octets. Il permet d’écrire la nouvelle application sans écraser celle qui s’exécute.

Aucun upload, flash physique ou mise à jour réseau n’est déclenché automatiquement par les tests de ce projet.

## Configuration Wi-Fi et SETRAM

1. Au premier démarrage sans réseau enregistré, l’écran présente le QR Wi-Fi du point d’accès `BusNotifier-XXXXXX` et sa clé aléatoire. Scanner le QR ou rejoindre ce réseau manuellement.
2. Accepter de rester connecté même sans Internet. Désactiver temporairement les données mobiles/VPN si le téléphone quitte le réseau.
3. Le portail peut s’ouvrir automatiquement. Sinon, OK alterne entre le QR Wi-Fi et le QR du lien `http://192.168.4.1` ; ouvrir ce lien dans le navigateur.
4. Dans **Configure WiFi**, sélectionner le réseau domestique 2,4 GHz et saisir son mot de passe. Le bouton Refresh relance le scan.
5. Dans **Mon arrêt SETRAM**, ou `/bus`, rechercher un arrêt, filtrer par ligne, puis choisir son quai physique, sa ligne, destination et sens. Le catalogue fonctionne sans Internet. Les identifiants techniques sont remplis automatiquement et les deux choix restent restaurés après rechargement.
6. Choisir la fréquence 30/60/120 secondes et enregistrer. Les valeurs bus sont validées, sauvegardées en une valeur NVS puis relues ; les erreurs restent visibles.

La saisie manuelle reste disponible : `stop_id` exact, `route_id` exact, `direction_id` -1 (tous), 0 ou 1. Deux services distincts peuvent utiliser le même `stop_id`; seul un tuple complet identique est refusé. Les noms sont des libellés, pas des filtres techniques ; les identifiants ne sont pas corrigés ou normalisés.

Le portail s’ouvre si la connexion initiale échoue ou si l’arrêt n’est pas configuré. MENU maintenu 2 secondes le rouvre hors mode OTA. EXIT le ferme ; délai maximum 5 minutes. Les nouvelles requêtes SETRAM sont suspendues pendant le portail. La connexion initiale peut prendre jusqu’à 15 secondes.

Les QR sont générés avec ricmoo/QRCode 0.0.1 et une marge blanche ; leur capacité est vérifiée avant encodage. Une saisie manuelle reste proposée si le QR est trop long ou illisible. La clé du point d’accès change au redémarrage. Les aperçus dans `docs/screens/portal-ui-*` contiennent uniquement des identifiants de test publics.

`include/config.example.h` peut être copié vers `include/bus_config.h` pour des valeurs bus par défaut facultatives. Aucun mot de passe Wi-Fi n’est requis dans les sources. La configuration sauvegardée par le portail est prioritaire. Les fichiers privés sont exclus par `.gitignore`.

Outil PC optionnel pour retrouver les identifiants :

```sh
python tools/setram.py search "nom de l’arrêt"
```

Détails du catalogue et régénération : [docs/BUS_PORTAL.md](docs/BUS_PORTAL.md).

## Interface et boutons

| Bouton | GPIO | Action normale |
| --- | --- | --- |
| MENU / HOME | 2 | Ouvre les options ; si déjà dans un menu, le ferme et revient à l’accueil |
| MENU maintenu 2 s | 2 | Ouvre le portail, sauf dans le mode OTA |
| EXIT / RETOUR | 1 | Revient d’un niveau ; ferme le portail actif |
| HAUT / PRV | 6 | Option/page précédente ; depuis la première page, retour à l’accueil |
| BAS / NEXT | 4 | Option/page suivante |
| OK / molette | 5 | Sélection ; depuis l’accueil, horaires ; dans les horaires, actualisation |
| MENU + EXIT ensemble 2 s | 2 + 1 | Quitte l’attente OTA, sauf pendant l’écriture du firmware |

La molette TM_2024A utilise des contacts UP/DOWN/CONF, pas une quadrature A/B. Entrées actives LOW, anti-rebond 40 ms, directions au pressage puis répétition après 450 ms et toutes les 200 ms. Les autres appuis courts sont validés au relâchement. Un MENU long ne déclenche pas aussi un MENU court. Les boutons maintenus au démarrage doivent être relâchés avant utilisation.

Les options contiennent six entrées :

1. Arrêt favori — portail.
2. Ligne suivie — portail.
3. Rafraîchir — alterne 30/60/120 secondes, sauvegarde en NVS avec relecture.
4. Wi-Fi — sous-menu Reconnecter, Adresse IP, Retour.
5. Langue — français uniquement.
6. Mise à jour OTA — activation temporaire explicite.

Cinq lignes sont visibles ; la liste défile pour atteindre la sixième. Les directions bouclent entre première et dernière option. La sélection n’active jamais l’OTA sans validation OK.

L’icône Wi-Fi indique la qualité du lien radio, pas la disponibilité d’Internet ou de SETRAM. Le RSSI est classé en quatre niveaux : faible sous -80 dBm, puis seuils -80/-70/-60 dBm. Une hystérésis de 3 dB évite qu’un bruit autour d’un seuil provoque des refreshs complets inutiles. La déconnexion est distincte. Elle est réévaluée au rendu normal, pas à haute fréquence.

## Mise à jour OTA volontaire

### Activation

1. Connecter l’appareil au Wi-Fi du même réseau que l’ordinateur.
2. Ouvrir les options, choisir **Mise à jour OTA**, puis OK.
3. L’écran dédié présente la préparation, l’adresse IP, le nom de l’appareil et la clé temporaire.
4. Attendre le statut prêt. Le firmware attend la fin d’une requête HTTPS ou d’un transfert panneau déjà commencé ; il ne lance plus de requêtes SETRAM.
5. Le service ArduinoOTA est activé sur UDP 3232 uniquement pendant ce mode. `ArduinoOTA.begin()` ne fournit pas de retour de validation du bind UDP/mDNS : le statut prêt décrit la mise en route logicielle, pas une preuve de connexion à travers le réseau/pare-feu. L’activation n’est pas enregistrée en NVS : après reset, l’OTA est éteinte.

Chaque activation génère une nouvelle clé temporaire. Elle est affichée sur l’appareil pour authentifier le poste d’envoi, mais jamais journalisée ni écrite dans le README, Git ou la NVS.

### Envoi depuis l’ordinateur

Compiler le firmware du bon contrôleur, puis exécuter :

```sh
pio run -e crowpanel_jd79661
python tools/upload_ota.py --ip ADRESSE_IP_AFFICHEE --firmware .pio/build/crowpanel_jd79661/firmware.bin
```

Remplacer `ADRESSE_IP_AFFICHEE` par l’IPv4 affichée. Le programme demande la clé avec une saisie masquée et appelle le vrai `espota.py` de PlatformIO dans le même processus : le secret n’est pas placé dans les arguments de commande, l’historique ou une variable d’environnement. S’il ne peut pas masquer la saisie, il refuse le transfert : le lancer depuis un terminal interactif.

Validation locale sans connexion ni mot de passe :

```sh
python tools/upload_ota.py --ip ADRESSE_IP_AFFICHEE --firmware .pio/build/crowpanel_jd79661/firmware.bin --dry-run
```

Le script vérifie le fichier, l’en-tête ESP32 et la taille maximale de la partition. Ces contrôles ne prouvent pas que le firmware correspond au bon contrôleur d’écran ; sélectionner le bon environnement reste indispensable. Il n’y a pas de signature de firmware ni de Secure Boot ajouté par ce projet.

Le protocole envoie une invitation UDP à l’appareil, puis l’ESP32 ouvre une connexion TCP de retour vers le PC. Autoriser ce retour sur le port TCP **32323** par défaut ; `--local-port` et `--local-ip` permettent de choisir l’interface/port. Le trafic multicast mDNS aide à la découverte, mais l’envoi par IP ne dépend pas d’un nom `.local`. Les réseaux invités avec isolation des clients, certains VPN et les pare-feu peuvent bloquer l’envoi.

### Pendant et après le transfert

- Modem-sleep désactivé et CPU forcé à 240 MHz ; boucle OTA fréquente, sans attente principale de 20 ms.
- Les économies de calcul ne doivent pas retarder les paquets OTA. Les workers inutilisés restent bloqués ; les réveiller artificiellement n’améliorerait pas le transfert.
- L’écran e-paper reste fixe pendant la réception : pas de SPI/refresh concurrent avec l’écriture flash. La progression est disponible côté outil d’envoi. Les callbacks OTA ne dessinent pas.
- ArduinoOTA effectue le transfert de façon synchrone ; une annulation pendant l’écriture n’est pas permise. Ne pas couper l’alimentation.
- Après succès, redémarrage sur la nouvelle application ; OTA désactivée au nouveau démarrage.
- Après erreur, l’écran affiche le problème ; un nouvel essai ou MENU + EXIT maintenus ensemble 2 secondes permettent de reprendre/quitter.
- À la sortie, service arrêté, clé invalidée et paramètres de sommeil Wi-Fi/fréquence CPU antérieurs restaurés. Le retour normal relance les données SETRAM, sans réutiliser une ancienne réponse.

Aucun serveur OTA n’est ouvert au démarrage ni par une option persistante. Ne pas exposer l’OTA ou le portail sur Internet. ArduinoOTA utilise une authentification à défi et n’offre pas une liaison TLS ni une signature cryptographique du binaire : utiliser uniquement un LAN de confiance. Seules les mises à jour application sont prévues ; les uploads de système de fichiers sont refusés.

## Passages et fiabilité des données

Au boot, les étapes Wi-Fi → NTP → première réponse SETRAM reposent sur des événements réels, sans progression temporelle inventée ni animation continue. Le portail et la navigation restent prioritaires. Une première réponse vide ou en erreur termine le boot sans fabriquer de bus.

Pour aujourd’hui, seuls les événements absolus GTFS-RT sont utilisés. Les annonces restantes restent en tête ; J+1/J+2 complètent les places suivantes avec des horaires théoriques explicitement marqués. Un futur passage affiche J+n, HH:MM et P/R, jamais un compte à rebours prévu présenté comme temps réel. Si J+1 est sans service, J+2 peut être utilisé. Doublons d’époque supprimés en faveur du temps réel.

Une absence d’annonce ne prouve pas une fin de service. Une panne Wi-Fi/API, un flux périmé/invalide ou une horloge non synchronisée reste une erreur visible, pas un prétexte pour afficher du théorique comme du temps réel. Les annulations, arrêts ignorés et départs passés sont filtrés ; les branches partageant les mêmes stop/route/direction ne sont pas distinguées par headsign.

Le timestamp du producteur peut être en avance :

- Jusqu’à 60 s : classification fraîche.
- De 61 à 180 s : utilisable avec avertissement, sans déplacer les heures absolues.
- Au-delà de 180 s d’avance ou d’âge : rejet.

La mention tolérée « Flux avance » n’est pas affichée en bas de l’écran ; le statut central et les diagnostics série la conservent. « Horloge flux avancee » indique un rejet réel et reste visible. Les légendes P/R, l’expiration du calendrier et les absences d’horaires sont préservées.

HTTPS valide le certificat avec le certificat racine embarqué ; pas de `setInsecure`. Le flux est téléchargé en PSRAM avec limite 2 Mo et décodage Protobuf borné. L’heure NTP est nécessaire pour TLS, fraîcheur et compte à rebours. Les durées réseau, offsets exacts et timestamps restent des diagnostics, pas des raisons de faire clignoter l’écran.

Sources et observations : [docs/API-SETRAM.md](docs/API-SETRAM.md).

## GTFS prévu et régénération

Le snapshot statique embarqué couvre **2026-09-30 au 2026-11-11 inclus**. Au-delà, le firmware signale son expiration ; il ne prolonge pas arbitrairement les services. Il faut régénérer puis recompiler/flasher pour actualiser le calendrier.

```sh
python tools/generate_timetable.py --refresh
python tools/generate_bus_catalogue.py --help
pio run -e crowpanel_jd79661
```

La provenance exacte (URL publique, SHA256 du ZIP, calendrier et comptes) est dans `lib/Timetable/timetable-source.json`. Régénérer séparément le catalogue d’arrêts/services si nécessaire. Les jours futurs sont des jours civils Europe/Paris, pas `maintenant + 86400` ; les tests couvrent changements d’heure et heures GTFS au-delà de 24:00.

Détails et commandes : [docs/TIMETABLE.md](docs/TIMETABLE.md), [docs/BUS_PORTAL.md](docs/BUS_PORTAL.md).

## Batterie, LED et consommation

La batterie n’est pas mesurée : le schéma officiel comporte un chargeur/connecteur, mais pas de liaison BAT → ADC ni de jauge. Son icône et son pourcentage sont masqués ; les champs compatibles des modèles n’affectent plus les pixels. Les helpers Battery/BatteryConfig sont conservés pour une future intégration, pas une mesure active. Définir leurs anciennes macros seul ne réactive rien.

Ne jamais brancher directement BAT à une GPIO. Une future jauge ou un pont diviseur nécessite vérification physique de la révision et de la tension ; GPIO40/41 ne sont pas ADC. Aucun état de charge n’est inféré de l’alimentation 3,3 V ou de l’USB.

PWR est une LED pilotée par GPIO19, pas un voyant automatique de charge. GPIO19 partage la fonction USB D− native. La présente version ne modifie pas la LED ni le chemin série ; une correction dédiée demande validation de l’USB réel.

Hors OTA :

- Workers écran/réseau réveillés par notifications, pas de polling périodique au repos.
- Une seule image suivante remplaçable ; jamais de file de refreshs intermédiaires.
- Boucle principale 20 ms au repos, 5 ms pendant l’interaction et les 500 ms suivantes.
- Boutons échantillonnés à 5 ms ; serveur du portail traité à 10 ms.
- QR recalculés seulement sur changement visible ; horaires futurs mis en cache par jour et sélection.
- Polling 60 s par défaut, choix 30/60/120 s ; compte à rebours/fraîcheur réévalués toutes les 10 s, indépendamment du polling.
- Modem-sleep Arduino conservé ; aucune veille profonde qui bloquerait boutons ou portail.

Le SDK Arduino fourni n’active pas `CONFIG_PM_ENABLE` : pas de promesse de light-sleep automatique ou de fréquence dynamique. Aucun gain en mA ou en heures d’autonomie n’a été mesuré. Si l’alimentation disparaît, l’e-paper conserve sa dernière image ; un ancien compte à rebours ne garantit donc pas que l’appareil fonctionne encore.

Le partial refresh est supprimé : ni menu, ni budgets, ni API d’activation, ni restauration de ses anciennes clés NVS. Les préférences bus/Wi-Fi/fréquence sont conservées. Un vrai changement visible produit encore le flash et la latence optique d’un refresh complet.

## Tests et limites de vérification

Après installation des dépendances PlatformIO, avec un compilateur C++ hôte (GCC/Clang), les en-têtes de développement OpenSSL/libcrypto pour les tests OTA et Pillow pour les aperçus :

```sh
python tests/run_navigation.py
python tests/run_wifi_signal.py
python tests/run_ota_mode.py
python tests/run_bus_ui.py
python tests/run_main_runtime.py
python tests/run_runtime_policy.py
python tests/run_transit.py
python tests/run_crowpanel_driver.py
python -m unittest discover -s tests -p 'test_*.py'
pio run -e crowpanel_jd79661 -e crowpanel_ssd1680
```

Pour tester les QR avec décodage indépendant :

```sh
python -m pip install -r tests/portal-ui-requirements.txt
python tests/run_portal_ui.py
```

Les tests GFX rendent les vrais framebuffers, contrôlent glyphes/padding et exportent des PNG natifs/x4 dans `docs/screens`. Les tests runtime compilent les vrais main, renderer, décodeur, calendrier et pilotes, avec seulement les primitives radio/HTTP/horloge/NVS/tâches contrôlées. Les tests OTA vérifient activation explicite, authentification configurée, paramètres d’énergie, callbacks et arrêt ; l’outil PC a un mode de validation sans réseau.

Cela ne prouve pas un transfert OTA radio réel, un reboot physique, la sauvegarde NVS sur appareil, les timings FreeRTOS, la lisibilité optique ou la consommation. Ces points doivent être testés sur une carte alimentée correctement. Les fixtures de `tests/build_fixture` ne doivent jamais être flashées.

En cas de GCC ASan absent sur le système, utiliser Clang pour les suites disposant d’un mode sanitaire. Les références pilotes peuvent être comparées à une copie pristine upstream avec `CROWPANEL_UPSTREAM` pour vérifier les fichiers vendorés et traces GC.

## Arborescence et publication

- `src/main.cpp` : orchestration, état UI/boot, workers, portail, requêtes et mode OTA.
- `lib/BusUI`, `lib/PortalUI` : rendu e-paper et QR.
- `lib/Navigation`, `lib/WiFiSignal`, `lib/OtaMode` : navigation, signal radio, session OTA.
- `lib/Transit`, `lib/Timetable`, `lib/DisplayPolicy`, `lib/PaintPolicy` : annonces, prévu et règles de visibilité.
- `lib/BusPortal`, `lib/PortalSettings` : catalogue, validation et sauvegarde.
- `lib/CrowPanel` : adaptateurs et sources constructeur.
- `tools/` : recherche SETRAM, génération des données et envoi OTA.
- `tests/` : tests hôte et fixtures publiques.
- `docs/` : documentation spécialisée et aperçus.
- `3D/` : boîtier et pièces exportées.

`.gitignore` exclut `.pio`, `.cache`, configurations privées, `.env`, clés privées, environnements Python, caches de tests et sauvegardes CAD. Les sources, exemples publics, certificats racines, aperçus documentés et modèles livrés restent publiables. Il ne retire pas un secret déjà suivi : inspecter les fichiers/l’historique avant publication. La NVS n’est pas chiffrée par ce projet. Le portail HTTP peut aussi être accessible depuis le LAN pendant son ouverture ; réseau de confiance uniquement.

## Sources constructeur

- Dépôt : https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250
- Commit importé : `11291e3e9a868be8943232b065f3e264dfaeeae3`.
- SSD1680 : `example/arduino/Example Code/2.13_Global_refresh`.
- JD79661 : `example/arduino-v1.2/main`.
- Produit : https://www.elecrow.com/crowpanel-esp32-2-13-e-paper-hmi-display-with-122-250-resolution-black-white-color-driven-by-spi-interface.html
- Wiki : https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_HMI_2.13-inch_Display.html

Les fichiers constructeur `EPD_Init.cpp` et `spi.cpp` sont vendorés comme `.cpp.inc`. Un timeout BUSY borné est fourni par le wrapper ; ce n’est pas une modification de LUT spéculative. Les droits des sources constructeur restent ceux du fabricant ; vérifier les conditions avant redistribution commerciale.

### Suivi double

Le portail SETRAM permet de choisir deux suivis indépendants (quai, ligne, destination, sens), y compris deux services distincts au même quai ; seul le tuple complet strictement identique est refusé. L’accueil affiche les deux lignes : HAUT ouvre la première en vue simple et BAS la seconde ; EXIT revient à la multivue sans faire défiler les lignes. En vue simple, HAUT/BAS naviguent dans les horaires. Chaque suivi est interrogé à chaque période avec son propre cache et sa propre échéance, et les erreurs du second suivi restent visibles en vue double.
# Suivi de deux arrêts

BusNotifier peut suivre deux quais physiques et lignes indépendants. Le portail catalogue conserve deux sélections, les affiche dans un résumé retirable et refuse les identifiants hors catalogue. En mode double, HAUT sélectionne la première ligne affichée et BAS la seconde ; EXIT revient à la multivue sans cycle ; « Inverser suivis » permute durablement l’ordre affiché et ces deux raccourcis.
