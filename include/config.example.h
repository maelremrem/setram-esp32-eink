#pragma once
// Copier ce fichier en bus_config.h, puis renseigner les valeurs localement.
// Le Wi-Fi se configure avec le portail WiFiManager, pas dans ce fichier.
// Identifiant du quai physique, pas seulement le nom de l'arret.
#define SETRAM_STOP_ID ""
#define SETRAM_ROUTE_ID "" // Exactement le route_id GTFS (ex. 10A), vide = toutes.
#define SETRAM_DIRECTION -1 // direction_id GTFS : 0/1 ; -1 = tous.
#define STOP_LABEL "Arret a configurer"
#define LINE_LABEL "Bus"
#define DESTINATION_LABEL "Direction a configurer"
// Cle partagee publiee dans le catalogue officiel, ce n'est pas un secret utilisateur.
#define SETRAM_FEED_URL "https://eur.mecatran.com/utw/ws/gtfsfeed/realtime/lemans?apiKey=73334a124f3a1654141d6979113106450f291d15"
