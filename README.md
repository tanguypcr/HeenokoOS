# HeenokoOS (NSY103)

> **Dossier de Conception & Spécifications Techniques**  
> **Formation :** IPST-CNAM — Module NSY103  
> **Binôme :** Aymen CHAROUKI & Tanguy PICUIRA  

---

## 1. Présentation du projet et objectifs

Dans le cadre du module **NSY103**, ce projet consiste à concevoir et implémenter en langage C un mini-système d'exploitation nommé **HeenokoOS**. 

Le but principal est de comprendre et de mettre en pratique la gestion d'un **système de gestion de fichiers (SGF)** et l'interaction avec un interpréteur de commandes découplé.

### Objectifs principaux :
- **Mini-shell interactif :** Fournir une invite de commande permettant à l'utilisateur de saisir des commandes usuelles (navigation, manipulation de fichiers, affichage).
- **Découplage de l'affichage :** Séparer physiquement le terminal de commande et la fenêtre de restitution des résultats, conformément aux exigences du sujet.
- **SGF persistant :** Mettre en place un système de gestion de fichiers stocké dans un fichier conteneur unique (`data.bin`) simulant un disque physique.
- **Architecture multi-processus :** Découper le projet en plusieurs processus distincts communicant par des mécanismes standards d'IPC Linux (pipes, FIFOs).

---

## 2. Architecture système et fonctionnement multi-processus

Le système **HeenokoOS** est structuré autour de trois processus distincts :

```
[ monbash.c (Shell) ] 
         │
         │  (Tube anonyme / pipe)
         ▼
  [ noyau.c (Noyau) ] ── (FIFO : /tmp/heenok_screen_fifo) ──► [ ecran.c (Écran) ]
         │
         ▼
    [ data.bin ]
```

1. **Le processus Écran (`ecran.c`) :**
   - Terminal passif indépendant lancé en premier.
   - Initialise un tube nommé (FIFO sous `/tmp/heenok_screen_fifo`) et attend en lecture bloquante les messages à afficher.
   - Fonctionne de manière découplée du cycle de vie direct du Shell.

2. **Le processus Shell (`monbash.c`) :**
   - Point d'entrée utilisateur s'exécutant dans un second terminal avec une invite interactive.
   - À l'initialisation, il crée un tube anonyme (`pipe()`), puis engendre le processus Noyau à l'aide de `fork()` et `execvp()`.
   - Lit les commandes, effectue l'analyse syntaxique (parsing) et transmet les requêtes d'exécution au Noyau via le tube.

3. **Le processus Noyau (`noyau.c`) :**
   - Processus fils du Shell exécuté en arrière-plan.
   - Reçoit les requêtes du Shell, vérifie les droits d'accès, appelle les primitives du SGF pour lire ou modifier le disque virtuel (`data.bin`), et transmet les sorties formatées au processus Écran via la FIFO.

### Mécanismes de communication (IPC) :
- **Shell $\rightarrow$ Noyau :** Tube anonyme standard (`pipe`). Les données transitent sous la forme d'une structure de requête contenant l'identifiant de l'opération, le chemin cible et un tampon de données.
- **Noyau $\rightarrow$ Écran :** Tube nommé (`FIFO`). Le noyau y écrit directement les textes et résultats formatés.

---

## 3. Organisation du Système de Gestion de Fichiers (SGF)

Le disque virtuel `data.bin` est initialisé à une taille de **1 Mo**, découpé en **2048 blocs de 512 octets** :

| Blocs | Rôle | Description |
| :--- | :--- | :--- |
| **Bloc 0** | Superbloc | Métadonnées globales du disque et table d'allocation des blocs. |
| **Blocs 1 à 4** | Table des Inodes | Espace réservé pour 64 inodes au total. |
| **Blocs 5 à 2047** | Blocs de données | Contenu des fichiers et répertoires. |

### Structures de données

- **Superbloc :** Stocke les informations globales du disque ainsi qu'un tableau d'allocation simple (0 = libre, 1 = occupé) pour éviter les manipulations complexes de masques de bits.
- **Inode :** Représente chaque fichier ou répertoire. Contient les métadonnées (permissions, taille, type) ainsi que les adresses des blocs de données.

### Fonctionnement de l'indirection simple

- **Fichiers $\le$ 2048 octets (jusqu'à 4 blocs) :** Utilisation directe des 4 pointeurs directs de l'inode.
- **Fichiers $>$ 2048 octets :** Allocation d'un bloc d'indirection simple (numéro stocké dans le champ `indirect`). Ce bloc contient un tableau de 128 entiers (adresses de blocs) :
  $$128 \times 4\text{ octets} = 512\text{ octets}$$
- **Capacité maximale théorique d'un fichier :**
  $$(4 + 128) \times 512\text{ octets} = 67\,584\text{ octets } (\approx 66\text{ Ko})$$

### Structure d'un répertoire

Un répertoire est un bloc de données de 512 octets contenant des entrées structurées associant un nom de fichier à un numéro d'inode. Chaque bloc peut contenir **16 entrées de répertoire de 32 octets**.

---

## 4. Primitives et Commandes

### Primitives SGF de bas niveau (`sgf_core.c`)

- `_mycreate(nom, mode)` : Réserve un inode libre, initialise ses métadonnées, l'enregistre dans le répertoire courant et renvoie son identifiant.
- `_myopen(nom, mode)` : Recherche le chemin dans l'arborescence, vérifie les droits d'accès et renvoie le numéro d'inode.
- `_myclose(inode)` : Clôture l'accès au fichier et synchronise les métadonnées sur le disque.
- `_myread(inode, buffer, n)` : Lit $n$ octets en calculant les blocs directs et indirects associés à l'offset de lecture.
- `_mywrite(inode, buffer, n)` : Écrit les données et gère l'allocation dynamique de nouveaux blocs (y compris le bloc d'indirection).
- `_mkdir(nom)` : Crée un répertoire et initialise son premier bloc avec les entrées `.` et `..`.
- `_rmdir(nom)` : Supprime un répertoire après vérification de sa vacuité (seulement `.` et `..`).
- `_unlink(nom)` : Supprime le lien du fichier dans le répertoire parent et libère les blocs associés dans la table d'allocation.

### Commandes du Shell (`monbash.c`)

- `ls` / `ls -l` : Liste le contenu du répertoire courant (`-l` affiche permissions, taille et propriétaire via interrogation des inodes).
- `cd <chemin>` : Modifie le répertoire courant de travail.
- `cp <source> <destination>` : Duplique un fichier en copiant son contenu.
- `mv <source> <destination>` : Déplace ou renomme un fichier/répertoire.
- `rm <fichier>` : Supprime un fichier via `_unlink`.
- `cat <fichier>` : Affiche le contenu intégral d'un fichier sur l'Écran.
- `echo "texte" > <fichier>` : Écrit ou écrase une chaîne dans un fichier via `_mycreate` et `_mywrite`.
- `df` : Affiche l'espace total, utilisé et restant sur `data.bin` d'après le Superbloc.
- `bling` : Commande d'évaluation affichant le taux d'occupation sous forme d'indicateur de rentabilité.

---

## 5. Répartition des tâches

| Membre | Responsabilités | Fichiers associés | Tâches clés |
| :--- | :--- | :--- | :--- |
| **Aymen CHAROUKI** | Noyau, Disque & SGF | `sgf_core.c`<br>`disk.c`<br>`noyau.c` | • Formatage et gestion de `data.bin`<br>• Implémentation du Superbloc et des Inodes<br>• Gestion de l'indirection simple<br>• Implémentation des 8 primitives SGF<br>• Traitement des requêtes côté noyau |
| **Tanguy PICUIRA** | Shell, Parsing, IPC & Affichage | `monbash.c`<br>`parser.c`<br>`ecran.c` | • Boucle d'invite interactive du shell<br>• Analyse syntaxique des commandes<br>• Instanciation du noyau (`fork`/`execvp`)<br>• Configuration des IPC (pipe et FIFO)<br>• Développement des commandes utilisateur |

---

## 6. Personnalisation HeenokoOS

Pour donner une identité propre au projet tout en maintenant une rigueur technique :
- **Message de boot :** À l'ouverture d'`ecran.c`, un message d'initialisation confirme le montage du volume `data.bin` avec une référence sobre à l'univers du Roi Heenok (*"Montage du volume data.bin... 100% pur coton"*, *"Noyau opérationnel sur le terrain"*).
- **Commande `bling` :** Variante stylisée de `df` calculant et affichant le "taux de rentabilité du stockage".

---

## 7. Compilation et exécution

Le projet utilise les IPC POSIX (`pipe`, `fork`, `mkfifo`) : il se compile et s'exécute sous **Linux ou WSL**.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Trois exécutables sont produits dans `build/` : `ecran`, `monbash` et `noyau`.

```sh
# Terminal 1 : l'Écran, à lancer en premier
./build/ecran

# Terminal 2 : le Shell (lance lui-même le noyau)
./build/monbash            # disque par défaut : ./data.bin
./build/monbash mon.bin    # ou un autre disque virtuel
```

Si `data.bin` n'existe pas, le noyau le crée et le formate. Si l'Écran n'est pas lancé, les résultats s'affichent dans le terminal du Shell. Tapez `help` pour la liste des commandes et `exit` (ou Ctrl-D) pour quitter.

### Commandes ajoutées

En plus des commandes de la section 4 : `pwd`, `mkdir`, `rmdir`, `touch`, `chmod <mode octal> <chemin>` et `echo "texte" >> fichier` (ajout en fin de fichier).

### Écarts par rapport à la conception

- **Carte d'allocation en bits :** une table d'un octet par bloc ferait 2048 octets et ne tiendrait pas dans le bloc 0 (512 octets). Le superbloc utilise donc un bit par bloc (256 octets).
- **Tube de réponse Noyau → Shell :** en plus du tube de requêtes, un second tube anonyme renvoie au Shell le statut de chaque commande et le répertoire courant, pour afficher l'invite `heenoko:/chemin$`. Les résultats passent toujours par l'Écran.
- **`_mycreate` ouvre le fichier en écriture** et tronque un fichier existant, comme `creat(2)`.
- **Les répertoires peuvent dépasser un bloc :** au-delà de 16 entrées, ils utilisent les blocs suivants de leur inode, comme un fichier.
