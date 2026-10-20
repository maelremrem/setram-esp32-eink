#!/usr/bin/env python3
"""Authenticated ESP32 firmware OTA using PlatformIO's real sender.
Password is prompted with getpass and passed in-process, never via argv/env.
--dry-run validates tooling/image only and opens no sockets.
"""
import argparse
import getpass
import importlib.util
import ipaddress
import logging
import os
from pathlib import Path
import sys
import warnings
MAX_IMAGE_BYTES=0x330000  # app0/app1 in the project's default_8MB.csv

def load_sender(path=None):
    home=Path(os.environ.get('PLATFORMIO_CORE_DIR',Path.home()/'.platformio'))
    source=Path(path) if path else home/'packages/framework-arduinoespressif32/tools/espota.py'
    if not source.is_file():
        raise FileNotFoundError('espota.py absent : compiler avec PlatformIO, ou utiliser --espota.')
    spec=importlib.util.spec_from_file_location('platformio_espota_sender',source)
    module=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ip',required=True,help='IPv4 affichee sur l\'ecran OTA')
    parser.add_argument('--firmware',required=True,type=Path,help='firmware.bin du bon controleur, pas une image fusionnee/filesystem')
    parser.add_argument('--local-ip',default='0.0.0.0',help='IPv4 locale pour ecouter le retour TCP')
    parser.add_argument('--local-port',type=int,default=32323,help='port TCP de retour a autoriser dans le pare-feu')
    parser.add_argument('--port',type=int,default=3232,help='port OTA UDP de l\'ESP32')
    parser.add_argument('--espota',type=Path,help='chemin alternatif vers le vrai espota.py PlatformIO')
    parser.add_argument('--dry-run',action='store_true',help='valider seulement, sans mot de passe ni reseau')
    args=parser.parse_args(argv)
    try:
        if ipaddress.ip_address(args.ip).version!=4 or ipaddress.ip_address(args.local_ip).version!=4:
            raise ValueError('Cette version attend des adresses IPv4 litterales.')
        if not (1<=args.port<=65535 and 1<=args.local_port<=65535):
            raise ValueError('Port hors plage.')
        firmware=args.firmware.resolve(strict=True)
        size=firmware.stat().st_size
        with firmware.open('rb') as file:
            magic=file.read(1)
        if magic!=b'\xe9' or size<24 or size>MAX_IMAGE_BYTES:
            raise ValueError('Image ESP32 invalide ou trop grande pour une partition OTA (0x330000).')
        sender=load_sender(args.espota)
        print(f'Cible : {args.ip}:{args.port} ; fichier : {firmware} ; taille : {size} octets')
        if args.dry_run:
            print('Validation locale OK ; aucune connexion ni ecriture sur l\'appareil.')
            return 0
        with warnings.catch_warnings():
            warnings.simplefilter('error',getpass.GetPassWarning)
            password=getpass.getpass('Cle OTA temporaire affichee sur l\'appareil : ')
        if len(password)<12:
            raise ValueError('Cle OTA trop courte : utiliser celle de l\'ecran actif.')
        sender.TIMEOUT=10
        sender.PROGRESS=True
        logging.basicConfig(level=logging.INFO,format='%(levelname)s: %(message)s')
        # Bypass espota.main/options debug logging: secret never reaches argv.
        return sender.serve(args.ip,args.local_ip,args.port,args.local_port,password,str(firmware),sender.FLASH)
    except getpass.GetPassWarning:
        print("Erreur : saisie masquee indisponible ; executer depuis un terminal interactif.",file=sys.stderr)
        return 2
    except (OSError,ValueError,EOFError) as error:
        print(f'Erreur : {error}',file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print('Interrompu.',file=sys.stderr)
        return 130
if __name__=='__main__':
    sys.exit(main())
