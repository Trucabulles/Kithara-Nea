# KITHÁRA NÉA
## Manuel d’utilisation
Version de travail C++ / JUCE • Octobre 2026

KITHÁRA NÉA permet d’analyser les accords d’un enregistrement, de suivre les paroles et de préparer une song sheet. L’analyse musicale et la transcription sont deux actions distinctes, lancées à la demande. Les traitements restent locaux.

## 1. Commencer et conserver son travail
L’installateur Windows propose le français ou l’anglais et présente la licence correspondante. Choisissez une installation pour vous seul dans un dossier accessible de votre profil, ou pour tous les utilisateurs dans Program Files avec autorisation administrateur. Sa langue s’applique au premier lancement ; une préférence de langue déjà enregistrée reste prioritaire. La version portable ne nécessite aucune installation : extrayez toute l’archive ZIP dans un dossier accessible en écriture, puis ouvrez kithara-nea.exe. Elle démarre en français, langue par défaut de l’application, modifiable dans le menu. Conservez portable.flag à côté de l’exécutable. Les préférences, les enregistrements et les modèles optionnels téléchargés restent dans le sous-dossier data. Déplacez ce dossier avec l’application pour les conserver. Les fichiers temporaires de traitement utilisent le dossier temporaire du système et sont supprimés à la fin du traitement.

Utilisez « Nouvelle transcription » pour ouvrir un audio. Ouvrir un fichier ne lance pas d’analyse. Le menu hamburger propose aussi l’ouverture d’un projet, l’enregistrement et les projets récents.

Le projet natif .knea conserve les accords, les paroles, les horodatages, les corrections et les réglages du morceau. Un projet autonome intègre l’audio ; un projet léger référence le fichier original. Gardez ce dernier accessible, ou relocalisez-le à l’ouverture si nécessaire. Rouvrir un projet ne relance aucun moteur.

Enregistrez régulièrement avec Ctrl+S. Ctrl+Maj+S permet de créer une autre version. Les exports PDF, ChordPro, MIDI ou WAV ne remplacent pas le projet de travail.

## 2. Analyser et contrôler les accords
« Analyser les accords » lance la reconnaissance harmonique. L’option de pulsations et tempo ajoute l’analyse rythmique. Une analyse existante demande confirmation avant remplacement ; l’annulation permet de revenir aux résultats précédents.

La fenêtre d’attente présente l’opération et le temps écoulé. Échap demande une interruption propre ; sa fermeture attend l’arrêt effectif. Les travaux demandés successivement sont traités dans une file séquentielle.

Les résultats sont des propositions, à contrôler à l’écoute. Un renversement, une extension ou un accord très bref peuvent être mal reconnus. Le tempo et la mesure peuvent rester non déterminés lorsque les repères sont insuffisants. Les indications rythmiques sont corrigeables et servent à la song sheet ; elles ne déplacent ni ne quantifient les événements.

## 3. Lire le morceau et anticiper
La forme d’onde conserve son axe temporel. Les noms et petits repères des changements d’accord y sont placés à leurs temps réels. À l’ouverture d’un projet et après une analyse, le zoom est calculé à partir de tous les changements pour éviter le chevauchement des noms. Un changement extrêmement bref peut donc donner une vue très rapprochée.

En mode automatique, le zoom lisible s’adapte à la largeur de la fenêtre. La molette et le curseur de zoom règlent ensuite librement la forme d’onde et conservent ce choix lors d’un redimensionnement. « Zoom lisible » réactive le mode automatique. L’intervalle des timecodes s’adapte au zoom pour éviter leur chevauchement. Le défilement suit progressivement la lecture. Cliquer dans l’onde repositionne la lecture ; le clic molette permet de déplacer la vue. Maj+glisser définit une boucle temporelle.

La piste Accords présente cinq positions de même largeur : deux précédentes, l’accord courant au centre, deux suivantes. Cette fenêtre avance d’un événement à chaque changement, indépendamment du zoom. Les emplacements non disponibles au début et à la fin restent vides. Les occurrences répétées ne sont pas fusionnées.

Chaque carte présente le nom au-dessus du doigté choisi. Cliquer une carte sélectionne l’événement et rejoint son début. Le clic droit conserve les commandes d’édition ; le double-clic permet de modifier l’accord. Les cartes fixes ne servent pas à déplacer les frontières temporelles : utilisez Édition des données.

La tête de lecture reste uniquement sur la forme d’onde. L’accord courant et le mot chanté ont leurs propres surbrillances. Les deux cartes suivantes permettent d’anticiper : aucun nom supplémentaire n’est superposé à l’onde ou aux paroles.

## 4. Transport et son
Le triangle lance la lecture ; l’icône pause la suspend en conservant la position. Le carré arrête la lecture et revient au début. Les deux flèches permettent de reculer ou d’avancer de cinq secondes ; un double-clic rejoint le début ou la fin. Un double-clic sur les libellés à gauche des pistes revient aussi au début.

L’icône de boucle active ou désactive la boucle. Le bouton rouge lance un décompte visuel 3–2–1, puis enregistre depuis l’entrée sélectionnée. Espace, Échap ou un second clic annule le décompte sans créer de prise. Pendant l’enregistrement, Espace ou R termine et ouvre la prise. Le gain d’entrée (0 à +24 dB, initialement 0 dB) agit seulement sur les nouvelles prises, indépendamment du volume d’écoute. La durée capturée, le niveau et la forme d’onde en direct sont affichés ; un tracé rouge et un avertissement signalent une saturation. Réduisez le gain si la prise sature. Le menu hamburger donne accès aux entrées et sorties audio. Vérifiez le périphérique, les canaux et les effets du microphone si aucun son n’est capturé. Les prises brutes sont temporairement conservées dans le dossier utilisateur de l’application. La sauvegarde intègre toujours une prise créée dans l’application, y compris avec Enregistrer un projet lié : aucun fichier audio séparé n’est nécessaire pour rouvrir la prise sauvegardée.

Le haut-parleur sous le clavier joue la disposition pianistique affichée. Celui dans le coin inférieur gauche du panneau des positions joue le doigté affiché avec l’accordage et le capo. Les cordes muettes ne sonnent pas. Le son correspond à la famille affichée : guitare nylon, ukulélé, mandoline ou banjo. La basse utilise provisoirement un son de guitare, indiqué dans l’infobulle. Chaque touche du clavier joue toujours le piano et se surligne pendant l’appui.

## 5. Instrument, positions et arrangement
Le piano est à gauche et le diagramme instrumental détaillé à droite. Le menu « Visualiser l’accord pour » choisit l’instrument à cordes représenté. La section Positions propose l’accordage correspondant. Cliquer le diagramme détaillé parcourt les doigtés disponibles ; le compteur indique leur nombre. Les cartes de la piste Accords reprennent la position choisie.

La transposition s’exprime en demi-tons : +1 monte d’un demi-ton. La tonalité détectée et celle obtenue sont présentées lorsqu’elles sont disponibles. Le capodastre et l’accordage adaptent les positions calculées. Ces réglages ne déplacent pas les horodatages et ne constituent pas une modification de l’enregistrement source.

« Options » masque ou affiche le panneau de réglages. « Pistes » permet de choisir séparément forme d’onde, accords et paroles. Glisser la poignée du séparateur règle la répartition verticale entre timeline et instruments. Le clavier et les diagrammes conservent leurs proportions. Une fenêtre réduite limite temporairement la hauteur instrumentale demandée ; l’agrandissement la restaure lorsque la place le permet.

La petite flèche à l’extrémité droite du séparateur masque ou restaure toute la zone instrumentale ; la poignée de redimensionnement reste au centre. La timeline récupère l’espace libéré et les commandes de zoom restent accessibles au-dessus du séparateur. La lecture, l’instrument choisi et les positions sont conservés. Redimensionner la fenêtre ne rouvre pas un panneau masqué. L’état replié et la hauteur demandée sont mémorisés entre les sessions. Le panneau des doigtés affiche une seule ligne de compteur et d’action au-dessus du diagramme, agrandi proportionnellement.

## 6. Paroles et alignement
« Transcrire les paroles » produit un texte et ses repères temporels. La qualité Rapide utilise le modèle Small livré avec l’application. Choisir Approfondie propose le téléchargement du modèle Medium (1,43 Gio), avec un lien vers la source whisper.cpp sur Hugging Face, une progression et une annulation par Échap. Le fichier est contrôlé par SHA-256 puis conservé dans le dossier utilisateur pour fonctionner hors ligne. Aucun téléchargement ne démarre sans confirmation. La langue est détectée automatiquement. Le chant, les chœurs et les mélanges instrumentaux peuvent rendre certains mots ou temps incertains.

Le calcul est réglé sur Automatique : un GPU Vulkan compatible est utilisé en priorité, avec repli CPU si le GPU est indisponible ou échoue. CPU et GPU peuvent aussi être sélectionnés explicitement ; ce choix, mémorisé, concerne la transcription et l’alignement. Une reprise CPU après un échec GPU est signalée dans la barre d’état. Le premier calcul GPU peut être plus long pendant la préparation du cache du pilote graphique. Les modèles et l’anticipation visuelle des mots de 60 ms sont conservés.

« Saisir / aligner les paroles » permet de coller votre propre texte. Conservez vos sauts de ligne et lignes vides. Vous pouvez garder le texte sans lancer l’alignement, puis l’aligner ultérieurement sur l’audio. « Vérifier l’alignement » permet de consulter les repères trouvés.

Un fichier instrumental peut ne donner aucun mot. En cas d’absence de repères exploitables, le texte saisi et les paroles antérieures sont conservés ; aucun horodatage n’est inventé pour un mot non retrouvé.

La piste Paroles affiche une phrase stable avec des espaces plus généreux et la surbrillance du mot courant. Cliquer un mot horodaté rejoint sa position. En mode Paroles seules, les accords apparaissent au-dessus : sur un mot, entre deux mots ou avant/après la phrase selon leurs temps. Les passages instrumentaux continuent à présenter les accords sans inventer de syllabe associée.

## 7. Édition et song sheet
« Édition des données », en bas du panneau de réglages, ouvre une fenêtre secondaire avec les onglets Accords et Paroles horodatées. Double-cliquer une cellule permet de modifier le nom d’accord, sa basse, le texte de la ligne ou ses temps de début et fin en secondes. Entrée valide ; Échap annule. La colonne Mots ouvre l’édition du texte et des temps de chaque mot. Corriger un mot conserve ses temps et met à jour les positions dans le texte source. Remplacer toute une ligne retire uniquement ses temps de mots ; utiliser Mots pour les corrections locales conservant l’horodatage. Les valeurs invalides et les chevauchements sont refusés. Annuler/Rétablir et la sauvegarde .knea incluent ces corrections.

L’analyse des accords et l’alignement des paroles peuvent être lancés dans n’importe quel ordre. La song sheet automatique combine les résultats disponibles sans relancer Whisper. Une sheet retouchée manuellement est conservée ; Recalculer le placement permet de la remplacer explicitement après confirmation. Une ancienne sheet sauvegardée n’est considérée comme automatique que si elle correspond exactement à l’ancien résultat du générateur.

Préparer la song sheet se trouve dans le menu hamburger. Texte source conserve les sauts de ligne et les accords [Am] ; « Insérer au curseur » y reste disponible. Dans l’aperçu : cliquer un mot insère l’accord choisi ; survoler un accord le surligne à sa place ; Ctrl+glisser le déplace avec un libellé et un repère de dépôt ; Échap annule le geste. Le clic droit supprime immédiatement l’accord, sans menu ni confirmation. Ctrl+Z annule chaque édition ; Ctrl+Y ou Ctrl+Maj+Z la rétablit. L’analyse musicale reste intacte. Vérifiez les placements à l’écoute ; les diagrammes utilisent les doigtés choisis.

« Exporter » propose la song sheet PDF ou ChordPro, une grille PDF, un accompagnement MIDI ou WAV et les accords en CSV ou texte. L’accompagnement MIDI est une réalisation des accords reconnus, pas une transcription de toutes les notes effectivement jouées. Le ChordPro est un document texte réutilisable.

## 8. Langue, aide et licences
Le menu hamburger permet de choisir Français ou English. Le choix est mémorisé ; vos paroles et vos projets ne sont pas traduits. Le bouton ? donne accès au présent manuel, aux raccourcis, à À propos et aux crédits et licences tierces.

Dans À propos, « Licence de KITHÁRA NÉA » présente le texte complet de la licence de Trucabulles. La version française est le texte original ; une traduction anglaise de courtoisie est fournie. Les composants et banques sonores tiers conservent leurs propres licences, consultables dans leur texte original depuis la fenêtre dédiée.

## Raccourcis utiles
Espace : lecture/pause. R : enregistrer/terminer. Échap : annuler le traitement. Ctrl+O : audio. Ctrl+Maj+O : projet. Ctrl+S : enregistrer. Ctrl+Maj+S : enregistrer sous. Ctrl+Z / Ctrl+Y : annuler/rétablir. Ctrl+flèches : ±5 secondes. Ctrl+Origine / Ctrl+Fin : début/fin. Origine : zoom lisible. Entrée : modifier l’accord. Suppr : sans accord. Ctrl+K : scinder. Ctrl+M : fusionner. Ctrl+B : repère.

## Grille d’accords PDF horodatée
« Nouveau projet », dans le menu hamburger, démarre un espace vide pour ouvrir un audio ou enregistrer. L’enregistrement des modifications en cours est proposé avant la remise à zéro. « Nouvelle transcription » reste le raccourci d’ouverture d’un audio.

La grille PDF présente trois cases de mesures par ligne, avec les noms des accords à l’intérieur. Un accord tenu est rappelé dans chaque mesure qu’il occupe. Les changements conservent leur position rythmique ; aucune liste de timecodes ne remplace la grille. Le préambule et la mesure finale incomplète sont distingués.

Les repères de pulsations et de premiers temps sont utilisés directement lorsqu’ils forment une grille cohérente. Sinon, une grille à tempo constant est estimée à partir du BPM affiché, de la signature à la noire et du premier temps détecté. Cette approximation est explicitement indiquée dans le PDF ; elle peut dériver sur un enregistrement à tempo variable. Sans signature, tempo ou origine exploitable, l’export demande ces indications au lieu de produire une liste horodatée. Aucun moteur n’est relancé et les événements du projet restent inchangés. Le dictionnaire séparé regroupe chaque doigté sélectionné une seule fois. Le nom proposé se termine par _chart.pdf.

## Piano : deux mains
Le clavier propose une disposition pianistique : basse ou fondamentale en bleu pour la main gauche, accord complet dans le registre médium en doré pour la main droite. Il s’agit d’une réalisation de l’accord reconnu, pas d’une identification des mains réellement jouées. La transposition s’applique aux deux mains ; les accordages et capodastres des instruments à cordes ne les modifient pas. Le haut-parleur du piano joue ces mêmes notes. Celui des cordes utilise le doigté affiché. Aucun de ces boutons ne modifie le projet ni les réglages sonores enregistrés pour les exports existants. L’export MIDI n’est pas modifié par cette représentation.



La phrase disparaît à la fin réelle du dernier mot, ou à sa propre fin sans temps de mots. L’anticipation ne la masque pas plus tôt. Dans les passages instrumentaux, aucun message d’absence ; les accords restent visibles.
