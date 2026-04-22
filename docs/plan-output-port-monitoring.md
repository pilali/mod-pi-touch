# Plan : Affichage temps réel des ports de sortie LV2 dans l'éditeur de paramètres

## Objectif

Permettre l'affichage de valeurs en temps réel provenant des ports de contrôle en sortie
(`lv2:ControlPort, lv2:OutputPort`) des plugins LV2, directement dans l'éditeur de
paramètres (appui long sur un bloc plugin).

Exemple concret : modmeter.lv2 expose trois ports de sortie (`level`, `peak`, `rms`)
qui transmettent le niveau audio en continu. L'éditeur doit afficher des barres de
niveau animées pour ces valeurs, en lecture seule.

---

## Mécanisme technique

```
mod-host port 5555 (commandes)
  → monitor_output <instance_id> <symbol>     (souscription)

mod-host port 5556 (feedback)
  ← output_set <instance_id> <symbol> <value> (flux continu, ~20 ms)
```

**Point critique** : mod-host n'a pas de commande `unmonitor_output`. Une fois
souscrit, le flux continue jusqu'à suppression de l'instance (`remove`) ou déconnexion.
Conséquence architecturale : **on souscrit à tous les ports CONTROL_OUT au chargement
du pedalboard**, pas seulement à l'ouverture de l'éditeur.

---

## Infrastructure existante (rien à créer)

| Composant | Fichier | État |
|-----------|---------|------|
| Type `PM_PORT_CONTROL_OUT` | `src/plugin_manager.h` | ✓ défini |
| Extraction via lilv | `src/plugin_manager.c` | ✓ fait — min/max/name récupérés |
| `host_monitor_output(instance, symbol)` | `src/host_comm.c` | ✓ implémenté |
| Parsing `output_set` dans `feedback_handler` | `src/main.c` | ✓ parsé — mais **ignoré** pour les plugins normaux |
| `ui_param_editor_update(symbol, value)` | `src/ui/ui_param_editor.c` | ✓ existe — traite CONTROL_IN uniquement |

**Ce qui manque** :
1. `output_set` non-pre-fx ignoré dans `feedback_handler` (ligne 42-44 de `main.c`)
2. Aucune souscription `monitor_output` au chargement du pedalboard
3. Aucun widget de sortie dans `ui_param_editor.c` (ligne 576 skipe CONTROL_OUT)
4. Aucune table de valeurs partagée entre thread feedback et éditeur

---

## Architecture de la solution

### Table de valeurs partagée

Une table plate dans `ui_pedalboard.c`, protégée par le mutex existant `g_pb_mutex` :

```c
#define OUTPUT_VAL_MAX 256   /* 4 ports OUT × 64 plugins max */

typedef struct {
    int   instance;
    char  symbol[PB_SYMBOL_MAX];
    float value;
} output_val_t;

static output_val_t g_output_vals[OUTPUT_VAL_MAX];
static int          g_output_val_count = 0;
```

Deux fonctions publiques déclarées dans `ui_pedalboard.h` :

```c
/* Appelé par feedback_handler (thread feedback). Met à jour la table
 * ET notifie l'éditeur si ouvert pour cette instance. Thread-safe. */
void ui_pedalboard_set_output(int instance, const char *symbol, float value);

/* Appelé par l'éditeur à son ouverture pour lire les valeurs courantes.
 * Retourne false si l'entrée n'existe pas encore. */
bool ui_pedalboard_get_output(int instance, const char *symbol, float *out);
```

### Flux de données complet

```
[Plugin LV2] → mod-host port 5556
    → feedback_handler() dans main.c  (thread feedback)
        → ui_pedalboard_set_output()  (verrouille g_pb_mutex)
            → met à jour g_output_vals[]
            → si éditeur ouvert pour cet instance :
                lv_async_call(update_meter_async, ...)  ← thread-safe LVGL
                    → ui_param_editor_update_output(symbol, value)
                        → lv_bar_set_value(widget, ...)
```

---

## Fichiers à modifier (5 fichiers, dans l'ordre)

### 1. `src/main.c`

**Modification** : dans `feedback_handler()`, router les `output_set` non-pre-fx.

```c
/* Avant (ligne 41-44) : */
if (sscanf(msg, "output_set %d %127s %f", &instance, symbol, &value) == 3) {
    if (instance == PRE_FX_TUNER_INSTANCE)
        pre_fx_on_feedback(instance, symbol, value);
    return;
}

/* Après : */
if (sscanf(msg, "output_set %d %127s %f", &instance, symbol, &value) == 3) {
    if (instance == PRE_FX_TUNER_INSTANCE)
        pre_fx_on_feedback(instance, symbol, value);
    else
        ui_pedalboard_set_output(instance, symbol, value);
    return;
}
```

---

### 2. `src/ui/ui_pedalboard.h`

**Ajout** : deux déclarations de fonctions publiques.

```c
/* Mise à jour d'une valeur de port de sortie (appelé depuis le thread feedback). */
void ui_pedalboard_set_output(int instance, const char *symbol, float value);

/* Lecture de la dernière valeur connue d'un port de sortie. */
bool ui_pedalboard_get_output(int instance, const char *symbol, float *out);
```

---

### 3. `src/ui/ui_pedalboard.c`

**3a. Déclarations** : ajouter la table et le compteur en haut du module (après `g_pb_mutex`) :

```c
#define OUTPUT_VAL_MAX 256
typedef struct { int instance; char symbol[PB_SYMBOL_MAX]; float value; } output_val_t;
static output_val_t g_output_vals[OUTPUT_VAL_MAX];
static int          g_output_val_count = 0;
```

**3b. Implémenter `ui_pedalboard_set_output()`** :

```c
typedef struct { int instance; char symbol[PB_SYMBOL_MAX]; float value; } output_async_t;

static void output_update_async(void *arg)
{
    output_async_t *d = arg;
    ui_param_editor_update_output(d->symbol, d->value);
    free(d);
}

void ui_pedalboard_set_output(int instance, const char *symbol, float value)
{
    pthread_mutex_lock(&g_pb_mutex);

    /* Chercher une entrée existante */
    output_val_t *slot = NULL;
    for (int i = 0; i < g_output_val_count; i++) {
        if (g_output_vals[i].instance == instance &&
            strcmp(g_output_vals[i].symbol, symbol) == 0) {
            slot = &g_output_vals[i];
            break;
        }
    }
    /* Créer si pas trouvée */
    if (!slot && g_output_val_count < OUTPUT_VAL_MAX) {
        slot = &g_output_vals[g_output_val_count++];
        slot->instance = instance;
        snprintf(slot->symbol, sizeof(slot->symbol), "%s", symbol);
    }
    if (slot) slot->value = value;

    pthread_mutex_unlock(&g_pb_mutex);

    /* Notifier l'éditeur si ouvert pour cette instance */
    if (ui_param_editor_instance() == instance) {
        output_async_t *d = malloc(sizeof(*d));
        if (d) {
            d->instance = instance;
            snprintf(d->symbol, sizeof(d->symbol), "%s", symbol);
            d->value = value;
            lv_async_call(output_update_async, d);
        }
    }
}
```

**3c. Implémenter `ui_pedalboard_get_output()`** :

```c
bool ui_pedalboard_get_output(int instance, const char *symbol, float *out)
{
    pthread_mutex_lock(&g_pb_mutex);
    bool found = false;
    for (int i = 0; i < g_output_val_count; i++) {
        if (g_output_vals[i].instance == instance &&
            strcmp(g_output_vals[i].symbol, symbol) == 0) {
            if (out) *out = g_output_vals[i].value;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_pb_mutex);
    return found;
}
```

**3d. Souscription dans `ui_pedalboard_load()`** : après la boucle de `host_param_set`
(vers la ligne 1556), ajouter dans la boucle principale des plugins :

```c
/* Souscrire aux ports de sortie pour feedback temps réel */
const pm_plugin_info_t *pm_info = pm_plugin_by_uri(plug->uri);
if (pm_info) {
    for (int j = 0; j < pm_info->port_count; j++) {
        if (pm_info->ports[j].type == PM_PORT_CONTROL_OUT)
            host_monitor_output(plug->instance_id, pm_info->ports[j].symbol);
    }
}
```

**3e. Réinitialisation de la table** : en début de `ui_pedalboard_load()`, avant
`host_remove_all()` :

```c
/* Vider la table des valeurs de sortie */
pthread_mutex_lock(&g_pb_mutex);
g_output_val_count = 0;
pthread_mutex_unlock(&g_pb_mutex);
```

---

### 4. `src/ui/ui_param_editor.h`

**Ajouts** :

```c
/* Mise à jour d'un port de SORTIE (barre de niveau lecture seule).
 * Appelé depuis lv_async_call() — thread LVGL uniquement. */
void ui_param_editor_update_output(const char *symbol, float value);

/* Retourne l'instance_id actuellement ouvert dans l'éditeur, ou -1. */
int ui_param_editor_instance(void);
```

---

### 5. `src/ui/ui_param_editor.c`

C'est le cœur de l'implémentation.

**5a. Nouveau type de contrôle** : dans l'enum (après `CTRL_ENUM`) :

```c
CTRL_METER,   /* port de sortie — barre horizontale, lecture seule */
```

Nouveau champ dans `ctrl_reg_t` :

```c
lv_obj_t *meter_bar;   /* lv_bar — uniquement pour CTRL_METER */
```

**5b. `ui_param_editor_instance()`** :

```c
int ui_param_editor_instance(void)
{
    return g_modal ? g_instance : -1;
}
```

**5c. `ui_param_editor_update_output()`** :

```c
void ui_param_editor_update_output(const char *symbol, float value)
{
    ctrl_reg_t *reg = find_ctrl(symbol);
    if (!reg || reg->type != CTRL_METER || !reg->meter_bar) return;

    /* Convertir en entier 0-1000 (précision 0.1%) */
    float range = reg->max - reg->min;
    int bar_val = 0;
    if (range > 0.0f)
        bar_val = (int)(((value - reg->min) / range) * 1000.0f + 0.5f);
    if (bar_val < 0)    bar_val = 0;
    if (bar_val > 1000) bar_val = 1000;

    lv_bar_set_value(reg->meter_bar, bar_val, LV_ANIM_OFF);

    /* Mettre à jour le label numérique */
    if (reg->val_lbl) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3g", (double)value);
        lv_label_set_text(reg->val_lbl, buf);
    }
}
```

**5d. Construction des widgets de sortie** dans `ui_param_editor_show()` :

Après la boucle qui construit les widgets CONTROL_IN (vers la ligne 630), ajouter une
section séparée pour les ports de sortie. Elle itère directement sur `pm_info->ports[]`
(pas sur `ports[]` qui ne contient que les CONTROL_IN du pedalboard) :

```c
/* ── Section ports de sortie (lecture seule) ── */
bool has_out = false;
if (pm_info) {
    for (int i = 0; i < pm_info->port_count; i++)
        if (pm_info->ports[i].type == PM_PORT_CONTROL_OUT) { has_out = true; break; }
}

if (has_out) {
    /* Séparateur */
    lv_obj_t *sep = lv_obj_create(scroll);
    lv_obj_set_size(sep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(sep, UI_COLOR_TEXT_DIM, 0);

    /* Titre de section */
    lv_obj_t *out_title = lv_label_create(scroll);
    lv_label_set_text(out_title, TR(TR_PARAM_OUTPUT_PORTS)); /* "Sorties" / "Outputs" */
    lv_obj_set_style_text_color(out_title, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(out_title, &lv_font_montserrat_14, 0);

    for (int i = 0; i < pm_info->port_count && g_ctrl_count < MAX_CONTROLS; i++) {
        const pm_port_info_t *pm_port = &pm_info->ports[i];
        if (pm_port->type != PM_PORT_CONTROL_OUT) continue;

        ctrl_reg_t *reg = &g_controls[g_ctrl_count++];
        memset(reg, 0, sizeof(*reg));
        snprintf(reg->symbol, sizeof(reg->symbol), "%s", pm_port->symbol);
        reg->type = CTRL_METER;
        reg->min  = pm_port->min;
        reg->max  = pm_port->max;
        if (reg->max <= reg->min) reg->max = reg->min + 1.0f;

        /* Ligne : [Nom]  [████████░░░░░░░]  [valeur] */
        lv_obj_t *row = lv_obj_create(scroll);
        lv_obj_set_size(row, LV_PCT(100), 36);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Nom du port */
        lv_obj_t *name_lbl = lv_label_create(row);
        lv_label_set_text(name_lbl, pm_port->name[0] ? pm_port->name : pm_port->symbol);
        lv_obj_set_style_text_color(name_lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(name_lbl, 110);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);

        /* Barre lv_bar */
        lv_obj_t *bar = lv_bar_create(row);
        lv_obj_set_size(bar, 0, 18);
        lv_obj_set_flex_grow(bar, 1);
        lv_bar_set_range(bar, 0, 1000);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        /* Couleur dégradée : vert → orange → rouge selon position */
        lv_obj_set_style_bg_color(bar, UI_COLOR_SURFACE, 0);
        lv_obj_set_style_bg_color(bar, UI_COLOR_ACTIVE,
                                  LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        reg->meter_bar = bar;

        /* Valeur numérique */
        lv_obj_t *val_lbl = lv_label_create(row);
        lv_label_set_text(val_lbl, "—");
        lv_obj_set_style_text_color(val_lbl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(val_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_width(val_lbl, 60);
        lv_obj_set_style_text_align(val_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        reg->val_lbl = val_lbl;

        /* Lire la valeur déjà connue (si le monitoring tourne depuis le chargement) */
        float cur = 0.0f;
        if (ui_pedalboard_get_output(g_instance, pm_port->symbol, &cur))
            ui_param_editor_update_output(pm_port->symbol, cur);
    }
}
```

**5e. Clé i18n à ajouter** (`src/i18n.h` + `src/i18n.c`) :

```c
TR_PARAM_OUTPUT_PORTS,   /* "Sorties" / "Outputs" */
```

---

## Ordre d'implémentation

```
1. src/i18n.h / i18n.c       — ajouter TR_PARAM_OUTPUT_PORTS
2. src/ui/ui_pedalboard.h    — déclarer set_output / get_output
3. src/ui/ui_pedalboard.c    — implémenter table + set/get + souscription au load
4. src/main.c                — router output_set → ui_pedalboard_set_output
5. src/ui/ui_param_editor.h  — déclarer update_output + instance()
6. src/ui/ui_param_editor.c  — CTRL_METER + widgets + update_output + instance()
```

Les étapes 1-4 constituent la couche données/transport — testables indépendamment
(vérifier dans stderr que les valeurs arrivent). L'étape 6 dépend de toutes les
précédentes.

---

## Test de validation

```bash
# 1. Charger un pedalboard contenant modmeter.lv2
# 2. Vérifier dans stderr :
#    [feedback] output_set 3 level 0.742
#    [feedback] output_set 3 peak  0.891
#    [feedback] output_set 3 rms   0.453

# 3. Appui long sur le bloc modmeter → éditeur de paramètres
# 4. Vérifier :
#    - Section "Sorties" visible sous les paramètres d'entrée
#    - Barres animées en temps réel (~20 ms de rafraîchissement)
#    - Valeurs numériques à droite de chaque barre
#    - Aucun impact sur les paramètres d'entrée (Reset Peak Hold toujours fonctionnel)

# 5. Fermer l'éditeur, le flux output_set continue (normal — pas d'unmonitor)
# 6. Recharger un autre pedalboard → table g_output_vals remise à zéro
```

---

## Points de vigilance

**Fréquence de rafraîchissement** : mod-host envoie `output_set` en continu dès
qu'un plugin tourne. Pour un pedalboard avec 8 plugins ayant chacun 3 sorties, c'est
~24 messages/20ms = ~1200 msgs/s sur le port 5556. Le `lv_async_call` pour chaque
message peut saturer la queue LVGL si l'éditeur est fermé. La garde
`ui_param_editor_instance() == instance` dans `ui_pedalboard_set_output()` évite
d'émettre des `lv_async_call` inutiles quand l'éditeur est fermé.

**Mutex et LVGL** : `ui_pedalboard_set_output()` est appelé depuis le thread feedback,
pas le thread LVGL. La table `g_output_vals` est protégée par `g_pb_mutex`. Tout
appel LVGL passe par `lv_async_call()` — ne jamais appeler directement `lv_bar_set_value`
depuis le thread feedback.

**Plugin sans ports de sortie** : la section "Sorties" n'est pas créée si `has_out`
est false. L'éditeur reste identique à aujourd'hui pour la majorité des plugins.

**`pm_info` NULL** : si le plugin n'est pas trouvé dans le plugin_manager (URI inconnue),
`pm_info` est NULL et la section sorties est simplement absente. Pas de crash.

**Couleur de la barre** : pour un VU-mètre lisible, on peut utiliser une couleur
fixe `UI_COLOR_ACTIVE` (vert). Une coloration dégradée (vert→rouge) nécessite
un style conditionnel dans le callback `draw` de LVGL — à envisager en phase 2.

---

## État au démarrage de la session d'implémentation

- Commit de base : `11b5fb8` (branche `devel`)
- Aucun fichier modifié par ce plan — tout est à créer/modifier
- Infrastructure `host_monitor_output` opérationnelle (utilisée par pre_fx)
- Infrastructure feedback port 5556 opérationnelle

---

*Plan rédigé le 2026-04-20*
