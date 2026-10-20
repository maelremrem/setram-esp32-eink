# Portail embarqué SETRAM

## Utilisation

Maintenir MENU 2 s pour ouvrir le portail (fermeture automatique après 5 minutes).
Les deux étapes QR sur l’écran sont conservées : OK alterne entre rejoindre le
Wi-Fi du boîtier et ouvrir le portail. Les valeurs manuelles SSID/mot de passe
restent affichées sur le boîtier. Adresse de secours : `http://192.168.4.1/`.

Sur l’accueil : **Mon arrêt SETRAM** ouvre `/bus`. Le lien figure aussi dans
`/wifi`, qui conserve le scan des réseaux et les champs manuels.

1. **Choisir une ligne** : toutes les lignes du catalogue embarqué sont affichées
   directement sous forme de boutons, sans ouvrir de liste déroulante. « Toutes
   les lignes » conserve la recherche globale.
2. **Rechercher un arrêt** par nom (sans distinction de casse ou d’accents) ou
   `stop_id`, puis sélectionner son quai physique. La liste contient tous les
   quais correspondants : aucune limite aux 80 premiers résultats.
3. **Choisir le sens** avec les cases de destination. Si une ligne est choisie,
   seuls ses quais et ses services sont proposés.

Les `stop_id`, `route_id` et `direction_id` restent visibles. Jusqu’à deux suivis
indépendants peuvent provenir de lignes et d’arrêts différents ; le récapitulatif
est conservé pendant les changements de ligne et les recherches. Après
rechargement, les deux suivis enregistrés sont restaurés sans restreindre la
liste complète des lignes ni préremplir la recherche avec le premier arrêt.
Sélectionner une fréquence de 30, 60 ou 120 secondes.
La saisie manuelle reste disponible quand le catalogue ne convient pas.
Elle vérifie le format, pas l’existence de l’identifiant dans le catalogue.

## Données et régénération

```sh
python tools/generate_bus_catalogue.py
# Ou utiliser une archive officielle déjà téléchargée :
python tools/generate_bus_catalogue.py --zip /chemin/gtfs-officiel.zip
pio run -e crowpanel_jd79661 -e crowpanel_ssd1680
```

Le générateur réutilise l’URL officielle de `tools/setram.py`. Il joint
`stop_times.trip_id` → `trips.route_id/direction_id/trip_headsign` → `routes`,
puis garde uniquement les arrêts desservis de `location_type` vide ou 0.
Il ne remplace jamais un quai par son `parent_station`. Les directions absentes
ne sont pas devinées : ces motifs ne sont pas proposés.

`lib/BusCatalog/catalogue.json` contient l’URL source, son URL finale, le SHA-256
de l’archive téléchargée, l’heure UTC de génération et la commande ci-dessus.
Le JSON gzip et les tables C++ sont embarqués dans le firmware, sans système de
fichiers, CDN ni dépendance JavaScript. La page source est
`lib/BusPortal/portal.html`; le générateur produit aussi `GeneratedPortal.h`.
Après une modification de la page seule, régénérer le header sans retélécharger
ni modifier le catalogue, puis compiler :

```sh
python -c 'from pathlib import Path; p=Path("lib/BusPortal"); (p/"GeneratedPortal.h").write_text("#pragma once\nstatic const char portalHtml[] PROGMEM = R\"BUSPORTAL(" + (p/"portal.html").read_text() + ")BUSPORTAL\";\n")'
pio run -e crowpanel_jd79661 -e crowpanel_ssd1680
```

Le catalogue est statique et peut être périmé. Les variantes de destinations
issues du GTFS ne sont pas des passages temps réel. Le filtre réel reste
stop/ligne/direction, pas le texte de destination. Les libellés enregistrés
respectent la validation existante : 64 octets UTF-8 au maximum, caractères
interdits remplacés par des espaces. Les identifiants ne sont pas normalisés.

## Portée et sécurité

Les routes vivent sur le serveur WiFiManager, uniquement pendant le portail.
Chaque ouverture génère un jeton CSRF aléatoire ; les POST personnalisés
contrôlent le jeton, l’origine (si présente) et l’hôte IP AP/STA. Les choix
catalogue sont validés contre la table C++ embarquée ; les noms affichés sont
issus de cette table et non de champs client. Les contenus dynamiques utilisent
`textContent`/`value`, pas `innerHTML`. Le serveur refuse plus de 12 arguments,
les champs de plus de 64 octets et les corps annoncés de plus de 2048 octets.
Limite : ces vérifications ont lieu après l’analyse HTTP du WebServer Arduino,
qui peut allouer le corps en amont ; ce n’est pas un pare-feu anti-DoS.

Le portail HTTP peut aussi être visible sur l’IP LAN du boîtier pendant cette
fenêtre. Il ne s’agit pas d’une administration LAN permanente. Les routes Wi-Fi
standard restent celles de WiFiManager. Aucun identifiant Wi-Fi n’est journalisé.
Le blob `settings` contient les six champs de chaque suivi et le booléen d’ordre
(13 champs au total), enregistré en une écriture NVS puis relu. Les anciens blobs
à six champs sont migrés en mono-suivi sans effacer les autres réglages. La
fréquence reste une clé `poll-ms` séparée, également relue. Les deux clés ne
constituent pas une transaction atomique commune. NVS n’est pas chiffré par défaut.

## Vérifications sans flash

```sh
python -m unittest discover -s tests -p 'test_*.py' -v
mkdir -p .cache
g++ -std=c++11 -Ilib/BusCatalog -Ilib/BusPortal -Ilib/PortalSettings \
    tests/test_bus_portal.cpp -o .cache/test_bus_portal
.cache/test_bus_portal
# Harness navigateur : vrais assets gzip + validation C++ du mapping
g++ -std=c++11 -Ilib/BusCatalog -Ilib/BusPortal -Ilib/PortalSettings \
    tests/bus_catalogue_lookup.cpp -o .cache/bus_catalogue_lookup
python tests/serve_bus_portal.py 18765
# Ouvrir http://127.0.0.1:18765/bus
# Test navigateur Chrome réel : POST dual, GET/current, reload, filtres et max2
node tests/browser_portal_test.mjs
```

Le harness est local, n’émule pas la radio ni la persistance NVS. Les soumissions
catalogue traversent le vrai code de recherche C++ pour les deux tuples, rejettent
les doublons exacts et sont conservées en mémoire pour `/api/current` et un reload.
Les tests Chrome comparent l’ensemble des 63 boutons de lignes et les quais
filtrés au catalogue réel (1062 quais), vérifient les destinations de la ligne
choisie, la recherche sans accents, la soumission par le bouton Enregistrer,
la restauration des deux libellés, les services distincts au même quai et le
rejet du troisième choix. Ils contrôlent aussi le maintien de toutes les lignes
après rechargement et l’absence de débordement à 1280 et 360 px.
Les captures réelles sont exportées dans `.cache/portal-desktop.png` et
`.cache/portal-mobile.png` ; la soumission relue est dans
`.cache/browser-submit.json`. Ces comptes correspondent au catalogue actuel,
non à une garantie d’exhaustivité du réseau à une date future.

Pas de flash effectué. La radio captive, le scan matériel, les QR sur panneau
physique et la persistance après redémarrage doivent encore être vérifiés sur
le boîtier.

## Double suivi

La sélection catalogue accepte jusqu’à deux tuples indépendants (quai physique,
ligne et sens), y compris sur deux recherches différentes. Le même quai peut
être choisi deux fois pour des services distincts ; seul un tuple identique est
refusé. La sélection reste visible, retirable et limitée à deux choix. Chaque
tuple est validé contre l’allowlist C++ puis relu via `/api/current`.

Chaque suivi possède son propre état temps réel, son cache d’horaires et son
échéance de polling. Les deux requêtes d’une période sont lancées l’une après
l’autre. Les erreurs et avertissements d’expiration sont affichés dans la rangée
du suivi concerné, sans masquer ceux de l’autre.

Sur l’écran, l’option conditionnelle « Inverser suivis » persiste l’ordre 1/2.
Elle affecte simultanément haut/bas et le cycle EXIT court à l’accueil : double,
simple 1, simple 2, double. Les répétitions EXIT n’activent pas ce cycle ; hors
accueil, les commandes de navigation existantes sont conservées.
