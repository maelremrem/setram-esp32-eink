# Passages mixtes temps reel et prevu J+1/J+2

## Comportement

Avec un flux temps reel utilisable, l'accueil et la liste conservent les annonces restantes aujourd'hui et ajoutent les heures prevues des deux prochains jours civils. L'accueil affiche les trois premiers passages, la liste jusqu'a quinze sur trois pages. Les passages futurs portent J+1/J+2 et P (prevu) ou R (annonce temps reel), avec une legende visible. Le grand horaire futur est en HH:MM avec J+n prevu/reel ; il ne simule pas des minutes restantes. Le compteur reste dans le bandeau superieur.

Le choix conserve les identifiants exacts de l'arret et de la ligne ainsi que le sens configure. direction_id=-1 accepte tous les sens ; une direction manquante dans le GTFS ne correspond pas arbitrairement a 0 ou 1. Les noms sont des libelles, pas des filtres. La configuration actuelle ne filtre pas les branches par trip_headsign : les services partageant stop_id/route_id/direction_id sont regroupes comme pour le temps reel.

Une absence d'annonce ne prouve pas une absence de circulation aujourd'hui : aucun horaire theorique du jour courant n'est fabrique. Une erreur Wi-Fi/HTTP, un flux invalide ou perime et une horloge non synchronisee restent visibles, sans remplacement par du prevu. Sur chaque jour futur, les annonces temps reel sont prioritaires ; le complement prevu commence apres la derniere annonce de ce jour. Les epoques identiques sont dedupliquees en faveur du temps reel, puis la liste totale est limitee a quinze. Les horaires disponibles en J+2 restent accessibles si J+1 est sans service. Si les deux jours sont sans horaires, un message l'indique ; hors validite et sans autre passage, Horaires prevus expires, sans extrapolation.

La mention toleree Flux avance est retiree du pied des ecrans, y compris lorsqu'elle precede une legende ou accompagne une absence de service. Le reste du message est conserve (P/R, expiration, absence d'horaire). Le statut de l'accueil et les diagnostics Serial restent inchanges ; l'erreur de rejet Horloge flux avancee n'est pas masquee.

## Source embarquee

Source : GTFS statique officiel SETRAM urbain/scolaire, endpoint courant de la ressource 84170 du catalogue transport.data.gouv.fr, utilise par tools/setram.py.

La provenance verifiable est dans lib/Timetable/timetable-source.json : URL publique, SHA256 du ZIP, date de generation UTC, feed_info et comptes verifies. Le snapshot embarque couvre les dates civiles du 2026-09-30 au 2026-11-11 inclus. Il contient 17 services, 44 exceptions calendaires, 2 021 combinaisons arret/ligne/sens et 201 197 heures distinctes par groupe de service. Ce compte n'est pas un nombre de passages quotidiens. Les donnees compilees occupent 375 570 octets de flash.

Le format deduplique les listes d'heures puis encode leurs differences en LEB128. Les tableaux const restent en flash, sans charger le reseau complet en RAM. La requete renvoie au maximum quinze epoques triees et uniques. Le resultat est cache par date civile et selection de l'utilisateur ; un polling ne relance pas la recherche s'il n'y a aucun changement. La garde de pixels reste active : un simple changement d'entete du flux n'impose pas de rafraichissement e-paper.

## Calendriers et temps

- calendar.txt : bornes de service et jours de la semaine.
- calendar_dates.txt : ajouts et suppressions prioritaires, y compris services definis uniquement par exceptions.
- feed_info.txt : validite explicite du feed ; a defaut, bornes declarees des calendriers et ajouts, jamais la date de telechargement.
- agency_timezone doit etre Europe/Paris. Le firmware configure TZ=CET-1CEST,M3.5.0,M10.5.0/3 avant la requete.
- `timetable::day(..., offset, limit)` accepte les offsets 1 et 2 ; les autres renvoient InvalidClock. `tomorrow()` reste un wrapper pour 1. L'offset est applique au calendrier civil local (`tm_mday`), pas en ajoutant 86 400 secondes. Chaque intervalle peut durer 23 ou 25 heures lors d'un changement d'heure.
- Les heures GTFS sont converties a partir de midi local du jour de service moins douze heures ecoulees. Les temps >=24:00 necessitent les jours de service precedents ; le debut d'un service de changement d'heure peut aussi appartenir a la date civile precedente.
- La date civile demandee doit etre dans la validite du feed, meme si un temps deborde d'un service precedent.
- Le generateur refuse les heures manquantes necessitant interpolation et les services frequencies.txt, plutot que d'inventer des heures exactes. Il exclut les montees interdites et les services avec arrangements prealables de prise en charge.

Ces horaires ne refletent pas les perturbations, annulations ou changements annonces apres publication. Il n'y a pas de mise a jour automatique du GTFS dans le firmware.

## Regeneration

Depuis la racine du projet, telecharger et valider la version courante :

    python tools/generate_timetable.py --refresh

Reutiliser le ZIP deja verifie sans reseau :

    python tools/generate_timetable.py

Avec une autre archive officielle :

    python tools/generate_timetable.py --input /chemin/gtfs.zip --source-url URL_OFFICIELLE

Le generateur produit lib/Timetable/TimetableData.h et timetable-source.json. Il refuse de depasser 1 000 000 octets de donnees compilees. Recompiler puis flasher pour mettre les horaires de l'appareil a jour :

    pio run -e crowpanel_jd79661 -e crowpanel_ssd1680
    pio run -e crowpanel_jd79661 -t upload

Le catalogue du portail est genere separement par tools/generate_bus_catalogue.py : le mettre aussi a jour si les arrets ou services changent. --generated-at permet de reproduire exactement la provenance d'une generation precedente.

## Verification

    python -m unittest discover -s tests -p 'test_timetable_*.py'
    python tests/run_main_runtime.py
    python tests/run_runtime_policy.py
    python tests/run_bus_ui.py

Les tests hote compilent la vraie bibliotheque avec un TIMETABLE_DATA_HEADER alternatif pour de petites fixtures GTFS declarees. Ils couvrent semaine/weekend, ajouts/suppressions, jours sans service, expiration, IDs exacts, sens manquant, deduplication et limite de quinze, debordements de service, changements d'heure printemps/automne et intervalle civil.

Les tests officiels verifient chaque heure encodee contre le ZIP source et comparent 40 420 requetes C++ historiques plus 80 840 requetes day(1)/day(2), sur 20 dates civiles et toutes les 2 021 combinaisons au calendrier source. Ces totaux decrivent le snapshot livre, pas une promesse sur les futurs feeds. Le test officiel est explicitement ignore si le ZIP source est absent : ce n'est alors pas une verification des donnees reelles.

Les tests du main reel avec fixtures radio/HTTP verifient Home et trois pages, priorite des predictions d'aujourd'hui et des erreurs, changement d'arret/sens et de date, expiration et absence de SPI lors d'un changement d'entete sans changement d'image. Les images de docs/screens/bus-ui-mixed-*, bus-ui-schedule-mixed-* et bus-ui-tomorrow-* sont issues du vrai Adafruit GFX avec des valeurs de test publiques, pas des observations SETRAM.

Aucune de ces verifications ne qualifie l'optique de l'e-paper ou le fonctionnement radio sur appareil.
