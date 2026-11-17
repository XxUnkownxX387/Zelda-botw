# AGENTS.md — Règles pour tous les travaux futurs sur ce projet

## 1. Compilation avant tout push
- Compiler les fichiers modifiés avec :
  ```bash
  clang --target=aarch64-none-linux-android30 -fsyntax-only -I<include paths identiques au CMake> <fichier.c>
  ```
- **Zéro erreur obligatoire**. Corriger avant de push.

## 2. Nouveaux fichiers .c
- Ajouter le fichier au `CMakeLists.txt` utilisé par la build Android (`android/botwrecompandroid/app/src/main/cpp/CMakeLists.txt`).
- Vérifier qu'il n'y a **aucun symbole non défini** au link.

## 3. Variables globales
- **Ne jamais déclarer deux fois la même variable globale**.
- Réutiliser les types (`struct`) existants au lieu de créer des structs anonymes redondants.

## 4. Git discipline
- Avant de modifier : afficher `git diff --stat`.
- Ne supprimer **aucun fichier** sans validation explicite.

## 5. Débogage des crashes
- Pour tout crash : résoudre les adresses avec `llvm-addr2line -f -C -e libmain.so <addr>` sur le `.so` non strippé (artifact CI) **avant** de proposer un correctif.